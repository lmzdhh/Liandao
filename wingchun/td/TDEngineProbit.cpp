#include "TDEngineProbit.h"
#include "longfist/ctp.h"
#include "longfist/LFUtils.h"
#include "TypeConvert.hpp"
#include <boost/algorithm/string.hpp>
#include <mutex>
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
#include "../../utils/crypto/openssl_util.h"
using cpr::Delete;
using cpr::Get;
using cpr::Url;
using cpr::Body;
using cpr::Header;
using cpr::Parameters;
using cpr::Payload;
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

std::mutex  g_orderMutex;
USING_WC_NAMESPACE

int g_RequestGap=5*60;
#define PARTIAL_TRADED "partialtraded"
static TDEngineProbit* global_td = nullptr;

static int ws_service_cb( struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len )
{

    switch( reason )
    {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
        {
            lws_callback_on_writable(wsi);
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
            if(global_td)
            {
                global_td->on_lws_connection_error(wsi);
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_RECEIVE_PONG:
        {
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

static struct lws_protocols protocols[] = {{"md-protocol", ws_service_cb, 0, 65536}, { NULL, NULL, 0, 0 }};


TDEngineProbit::TDEngineProbit(): ITDEngine(SOURCE_PROBIT)
{
    logger = yijinjing::KfLog::getLogger("TradeEngine.Probit");
    KF_LOG_INFO(logger, "[TDEngineProbit]");
}

TDEngineProbit::~TDEngineProbit()
{}

void TDEngineProbit::init()
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

void TDEngineProbit::pre_load(const json& j_config)
{
    KF_LOG_INFO(logger, "[pre_load]");
}

void TDEngineProbit::resize_accounts(int account_num)
{
    account_units.resize(account_num);
    KF_LOG_INFO(logger, "[resize_accounts]");
}

TradeAccount TDEngineProbit::load_account(int idx, const json& j_config)
{
    KF_LOG_INFO(logger, "[load_account]");
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();
    AccountUnitProbit& unit = account_units[idx];
    unit.api_key = j_config["APIKey"].get<string>();
    unit.secret_key = j_config["SecretKey"].get<string>();
    unit.baseUrl = j_config["baseUrl"].get<string>();
	unit.authUrl = j_config["acountUrl"].get<string>();
    unit.wsUrl = j_config["wsUrl"].get<string>();
    KF_LOG_INFO(logger, "[load_account] (api_key)" << unit.api_key  << " (baseUrl)" << unit.baseUrl);
    unit.coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
    unit.coinPairWhiteList.Debug_print();
    unit.positionWhiteList.ReadWhiteLists(j_config, "positionWhiteLists");
    unit.positionWhiteList.Debug_print();
    if(unit.coinPairWhiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "TDEngineProbit::load_account: please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"BTC_USDT\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"ETC_ETH\"");
        KF_LOG_ERROR(logger, "},");
    }
    cancel_all_orders(unit);
    // set up
    TradeAccount account = {};
    //partly copy this fields
    strncpy(account.UserID, unit.api_key .c_str(), 16);
    strncpy(account.Password, unit.secret_key.c_str(), 21);
    return account;
}


void TDEngineProbit::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[connect]");
    for (int idx = 0; idx < account_units.size(); idx ++)
    {
        AccountUnitProbit& unit = account_units[idx];
        KF_LOG_INFO(logger, "[connect] (api_key)" << unit.api_key);
        if (!unit.logged_in)
        {
            //exchange infos
            Document doc;
            //TODO
            get_products(unit, doc);
            KF_LOG_INFO(logger, "[connect] get_products");
            printResponse(doc);
            loadExchangeOrderFilters(unit);
            debug_print(unit.sendOrderFilters);
			lws_login(unit, 0);
            unit.logged_in = true;
        }
    }
}

//TODO
bool TDEngineProbit::loadExchangeOrderFilters(AccountUnitProbit& unit)
{
    KF_LOG_INFO(logger, "[loadExchangeOrderFilters]");
    //changelog 2018-07-20. use hardcode mode
    /*
    BTC_USDT	0.0001		4
    ETH_USDT	0.0001		4
    LTC_USDT	0.0001		4
    BCH_USDT	0.0001		4
    ETC_USDT	0.0001		4
    ETC_ETH	0.00000001		8
    LTC_BTC	0.00000001		8
    BCH_BTC	0.00000001		8
    ETH_BTC	0.00000001		8
    ETC_BTC	0.00000001		8
     * */
    SendOrderFilter afilter;

    strncpy(afilter.InstrumentID, "BTC_USDT", 31);
    afilter.ticksize = 4;
    unit.sendOrderFilters.insert(std::make_pair("BTC_USDT", afilter));

    strncpy(afilter.InstrumentID, "ETH_USDT", 31);
    afilter.ticksize = 4;
    unit.sendOrderFilters.insert(std::make_pair("ETH_USDT", afilter));

    strncpy(afilter.InstrumentID, "LTC_USDT", 31);
    afilter.ticksize = 4;
    unit.sendOrderFilters.insert(std::make_pair("LTC_USDT", afilter));

    strncpy(afilter.InstrumentID, "BCH_USDT", 31);
    afilter.ticksize = 4;
    unit.sendOrderFilters.insert(std::make_pair("BCH_USDT", afilter));

    strncpy(afilter.InstrumentID, "ETC_USDT", 31);
    afilter.ticksize = 4;
    unit.sendOrderFilters.insert(std::make_pair("ETC_USDT", afilter));

    strncpy(afilter.InstrumentID, "ETC_ETH", 31);
    afilter.ticksize = 8;
    unit.sendOrderFilters.insert(std::make_pair("ETC_ETH", afilter));

    strncpy(afilter.InstrumentID, "LTC_BTC", 31);
    afilter.ticksize = 8;
    unit.sendOrderFilters.insert(std::make_pair("LTC_BTC", afilter));

    strncpy(afilter.InstrumentID, "BCH_BTC", 31);
    afilter.ticksize = 8;
    unit.sendOrderFilters.insert(std::make_pair("BCH_BTC", afilter));

    strncpy(afilter.InstrumentID, "ETH_BTC", 31);
    afilter.ticksize = 8;
    unit.sendOrderFilters.insert(std::make_pair("ETH_BTC", afilter));

    strncpy(afilter.InstrumentID, "ETC_BTC", 31);
    afilter.ticksize = 8;
    unit.sendOrderFilters.insert(std::make_pair("ETC_BTC", afilter));

    //parse bitmex json
    /*
     [{"baseCurrency":"LTC","baseMaxSize":"100000.00","baseMinSize":"0.001","code":"LTC_BTC","quoteCurrency":"BTC","quoteIncrement":"8"},
     {"baseCurrency":"BCH","baseMaxSize":"100000.00","baseMinSize":"0.001","code":"BCH_BTC","quoteCurrency":"BTC","quoteIncrement":"8"},
     {"baseCurrency":"ETH","baseMaxSize":"100000.00","baseMinSize":"0.001","code":"ETH_BTC","quoteCurrency":"BTC","quoteIncrement":"8"},
     {"baseCurrency":"ETC","baseMaxSize":"100000.00","baseMinSize":"0.01","code":"ETC_BTC","quoteCurrency":"BTC","quoteIncrement":"8"},
     ...
     ]
     */
    return true;
}

void TDEngineProbit::debug_print(const std::map<std::string, SendOrderFilter> &sendOrderFilters)
{
    for(auto filterIter = sendOrderFilters.begin(); filterIter != sendOrderFilters.end();  ++filterIter)
    {
        KF_LOG_INFO(logger, "[debug_print] sendOrderFilters (symbol)" << filterIter->first << " (tickSize)" << filterIter->second.ticksize);
    }
}

SendOrderFilter TDEngineProbit::getSendOrderFilter(const AccountUnitProbit& unit, const char *symbol)
{
    for(auto filterIter = unit.sendOrderFilters.begin(); filterIter != unit.sendOrderFilters.end();  ++filterIter)
    {
        if(strcmp(filterIter->first.c_str(), symbol) == 0)
        {
            return filterIter->second;
        }
    }
    SendOrderFilter defaultFilter;
    defaultFilter.ticksize = 8;
    strcpy(defaultFilter.InstrumentID, "notfound");
    return defaultFilter;
}

void TDEngineProbit::login(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[login]");
    connect(timeout_nsec);
}

void TDEngineProbit::logout()
{
    KF_LOG_INFO(logger, "[logout]");
}

void TDEngineProbit::release_api()
{
    KF_LOG_INFO(logger, "[release_api]");
}

bool TDEngineProbit::is_logged_in() const
{
    KF_LOG_INFO(logger, "[is_logged_in]");
    for (auto& unit: account_units)
    {
        if (!unit.logged_in)
            return false;
    }
    return true;
}

bool TDEngineProbit::is_connected() const
{
    KF_LOG_INFO(logger, "[is_connected]");
    return is_logged_in();
}



std::string TDEngineProbit::GetSide(const LfDirectionType& input) {
    if (LF_CHAR_Buy == input) {
        return "buy";
    } else if (LF_CHAR_Sell == input) {
        return "sell";
    } else {
        return "";
    }
}

LfDirectionType TDEngineProbit::GetDirection(const std::string& direct) {
    if ("buy" == direct)
    {
        return LF_CHAR_Buy;
    }
    else if ("sell" == direct)
    {
        return LF_CHAR_Sell;
    }
    else
    {
        return LF_CHAR_Buy;
    }
}

std::string TDEngineProbit::GetType(const LfOrderPriceTypeType& input) {
    if (LF_CHAR_LimitPrice == input) {
        return "limit";
    } else if (LF_CHAR_AnyPrice == input) {
        return "market";
    } else {
        return "";
    }
}

LfOrderPriceTypeType TDEngineProbit::GetPriceType(const std::string& type)
{
    if ("limit" == type)
    {
        return LF_CHAR_LimitPrice;
    }
    else if ("market" == type)
    {
        return LF_CHAR_AnyPrice;
    }
    else
    {
        return '0';
    }
}
//订单状态，﻿open（未成交）、filled（已完成）、canceled（已撤销）
LfOrderStatusType TDEngineProbit::GetOrderStatus(const std::string& type)
{
    if("open" == type)
    {
        return LF_CHAR_NotTouched;
    }
    else if("filled" == type)
    {
        return LF_CHAR_AllTraded;
    }
    else if ("cancelled" == type)
    {
        return LF_CHAR_Canceled;
    }
    else if(PARTIAL_TRADED == type)
    {
        return LF_CHAR_PartTradedQueueing;
    }
}
LfTimeConditionType TDEngineProbit::GetTimeCondition(const std::string&)
{
	return LF_CHAR_GTC;
}
/**
 * req functions
 */
void TDEngineProbit::req_investor_position(const LFQryPositionField* data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId);

    AccountUnitProbit& unit = account_units[account_index];
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
    send_writer->write_frame(data, sizeof(LFQryPositionField), source_id, MSG_TYPE_LF_QRY_POS_PROBIT, 1, requestId);

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


/*
 # Response
    [{"available":"0.099","balance":"0.099","currencyCode":"BTC","hold":"0","id":83906},{"available":"188","balance":"188","currencyCode":"MVP","hold":"0","id":83906}]
 * */
    std::vector<LFRspPositionField> tmp_vector;
    if(d.HasMember("data") && d["data"].IsArray())
    {
		auto& dataArray = d["data"];
        size_t len = dataArray.Size();
        KF_LOG_INFO(logger, "[req_investor_position] (asset.length)" << len);
        for(size_t i = 0; i < len; i++)
        {
            std::string symbol = dataArray[i]["currency_id"].GetString();
            std::string ticker = unit.positionWhiteList.GetKeyByValue(symbol);
            if(ticker.length() > 0)
            {
                strncpy(pos.InstrumentID, ticker.c_str(), 31);
                pos.Position = std::round(std::stod(dataArray[i]["available"].GetString()) * scale_offset);
                tmp_vector.push_back(pos);
                KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId << " (symbol) " << symbol
                                                                          << " available:" << d.GetArray()[i]["available"].GetString()
                                                                          << " total: " << d.GetArray()[i]["total"].GetString());
                KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId << " (symbol) " << symbol << " (position) " << pos.Position);
            }
        }
    }

    bool findSymbolInResult = false;
    //send the filtered position
    for (int i = 0; i < tmp_vector.size(); i++)
    {
        on_rsp_position(&tmp_vector[i], i == (tmp_vector.size()- 1), requestId, errorId, errorMsg.c_str());
        findSymbolInResult = true;
    }

    if(!findSymbolInResult)
    {
        KF_LOG_INFO(logger, "[req_investor_position] (!findSymbolInResult) (requestId)" << requestId);
        on_rsp_position(&pos, 1, requestId, errorId, errorMsg.c_str());
    }
    if(errorId != 0)
    {
        raw_writer->write_error_frame(&pos, sizeof(LFRspPositionField), source_id, MSG_TYPE_LF_RSP_POS_PROBIT, 1, requestId, errorId, errorMsg.c_str());
    }
}

