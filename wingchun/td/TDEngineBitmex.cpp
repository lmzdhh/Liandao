#include "TDEngineBitmex.h"
#include "longfist/ctp.h"
#include "longfist/LFUtils.h"
#include "TypeConvert.hpp"
#include <boost/algorithm/string.hpp>

#include <writer.h>
#include <stringbuffer.h>
#include <document.h>
#include <iostream>
#include <string>
#include <sstream>
#include <stdio.h>
#include <assert.h>
#include <cpr/cpr.h>
#include <chrono>
#include <algorithm>
#include "../../utils/crypto/openssl_util.h"
using cpr::Delete;
using cpr::Get;
using cpr::Url;
using cpr::Body;
using cpr::Header;
using cpr::Parameters;
using cpr::Payload;
using cpr::Post;
using cpr::Timeout;

using rapidjson::StringRef;
using rapidjson::Writer;
using rapidjson::StringBuffer;
using rapidjson::Document;
using rapidjson::SizeType;
using rapidjson::Value;
using std::string;
using std::to_string;
using std::stod;
using std::stoi;
using utils::crypto::hmac_sha256;
using utils::crypto::hmac_sha256_byte;
using utils::crypto::base64_encode;


USING_WC_NAMESPACE

int g_RequestGap=5*60;

static TDEngineBitmex* global_td = nullptr;
std::mutex g_reqMutex;
std::mutex unit_mutex;