void TDEngineProbit::req_qry_account(const LFQryAccountField *data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_qry_account]");
}

int64_t TDEngineProbit::fixPriceTickSize(int keepPrecision, int64_t price, bool isBuy)
{
    if(keepPrecision == 8) return price;

    int removePrecisions = (8 - keepPrecision);
    double cutter = pow(10, removePrecisions);

    KF_LOG_INFO(logger, "[fixPriceTickSize input]" << " 1(price)" << std::fixed  << std::setprecision(9) << price);
    double new_price = price/cutter;
    KF_LOG_INFO(logger, "[fixPriceTickSize input]" << " 2(price/cutter)" << std::fixed  << std::setprecision(9) << new_price);
    if(isBuy)
    {
        new_price += 0.9;
        new_price = std::floor(new_price);
        KF_LOG_INFO(logger, "[fixPriceTickSize input]" << " 3(price is buy)" << std::fixed  << std::setprecision(9) << new_price);
    }
    else
    {
        new_price = std::floor(new_price);
        KF_LOG_INFO(logger, "[fixPriceTickSize input]" << " 3(price is sell)" << std::fixed  << std::setprecision(9) << new_price);
    }
    int64_t  ret_price = new_price * cutter;
    KF_LOG_INFO(logger, "[fixPriceTickSize input]" << " 4(new_price * cutter)" << std::fixed  << std::setprecision(9) << new_price);
    return ret_price;
}