static int ws_service_cb( struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len )
{

    switch( reason )
    {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
        {
            lws_callback_on_writable( wsi );
            break;
        }
        case LWS_CALLBACK_PROTOCOL_INIT:
        {
            break;
        }
        case LWS_CALLBACK_CLIENT_RECEIVE:
        {
            if(global_td)
            {
                global_td->on_lws_data(wsi, (const char*)in, len);
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_CLOSED:
        {
            std::cout << "3.1415926 LWS_CALLBACK_CLIENT_CLOSED, reason = " << reason << std::endl;
            if(global_td) {
                std::cout << "3.1415926 LWS_CALLBACK_CLIENT_CLOSED 2,  (call on_lws_connection_error)  reason = " << reason << std::endl;
                global_td->on_lws_connection_error(wsi);
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_RECEIVE_PONG:
        {
            //std::cout << "3.1415926 LWS_CALLBACK_CLIENT_RECEIVE_PONG, reason = " << reason << std::endl;
            break;
        }
        case LWS_CALLBACK_CLIENT_WRITEABLE:
        {
            if(global_td)
            {
                global_td->lws_write_subscribe(wsi);
            }
            break;
        }
        case LWS_CALLBACK_TIMER:
        {
            break;
        }
        case LWS_CALLBACK_CLOSED:
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        {
            std::cout << "3.1415926 LWS_CALLBACK_CLOSED/LWS_CALLBACK_CLIENT_CONNECTION_ERROR writeable, reason = " << reason << std::endl;
            if(global_td)
            {
                global_td->on_lws_connection_error(wsi);
            }
            break;
        }
        default:
            break;
    }

    return 0;
}

static struct lws_protocols protocols[] =
        {
                {
                        "md-protocol",
                        ws_service_cb,
                              0,
                                 65536,
                },
                { NULL, NULL, 0, 0 } /* terminator */
        };


enum protocolList {
    PROTOCOL_TEST,

    PROTOCOL_LIST_COUNT
};

struct session_data {
    int fd;
};



TDEngineBitmex::TDEngineBitmex(): ITDEngine(SOURCE_BITMEX)
{
    logger = yijinjing::KfLog::getLogger("TradeEngine.Bitmex");
    KF_LOG_INFO(logger, "[TDEngineBitmex]");
  
}

TDEngineBitmex::~TDEngineBitmex()
{
}

void TDEngineBitmex::init()
{
    ITDEngine::init();
    JournalPair tdRawPair = getTdRawJournalPair(source_id);
    raw_writer = yijinjing::JournalWriter::create(tdRawPair.first, tdRawPair.second, "RAW_" + name());
    KF_LOG_INFO(logger, "[init]");


    std::time_t baseNow = std::time(nullptr);
    struct tm* tm = std::localtime(&baseNow);
    tm->tm_sec += 30;
    std::time_t next = std::mktime(tm);

    std::cout << "std::to_string(next):" << std::to_string(next)<< std::endl;

    std::cout << "getTimestamp:" << std::to_string(getTimestamp())<< std::endl;



}

void TDEngineBitmex::pre_load(const json& j_config)
{
    KF_LOG_INFO(logger, "[pre_load]");
}

void TDEngineBitmex::resize_accounts(int account_num)
{
    account_units.resize(account_num);
    KF_LOG_INFO(logger, "[resize_accounts]");
}

TradeAccount TDEngineBitmex::load_account(int idx, const json& j_config)
{
    KF_LOG_INFO(logger, "[load_account]");
    // internal load
    string api_key = j_config["APIKey"].get<string>();
    string secret_key = j_config["SecretKey"].get<string>();

    string baseUrl = j_config["baseUrl"].get<string>();
    base_interval_ms = j_config["rest_get_interval_ms"].get<int>();
    base_interval_ms = std::max(base_interval_ms,(int64_t)500);

    AccountUnitBitmex& unit = account_units[idx];
    unit.api_key = api_key;
    unit.secret_key = secret_key;
    unit.baseUrl = "https://" + baseUrl;
    unit.wsUrl = baseUrl;
    KF_LOG_INFO(logger, "[load_account] (api_key)" << api_key << " (baseUrl)" << unit.baseUrl);

    unit.coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
    unit.coinPairWhiteList.Debug_print();

    unit.positionWhiteList.ReadWhiteLists(j_config, "positionWhiteLists");
    unit.positionWhiteList.Debug_print();

    //display usage:
    if(unit.coinPairWhiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "TDEngineBitmex::load_account: please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"BTC_USDT\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"ETC_ETH\"");
        KF_LOG_ERROR(logger, "},");
    }

    //cancel all openning orders on TD startup
    Document d;
    cancel_all_orders(unit, d);

    // set up
    TradeAccount account = {};
    //partly copy this fields
    strncpy(account.UserID, api_key.c_str(), 16);
    strncpy(account.Password, secret_key.c_str(), 21);
    return account;
}


void TDEngineBitmex::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[connect]");
    for (int idx = 0; idx < account_units.size(); idx ++)
    {
        AccountUnitBitmex& unit = account_units[idx];
        KF_LOG_INFO(logger, "[connect] (api_key)" << unit.api_key);
        if (!unit.logged_in)
        {
            //exchange infos
            Document doc;
            //TODO
            get_products(unit, doc);
            KF_LOG_INFO(logger, "[connect] get_products");
            printResponse(doc);

            if(loadExchangeOrderFilters(unit, doc))
            {
                unit.logged_in = true;
            } else {
                KF_LOG_ERROR(logger, "[connect] logged_in = false for loadExchangeOrderFilters return false");
            }
            debug_print(unit.sendOrderFilters);
			lws_login(unit, 0);
            unit.logged_in = true;
        }
    }
}

//TODO
bool TDEngineBitmex::loadExchangeOrderFilters(AccountUnitBitmex& unit, Document &doc) {
    KF_LOG_INFO(logger, "[loadExchangeOrderFilters]");
//    //parse bitmex json
    /*
//     [{"baseCurrency":"LTC","baseMaxSize":"100000.00","baseMinSize":"0.001","code":"LTC_BTC","quoteCurrency":"BTC","quoteIncrement":"8"},
//     {"baseCurrency":"BCH","baseMaxSize":"100000.00","baseMinSize":"0.001","code":"BCH_BTC","quoteCurrency":"BTC","quoteIncrement":"8"},
//     {"baseCurrency":"ETH","baseMaxSize":"100000.00","baseMinSize":"0.001","code":"ETH_BTC","quoteCurrency":"BTC","quoteIncrement":"8"},
//     {"baseCurrency":"ETC","baseMaxSize":"100000.00","baseMinSize":"0.01","code":"ETC_BTC","quoteCurrency":"BTC","quoteIncrement":"8"},
//     ...
     ]
//     * */
    if (doc.HasParseError() || doc.IsObject()) {
        return false;
    }
    if (doc.IsArray()) {
        int symbolsCount = doc.Size();
        for (SizeType i = 0; i < symbolsCount; i++) {
            const rapidjson::Value &sym = doc[i];
            if (sym.HasMember("symbol") && sym.HasMember("tickSize")) {
                std::string symbol = sym["symbol"].GetString();
                double tickSize = sym["tickSize"].GetDouble();
                KF_LOG_INFO(logger, "[loadExchangeOrderFilters] sendOrderFilters (symbol)" << symbol << " (tickSize)"
                                                                                           << tickSize);
                //0.0000100; 0.001;  1; 10
                SendOrderFilter afilter;
                afilter.InstrumentID = symbol;
                afilter.ticksize = tickSize;
                unit.sendOrderFilters.insert(std::make_pair(symbol, afilter));
            }
        }
    }

    return true;
}

void TDEngineBitmex::debug_print(std::map<std::string, SendOrderFilter> &sendOrderFilters)
{
    std::map<std::string, SendOrderFilter>::iterator map_itr = sendOrderFilters.begin();
    while(map_itr != sendOrderFilters.end())
    {
        KF_LOG_DEBUG(logger, "[debug_print] sendOrderFilters (symbol)" << map_itr->first <<
                                                                      " (tickSize)" << map_itr->second.ticksize);
        map_itr++;
    }
}

SendOrderFilter TDEngineBitmex::getSendOrderFilter(AccountUnitBitmex& unit, const std::string& symbol) {
    std::map<std::string, SendOrderFilter>::iterator map_itr = unit.sendOrderFilters.find(symbol);
    if (map_itr != unit.sendOrderFilters.end()) {
        return map_itr->second;
    }
    SendOrderFilter defaultFilter;
    defaultFilter.ticksize = 0.00000001;
    defaultFilter.InstrumentID = "";
    return defaultFilter;
}

void TDEngineBitmex::login(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[login]");
    connect(timeout_nsec);
}

void TDEngineBitmex::logout()
{
    KF_LOG_INFO(logger, "[logout]");
}

void TDEngineBitmex::release_api()
{
    KF_LOG_INFO(logger, "[release_api]");
}

bool TDEngineBitmex::is_logged_in() const
{
    KF_LOG_INFO(logger, "[is_logged_in]");
    for (auto& unit: account_units)
    {
        if (!unit.logged_in)
            return false;
    }
    return true;
}

bool TDEngineBitmex::is_connected() const
{
    KF_LOG_INFO(logger, "[is_connected]");
    return is_logged_in();
}



std::string TDEngineBitmex::GetSide(const LfDirectionType& input) {
    if (LF_CHAR_Buy == input) {
        return "Buy";
    } else if (LF_CHAR_Sell == input) {
        return "Sell";
    } else {
        return "";
    }
}

LfDirectionType TDEngineBitmex::GetDirection(std::string input) {
    if ("Buy" == input) {
        return LF_CHAR_Buy;
    } else if ("Sell" == input) {
        return LF_CHAR_Sell;
    } else {
        return LF_CHAR_Buy;
    }
}

std::string TDEngineBitmex::GetType(const LfOrderPriceTypeType& input) {
    if (LF_CHAR_LimitPrice == input) {
        return "Limit";
    } else if (LF_CHAR_AnyPrice == input) {
        return "Market";
    } else {
        return "";
    }
}

LfOrderPriceTypeType TDEngineBitmex::GetPriceType(std::string input) {
    if ("Limit" == input) {
        return LF_CHAR_LimitPrice;
    } else if ("Market" == input) {
        return LF_CHAR_AnyPrice;
    } else {
        return '0';
    }
}
//订单状态，﻿open（未成交）、filled（已完成）、canceled（已撤销）、cancel（撤销中）、partially-filled（部分成交）
LfOrderStatusType TDEngineBitmex::GetOrderStatus(std::string input) {

    if("Pending New" == input){
        return LF_CHAR_Unknown;
    }
    else if("New" == input){
        return LF_CHAR_NotTouched;
    }
    else if ("Partially filled" == input) {
        return LF_CHAR_PartTradedQueueing;
    } else if ("Filled" == input) {
        return LF_CHAR_AllTraded;
    } else if ("Canceled" == input) {
        return LF_CHAR_Canceled;
    } else if ("Rejected" == input) {
        return LF_CHAR_NoTradeNotQueueing;
    } else {
        return LF_CHAR_NotTouched;
    }
}

/**
 * req functions
 */
void TDEngineBitmex::req_investor_position(const LFQryPositionField* data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId);

    AccountUnitBitmex& unit = account_units[account_index];
    KF_LOG_INFO(logger, "[req_investor_position] (api_key)" << unit.api_key << " (InstrumentID) " << data->InstrumentID);

    int errorId = 0;
    std::string errorMsg = "";
    Document d;
    get_account(unit, d);

    if(d.IsObject() && d.HasMember("code"))
    {
        errorId = d["code"].GetInt();
        if(d.HasMember("message") && d["message"].IsString())
        {
            errorMsg = d["message"].GetString();
        }
        KF_LOG_ERROR(logger, "[req_investor_position] failed!" << " (rid)" << requestId << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }
    send_writer->write_frame(data, sizeof(LFQryPositionField), source_id, MSG_TYPE_LF_QRY_POS_BITMEX, 1, requestId);

    LFRspPositionField pos;
    memset(&pos, 0, sizeof(LFRspPositionField));
    strncpy(pos.BrokerID, data->BrokerID, 11);
    strncpy(pos.InvestorID, data->InvestorID, 19);
    strncpy(pos.InstrumentID, data->InstrumentID, 31);
    pos.PosiDirection = LF_CHAR_Long;
    pos.HedgeFlag = LF_CHAR_Speculation;
    pos.Position = 0;
    pos.YdPosition = 0;
    pos.PositionCost = 0;

	std::string ticker = unit.coinPairWhiteList.GetValueByKey(data->InstrumentID);
/*
 # Response
    [{"available":"0.099","balance":"0.099","currencyCode":"BTC","hold":"0","id":83906},{"available":"188","balance":"188","currencyCode":"MVP","hold":"0","id":83906}]
 * */
	bool findSymbolInResult = false;
    if(d.IsArray())
    {
        SizeType len = d.Size();
        KF_LOG_INFO(logger, "[req_investor_position] (asset.length)" << len);
        for(SizeType i = 0; i < len; i++)
        {
            std::string symbol = d[i]["symbol"].GetString();          
            if(symbol.length() > 0 && symbol == ticker) {
                //strncpy(pos.InstrumentID, ticker.c_str(), 31);
                pos.Position = std::round(d[i]["currentQty"].GetDouble() * scale_offset);
               
                //KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId << " (symbol) " << symbol
                //                                                         << " hold:" << d.GetArray()[i]["currentQty"].GetDouble()
                //                                                          << " balance: " << d.GetArray()[i]["currentCost"].GetDouble());
                KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId << " (symbol) " << symbol << " (position) " << pos.Position);
				on_rsp_position(&pos, 1, requestId, errorId, errorMsg.c_str());
				findSymbolInResult = true;
            }
        }
    }

  
    //send the filtered position
    /*int position_count = tmp_vector.size();
    for (int i = 0; i < position_count; i++)
    {
        on_rsp_position(&tmp_vector[i], i == (position_count - 1), requestId, errorId, errorMsg.c_str());
        findSymbolInResult = true;
    }*/

    if(!findSymbolInResult)
    {
        KF_LOG_INFO(logger, "[req_investor_position] (!findSymbolInResult) (requestId)" << requestId);
        on_rsp_position(&pos, 1, requestId, errorId, errorMsg.c_str());
    }
    if(errorId != 0)
    {
        raw_writer->write_error_frame(&pos, sizeof(LFRspPositionField), source_id, MSG_TYPE_LF_RSP_POS_BITMEX, 1, requestId, errorId, errorMsg.c_str());
    }
}

void TDEngineBitmex::req_qry_account(const LFQryAccountField *data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_qry_account]");
}

int64_t TDEngineBitmex::fixPriceTickSize(double keepPrecision, int64_t price, bool isBuy) {


    int64_t tickSize = int64_t((keepPrecision+0.000000001)* scale_offset);

    KF_LOG_INFO(logger, "[fixPriceTickSize input]" << "(price)" << price);
    int64_t count = price/tickSize;
    int64_t new_price = tickSize * count;
    if(isBuy){
        KF_LOG_INFO(logger, "[fixPriceTickSize output]" << "(price is buy)"  << new_price);
    } else {
        if(price%tickSize > 0)
        {
            new_price+=tickSize;
        }
        KF_LOG_INFO(logger, "[fixPriceTickSize output]" << "(price is sell)" << new_price);
    }
    return new_price;
}

int TDEngineBitmex::Round(std::string tickSizeStr) {
    size_t docAt = tickSizeStr.find( ".", 0 );
    size_t oneAt = tickSizeStr.find( "1", 0 );

    if(docAt == string::npos) {
        //not ".", it must be "1" or "10"..."100"
        return -1 * (tickSizeStr.length() -  1);
    }
    //there must exist 1 in the string.
    return oneAt - docAt;
}

bool TDEngineBitmex::ShouldRetry(const Document& json)
{
	std::lock_guard<std::mutex> lck(unit_mutex);
    if(json.IsObject() && json.HasMember("code") && json["code"].IsNumber())
    {
        int code = json["code"].GetInt();
		if (code == 503 || code == 429)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(rest_get_interval_ms));
			return true;
		}
          
    }
    return false;
}
void TDEngineBitmex::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitBitmex& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_insert]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Tid)" << data->InstrumentID
                                              << " (Volume)" << data->Volume
                                              << " (LimitPrice)" << data->LimitPrice
                                              << " (OrderRef)" << data->OrderRef);
    send_writer->write_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BITMEX, 1/*ISLAST*/, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_insert]: not in WhiteList, ignore it  (rid)" << requestId <<
                                                                                      " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BITMEX, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_insert] (exchange_ticker)" << ticker);

    double funds = 0;
    Document d;

    SendOrderFilter filter = getSendOrderFilter(unit, ticker.c_str());

    int64_t fixedPrice = fixPriceTickSize(filter.ticksize, data->LimitPrice, LF_CHAR_Buy == data->Direction);

    KF_LOG_DEBUG(logger, "[req_order_insert] SendOrderFilter  (Tid)" << ticker <<
                                                                     " (LimitPrice)" << data->LimitPrice <<
                                                                     " (ticksize)" << filter.ticksize <<
                                                                     " (fixedPrice)" << fixedPrice);
	addNewQueryOrdersAndTrades(unit, data->InstrumentID, data->OrderRef,data->Direction, LF_CHAR_Unknown, 0, requestId);
    send_order(unit, ticker.c_str(), GetSide(data->Direction).c_str(),
            GetType(data->OrderPriceType).c_str(), data->Volume*1.0/scale_offset, fixedPrice*1.0/scale_offset, data->OrderRef, d);
    int nRetryTimes=0;
    while(ShouldRetry(d) && nRetryTimes < unit.maxRetryCount)
    {
        send_order(unit, ticker.c_str(), GetSide(data->Direction).c_str(),
            GetType(data->OrderPriceType).c_str(), data->Volume*1.0/scale_offset, fixedPrice*1.0/scale_offset, data->OrderRef, d);
    }
    /*
     {"orderID":"18eb8aeb-3a29-b546-b2fe-1b55f24ef63f","clOrdID":"5","clOrdLinkID":"","account":272991,"symbol":"XBTUSD","side":"Buy",
     "simpleOrderQty":null,"orderQty":10,"price":1,"displayQty":null,"stopPx":null,"pegOffsetValue":null,"pegPriceType":"","currency":"USD",
     "settlCurrency":"XBt","ordType":"Limit","timeInForce":"GoodTillCancel","execInst":"","contingencyType":"",
     "exDestination":"XBME","ordStatus":"New","triggered":"","workingIndicator":true,"ordRejReason":"","simpleLeavesQty":null,
     "leavesQty":10,"simpleCumQty":null,"cumQty":0,"avgPx":null,"multiLegReportingType":"SingleSecurity","text":"Submitted via API.",
     "transactTime":"2018-11-18T13:18:33.598Z","timestamp":"2018-11-18T13:18:33.598Z"}
     */
    //not expected response
    if(d.HasParseError() || !d.IsObject())
    {
        errorId = 100;
        errorMsg = "send_order http response has parse error or is not json. please check the log";
        KF_LOG_ERROR(logger, "[req_order_insert] send_order error!  (rid)" << requestId << " (errorId)" <<
                                                                           errorId << " (errorMsg) " << errorMsg);
    } else  if(d.HasMember("orderID"))
    {

            //if send successful and the exchange has received ok, then add to  pending query order list
            std::string remoteOrderId = d["orderID"].GetString();
            localOrderRefRemoteOrderId.insert(std::make_pair(std::string(data->OrderRef), remoteOrderId));
            KF_LOG_INFO(logger, "[req_order_insert] after send  (rid)" << requestId << " (OrderRef) " <<
                                                                       data->OrderRef << " (remoteOrderId) " << remoteOrderId);

            char noneStatus = GetOrderStatus(d["ordStatus"].GetString());//none
            //addNewQueryOrdersAndTrades(unit, data->InstrumentID, data->OrderRef, noneStatus, 0,requestId);
            //success, only record raw data
            raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BITMEX, 1, requestId, errorId, errorMsg.c_str());
    }  else if (d.HasMember("code") && d["code"].IsNumber()) {
        //send error, example: http timeout.
        errorId = d["code"].GetInt();
        if(d.HasMember("message") && d["message"].IsString())
        {
            errorMsg = d["message"].GetString();
        }
        KF_LOG_ERROR(logger, "[req_order_insert] failed!" << " (rid)" << requestId << " (errorId)" <<
                                                          errorId << " (errorMsg) " << errorMsg);
    }


    if(errorId != 0)
    {
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
		std::lock_guard<std::mutex> lck(unit_mutex);
		unit.ordersMap.erase(data->OrderRef);
    }
    raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BITMEX, 1, requestId, errorId, errorMsg.c_str());
}