int TDEngineProbit::Round(std::string tickSizeStr)
{
    size_t docAt = tickSizeStr.find( ".", 0 );
    size_t oneAt = tickSizeStr.find( "1", 0 );

    if(docAt == string::npos)
    {
        //not ".", it must be "1" or "10"..."100"
        return -1 * (tickSizeStr.length() -  1);
    }
    //there must exist 1 in the string.
    return oneAt - docAt;
}

std::string TDEngineProbit::TimeToFormatISO8601(int64_t timestamp)
{
	int ms = timestamp % 1000;
	tm utc_time;
	time_t time = timestamp/1000;
	gmtime_r(&time, &utc_time);
	char timeStr[50];
	sprintf(timeStr, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", utc_time.tm_year + 1900, utc_time.tm_mon + 1, utc_time.tm_mday,
		utc_time.tm_hour, utc_time.tm_min, utc_time.tm_sec,ms);
	return std::string(timeStr);
}

void TDEngineProbit::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitProbit& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_insert]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Tid)" << data->InstrumentID
                                              << " (Volume)" << data->Volume
                                              << " (LimitPrice)" << data->LimitPrice
                                              << " (OrderRef)" << data->OrderRef);
    send_writer->write_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_PROBIT, 1/*ISLAST*/, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0)
    {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_insert]: not in WhiteList, ignore it  (rid)" << requestId <<
                                                                                      " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_PROBIT, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_insert] (exchange_ticker)" << ticker);
    LFRtnOrderField order {};
    std::unique_lock<std::mutex> l(g_orderMutex);
    unit.ordersMap[data->OrderRef] = order;

    SendOrderFilter filter = getSendOrderFilter(unit, ticker.c_str());

    int64_t fixedPrice = fixPriceTickSize(filter.ticksize, data->LimitPrice, LF_CHAR_Buy == data->Direction);

    KF_LOG_DEBUG(logger, "[req_order_insert] SendOrderFilter  (Tid)" << ticker <<
                                                                     " (LimitPrice)" << data->LimitPrice <<
                                                                     " (ticksize)" << filter.ticksize <<
                                                                     " (fixedPrice)" << fixedPrice);
    Document rspjson;
	send_order(unit, ticker.c_str(), GetSide(data->Direction).c_str(),GetType(data->OrderPriceType).c_str(), data->Volume*1.0 / scale_offset, fixedPrice*1.0 / scale_offset, 0, data->OrderRef, rspjson);
    //not expected response
    if(rspjson.HasParseError() || !rspjson.IsObject())
    {
        errorId = 100;
        errorMsg = "send_order http response has parse error or is not json. please check the log";
        KF_LOG_ERROR(logger, "[req_order_insert] send_order error!  (rid)" << requestId << " (errorId)" <<
                                                                           errorId << " (errorMsg) " << errorMsg);
    }
	else if (rspjson.HasMember("data"))
	{

		auto& dataJson = rspjson["data"];
		//if send successful and the exchange has received ok, then add to  pending query order list
		std::string remoteOrderId = dataJson["id"].GetString();
		localOrderRefRemoteOrderId.insert(std::make_pair(std::string(data->OrderRef), remoteOrderId));
		KF_LOG_INFO(logger, "[req_order_insert] after send  (rid)" << requestId << " (OrderRef) " << data->OrderRef << " (remoteOrderId) " << remoteOrderId);
		
		//OpenOrderToLFOrder(unit, dataJson, order);
		//std::lock_guard<std::mutex> guard_mutex(g_orderMutex);

		//char noneStatus = GetOrderStatus(d["status"].GetString());//none
	   // addNewQueryOrdersAndTrades(unit, data->InstrumentID, data->OrderRef, noneStatus, 0,requestId);
		//success, only record raw data
		//raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_PROBIT, 1, requestId, errorId, errorMsg.c_str());
		//

		// do writer
		//on_rtn_order(&order);
		//do raw writer
        //raw_writer->write_frame(&order, sizeof(LFRtnOrderField), source_id, MSG_TYPE_LF_RTN_ORDER_PROBIT, 1, requestId);

	}
    else if (rspjson.HasMember("code") && rspjson["code"].IsNumber())
    {
        //send error, example: http timeout.
        errorId = rspjson["code"].GetInt();
        if(rspjson.HasMember("message") && rspjson["message"].IsString())
        {
            errorMsg = rspjson["message"].GetString();
        }
        KF_LOG_ERROR(logger, "[req_order_insert] failed!" << " (rid)" << requestId << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }

    if(errorId != 0)
    {
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        unit.ordersMap.erase(data->OrderRef);
    }
    raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_PROBIT, 1, requestId, errorId, errorMsg.c_str());
}