void TDEngineBitmex::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitBitmex& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_action]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Iid)" << data->InvestorID
                                              << " (OrderRef)" << data->OrderRef
                                              << " (KfOrderID)" << data->KfOrderID);

    send_writer->write_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BITMEX, 1, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_action]: not in WhiteList , ignore it: (rid)" << requestId << " (errorId)" <<
                                                                                       errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BITMEX, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_action] (exchange_ticker)" << ticker);
	std::unique_lock<std::mutex> lck(unit_mutex);
    std::map<std::string, std::string>::iterator itr = localOrderRefRemoteOrderId.find(data->OrderRef);
    std::string remoteOrderId;
    if(itr == localOrderRefRemoteOrderId.end()) {
        errorId = 1;
        std::stringstream ss;
        ss << "[req_order_action] not found in localOrderRefRemoteOrderId map (orderRef) " << data->OrderRef;
        errorMsg = ss.str();
        KF_LOG_ERROR(logger, "[req_order_action] not found in localOrderRefRemoteOrderId map. "
                             << " (rid)" << requestId << " (orderRef)" << data->OrderRef << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BITMEX, 1, requestId, errorId, errorMsg.c_str());
        return;
    } else {
        remoteOrderId = itr->second;
        KF_LOG_DEBUG(logger, "[req_order_action] found in localOrderRefRemoteOrderId map (orderRef) "
                             << data->OrderRef << " (remoteOrderId) " << remoteOrderId);
    }
	lck.unlock();
    Document d;
    cancel_order(unit, data->OrderRef, d);

    //cancel order response "" as resultText, it cause json.HasParseError() == true, and json.IsObject() == false.
    //it is not an error, so dont check it.
    //not expected response
    if(d.IsObject() && !d.HasParseError() && d.HasMember("code") && d["code"].IsNumber()) {
        errorId = d["code"].GetInt();
        if(d.HasMember("message") && d["message"].IsString())
        {
            errorMsg = d["message"].GetString();
        }
        KF_LOG_ERROR(logger, "[req_order_action] cancel_order failed!" << " (rid)" << requestId
                                                                       << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }

    if(errorId != 0)
    {
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
    }
    raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BITMEX, 1, requestId, errorId, errorMsg.c_str());
}


void TDEngineBitmex::moveNewtoPending(AccountUnitBitmex& unit)
{
    std::lock_guard<std::mutex> guard_mutex(unit_mutex);

    std::vector<PendingBitmexOrderStatus>::iterator newOrderStatusIterator;
    for(newOrderStatusIterator = unit.newOrderStatus.begin(); newOrderStatusIterator != unit.newOrderStatus.end();)
    {
        unit.pendingOrderStatus.push_back(*newOrderStatusIterator);
        newOrderStatusIterator = unit.newOrderStatus.erase(newOrderStatusIterator);
    }
}



void TDEngineBitmex::addNewQueryOrdersAndTrades(AccountUnitBitmex& unit, const char_31 InstrumentID,
                                                 const char_21 OrderRef, LfDirectionType direction, const LfOrderStatusType OrderStatus,const uint64_t VolumeTraded,int reqID)
{
    //add new orderId for GetAndHandleOrderTradeResponse
    std::lock_guard<std::mutex> guard_mutex(unit_mutex);
     KF_LOG_INFO(logger, "[addNewQueryOrdersAndTrades]");

    PendingBitmexOrderStatus status;
    memset(&status, 0, sizeof(PendingBitmexOrderStatus));
    strncpy(status.InstrumentID, InstrumentID, 31);
    strncpy(status.OrderRef, OrderRef, 21);
    status.OrderStatus = OrderStatus;
    status.VolumeTraded = VolumeTraded;
    status.averagePrice = 0.0;
	status.requestID = reqID;
    unit.newOrderStatus.push_back(status);
     KF_LOG_INFO(logger, "[addNewQueryOrdersAndTrades2222]");
	LFRtnOrderField order;
    memset(&order, 0, sizeof(LFRtnOrderField));
	order.OrderStatus = OrderStatus;
	order.VolumeTraded = VolumeTraded;
	strncpy(order.OrderRef, OrderRef, 21);
	strncpy(order.InstrumentID, InstrumentID, 31);
	order.RequestID = reqID;
	strcpy(order.ExchangeID, "BitMEX");
	strncpy(order.UserID, unit.api_key.c_str(), 16);
	order.TimeCondition = LF_CHAR_GTC;
	order.Direction = direction;

	unit.ordersMap.insert(std::make_pair(OrderRef, order));
    KF_LOG_INFO(logger, "[addNewQueryOrdersAndTrades] (InstrumentID) " << InstrumentID
                                                                       << " (OrderRef) " << OrderRef
                                                                       << "(VolumeTraded)" << VolumeTraded);
}


void TDEngineBitmex::set_reader_thread()
{
    ITDEngine::set_reader_thread();

    KF_LOG_INFO(logger, "[set_reader_thread] ws_thread start on TDEngineBitmex::wsloop");
    ws_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineBitmex::wsloop, this)));

}


void TDEngineBitmex::wsloop()
{
    KF_LOG_INFO(logger, "[loop] (isRunning) " << isRunning);
    while(isRunning)
    {
        int n = lws_service( context, base_interval_ms );
        //std::cout << " 3.1415 loop() lws_service (n)" << n << std::endl;
    }
}

std::vector<std::string> TDEngineBitmex::split(std::string str, std::string token)
{
    std::vector<std::string>result;
    while (str.size()) {
        size_t index = str.find(token);
        if (index != std::string::npos) {
            result.push_back(str.substr(0, index));
            str = str.substr(index + token.size());
            if (str.size() == 0)result.push_back(str);
        }
        else {
            result.push_back(str);
            str = "";
        }
    }
    return result;
}

void TDEngineBitmex::printResponse(const Document& d)
{
    if(d.IsObject() && d.HasMember("code")) {
        KF_LOG_INFO(logger, "[printResponse] error (code) " << d["code"].GetInt() << " (message) " << d["message"].GetString());
    } else {
        StringBuffer buffer;
        Writer<StringBuffer> writer(buffer);
        d.Accept(writer);
        KF_LOG_INFO(logger, "[printResponse] ok (text) " << buffer.GetString());
    }
}

std::string TDEngineBitmex::getLwsAuthReq(AccountUnitBitmex& unit) {
    std::string expires = std::to_string(getTimestamp() + g_RequestGap);
    std::string message = "GET/realtime" + expires;
    std::string signature = hmac_sha256(unit.secret_key.c_str(), message.c_str());
    return "\"" + unit.api_key + "\"," + expires + ",\"" + signature + "\"";
}
std::string TDEngineBitmex::getLwsSubscribe(AccountUnitBitmex& unit) {
    return R"("order","execution")";
}

void TDEngineBitmex::handleResponse(cpr::Response rsp, Document& json)
{
	std::lock_guard<std::mutex> lck(unit_mutex);
	auto& header = rsp.header;
	std::stringstream stream;
	for (auto& item : header)
	{
		stream << item.first << ':' << item.second << ',';
	}
	KF_LOG_INFO(logger, "[handleResponse] (header) " << stream.str());
    
    if(rsp.status_code == HTTP_RESPONSE_OK)
    {
        auto iter = header.find("x-ratelimit-remaining");
        if(iter != header.end())
        {
            m_limitRate_Remain = atoi(iter->second.c_str());
        }
        else
            m_limitRate_Remain =300;
        iter = header.find("x-ratelimit-reset");
        if(iter != header.end())
        {
            m_TimeStamp_Reset = atoll(iter->second.c_str());
            rest_get_interval_ms = (m_TimeStamp_Reset - getTimestamp())*1000;
            rest_get_interval_ms = std::max(rest_get_interval_ms,base_interval_ms);

        } 
		else
        {
            m_TimeStamp_Reset =getTimestamp();
            rest_get_interval_ms = 0;
        }
    } 
	else if(rsp.status_code == 429)
    {
        auto iter  = header.find("Retry-After");
        if(iter != header.end())
        {
            rest_get_interval_ms = atoll(iter->second.c_str());
        }
		else
		{
			rest_get_interval_ms = base_interval_ms;
		}
    }
    else if(rsp.status_code == 503)
    {
        rest_get_interval_ms =base_interval_ms;
	}
	else
	{
		rest_get_interval_ms = 0;
	}
	return getResponse(rsp.status_code, rsp.text, rsp.error.message, json);
}

//an error:
/*
 * {"error": {
      "message": "...",
      "name": "HTTPError" | "ValidationError" | "WebsocketError" | "Error"
    }}
 * */
void TDEngineBitmex::getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& json)
{
    if(http_status_code == HTTP_RESPONSE_OK)
    {
        KF_LOG_INFO(logger, "[getResponse] (http_status_code == 200) (responseText)" << responseText << " (errorMsg) " << errorMsg);
        json.Parse(responseText.c_str());
        KF_LOG_INFO(logger, "[getResponse] (http_status_code == 200) (HasParseError)" << json.HasParseError());
    } else if(http_status_code == 0 && responseText.length() == 0)
    {
        json.SetObject();
        Document::AllocatorType& allocator = json.GetAllocator();
        int errorId = 1;
        json.AddMember("code", errorId, allocator);
        //KF_LOG_INFO(logger, "[getResponse] (errorMsg)" << errorMsg);
        rapidjson::Value val;
        val.SetString(errorMsg.c_str(), errorMsg.length(), allocator);
        json.AddMember("message", val, allocator);
    } else
    {
        Document d;
        d.Parse(responseText.c_str());
        //KF_LOG_INFO(logger, "[getResponse] (err) (responseText)" << responseText.c_str());

        json.SetObject();
        Document::AllocatorType& allocator = json.GetAllocator();
        json.AddMember("code", http_status_code, allocator);
        if(!d.HasParseError() && d.IsObject()) {
            if( d.HasMember("message")) {
                //KF_LOG_INFO(logger, "[getResponse] (err) (errorMsg)" << d["message"].GetString());
                std::string message = d["message"].GetString();
                rapidjson::Value val;
                val.SetString(message.c_str(), message.length(), allocator);
                json.AddMember("message", val, allocator);
            }
        } else {
            rapidjson::Value val;
            val.SetString(errorMsg.c_str(), errorMsg.length(), allocator);
            json.AddMember("message", val, allocator);
        }
    }
}


void TDEngineBitmex::get_account(AccountUnitBitmex& unit, Document& json)
{
    KF_LOG_INFO(logger, "[get_account]");
    std::string Timestamp = std::to_string(getTimestamp()+g_RequestGap);
    std::string Method = "GET";
    std::string requestPath = "/api/v1/position";
    std::string queryString= "?count=100000";
    std::string body = "";
    string Message = Method + requestPath + queryString + Timestamp + body;

    std::string signature = hmac_sha256(unit.secret_key.c_str(), Message.c_str());
    string url = unit.baseUrl + requestPath + queryString;

	std::lock_guard<std::mutex> lck(g_reqMutex);
    const auto response = Get(Url{url},
                                 Header{{"api-key", unit.api_key},
                                        {"Content-Type", "application/json"},
                                        {"api-signature", signature},
                                        {"api-expires", Timestamp }},
                                 Timeout{30000});

    KF_LOG_INFO(logger, "[get_account] (url) " << url  << " (response.status_code) " << response.status_code <<
                                                " (response.error.message) " << response.error.message <<
                                                " (response.text) " << response.text.c_str());

    return handleResponse(response,json);
}

void TDEngineBitmex::get_products(AccountUnitBitmex& unit, Document& json)
{
 /*
[
  {
    "symbol": "XBTZ14",
    "rootSymbol": "XBT",
    "state": "Settled",
    ...........
    "maxOrderQty": 10000000,
    "maxPrice": 1000000,
    "lotSize": 1,
    "tickSize": 0.01,
    "multiplier": 1000,
	.........
  },
  .......
  ]
  * */
    KF_LOG_INFO(logger, "[get_products]");
    std::string Timestamp = std::to_string(getTimestamp());
    std::string Method = "GET";
    std::string requestPath = "/api/v1/instrument/activeAndIndices";
    std::string queryString= "";
    std::string body = "";

    string url = unit.baseUrl + requestPath;
	std::lock_guard<std::mutex> lck(g_reqMutex);
    const auto response = Get(Url{url},
                              Header{
                                     {"Content-Type", "application/json"}},
                                     Timeout{10000} );

    KF_LOG_INFO(logger, "[get_products] (url) " << url  << " (response.status_code) " << response.status_code <<
                                                     " (response.error.message) " << response.error.message <<
                                                     " (response.text) " << response.text.c_str());

	return handleResponse(response, json);
}