void TDEngineProbit::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitProbit& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_action]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Iid)" << data->InvestorID
                                              << " (OrderRef)" << data->OrderRef
                                              << " (KfOrderID)" << data->KfOrderID);

    send_writer->write_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_PROBIT, 1, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0)
    {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_action]: not in WhiteList , ignore it: (rid)" << requestId << " (errorId)" <<
                                                                                       errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_PROBIT, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_action] (exchange_ticker)" << ticker);

    auto remoteIter = localOrderRefRemoteOrderId.find(data->OrderRef);
    std::unique_lock<std::mutex> l(g_orderMutex);
	auto orderIter = unit.ordersMap.find(data->OrderRef);
    if(remoteIter == localOrderRefRemoteOrderId.end() || orderIter == unit.ordersMap.end())
    {
        errorId = 1;
        std::stringstream ss;
        ss << "[req_order_action] not found in localOrderRefRemoteOrderId map (orderRef) " << data->OrderRef;
        errorMsg = ss.str();
        KF_LOG_ERROR(logger, "[req_order_action] not found in localOrderRefRemoteOrderId map. " <<
                            " (rid)" << requestId << \
                            " (orderRef)" << data->OrderRef << \
                            " (errorId)" << errorId << \
                            " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_PROBIT, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
   
    
    KF_LOG_DEBUG(logger, "[req_order_action] found in localOrderRefRemoteOrderId map (orderRef) " << data->OrderRef);
	auto remoteID = remoteIter->second;
	auto order = orderIter->second;
	//

    Document d;
	cancel_order(unit, remoteID, ticker, order.VolumeTotal*1.0/scale_offset, d);

    //cancel order response "" as resultText, it cause json.HasParseError() == true, and json.IsObject() == false.
    //it is not an error, so dont check it.
    //not expected response
    if(d.IsObject() && !d.HasParseError() && d.HasMember("code") && d["code"].IsNumber())
    {
        errorId = d["code"].GetInt();
        if(d.HasMember("message") && d["message"].IsString())
        {
            errorMsg = d["message"].GetString();
        }
		KF_LOG_ERROR(logger, "[req_order_action] cancel_order failed!" << " (rid)" << requestId << " (errorId)" << errorId << " (errorMsg) " << errorMsg);;
		on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
    }
	else
	{
		KF_LOG_ERROR(logger, "[req_order_action] cancel_order success!" << " (rid)" << requestId <<"(order ref)"<< data->OrderRef);
	}
	
    raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_PROBIT, 1, requestId, errorId, errorMsg.c_str());
}


//
bool TDEngineProbit::OpenOrderToLFOrder(AccountUnitProbit& unit, rapidjson::Value& json, LFRtnOrderField& order)
{
	//memset(&order, 0, sizeof(LFRtnOrderField));
	KF_LOG_INFO(logger, "[OpenOrderToLFOrder] " << json.IsObject());
	if (json.IsObject())
	{
		std::string marketID = json["market_id"].GetString();
		std::string ticker = unit.coinPairWhiteList.GetKeyByValue(marketID);
		if (ticker.length() == 0)
		{
			return false;
		}
		order.OrderStatus = GetOrderStatus(json["status"].GetString());
		order.VolumeTraded = int64_t(atof(json["filled_quantity"].GetString())*scale_offset);
		order.VolumeTotalOriginal = int64_t(atof(json["open_quantity"].GetString())*scale_offset);
		order.VolumeTotal = order.VolumeTotalOriginal - order.VolumeTraded;
		strncpy(order.OrderRef, json["client_order_id"].GetString(), 21);
		strncpy(order.InstrumentID, ticker.c_str(), 31);
		strcpy(order.ExchangeID, "Probit");
		strncpy(order.UserID, unit.api_key.c_str(), 16);
		order.TimeCondition = LF_CHAR_GTC;
		KF_LOG_INFO(logger, "[OpenOrderToLFOrder] (InstrumentID) " << ticker.c_str());
		return true;
	}
	return false;
}



void TDEngineProbit::addNewQueryOrdersAndTrades(AccountUnitProbit& unit, const char_31 InstrumentID,
                                                 const char_21 OrderRef, const LfOrderStatusType OrderStatus, const uint64_t VolumeTraded,int reqID)
{
    //add new orderId for GetAndHandleOrderTradeResponse
    //std::lock_guard<std::mutex> guard_mutex(g_orderMutex);

    PendingOrderStatus status;
    memset(&status, 0, sizeof(PendingOrderStatus));
    strncpy(status.InstrumentID, InstrumentID, 31);
    strncpy(status.OrderRef, OrderRef, 21);
    status.OrderStatus = OrderStatus;
    status.VolumeTraded = VolumeTraded;
    status.averagePrice = 0.0;
	status.requestID = reqID;
    unit.newOrderStatus.push_back(status);

	LFRtnOrderField order;	
	order.OrderStatus = OrderStatus;
	order.VolumeTraded = VolumeTraded;
	strncpy(order.OrderRef, OrderRef, 21);
	strncpy(order.InstrumentID, InstrumentID, 31);
	order.RequestID = reqID;
	strcpy(order.ExchangeID, "Probit");
	strncpy(order.UserID, unit.api_key.c_str(), 16);
	order.TimeCondition = LF_CHAR_GTC;

	unit.ordersMap.insert(std::make_pair(OrderRef, order));
    KF_LOG_INFO(logger, "[addNewQueryOrdersAndTrades] (InstrumentID) " << InstrumentID
                                                                       << " (OrderRef) " << OrderRef
                                                                       << "(VolumeTraded)" << VolumeTraded);
}


void TDEngineProbit::set_reader_thread()
{
    ITDEngine::set_reader_thread();

    KF_LOG_INFO(logger, "[set_reader_thread] ws_thread start on TDEngineProbit::wsloop");
    ws_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineProbit::wsloop, this)));

}


void TDEngineProbit::wsloop()
{
    KF_LOG_INFO(logger, "[loop] (isRunning) " << isRunning);
    while(isRunning)
    {
        lws_service( context, rest_get_interval_ms );
    }
}