//https://www.bitmex.com/api/explorer/#!/Order/Order_new
void TDEngineBitmex::send_order(AccountUnitBitmex& unit, const char *code,
                                     const char *side, const char *type, double size, double price,  std::string orderRef, Document& json)
{
    KF_LOG_INFO(logger, "[send_order]");

    std::string priceStr;
    std::stringstream convertPriceStream;
    convertPriceStream <<std::fixed << std::setprecision(8) << price;
    convertPriceStream >> priceStr;

    std::string sizeStr;
    std::stringstream convertSizeStream;
    convertSizeStream <<std::fixed << std::setprecision(8) << size;
    convertSizeStream >> sizeStr;

    KF_LOG_INFO(logger, "[send_order] (code) " << code << " (side) "<< side << " (type) " <<
                                               type << " (size) "<< sizeStr << " (price) "<< priceStr);

    Document document;
    document.SetObject();
    Document::AllocatorType& allocator = document.GetAllocator();
    //used inner this method only.so  can use reference
    /*Instrument symbol. e.g. 'XBTUSD'.
    * */
    document.AddMember("symbol", StringRef(code), allocator);
    /*Order side. Valid options: Buy, Sell. Defaults to 'Buy' unless orderQty or simpleOrderQty is negative.
    * */
    document.AddMember("side", StringRef(side), allocator);
    /*Order type. Valid options: Market, Limit, Stop, StopLimit, MarketIfTouched, LimitIfTouched, MarketWithLeftOverAsLimit, Pegged. Defaults to 'Limit' when price is specified. Defaults to 'Stop' when stopPx is specified. Defaults to 'StopLimit' when price and stopPx are specified.
     * */
    document.AddMember("ordType", StringRef(type), allocator);
    /*simpleOrderQty:   Order quantity in units of the underlying instrument (i.e. Bitcoin).
     *
     * orderQty: Order quantity in units of the instrument (i.e. contracts).
     * */
    document.AddMember("orderQty", StringRef(sizeStr.c_str()), allocator);
    /*
     * Optional limit price for 'Limit', 'StopLimit', and 'LimitIfTouched' orders.
     * */
	if (strcmp(type, "Limit") == 0)
	{
		document.AddMember("price", StringRef(priceStr.c_str()), allocator);
	}
    /*
     * clOrdID : Optional Client Order ID. This clOrdID will come back on the order and any related executions.
     * */
    document.AddMember("clOrdID", StringRef(orderRef.c_str()), allocator);

    StringBuffer jsonStr;
    Writer<StringBuffer> writer(jsonStr);
    document.Accept(writer);

    std::string Timestamp = std::to_string(getTimestamp()+g_RequestGap);
    std::string Method = "POST";
    std::string requestPath = "/api/v1/order";
    std::string queryString= "";
    std::string body = jsonStr.GetString();

    string Message = Method + requestPath + queryString + Timestamp + body;
    KF_LOG_INFO(logger, "[send_order] (Message)" << Message);

    std::string signature = hmac_sha256(unit.secret_key.c_str(), Message.c_str());
    string url = unit.baseUrl + requestPath + queryString;

    /*
    #
    # POST
    #
    verb = 'POST'
    path = '/api/v1/order'
    expires = 1518064238 # 2018-02-08T04:30:38Z
    data = '{"symbol":"XBTM15","price":219.0,"clOrdID":"mm_bitmex_1a/oemUeQ4CAJZgP3fjHsA","orderQty":98}'

    # HEX(HMAC_SHA256(apiSecret, 'POST/api/v1/order1518064238{"symbol":"XBTM15","price":219.0,"clOrdID":"mm_bitmex_1a/oemUeQ4CAJZgP3fjHsA","orderQty":98}'))
    # Result is:
    # '1749cd2ccae4aa49048ae09f0b95110cee706e0944e6a14ad0b3a8cb45bd336b'
    signature = HEX(HMAC_SHA256(apiSecret, verb + path + str(expires) + data))

     * */
    const auto response = Post(Url{url},
                               Header{{"api-key", unit.api_key},
                                      {"Accept", "application/json"},
                                      {"Content-Type", "application/json"},
                                      {"Content-Length", to_string(body.size())},
                                      {"api-signature", signature},
                                      {"api-expires", Timestamp}},
                               Body{body}, Timeout{30000});


    //{ "error": {"message": "Authorization Required","name": "HTTPError"} }
    KF_LOG_INFO(logger, "[send_order] (url) " << url << " (body) "<< body << " (response.status_code) " << response.status_code <<
                                              " (response.error.message) " << response.error.message <<
                                              " (response.text) " << response.text.c_str());
	return handleResponse(response, json);
}


void TDEngineBitmex::cancel_all_orders(AccountUnitBitmex& unit, Document& json)
{
    KF_LOG_INFO(logger, "[cancel_all_orders]");
    std::string Timestamp = std::to_string(getTimestamp()+g_RequestGap);
    std::string Method = "DELETE";
    std::string requestPath = "/api/v1/order/all";
    std::string queryString= "";
    std::string body = "";

    string Message = Method + requestPath + queryString + Timestamp + body;

    std::string signature = hmac_sha256(unit.secret_key.c_str(), Message.c_str());
    string url = unit.baseUrl + requestPath;
	std::lock_guard<std::mutex> lck(g_reqMutex);
    const auto response = Delete(Url{url},
                                 Header{{"api-key", unit.api_key},
                                        {"Content-Type", "application/json"},
                                        {"api-signature", signature},
                                        {"api-expires", Timestamp }},
                                 Timeout{30000});

    KF_LOG_INFO(logger, "[cancel_all_orders] (url) " << url  << " (response.status_code) " << response.status_code <<
                                                     " (response.error.message) " << response.error.message <<
                                                     " (response.text) " << response.text.c_str());
	return handleResponse(response, json);
}


void TDEngineBitmex::cancel_order(AccountUnitBitmex& unit, std::string orderId, Document& json)
{
    KF_LOG_INFO(logger, "[cancel_order]");
    std::string Timestamp = std::to_string(getTimestamp()+g_RequestGap);
    std::string Method = "DELETE";
    std::string requestPath = "/api/v1/order";
    std::string queryString= "?clOrdID="+orderId;
    std::string body = "";

    string Message = Method + requestPath + queryString + Timestamp + body;
    std::string signature = hmac_sha256(unit.secret_key.c_str(), Message.c_str());

    string url = unit.baseUrl + requestPath + queryString;

    /*
     *
     #
    # GET with complex querystring (value is URL-encoded)
    #
    verb = 'GET'
    # Note url-encoding on querystring - this is '/api/v1/instrument?filter={"symbol": "XBTM15"}'
    # Be sure to HMAC *exactly* what is sent on the wire
    path = '/api/v1/instrument?filter=%7B%22symbol%22%3A+%22XBTM15%22%7D'
    expires = 1518064237 # 2018-02-08T04:30:37Z
    data = ''

    # HEX(HMAC_SHA256(apiSecret, 'GET/api/v1/instrument?filter=%7B%22symbol%22%3A+%22XBTM15%22%7D1518064237'))
    # Result is:
    # 'e2f422547eecb5b3cb29ade2127e21b858b235b386bfa45e1c1756eb3383919f'
    signature = HEX(HMAC_SHA256(apiSecret, verb + path + str(expires) + data))

    #
     * */
	std::lock_guard<std::mutex> lck(g_reqMutex);
    const auto response = Delete(Url{url},
                                 Header{{"api-key", unit.api_key},
                                        {"Content-Type", "application/json"},
                                        {"api-signature", signature},
                                        {"api-expires", Timestamp }},
                                 Timeout{30000});

    KF_LOG_INFO(logger, "[cancel_order] (url) " << url  << " (body) "<< body << " (response.status_code) " << response.status_code <<
                                                " (response.error.message) " << response.error.message <<
                                                " (response.text) " << response.text.c_str());
	return handleResponse(response, json);
}



inline int64_t TDEngineBitmex::getTimestamp()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp/1000;
}




void TDEngineBitmex::on_lws_connection_error(struct lws* conn)
{
    KF_LOG_ERROR(logger, "TDEngineBitmex::on_lws_connection_error.");
    //market logged_in false;
    AccountUnitBitmex& unit = findAccountUnitByWebsocketConn(conn);
    unit.logged_in = false;
    KF_LOG_ERROR(logger, "TDEngineBitmex::on_lws_connection_error. login again.");

    long timeout_nsec = 0;
    unit.newPendingSendMsg.push_back(createAuthJsonString(unit ));
    lws_login(unit, timeout_nsec);
}

int TDEngineBitmex::lws_write_subscribe(struct lws* conn)
{
    //KF_LOG_INFO(logger,"TDEngineBitmex::lws_write_subscribe");
    auto& unit = findAccountUnitByWebsocketConn(conn);
    std::string reqMsg,args;
    if(unit.wsStatus == 0)
    {
		args = getLwsAuthReq(unit);
		reqMsg = "{\"op\": \"authKeyExpires\", \"args\": [" + args + "]}";
    }
	else if (unit.wsStatus == 1)
	{
		args = getLwsSubscribe(unit);
		reqMsg = "{\"op\": \"subscribe\", \"args\": [" + args + "]}";
	}
	else
    {
	    return 0;
		args = getLwsSubscribe(unit);
		reqMsg = "{\"op\": \"unsubscribe\", \"args\": [" + args + "]}";
    }
	int length = reqMsg.length();
    unsigned char *msg  = new unsigned char[LWS_PRE+ length];
    memset(&msg[LWS_PRE], 0, length);
    KF_LOG_INFO(logger, "TDEngineBitfinex::lws_write_subscribe: " + reqMsg);
  

    strncpy((char *)msg+LWS_PRE, reqMsg.c_str(), length);
    int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);

    if(unit.wsStatus == 0)
    {    //still has pending send data, emit a lws_callback_on_writable()
        lws_callback_on_writable( conn );
    }
	unit.wsStatus += 1;
    return ret;
}

void TDEngineBitmex::lws_login(AccountUnitBitmex& unit, long timeout_nsec) {
    KF_LOG_INFO(logger, "TDEngineBitmex::lws_login:");
    global_td = this;

    if (context == NULL) {
        struct lws_context_creation_info info;
        memset( &info, 0, sizeof(info) );

        info.port = CONTEXT_PORT_NO_LISTEN;
        info.protocols = protocols;
        info.iface = NULL;
        info.ssl_cert_filepath = NULL;
        info.ssl_private_key_filepath = NULL;
        info.extensions = NULL;
        info.gid = -1;
        info.uid = -1;
        info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        info.max_http_header_pool = 1024;
        info.fd_limit_per_thread = 1024;
        info.ws_ping_pong_interval = 10;
        info.ka_time = 10;
        info.ka_probes = 10;
        info.ka_interval = 10;

        context = lws_create_context( &info );
        KF_LOG_INFO(logger, "TDEngineBitmex::lws_login: context created.");
    }

    if (context == NULL) {
        KF_LOG_ERROR(logger, "TDEngineBitmex::lws_login: context is NULL. return");
        return;
    }

    int logs = LLL_ERR | LLL_DEBUG | LLL_WARN;
    lws_set_log_level(logs, NULL);

    struct lws_client_connect_info ccinfo = {0};

    static std::string host  = unit.wsUrl;
    static std::string path = "/realtime";
    static int port = 443;

    ccinfo.context 	= context;
    ccinfo.address 	= host.c_str();
    ccinfo.port 	= port;
    ccinfo.path 	= path.c_str();
    ccinfo.host 	= host.c_str();
    ccinfo.origin 	= host.c_str();
    ccinfo.ietf_version_or_minus_one = -1;
    ccinfo.protocol = protocols[0].name;
    ccinfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;

    unit.websocketConn = lws_client_connect_via_info(&ccinfo);
    KF_LOG_INFO(logger, "TDEngineBitmex::lws_login: Connecting to " <<  ccinfo.host << ":" << ccinfo.port << ":" << ccinfo.path);

    if (unit.websocketConn == NULL) {
        KF_LOG_ERROR(logger, "TDEngineBitmex::lws_login: wsi create error.");
        return;
    }
    KF_LOG_INFO(logger, "TDEngineBitmex::lws_login: wsi create success.");
}


void TDEngineBitmex::on_lws_data(struct lws* conn, const char* data, size_t len) {
    AccountUnitBitmex &unit = findAccountUnitByWebsocketConn(conn);
    KF_LOG_INFO(logger, "TDEngineBitmex::on_lws_data: " << data);
    Document json;
    json.Parse(data,len);
    if (json.HasParseError() || !json.IsObject()) {
        KF_LOG_ERROR(logger, "TDEngineBitmex::on_lws_data. parse json error: " << data);        
    }
	else if (json.HasMember("error"))
	{
		KF_LOG_ERROR(logger, "TDEngineBitmex::on_lws_data. subscribe error: " << json["error"].GetString());
	}
	else if(json.HasMember("subscribe"))
	{
		KF_LOG_ERROR(logger, "TDEngineBitmex::on_lws_data. subscribe sucess ");
	}
	else if(json.HasMember("table"))
	{
		std::string  tablename = json["table"].GetString();
		if (tablename == "order")
		{
			onOrder(conn, json);
		}
		else if (tablename == "execution")
		{
			onTrade(conn, json);
		}
	}
}