void TDEngineProbit::printResponse(const Document& d)
{
    if(d.IsObject() && d.HasMember("code"))
    {
        KF_LOG_INFO(logger, "[printResponse] error (code) " << d["code"].GetInt() << " (message) " << d["message"].GetString());
    }
    else
    {
        StringBuffer buffer;
        Writer<StringBuffer> writer(buffer);
        d.Accept(writer);
        KF_LOG_INFO(logger, "[printResponse] ok (text) " << buffer.GetString());
    }
}




//an error:
/*
 * {"error": {
      "message": "...",
      "name": "HTTPError" | "ValidationError" | "WebsocketError" | "Error"
    }}
 * */
void TDEngineProbit::getResponse(int http_status_code, const std::string& responseText, const std::string& errorMsg, Document& json)
{
    if(http_status_code == HTTP_RESPONSE_OK)
    {
        KF_LOG_INFO(logger, "[getResponse] (http_status_code == 200) (responseText)" << responseText << " (errorMsg) " << errorMsg);
        json.Parse(responseText.c_str());
        KF_LOG_INFO(logger, "[getResponse] (http_status_code == 200) (HasParseError)" << json.HasParseError());
    }
    else if(http_status_code == 0 && responseText.size() == 0)
    {
        json.SetObject();
        Document::AllocatorType& allocator = json.GetAllocator();
        int errorId = 1;
        json.AddMember("code", errorId, allocator);
        //KF_LOG_INFO(logger, "[getResponse] (errorMsg)" << errorMsg);
        rapidjson::Value val;
        val.SetString(errorMsg.c_str(), errorMsg.size(), allocator);
        json.AddMember("message", val, allocator);
    }
    else
    {
        Document d;
        d.Parse(responseText.c_str());
        //KF_LOG_INFO(logger, "[getResponse] (err) (responseText)" << responseText.c_str());

        json.SetObject();
        Document::AllocatorType& allocator = json.GetAllocator();
        json.AddMember("code", http_status_code, allocator);
        if(!d.HasParseError() && d.IsObject())
        {
            if( d.HasMember("message"))
            {
                //KF_LOG_INFO(logger, "[getResponse] (err) (errorMsg)" << d["message"].GetString());
                std::string message = d["message"].GetString();
                rapidjson::Value val;
                val.SetString(message.c_str(), message.size(), allocator);
                json.AddMember("message", val, allocator);
            }
        }
        else
        {
            rapidjson::Value val;
            val.SetString(errorMsg.c_str(), errorMsg.size(), allocator);
            json.AddMember("message", val, allocator);
        }
    }
}


void TDEngineProbit::get_account(const AccountUnitProbit& unit, Document& json)
{
    KF_LOG_INFO(logger, "[get_account]");
    std::string requestPath = "/api/exchange/v1/balance";

    std::string authToken =getAuthToken(unit);
    string url = unit.baseUrl + requestPath;

    const auto response = Get(Url{url},
                                 Header{
                                        {"Content-Type", "application/json"},
                                        { "authorization", "Bearer " + authToken }},
                                 Timeout{30000});

    KF_LOG_INFO(logger, "[get_account] (url) " << url  << " (response.status_code) " << response.status_code <<
                                                " (response.error.message) " << response.error.message <<
                                                " (response.text) " << response.text.c_str());

    return getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineProbit::get_products(const AccountUnitProbit& unit, Document& json)
{
 
    KF_LOG_INFO(logger, "[get_products]");
	return;
}

void TDEngineProbit::send_order(const AccountUnitProbit& unit, const char *code, const char *side, const char *type, double size, double price,double cost, const std::string& orderRef,  Document& json)
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

	std::string costStr;
	std::stringstream convertCostStream;
	convertCostStream << std::fixed << std::setprecision(8) << cost;
	convertCostStream >> costStr;

    KF_LOG_INFO(logger, "[send_order] (code) " << code << " (side) "<< side << " (type) " <<
                                               type << " (size) "<< sizeStr << " (price) "<< priceStr);

    Document document;
    document.SetObject();
    Document::AllocatorType& allocator = document.GetAllocator();
    //used inner this method only.so  can use reference
    /*Instrument symbol. e.g. 'XBTUSD'.
    * */
    document.AddMember("market_id", StringRef(code), allocator);
    /*Order side. Valid options: Buy, Sell. Defaults to 'Buy' unless orderQty or simpleOrderQty is negative.
    * */
    document.AddMember("side", StringRef(side), allocator);
    /*Order type. Valid options: Market, Limit, Stop, StopLimit, MarketIfTouched, LimitIfTouched, MarketWithLeftOverAsLimit, Pegged. Defaults to 'Limit' when price is specified. Defaults to 'Stop' when stopPx is specified. Defaults to 'StopLimit' when price and stopPx are specified.
     * */
    document.AddMember("type", StringRef(type), allocator);
	document.AddMember("time_in_force", StringRef("gtc"), allocator);
    /*simpleOrderQty:   Order quantity in units of the underlying instrument (i.e. Bitcoin).
     *
     * orderQty: Order quantity in units of the instrument (i.e. contracts).
     * */
	rapidjson::Value nullObject(rapidjson::kNullType); 
	if (strcmp(type, "limit") == 0)
	{     
	      
	      document.AddMember("quantity", StringRef(sizeStr.c_str()), allocator);
	      document.AddMember("cost", nullObject,allocator);
	}
	else
	{
		if (strcmp(side, "buy") == 0)
		{
			document.AddMember("quantity", StringRef(""), allocator);
			document.AddMember("cost", StringRef(costStr.c_str()), allocator);
		}
		else
		{
			document.AddMember("quantity", StringRef(sizeStr.c_str()), allocator);
			document.AddMember("cost", StringRef(""), allocator);
		}
	}
    /*
     * Optional limit price for 'Limit', 'StopLimit', and 'LimitIfTouched' orders.
     * */
    document.AddMember("limit_price", StringRef(priceStr.c_str()), allocator);
	
    /*
     * clOrdID : Optional Client Order ID. This clOrdID will come back on the order and any related executions.
     * */
    document.AddMember("client_order_id", StringRef(orderRef.c_str()), allocator);

    StringBuffer jsonStr;
    Writer<StringBuffer> writer(jsonStr);
    document.Accept(writer);

    std::string requestPath = "/api/exchange/v1/new_order";
    std::string body = jsonStr.GetString();
	std::string authToken = getAuthToken(unit);
	string url = unit.baseUrl + requestPath;

	MyPost(url, "Bearer " + authToken, body, json);
}


void TDEngineProbit::cancel_all_orders(AccountUnitProbit& unit)
{
	return;
	//
    KF_LOG_INFO(logger, "[cancel_all_orders](get history open order)");
    std::string requestPath = "/api/exchange/v1/order_history";
	std::string authToken = getAuthToken(unit);	
	int64_t timeStamp = getTimestamp();
	std::string startTime = TimeToFormatISO8601(timeStamp - unit.gpTimes);
	std::string endTime = TimeToFormatISO8601(timeStamp);
	std::string reqParams = "start_time=" + startTime + "&end_time=" + endTime + "&limit=1000";
	string url = unit.baseUrl + requestPath + "?" + reqParams;

    const auto response = Get(Url{url},
                                 Header{{"Content-Type", "application/json"},
										{"authorization", "Bearer " + authToken} 
										},
                                 Timeout{30000});

    KF_LOG_INFO(logger, "[history_orders] (url) " << url  << " (response.status_code) " << response.status_code <<
                                                     " (response.error.message) " << response.error.message <<
                                                     " (response.text) " << response.text.c_str());
	Document d;
    getResponse(response.status_code, response.text, response.error.message, d);

	if (d.HasParseError() || !d.IsObject())
	{
		int errorId = 100;
		std::string errorMsg = "history_orders http response has parse error or is not json. please check the log";
		KF_LOG_ERROR(logger, "[history_orders] history_orders error!  (errorId)" << errorId << " (errorMsg) " << errorMsg);
	}
	else if (d.HasMember("data") && d["data"].IsArray())
	{
		auto& arrayData = d["data"];
		for (SizeType index = 0; index = arrayData.Size(); ++index)
		{
			auto& item = arrayData[index];
			if (item.HasMember("status") && item["status"].GetString() == "open")
			{
				KF_LOG_INFO(logger, "[history_order] (open order)  (cid)" << item["client_order_id"].GetString());
				LFRtnOrderField order;
				memset(&order, 0, sizeof(order));
				bool isOk = OpenOrderToLFOrder(unit, item, order);
				if (isOk)
				{
					unit.ordersMap.insert(std::make_pair(order.OrderRef, order));
				}
			}
			
		}
		for(auto& orderItem :unit.ordersMap)
		{
			Document json;
			cancel_order(unit, orderItem.second.OrderRef, orderItem.second.InstrumentID,orderItem.second.VolumeTotal*1.0/ scale_offset,json);
		}

		//success, only record raw data
		//raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_PROBIT, 1, requestId, 0, "");
	}
	else if (d.HasMember("code") && d["code"].IsNumber())
	{
		//send error, example: http timeout.
		std::string errorMsg;
		int errorId = d["code"].GetInt();
		if (d.HasMember("message") && d["message"].IsString())
		{
			errorMsg = d["message"].GetString();
		}
		KF_LOG_ERROR(logger, "[history_orders] failed! (errorId)" << errorId << " (errorMsg) " << errorMsg);
	}
}


void TDEngineProbit::cancel_order(const AccountUnitProbit& unit, const std::string& orderId, const std::string& marketID, double quantity,  Document& json)
{
    KF_LOG_INFO(logger, "[cancel_order]");
    std::string requestPath = "/api/exchange/v1/cancel_order";
	char strQuantity[20];
	sprintf(strQuantity, "%.4f", quantity);
	std::string body = "{\"market_id\":\"" + marketID + "\",\"order_id\":\"" + orderId + "\",\"limit_open_quantity\":\"0\"}";
	std::string authToken = getAuthToken(unit);
    string url = unit.baseUrl + requestPath;

    const auto response = Post(Url{url},
                                 Header{{"Content-Type", "application/json"},
										{ "authorization", "Bearer " + authToken } },
										Body{body},
								Timeout{30000});

    KF_LOG_INFO(logger, "[cancel_order] (url) " << url  << " (body) "<< body << " (response.status_code) " << response.status_code <<
                                                " (response.error.message) " << response.error.message <<
                                                " (response.text) " << response.text.c_str());
    getResponse(response.status_code, response.text, response.error.message, json);
}



inline int64_t TDEngineProbit::getTimestamp()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp;
}

void TDEngineProbit::lws_login(AccountUnitProbit& unit, long timeout_nsec)
{
    KF_LOG_INFO(logger, "TDEngineProbit::lws_login:");
    global_td = this;

    if (context == NULL)
    {
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
        KF_LOG_INFO(logger, "TDEngineProbit::lws_login: context created.");
    }

    if (context == NULL) {
        KF_LOG_ERROR(logger, "TDEngineProbit::lws_login: context is NULL. return");
        return;
    }

    int logs = LLL_ERR | LLL_DEBUG | LLL_WARN;
    lws_set_log_level(logs, NULL);

    struct lws_client_connect_info ccinfo = { 0 };
    ccinfo.context 	= context;
    ccinfo.address 	= unit.wsUrl.c_str();
    ccinfo.port 	= 443;
    ccinfo.path 	= "/api/exchange/v1/ws";
    ccinfo.host 	= unit.wsUrl.c_str();
    ccinfo.origin 	= unit.wsUrl.c_str();
    ccinfo.ietf_version_or_minus_one = -1;
    ccinfo.protocol = "ws://";
    ccinfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;

    unit.websocketConn = lws_client_connect_via_info(&ccinfo);
    KF_LOG_INFO(logger, "TDEngineProbit::lws_login: Connecting to "<< ccinfo.protocol <<  ccinfo.host << ":" << ccinfo.port << ":" << ccinfo.path);

    if (unit.websocketConn == NULL)
    {
        KF_LOG_ERROR(logger, "TDEngineProbit::lws_login: wsi create error.");
        return;
    }
    KF_LOG_INFO(logger, "TDEngineProbit::lws_login: wsi create success.");
}


void TDEngineProbit::on_lws_connection_error(struct lws* conn)
{
    KF_LOG_ERROR(logger, "TDEngineProbit::on_lws_connection_error.");
    //market logged_in false;
    AccountUnitProbit& unit = findAccountUnitByWebsocketConn(conn);
    unit.logged_in = false;
    unit.status = AccountStatus::AS_AUTH;
    KF_LOG_ERROR(logger, "TDEngineProbit::on_lws_connection_error. login again.");
    //unit.newPendingSendMsg.push_back(createAuthJsonString(unit ));
    lws_login(unit, 0);
}