AccountUnitBitmex& TDEngineBitmex::findAccountUnitByWebsocketConn(struct lws * websocketConn)
{
    for (size_t idx = 0; idx < account_units.size(); idx++) {
        AccountUnitBitmex &unit = account_units[idx];
        if(unit.websocketConn == websocketConn) {
            return unit;
        }
    }
    return account_units[0];
}





void TDEngineBitmex::onOrder(struct lws* conn, Document& json) {
	KF_LOG_INFO(logger, "TDEngineBitmex::onOrder");

    if (json.HasMember("data") && json["data"].IsArray()) {
		AccountUnitBitmex &unit = findAccountUnitByWebsocketConn(conn);
		std::lock_guard<std::mutex> lck(unit_mutex);
		auto& arrayData = json["data"];
		for (SizeType index = 0; index < arrayData.Size(); ++index)
		{
			auto& order = arrayData[index];			
			std::string OrderRef= order["clOrdID"].GetString();
			auto it = unit.ordersMap.find(OrderRef);
			if (it == unit.ordersMap.end())
			{ 
				KF_LOG_ERROR(logger, "TDEngineBitmex::onOrder,no order match");
				continue;
			}
			LFRtnOrderField& rtn_order = it->second;
            char status=LF_CHAR_NotTouched;
			if (order.HasMember("ordStatus"))
            {   
                status = GetOrderStatus(order["ordStatus"].GetString());				
            }
            if(status == LF_CHAR_NotTouched &&  rtn_order.OrderStatus == status)
            {
                KF_LOG_INFO(logger, "TDEngineBitmex::onOrder,status is not changed");
                continue;
            }
            rtn_order.OrderStatus = status;
			if (order.HasMember("leavesQty"))
				rtn_order.VolumeTotal = int64_t(order["leavesQty"].GetDouble()*scale_offset);	
			if (order.HasMember("side"))
				rtn_order.Direction = GetDirection(order["side"].GetString());
			if (order.HasMember("ordType"))
				rtn_order.OrderPriceType = GetPriceType(order["ordType"].GetString());
			if (order.HasMember("orderQty"))
				rtn_order.VolumeTotalOriginal = int64_t(order["orderQty"].GetDouble()*scale_offset);
			if (order.HasMember("price") && order["price"].IsNumber())
				rtn_order.LimitPrice = order["price"].GetDouble()*scale_offset;
			if (order.HasMember("cumQty"))
				rtn_order.VolumeTraded= int64_t(order["cumQty"].GetDouble()*scale_offset);
			KF_LOG_INFO(logger, "TDEngineBitmex::onOrder,rtn_order");
			on_rtn_order(&rtn_order);
			raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
				source_id, MSG_TYPE_LF_RTN_ORDER_BITMEX,
				1, (rtn_order.RequestID > 0) ? rtn_order.RequestID : -1);
			if (rtn_order.OrderStatus == LF_CHAR_AllTraded || rtn_order.OrderStatus == LF_CHAR_PartTradedNotQueueing ||
				rtn_order.OrderStatus == LF_CHAR_Canceled || rtn_order.OrderStatus == LF_CHAR_NoTradeNotQueueing || rtn_order.OrderStatus == LF_CHAR_Error)
			{
				unit.ordersMap.erase(it);
				localOrderRefRemoteOrderId.erase(OrderRef);
			}
		}
       
    }

}
void TDEngineBitmex::onTrade(struct lws * websocketConn, Document& json)
{ 
	KF_LOG_ERROR(logger, "TDEngineBitmex::onTrade");
	//if(json.HasMember("action") && json["action"].GetString() == std::string("insert"))
	if (json.HasMember("data") && json["data"].IsArray()) {
		AccountUnitBitmex &unit = findAccountUnitByWebsocketConn(websocketConn);
		std::lock_guard<std::mutex> lck(unit_mutex);
		auto& arrayData = json["data"];
		for (SizeType index = 0; index < arrayData.Size(); ++index)
		{
			
			auto& trade = arrayData[index];
			//send OnRtnTrade
			LFRtnTradeField rtn_trade;
			memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
			strncpy(rtn_trade.OrderRef, trade["clOrdID"].GetString(), 13);
			auto it = unit.ordersMap.find(rtn_trade.OrderRef);
			if (it == unit.ordersMap.end())
			{	
				KF_LOG_ERROR(logger, "TDEngineBitmex::onTrade,not match" << rtn_trade.OrderRef);
				continue;
			}
			auto& order = it->second;
			strcpy(rtn_trade.ExchangeID, "BitMEX");
			strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
			strncpy(rtn_trade.InstrumentID, it->second.InstrumentID, 31);
			rtn_trade.Direction = order.Direction;
			
			if(trade.HasMember("lastQty") && trade["lastQty"].IsNumber())
				rtn_trade.Volume = int64_t(trade["lastQty"].GetDouble()*scale_offset);
			else
				continue;
			if (trade.HasMember("lastPx") && trade["lastPx"].IsNumber())
				rtn_trade.Price = int64_t(trade["lastPx"].GetDouble()*scale_offset);
			else
				continue;
			KF_LOG_ERROR(logger, "TDEngineBitmex::onTrade,rtn_trade");
			on_rtn_trade(&rtn_trade);
			raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
				source_id, MSG_TYPE_LF_RTN_TRADE_BITMEX, 1, -1);
		}
	}
}



std::string TDEngineBitmex::createAuthJsonString(AccountUnitBitmex& unit )
{
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();
    writer.Key("event");
    writer.String("signin");

    writer.Key("params");
    writer.StartObject();

    writer.Key("api_key");
    writer.String(unit.api_key.c_str());


    writer.EndObject();
    writer.EndObject();
    return s.GetString();
}

/*
 {
    event: "subscribe",
    params: {
        "type": "orders",
        "zip": false,
        "biz": "spot",
    }
}

 * */
std::string TDEngineBitmex::createOrderJsonString()
{
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();
    writer.Key("event");
    writer.String("subscribe");

    writer.Key("params");
    writer.StartObject();

    writer.Key("type");
    writer.String("orders");

    writer.Key("zip");
    writer.Bool(false);

    writer.Key("biz");
    writer.String("spot");

    writer.EndObject();
    writer.EndObject();
    return s.GetString();
}


#define GBK2UTF8(msg) kungfu::yijinjing::gbk2utf8(string(msg))

BOOST_PYTHON_MODULE(libbitmextd)
{
    using namespace boost::python;
    class_<TDEngineBitmex, boost::shared_ptr<TDEngineBitmex> >("Engine")
            .def(init<>())
            .def("init", &TDEngineBitmex::initialize)
            .def("start", &TDEngineBitmex::start)
            .def("stop", &TDEngineBitmex::stop)
            .def("logout", &TDEngineBitmex::logout)
            .def("wait_for_stop", &TDEngineBitmex::wait_for_stop);
}