void TDEngineProbit::lws_write_subscribe(struct lws* conn)
{
    auto& accout = findAccountUnitByWebsocketConn(conn);
    switch (accout.status)
    {
        case AccountStatus::AS_AUTH:
        {
            KF_LOG_INFO(logger,"lws_write_subscribe do auth");
            auto subscribe_msg = "{\"type\": \"authorization\", \"token\":\"" + getAuthToken(accout) + "\"}";
            accout.status = AccountStatus::AS_WAITING;
            sendMessage(std::move(subscribe_msg), conn);
            break;
        }
        case AccountStatus::AS_OPEN_ORDER:
        {
            KF_LOG_INFO(logger,"lws_write_subscribe open order");
            auto subscribe_msg = "{\"type\": \"subscribe\", \"channel\":\"open_order\"}";
            accout.status = AccountStatus::AS_OVER;
            sendMessage(std::move(subscribe_msg), conn);
            break;
        }
        case AccountStatus ::AS_WAITING:
        {
            KF_LOG_INFO(logger, "lws_write_subscribe: wait for auth response" );
            break;
        }
        default:
            break;
    }
}

void TDEngineProbit::on_lws_data(struct lws* conn, const char* data, size_t len)
{
    KF_LOG_DEBUG(logger, "TDEngineProbit::on_lws_data: " << data);
    Document json;
    json.Parse(data,len);
    if (json.HasParseError() || !json.IsObject())
    {
        KF_LOG_ERROR(logger, "TDEngineProbit::on_lws_data, parse json error," << data);
        return;
    }
    if (json.HasMember("errorCode") )
    {
        KF_LOG_ERROR(logger, "TDEngineProbit::on_lws_data," << data);
        return;
    }
    if(json.HasMember("channel") && json["channel"].IsString())
    {
        std::string channel = json["channel"].GetString();
        if(channel== "open_order" )
        {
            onOrder(conn, json);
        }
        return;
    }
    if (json.HasMember("type") && json["type"].IsString())
	{
        std::string type = json["type"].GetString();
        if(type != "authorization")
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::on_lws_data, parse json error:json string has no member \"authorization\","<< data);
            return;
        }
        if(!json.HasMember("result") || !json["result"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::on_lws_data, parse json error:json string has no member \"result\","<< data);
            return;
        }
        if (std::string("ok") == json["result"].GetString())
        {
            AccountUnitProbit &unit = findAccountUnitByWebsocketConn(conn);
            unit.status = AccountStatus::AS_OPEN_ORDER;
            lws_callback_on_writable(conn);
            KF_LOG_INFO(logger, "TDEngineProbit::on_lws_data, probit authorize success,"<< data);
            return;
        }
        KF_LOG_ERROR(logger, "TDEngineProbit::on_lws_data, probit authorize failed,"<< data);
	}
}

void TDEngineProbit::onOrder(struct lws* conn, Document& json)
{
    if (!json.HasMember("data") || !json["data"].IsArray())
    {
        KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"data\"");
        return;
    }

	bool isReset = false;
	if (json.HasMember("reset") && json["reset"].IsBool())
	{
		isReset = json["reset"].GetBool();
	}
    AccountUnitProbit &unit = findAccountUnitByWebsocketConn(conn);
    auto& orderData = json["data"];
	for (SizeType index = 0; index < orderData.Size(); ++index)
	{
		auto& order = orderData[index];
		if (isReset)
		{
			if (order.HasMember("id") && order["id"].IsString() && order.HasMember("market_id") && order["market_id"].IsString() && order.HasMember("open_quantity") && order["open_quantity"].IsString())
			{
				std::string order_id = order["id"].GetString();
				std::string market_id = order["market_id"].GetString();
				double open_quantity = atof(order["open_quantity"].GetString());
				Document json;
				cancel_order(unit, order_id, market_id, open_quantity, json);
			}
            return;
        }
        if (!order.HasMember("client_order_id") || !order["client_order_id"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"client_order_id\"");
            return;
        }
        std::unique_lock<std::mutex> l(g_orderMutex);
        auto orderRef = order["client_order_id"].GetString();
        auto orderIter = unit.ordersMap.find(orderRef);
        if (orderIter == unit.ordersMap.end())
        {
            KF_LOG_DEBUG(logger, "TDEngineProbit::onOrder, can not find orderRef:" << orderRef);
            continue;
        }
        if (!order.HasMember("id") || !order["id"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"id\"");
            return;
        }
        auto exchangeOrderID = order["id"].GetString();
        //kungfu order
        LFRtnOrderField& rtn_order = orderIter->second;

        strncpy(rtn_order.OrderRef, orderRef, sizeof(sizeof(rtn_order.OrderRef)) - 1);

        if (!order.HasMember("market_id") || !order["market_id"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"market_id\"");
        }
        strncpy(rtn_order.InstrumentID, order["market_id"].GetString(), sizeof(sizeof(rtn_order.InstrumentID)) - 1);

        if (!order.HasMember("filled_quantity") || !order["filled_quantity"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"filled_quantity\"");
            return;
        }
        int64_t filled_quantity =  (int64_t)(std::atof(order["filled_quantity"].GetString())*scale_offset);
        auto cur_quantity = filled_quantity - rtn_order.VolumeTraded;
        rtn_order.VolumeTraded = filled_quantity;
        if (!order.HasMember("quantity") || !order["quantity"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"quantity\"");
            return;
        }
        rtn_order.VolumeTotalOriginal = (int64_t)(std::atof(order["quantity"].GetString())*scale_offset);

        if (!order.HasMember("side") || !order["side"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"side\"");
            return;
        }
        rtn_order.Direction = GetDirection(order["side"].GetString());
        if (!order.HasMember("type") || !order["type"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"type\"");
            return;
        }
        rtn_order.OrderPriceType = GetPriceType(order["type"].GetString());
        if (!order.HasMember("limit_price") || !order["limit_price"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"limit_price\"");
            return;
        }
        rtn_order.LimitPrice = (int64_t)(std::atof(order["limit_price"].GetString()) * scale_offset);
        if (!order.HasMember("open_quantity") || !order["open_quantity"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"open_quantity\"");
            return;
        }
        rtn_order.VolumeTotal = (int64_t)(std::atof(order["open_quantity"].GetString()) * scale_offset);
        rtn_order.RequestID = orderIter->second.RequestID;
        if (!order.HasMember("status") || !order["status"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"status\"");
            return;
        }
        rtn_order.OrderStatus = GetOrderStatus(order["status"].GetString());
        if (rtn_order.OrderStatus == LF_CHAR_NotTouched)
        {
            if (filled_quantity > 0)
            {
                rtn_order.OrderStatus = LF_CHAR_PartTradedQueueing;
            }
        }
        if (!order.HasMember("cancelled_quantity") || !order["cancelled_quantity"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"cancelled_quantity\"");
            return;
        }
        auto cancelled_quantity = (int64_t)(std::atof(order["cancelled_quantity"].GetString()) * scale_offset);
        if(cancelled_quantity > 0)
        {
            rtn_order.OrderStatus = LF_CHAR_Canceled;
        }
        KF_LOG_DEBUG(logger, "TDEngineProbit::onOrder, ON_RTN_ORDER");
        // on_rtn_order
        on_rtn_order(&rtn_order);
        raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField), source_id, MSG_TYPE_LF_RTN_ORDER_PROBIT, 1, (rtn_order.RequestID > 0) ? rtn_order.RequestID : -1);
        // on_rtn_trade
        if (cur_quantity > 0)
        {
            onTrade(conn, rtn_order.OrderRef,unit.api_key.c_str(), rtn_order.InstrumentID, rtn_order.Direction, cur_quantity, rtn_order.LimitPrice);
        }
	}
}

void TDEngineProbit::onTrade(struct lws * conn, const char* orderRef, const char* api_key, const char* instrumentID, LfDirectionType direction, int64_t volume, int64_t price)
{
    KF_LOG_DEBUG(logger, "TDEngineProbit::onTrade, start");
    LFRtnTradeField rtn_trade;
    memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
    strncpy(rtn_trade.OrderRef, orderRef, sizeof(rtn_trade.OrderRef)-1);
    strcpy(rtn_trade.ExchangeID, "ProBit");
    strncpy(rtn_trade.UserID, api_key, 16);
    strncpy(rtn_trade.InstrumentID, instrumentID, 31);
    rtn_trade.Direction = direction;
    rtn_trade.Volume = volume;
    rtn_trade.Price = price;
    on_rtn_trade(&rtn_trade);
    raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField), source_id, MSG_TYPE_LF_RTN_TRADE_PROBIT, 1, -1);
    KF_LOG_DEBUG(logger, "TDEngineProbit::onTrade end" );
}

AccountUnitProbit& TDEngineProbit::findAccountUnitByWebsocketConn(struct lws * websocketConn)
{
    for (size_t idx = 0; idx < account_units.size(); idx++)
    {
        AccountUnitProbit &unit = account_units[idx];
        if(unit.websocketConn == websocketConn)
        {
            return unit;
        }
    }
    return account_units[0];
}

void TDEngineProbit::MyPost(const std::string& url,const std::string& auth, const std::string& body, Document& json)
{
	const auto response = cpr::Post(Url{ url },
		Header{
			{ "Content-Type", "application/json" },
		{ "authorization", auth }
		},
		Body{ body }, Timeout{ 30000 });

	//{ "error": {"message": "Authorization Required","name": "HTTPError"} }
	KF_LOG_INFO(logger, "[Post] (url) " << url << " (body) " << body << "(msg)" << auth << " (response.status_code) " << response.status_code <<
		" (response.error.message) " << response.error.message <<
		" (response.text) " << response.text.c_str());
	getResponse(response.status_code, response.text, response.error.message, json);
}


std::string TDEngineProbit::getAuthToken(const AccountUnitProbit& unit )
{
   //TODO:if authToken is expired,get a new token 
	if (m_tokenExpireTime < (getTimestamp() - 60000))
	{
		std::string requestPath = "/token ";
		std::string body = R"({"grant_type":"client_credentials"})";
		std::string msg = unit.api_key + ":" + unit.secret_key;
		std::string authEncode = base64_encode((const unsigned char*)msg.c_str(), msg.length());
		string url = unit.authUrl + requestPath;
		Document json;
		//getResponse(response.status_code, response.text, response.error.message, json);
		MyPost(url, "Basic " + authEncode, body, json);
		if (json.HasParseError() || !json.IsObject())
		{
			int errorId = 100;
			std::string errorMsg = "getAuthToken http response has parse error or is not json. please check the log";
			KF_LOG_ERROR(logger, "[getAuthToken]  error!  (errorId)" << errorId << " (errorMsg) " << errorMsg);
		}
		else if (json.HasMember("access_token") && json.HasMember("expires_in"))
		{

			m_authToken = json["access_token"].GetString();
			//TODO ms or second???
			m_tokenExpireTime = json["expires_in"].GetInt()*1000 + getTimestamp();
		}
		else if (json.HasMember("code") && json["code"].IsNumber())
		{
			//send error, example: http timeout.
			std::string errorMsg;
			int errorId = json["code"].GetInt();
			if (json.HasMember("message") && json["message"].IsString())
			{
				errorMsg = json["message"].GetString();
			}
			KF_LOG_ERROR(logger, "[getAuthToken] failed! (errorId)" << errorId << " (errorMsg) " << errorMsg);
		}
	}
	return m_authToken;
}


void TDEngineProbit::sendMessage(std::string &&msg, struct lws * conn)
{
    msg.insert(msg.begin(),  LWS_PRE, 0x00);
    lws_write(conn, (uint8_t*)(msg.data() + LWS_PRE), msg.size() - LWS_PRE, LWS_WRITE_TEXT);
}




#define GBK2UTF8(msg) kungfu::yijinjing::gbk2utf8(string(msg))

BOOST_PYTHON_MODULE(libprobittd)
{
    using namespace boost::python;
    class_<TDEngineProbit, boost::shared_ptr<TDEngineProbit> >("Engine")
            .def(init<>())
            .def("init", &TDEngineProbit::initialize)
            .def("start", &TDEngineProbit::start)
            .def("stop", &TDEngineProbit::stop)
            .def("logout", &TDEngineProbit::logout)
            .def("wait_for_stop", &TDEngineProbit::wait_for_stop);
}
