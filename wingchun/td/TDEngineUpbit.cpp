#include "TDEngineUpbit.h"
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
#include "../../utils/crypto/openssl_util.h"
#include "sstream"
#include<cstdlib>

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

TDEngineUpbit::TDEngineUpbit(): ITDEngine(SOURCE_UPBIT)
{
    logger = yijinjing::KfLog::getLogger("TradeEngine.Upbit");
    KF_LOG_INFO(logger, "[ATTENTION] default to confirm settlement and no authentication!");

    mutex_order_and_trade = new std::mutex();
}

TDEngineUpbit::~TDEngineUpbit()
{
    if(mutex_order_and_trade != nullptr) delete mutex_order_and_trade;
}

void TDEngineUpbit::init()
{
    ITDEngine::init();
    JournalPair tdRawPair = getTdRawJournalPair(source_id);
    raw_writer = yijinjing::JournalSafeWriter::create(tdRawPair.first, tdRawPair.second, "RAW_" + name());
}

void TDEngineUpbit::pre_load(const json& j_config)
{
    KF_LOG_INFO(logger, "[pre_load]");
}

void TDEngineUpbit::resize_accounts(int account_num)
{
    account_units.resize(account_num);
    KF_LOG_INFO(logger, "[resize_accounts]");
}

TradeAccount TDEngineUpbit::load_account(int idx, const json& j_config)
{
    KF_LOG_INFO(logger, "[load_account]");
    // internal load
    string api_key = j_config["APIKey"].get<string>();
    string secret_key = j_config["SecretKey"].get<string>();
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();

    if(j_config.find("sync_time_interval") != j_config.end()) {
        SYNC_TIME_DEFAULT_INTERVAL = j_config["sync_time_interval"].get<int>();
    }
    KF_LOG_INFO(logger, "[load_account] (SYNC_TIME_DEFAULT_INTERVAL)" << SYNC_TIME_DEFAULT_INTERVAL);

    if(j_config.find("exchange_shift_ms") != j_config.end()) {
        exchange_shift_ms = j_config["exchange_shift_ms"].get<int>();
    }
    KF_LOG_INFO(logger, "[load_account] (exchange_shift_ms)" << exchange_shift_ms);

    if(j_config.find("order_insert_recvwindow_ms") != j_config.end()) {
        order_insert_recvwindow_ms = j_config["order_insert_recvwindow_ms"].get<int>();
    }
    KF_LOG_INFO(logger, "[load_account] (order_insert_recvwindow_ms)" << order_insert_recvwindow_ms);

    if(j_config.find("order_action_recvwindow_ms") != j_config.end()) {
        order_action_recvwindow_ms = j_config["order_action_recvwindow_ms"].get<int>();
    }
    KF_LOG_INFO(logger, "[load_account] (order_action_recvwindow_ms)" << order_action_recvwindow_ms);


    if(j_config.find("max_rest_retry_times") != j_config.end()) {
        max_rest_retry_times = j_config["max_rest_retry_times"].get<int>();
    }
    KF_LOG_INFO(logger, "[load_account] (max_rest_retry_times)" << max_rest_retry_times);


    if(j_config.find("retry_interval_milliseconds") != j_config.end()) {
        retry_interval_milliseconds = j_config["retry_interval_milliseconds"].get<int>();
    }
    KF_LOG_INFO(logger, "[load_account] (retry_interval_milliseconds)" << retry_interval_milliseconds);
    if(j_config.find("time_to_wait_before_cancel_ms") != j_config.end()) {
        time_to_wait_before_cancel_ms = j_config["time_to_wait_before_cancel_ms"].get<int>();
    }
    else time_to_wait_before_cancel_ms=0;
    KF_LOG_INFO(logger, "[load_account] (for test ,time to wait before cancel (s))" << time_to_wait_before_cancel_ms);

    AccountUnitUpbit& unit = account_units[idx];
    unit.api_key = api_key;
    unit.secret_key = secret_key;

    KF_LOG_INFO(logger, "[load_account] (api_key)" << api_key << "(SecretKey)" << secret_key);

    unit.coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
    unit.coinPairWhiteList.Debug_print();

    unit.positionWhiteList.ReadWhiteLists(j_config, "positionWhiteLists");
    unit.positionWhiteList.Debug_print();

    //display usage:
    if(unit.coinPairWhiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "TDEngineUpbit::load_account: subscribeCoinBaseQuote is empty. please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"BTCUSDT\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"ETCETH\"");
        KF_LOG_ERROR(logger, "},");
    }

    //cancel all openning orders on TD startup
    if(unit.coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().size() > 0)
    {
        Document d;
        //getAccountResponce(unit,d);
        get_open_orders(unit, d);
        KF_LOG_INFO(logger, "[load_account] print get_open_orders");
        printResponse(d);

        if(!d.HasParseError() && d.IsArray()) { // expected success response is array
            size_t len = d.Size();
            KF_LOG_INFO(logger, "[load_account][get_open_orders] (length)" << len);
            for (size_t i = 0; i < len; i++) {
                if(d.GetArray()[i].IsObject() && d.GetArray()[i].HasMember("market") && d.GetArray()[i].HasMember("uuid"))
                {
                    if(d.GetArray()[i]["market"].IsString() && d.GetArray()[i]["uuid"].IsString())
                    {
                        std::string symbol = d.GetArray()[i]["market"].GetString();
                        std::string strTicker  = unit.coinPairWhiteList.GetKeyByValue(symbol);
                        if(strTicker.length() <= 0)
                        {
                            KF_LOG_INFO(logger, "[load_account] " << symbol << "is not in coinPairWhiteList");
                            continue;
                        }
                        std::string orderRef = d.GetArray()[i]["uuid"].GetString();
                        Document cancelResponse;
                        cancel_order(unit, symbol.c_str(), orderRef.c_str(), cancelResponse);

                        KF_LOG_INFO(logger, "[load_account] cancel_order:(orderRef)" <<orderRef<< "(Ticker)" << strTicker);
                        printResponse(cancelResponse);
                        int errorId = 0;
                        std::string errorMsg = "";
                        if(d.HasParseError() )
                        {
                            errorId = 100;
                            errorMsg = "cancel_order http response has parse error. please check the log";
                            KF_LOG_ERROR(logger, "[load_account] cancel_order error! (rid)  -1 (errorId)" << errorId << " (errorMsg) " << errorMsg);
                        }
                        if(!cancelResponse.HasParseError() && cancelResponse.IsObject() && cancelResponse.HasMember("code") && cancelResponse["code"].IsNumber())
                        {
                            errorId = cancelResponse["code"].GetInt();
                            if(cancelResponse.HasMember("msg") && cancelResponse["msg"].IsString())
                            {
                                errorMsg = cancelResponse["msg"].GetString();
                            }

                            KF_LOG_ERROR(logger, "[load_account] cancel_order failed! (rid)  -1 (errorId)" << errorId << " (errorMsg) " << errorMsg);
                        }
                    }
                }
            }
        }
    }

    // set up
    TradeAccount account = {};
    //partly copy this fields
    strncpy(account.UserID, api_key.c_str(), 16);
    strncpy(account.Password, secret_key.c_str(), 21);
    return account;
}


void TDEngineUpbit::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[connect]");
    //sync time of exchange
    timeDiffOfExchange = getTimeDiffOfExchange(account_units[0]);
    for (size_t idx = 0; idx < account_units.size(); idx++)
    {
        AccountUnitUpbit& unit = account_units[idx];

        KF_LOG_INFO(logger, "[connect] (api_key)" << unit.api_key);
        if (!unit.logged_in)
        {
            std::vector<std::string> vstrMarkets;
            getAllMarkets(vstrMarkets);
            filterMarkets(vstrMarkets);
            if( loadMarketsInfo(unit,vstrMarkets))
            {
                unit.logged_in = true;
            } else {
                KF_LOG_ERROR(logger, "[connect] logged_in = false for loadExchangeOrderFilters return false");
            }
            debug_print(unit.sendOrderFilters);
        }
    }

}

void TDEngineUpbit::debug_print(std::map<std::string, SendOrderFilter> &sendOrderFilters)
{
    std::map<std::string, SendOrderFilter>::iterator map_itr = sendOrderFilters.begin();
    while(map_itr != sendOrderFilters.end())
    {
        KF_LOG_INFO(logger, "[debug_print] sendOrderFilters (symbol)" << map_itr->first <<
                "(AskCurrency)" << map_itr->second.strAskCurrency << " (AskMintotal)" << map_itr->second.nAskMinTotal << 
                "(BidCurrency)" << map_itr->second.strBidCurrency << "(BidMinTotal)" << map_itr->second.nBidMinTotal << 
                "(Maxtotal)" << map_itr->second.nMaxTotal << "(State)" << map_itr->second.strState );
        map_itr++;
    }
}

SendOrderFilter TDEngineUpbit::getSendOrderFilter(AccountUnitUpbit& unit, const char *symbol)
{
    std::map<std::string, SendOrderFilter>::iterator map_itr = unit.sendOrderFilters.begin();
    while(map_itr != unit.sendOrderFilters.end())
    {
        if(strcmp(map_itr->first.c_str(), symbol) == 0)
        {
            return map_itr->second;
        }
        map_itr++;
    }
    SendOrderFilter defaultFilter;
    defaultFilter.nBidTickSize = 8;
    defaultFilter.nAskTickSize = 8;
    strcpy(defaultFilter.InstrumentID, "notfound");
    return defaultFilter;
}

void TDEngineUpbit::login(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[login]");
    connect(timeout_nsec);
}

void TDEngineUpbit::logout()
{
    KF_LOG_INFO(logger, "[logout]");
}

void TDEngineUpbit::release_api()
{
    KF_LOG_INFO(logger, "[release_api]");
}

bool TDEngineUpbit::is_logged_in() const
{
    KF_LOG_INFO(logger, "[is_logged_in]");
    for (auto& unit: account_units)
    {
        if (!unit.logged_in)
            return false;
    }
    return true;
}

bool TDEngineUpbit::is_connected() const
{
    KF_LOG_INFO(logger, "[is_connected]");
    return is_logged_in();
}



std::string TDEngineUpbit::GetSide(const LfDirectionType& input) {
    if (LF_CHAR_Buy == input) {
        return "bid";
    } else if (LF_CHAR_Sell == input) {
        return "ask";
    } else {
        return "UNKNOWN";
    }
}

LfDirectionType TDEngineUpbit::GetDirection(std::string input) {
    if ("bid" == input) {
        return LF_CHAR_Buy;
    } else if ("ask" == input) {
        return LF_CHAR_Sell;
    } else {
        return LF_CHAR_Buy;
    }
}

std::string TDEngineUpbit::GetType(const LfOrderPriceTypeType& input) {
    if (LF_CHAR_LimitPrice == input) {
        return "limit";
    } 
    else {
        return "UNKNOWN";
    }
}

LfOrderPriceTypeType TDEngineUpbit::GetPriceType(std::string input) {
    if ("limit" == input) {
        return LF_CHAR_LimitPrice;
    } else {
        return '0';
    }
}

std::string TDEngineUpbit::GetTimeInForce(const LfTimeConditionType& input) {
    if (LF_CHAR_IOC == input) {
        return "IOC";
    } else if (LF_CHAR_GFD == input) {
        return "GTC";
    } else if (LF_CHAR_FOK == input) {
        return "FOK";
    } else {
        return "UNKNOWN";
    }
}

LfTimeConditionType TDEngineUpbit::GetTimeCondition(std::string input) {
    if ("IOC" == input) {
        return LF_CHAR_IOC;
    } else if ("GTC" == input) {
        return LF_CHAR_GFD;
    } else if ("FOK" == input) {
        return LF_CHAR_FOK;
    } else {
        return '0';
    }
}

LfOrderStatusType TDEngineUpbit::GetOrderStatus(std::string input) {
    if ("NEW" == input) {
        return LF_CHAR_NotTouched;
    } else if ("PARTIALLY_FILLED" == input) {
        return LF_CHAR_PartTradedQueueing;
    } else if ("FILLED" == input) {
        return LF_CHAR_AllTraded;
    } else if ("CANCELED" == input) {
        return LF_CHAR_Canceled;
    } else if ("PENDING_CANCEL" == input) {
        return LF_CHAR_NotTouched;
    } else if ("REJECTED" == input) {
        return LF_CHAR_Error;
    } else if ("EXPIRED" == input) {
        return LF_CHAR_Error;
    } else {
        return LF_CHAR_NotTouched;
    }
}

std::int32_t TDEngineUpbit::getAccountResponce(const AccountUnitUpbit& unit,Document& d)
{
    long recvWindow = 5000;
    std::string Method = "GET";
    std::string strQueryString = "";
    std::string url = "https://api.upbit.com/v1/accounts";
    std::string body = "";


    int retry_times = 0;
    cpr::Response response;
    bool should_retry = false;
    do {
        should_retry = false;
        std::string Authorization = getAuthorization(unit);

        response = Get(Url{url},
                Header{{  "Authorization", Authorization}},
                Body{body}, Timeout{100000});
        KF_LOG_INFO(logger, "[getAccountResponce] (url) " << url << " (response.status_code) " << response.status_code <<
                " (response.error.message) " << response.error.message <<
                " (response.text) " << response.text.c_str());
        if(response.status_code != 200) {
            should_retry = true;
            retry_times++;
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_milliseconds));
        }
    } while(should_retry && retry_times < max_rest_retry_times);

    d.Parse(response.text.c_str());
    return response.status_code;
}

/**
 * req functions
 */
void TDEngineUpbit::req_investor_position(const LFQryPositionField* data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_investor_position]");

    AccountUnitUpbit& unit = account_units[account_index];
    KF_LOG_INFO(logger, "[req_investor_position] (api_key)" << unit.api_key);

    // User Balance
    Document d;
    auto nResponseCode = getAccountResponce(unit,d);
    KF_LOG_INFO(logger, "[req_investor_position] get_account");

    int errorId = 0;
    std::string errorMsg = "";
    if(d.HasParseError() )
    {
        errorId=100;
        errorMsg= "get_account http response has parse error. please check the log";
        KF_LOG_ERROR(logger, "[req_investor_position] get_account error! (rid)  -1 (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }

    if(!d.HasParseError() && d.IsObject() && d.HasMember("error"))
    {
        errorId = nResponseCode;
        if(d.HasMember("name") && d["name"].IsString())
        {
            errorMsg = d["name"].GetString();
        }

        KF_LOG_ERROR(logger, "[req_investor_position] get_account failed! (rid)  -1 (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }
    send_writer->write_frame(data, sizeof(LFQryPositionField), source_id, MSG_TYPE_LF_QRY_POS_UPBIT, 1, requestId);

    LFRspPositionField pos;
    memset(&pos, 0, sizeof(LFRspPositionField));
    strncpy(pos.BrokerID, data->BrokerID, 11);
    strncpy(pos.InvestorID, data->InvestorID, 19);
    strncpy(pos.InstrumentID, data->InstrumentID, 31);
    pos.PosiDirection = LF_CHAR_Long;
    pos.Position = 0;

    std::vector<LFRspPositionField> tmp_vector;

    if(!d.HasParseError() && d.IsArray())
    {
        int len = d.Size();
        for ( int i  = 0 ; i < len ; i++ ) {
            std::string symbol = d.GetArray()[i]["currency"].GetString();
            std::string ticker = unit.positionWhiteList.GetKeyByValue(symbol);
            if(ticker.length() > 0) {
                strncpy(pos.InstrumentID, ticker.c_str(), 31);
                pos.Position = std::round(stod(d.GetArray()[i]["balance"].GetString()) * scale_offset);
                tmp_vector.push_back(pos);
                KF_LOG_INFO(logger,  "[connect] (symbol)" << symbol << " (balance)" <<  d.GetArray()[i]["balance"].GetString()
                        << " (locked)" << d.GetArray()[i]["locked"].GetString() 
                        << "(avg_krw_buy_price)" << d.GetArray()[i]["avg_krw_buy_price"].GetString());
                KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId << " (symbol) " << symbol << " (position) " << pos.Position);
            }
        }
    }

    bool findSymbolInResult = false;
    //send the filtered position
    int position_count = tmp_vector.size();
    for (int i = 0; i < position_count; i++)
    {
        on_rsp_position(&tmp_vector[i], i == (position_count - 1), requestId, errorId, errorMsg.c_str());
        findSymbolInResult = true;
    }

    if(!findSymbolInResult)
    {
        on_rsp_position(&pos, 1, requestId, errorId, errorMsg.c_str());
    }
    if(errorId != 0)
    {
        raw_writer->write_error_frame(&pos, sizeof(LFRspPositionField), source_id, MSG_TYPE_LF_RSP_POS_UPBIT, 1, requestId, errorId, errorMsg.c_str());
    }
}

void TDEngineUpbit::req_qry_account(const LFQryAccountField *data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_qry_account]");
}


int64_t TDEngineUpbit::fixPriceTickSize(int keepPrecisionBid, int keepPrecisionAsk, int64_t price, bool isBuy)
{
    int keepPrecision = isBuy ? keepPrecisionBid : keepPrecisionAsk;
    //the 8 is come from 1e8.
    if(keepPrecision == 8) return price;
    int removePrecisions = (8 - keepPrecision);
    double cutter =  pow(10, removePrecisions);
    int64_t new_price = 0;
    if(isBuy)
    {
        new_price = std::ceil(price / cutter) * cutter;
    } else {
        new_price = std::floor(price / cutter) * cutter;
    }
    return new_price;
}

void TDEngineUpbit::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time)//quest5v3 fxw
{
    AccountUnitUpbit& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_insert]" << " (rid)" << requestId
            << " (APIKey)" << unit.api_key
            << " (Tid)" << data->InstrumentID
            << " (OrderRef)" << data->OrderRef);
    send_writer->write_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_UPBIT, 1/*ISLAST*/, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_insert]: not in WhiteList , ignore it: (rid)" << requestId << " (errorId)" <<
                errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_UPBIT, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_insert] (exchange_ticker)" << ticker);

    double stopPrice = 0;
    double icebergQty = 0;
    Document d;

    SendOrderFilter filter = getSendOrderFilter(unit, ticker.c_str());

    int64_t fixedPrice = fixPriceTickSize(filter.nBidTickSize,filter.nAskTickSize, data->LimitPrice, LF_CHAR_Buy == data->Direction);

    KF_LOG_DEBUG(logger, "[req_order_insert] SendOrderFilter  (Tid)" << ticker <<
            " (LimitPrice)" << data->LimitPrice <<
            " (bidticksize)" << filter.nBidTickSize <<
            " (askticksize)" << filter.nAskTickSize <<
            " (fixedPrice)" << fixedPrice);

    auto nRsponseCode = send_order(unit, ticker.c_str(), GetSide(data->Direction).c_str(), GetType(data->OrderPriceType).c_str(),
            GetTimeInForce(data->TimeCondition).c_str(), data->Volume*1.0/scale_offset, fixedPrice*1.0/scale_offset, data->OrderRef,
            stopPrice, icebergQty, d);
    //    KF_LOG_INFO(logger, "[req_order_insert] send_order");
    //    printResponse(d);

    if(d.HasParseError() )
    {
        errorId=100;
        errorMsg= "send_order http response has parse error. please check the log";
        KF_LOG_ERROR(logger, "[req_order_insert] send_order error! (rid)" << requestId << " (errorId)" <<
                errorId << " (errorMsg) " << errorMsg);
    }
    if(!d.HasParseError() && d.IsObject() && d.HasMember("error"))
    {
        errorId = nRsponseCode;
        if(d["error"].HasMember("name"))
        {
            errorMsg = d["error"]["name"].GetString();
        }
        KF_LOG_ERROR(logger, "[req_order_insert] send_order failed! (rid)  -1 (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }

    if(errorId != 0)
    {
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_UPBIT, 1, requestId, errorId, errorMsg.c_str());
    }

    //paser the order/trade info in the response result
    if(!d.HasParseError() && d.IsObject() && !d.HasMember("error"))
    {
        OrderInfo stOrderInfo;
        stOrderInfo.strRemoteUUID = d["uuid"].GetString();
        stOrderInfo.nRequestID = requestId;
        stOrderInfo.timestamp=getTimestamp();//quest5v5
        unit.mapOrderRef2OrderInfo[data->OrderRef] = stOrderInfo;
        std::string strStatus=d["state"].GetString();
        int64_t nTrades = d["trades_count"].GetInt64();
        auto cStatus = convertOrderStatus(strStatus,nTrades);
        KF_LOG_INFO(logger, "[req_order_insert] (cStatus)" << cStatus);
        if(cStatus == LF_CHAR_NotTouched)
        {//no status, it is ACK
            onRspNewOrderACK(data, unit, d, requestId);
        } 
        //else {
            // if(cStatus == LF_CHAR_AllTraded) 
            //{            
            // it is RESULT            
            //    onRspNewOrderRESULT(data, unit, d, requestId);
            //} else {
            // it is FULL
            // onRspNewOrderFULL(data, unit, d, requestId);//quest5v4 fxw
            //}
        //}
        KF_LOG_INFO(logger, "[req_order_insert] success");
    }
    else{KF_LOG_INFO(logger, "[req_order_insert] failed");}
}

void TDEngineUpbit::onRspNewOrderACK(const LFInputOrderField* data, AccountUnitUpbit& unit, Document& result, int requestId)
{
    //if not Traded, add pendingOrderStatus for GetAndHandleOrderTradeResponse
    char noneStatus = '\0';
    addNewQueryOrdersAndTrades(unit, data->InstrumentID, data->OrderRef, noneStatus, 0, data->Direction, requestId);
}

LfOrderStatusType TDEngineUpbit::convertOrderStatus(const std::string& strStatus,int64_t nTrades)
{
    if(strStatus == "wait")
    {
        if(nTrades)
        {
            return LF_CHAR_PartTradedQueueing;
        }
        return LF_CHAR_NotTouched;
    }
    if(strStatus == "done")
    {
        return  LF_CHAR_AllTraded;
    }
    if(strStatus == "cancel")
    {
        return LF_CHAR_Canceled;
    }
}

void TDEngineUpbit::onRspNewOrderRESULT(const LFInputOrderField* data, AccountUnitUpbit& unit, Document& result, int requestId)
{
    KF_LOG_DEBUG(logger, "TDEngineUpbit::onRspNewOrderRESULT:");
    //printResponse(result);

    // no strike price, dont emit OnRtnTrade
    LFRtnOrderField rtn_order;
    memset(&rtn_order, 0, sizeof(LFRtnOrderField));
    strcpy(rtn_order.ExchangeID, "upbit");
    strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
    strncpy(rtn_order.InstrumentID, data->InstrumentID, 31);
    rtn_order.Direction = data->Direction;
    rtn_order.TimeCondition = data->TimeCondition;
    rtn_order.OrderPriceType = data->OrderPriceType;
    strncpy(rtn_order.OrderRef, data->OrderRef, 13);
    rtn_order.VolumeTraded = std::round(stod(result["executed_volume"].GetString()) * scale_offset);
    rtn_order.VolumeTotalOriginal = std::round(stod(result["volume"].GetString()) * scale_offset);
    rtn_order.VolumeTotal = rtn_order.VolumeTotalOriginal - rtn_order.VolumeTraded;
    rtn_order.LimitPrice = std::round(stod(result["price"].GetString()) * scale_offset);
    rtn_order.RequestID = requestId;
    rtn_order.OrderStatus =  convertOrderStatus(result["state"].GetString(),result["trades_count"].GetInt64());
    on_rtn_order(&rtn_order);
    KF_LOG_INFO(logger, "[TDEngineUpbit::onRspNewOrderFULL]:on_rtn_order (orderRef)" << rtn_order.OrderRef
            << "(requestId)" << requestId << "(status)" << rtn_order.OrderStatus);
    raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
            source_id, MSG_TYPE_LF_RTN_ORDER_UPBIT,
            1/*islast*/, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);

    //if All Traded, emit OnRtnTrade
    if(rtn_order.OrderStatus == LF_CHAR_AllTraded)
    {
        LFRtnTradeField rtn_trade;
        memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
        strcpy(rtn_trade.ExchangeID, "upbit");
        strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
        strncpy(rtn_trade.InstrumentID, data->InstrumentID, 31);
        strncpy(rtn_trade.OrderRef, data->OrderRef, 13);
        rtn_trade.Direction = data->Direction;
        rtn_trade.Volume = std::round(stod(result["executed_volume"].GetString()) * scale_offset);
        rtn_trade.Price = std::round(stod(result["price"].GetString()) * scale_offset);

        on_rtn_trade(&rtn_trade);
        KF_LOG_INFO(logger, "[TDEngineUpbit::onRspNewOrderFULL]:on_rtn_trade (orderRef)" << rtn_order.OrderRef
                << "(requestId)" << requestId );
        raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
                source_id, MSG_TYPE_LF_RTN_TRADE_UPBIT, 1/*islast*/, -1/*invalidRid*/);
        //this response has no tradeId, so dont call unit.newSentTradeIds.push_back(tradeid)

        KF_LOG_INFO(logger, "TDEngineUpbit::onRspNewOrderRESULT:AllTraded  (RequestID)" << rtn_order.RequestID);
    }else{ KF_LOG_ERROR(logger, "TDEngineUpbit::onRspNewOrderRESULT:ERROR  (RequestID)" << rtn_order.RequestID);}
}

void TDEngineUpbit::onRspNewOrderFULL(const LFInputOrderField* data, AccountUnitUpbit& unit, Document& result, int requestId)
{
    KF_LOG_INFO(logger, "TDEngineUpbit::onRspNewOrderFULL:");
    LFRtnOrderField rtn_order;
    memset(&rtn_order, 0, sizeof(LFRtnOrderField));
    strcpy(rtn_order.ExchangeID, "upbit");
    strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
    strncpy(rtn_order.InstrumentID, data->InstrumentID, 31);
    rtn_order.Direction = data->Direction;
    rtn_order.TimeCondition = data->TimeCondition;
    rtn_order.OrderPriceType = data->OrderPriceType;
    strncpy(rtn_order.OrderRef, data->OrderRef, 13);
    rtn_order.RequestID = requestId;
    rtn_order.OrderStatus = convertOrderStatus(result["state"].GetString(),result["trades_count"].GetInt64());

    uint64_t volumeTotalOriginal = std::round(stod(result["volume"].GetString()) * scale_offset);
    //数量
    rtn_order.VolumeTotalOriginal = volumeTotalOriginal;

    LFRtnTradeField rtn_trade;
    memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
    strcpy(rtn_trade.ExchangeID, "upbit");
    strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
    strncpy(rtn_trade.InstrumentID, data->InstrumentID, 31);
    strncpy(rtn_trade.OrderRef, data->OrderRef, 13);
    rtn_trade.Direction = data->Direction;
    Document d;
    auto stOrderInfo = findValue(unit.mapOrderRef2OrderInfo,data->OrderRef);

    std::string strRemoteUUID = result["uuid"].GetString();
    strncpy(rtn_order.BusinessUnit,strRemoteUUID.c_str(),21);
    strncpy(rtn_trade.OrderSysID,strRemoteUUID.c_str(),31);
    if(200 != get_order(unit,stOrderInfo.strRemoteUUID.c_str(),d))
    {
        KF_LOG_DEBUG(logger, "TDEngineUpbit::onRspNewOrderFULL:order not found ");
    }
    //we have strike price, emit OnRtnTrade
    int fills_size = atoi(d["trades"].GetString());

    for(int i = 0; i < fills_size; ++i)
    {
        uint64_t volume = std::round(stod(result["trades"].GetArray()[i]["volume"].GetString()) * scale_offset);
        int64_t price = std::round(stod(result["trades"].GetArray()[i]["price"].GetString()) * scale_offset);
        //今成交数量
        rtn_order.VolumeTraded = volume;
        rtn_order.LimitPrice = price;
        //剩余数量
        volumeTotalOriginal = volumeTotalOriginal - volume;
        rtn_order.VolumeTotal = volumeTotalOriginal;

        if(i == fills_size - 1) {
            //the last one
            rtn_order.OrderStatus = convertOrderStatus(result["state"].GetString(),result["trades_count"].GetInt64());
        } else {
            rtn_order.OrderStatus = LF_CHAR_PartTradedQueueing;
        }
        on_rtn_order(&rtn_order);
        KF_LOG_INFO(logger, "[TDEngineUpbit::onRspNewOrderFULL]:on_rtn_order (orderRef)" << rtn_order.OrderRef
                << "(requestId)" << requestId << "(status)" << rtn_order.OrderStatus);
        raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                source_id, MSG_TYPE_LF_RTN_ORDER_UPBIT,
                1/*islast*/, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);

        rtn_trade.Volume = volume;
        rtn_trade.Price = price;
        strncpy(rtn_trade.TradeID,result["trades"].GetArray()[i]["uuid"].GetString(),21);
        strncpy(rtn_trade.OrderSysID,strRemoteUUID.c_str(),31);
        on_rtn_trade(&rtn_trade);
        KF_LOG_INFO(logger, "[TDEngineUpbit::onRspNewOrderFULL]:on_rtn_trade (orderRef)" << rtn_order.OrderRef
                << "(requestId)" << requestId );
        raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
                source_id, MSG_TYPE_LF_RTN_TRADE_UPBIT, 1/*islast*/, -1/*invalidRid*/);
    }

    if(rtn_order.OrderStatus == LF_CHAR_PartTradedQueueing)
    {
        addNewQueryOrdersAndTrades(unit,rtn_order.InstrumentID,rtn_order.OrderRef,rtn_order.OrderStatus,rtn_order.VolumeTraded,rtn_order.Direction,requestId);
    }  
}

void TDEngineUpbit::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time)
{
    int64_t start = getTimestamp();
    AccountUnitUpbit& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_action]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Iid)" << data->InvestorID
                                              << " (OrderRef)" << data->OrderRef << " (KfOrderID)" << data->KfOrderID);

    send_writer->write_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_UPBIT, 1, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + "not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_action]: not in WhiteList , ignore it. (rid)" << requestId << " (errorId)" <<
                                                                                      errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_UPBIT, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_action] (exchange_ticker)" << ticker);

    Document d;
    auto stOrderInfo = findValue(unit.mapOrderRef2OrderInfo,data->OrderRef);
    /*quest5v5 fxw starts here*/
    int64_t between = getTimestamp() - stOrderInfo.timestamp;
    if (between < time_to_wait_before_cancel_ms)
    {
        between = time_to_wait_before_cancel_ms - between;
        KF_LOG_DEBUG(logger, "[req_order_action] (cancel will work after) " << between<<"ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(between));
    }
    /*quest5v5 fxw ends here*/
    auto nResponseCode =  cancel_order(unit, ticker.c_str(), stOrderInfo.strRemoteUUID.c_str() ,  d);
//    KF_LOG_INFO(logger, "[req_order_action] cancel_order");
//    printResponse(d);

    if(d.HasParseError() )
    {
        errorId=100;
        errorMsg= "cancel_order http response has parse error. please check the log";
        KF_LOG_ERROR(logger, "[req_order_action] cancel_order error! (rid)" << requestId << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }
    if(!d.HasParseError() && d.IsObject() && d.HasMember("error"))
    {
        errorId = nResponseCode;
        if(d.HasMember("name") && d["name"].IsString())
        {
            errorMsg = d["name"].GetString();
        }
        KF_LOG_ERROR(logger, "[req_order_action] cancel_order failed! (rid)  -1 (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }
    if(errorId != 0)
    {
        KF_LOG_INFO(logger, "[req_order_action] error (reeorId)  while retry after" <<  errorId);
        //raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_UPBIT, 1, requestId, errorId, errorMsg.c_str());
    }
    on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());

    std::lock_guard<std::mutex> guard_mutex(*mutex_order_and_trade);
    unit.mapNewCancelOrders[data->OrderRef] = stOrderInfo; 
    KF_LOG_INFO(logger, "[req_order_action] (orderRef)" <<  data->OrderRef << "(retCode)" << errorId);
    
}

void TDEngineUpbit::retrieveOrderAndTradesStatus(AccountUnitUpbit& unit)
{
    // KF_LOG_INFO(logger, "[retrieveOrderAndTradesStatus]: (Account))" << unit.api_key);
    for(auto orderStatusIterator = unit.pendingOrderStatus.begin(); orderStatusIterator != unit.pendingOrderStatus.end();)
    {
        KF_LOG_INFO(logger, "[retrieveOrderAndTradesStatus] get_order " << "account.api_key:"<< unit.api_key
                << "  account.pendingOrderStatus.InstrumentID: "<< orderStatusIterator->InstrumentID
                <<"  account.pendingOrderStatus.OrderRef: " << orderStatusIterator->OrderRef
                <<"  account.pendingOrderStatus.OrderStatus: " << orderStatusIterator->OrderStatus
                );

        std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(orderStatusIterator->InstrumentID));
        if(ticker.length() == 0) {
            KF_LOG_ERROR(logger, "[retrieveOrderStatus]: not in WhiteList , ignore it:" << orderStatusIterator->InstrumentID);
            ++orderStatusIterator;
            continue;
        }

        Document orderResult;
        auto stOrderInfo = findValue(unit.mapOrderRef2OrderInfo,orderStatusIterator->OrderRef);
        auto nCode =  get_order(unit, stOrderInfo.strRemoteUUID.c_str(), orderResult);
        if(200 != nCode)
        {
            if(nCode == 404)
            {
                KF_LOG_ERROR(logger, "[retrieveOrderStatus] Order not found ,erase order (orderRef)" << orderStatusIterator->OrderRef);
                orderStatusIterator = unit.pendingOrderStatus.erase(orderStatusIterator);
            }
            else
            {
                ++orderStatusIterator;
            }
            continue;
        }
        //KF_LOG_INFO(logger, "[retrieveOrderStatus] get_order " << " (symbol)" << orderStatusIterator->InstrumentID
        //                                                               << " (orderId)" << orderStatusIterator->OrderRef);
        //printResponse(orderResult);
        if(orderResult.HasParseError()) {
            KF_LOG_INFO(logger, "[retrieveOrderStatus] get_order HasParseError, call continue");
            ++orderStatusIterator;
            continue;
        }
        //parse order status
        bool isAllTradeOrCancel = retrieveOrderStatus(unit,orderResult,*orderStatusIterator);
        retrieveTradeStatus(unit,orderResult,orderStatusIterator->sentTradeIds);
        if(isAllTradeOrCancel)
        {
            orderStatusIterator =  unit.pendingOrderStatus.erase(orderStatusIterator);
        }
        else
        {
            ++orderStatusIterator;
        }
    }
}

void TDEngineUpbit::GetAndHandleOrderTradeResponse()
{
    //every account
    for (size_t idx = 0; idx < account_units.size(); idx++)
    {
        AccountUnitUpbit& unit = account_units[idx];
        //KF_LOG_INFO(logger, "[GetAndHandleOrderTradeResponse] (api_key)" << unit.api_key);
        if (!unit.logged_in)
        {
            continue;
        }

        for(auto it = unit.mapCancelOrders.begin(); it != unit.mapCancelOrders.end();  ++it)
        {
            Document d;
            cancel_order(unit,nullptr,it->second.strRemoteUUID.c_str(),d);
            KF_LOG_INFO(logger, "[GetAndHandleOrderTradeResponse] (req_order_action) (nRequestID)" << it->second.nRequestID
                    <<"(OrderRef)"<<it->second.nRequestID);
        }

        moveNewtoPending(unit);
        retrieveOrderAndTradesStatus(unit);
    }//end every account

    sync_time_interval--;
    if(sync_time_interval <= 0) {
        //reset
        sync_time_interval = SYNC_TIME_DEFAULT_INTERVAL;
        timeDiffOfExchange = getTimeDiffOfExchange(account_units[0]);
        KF_LOG_INFO(logger, "[GetAndHandleOrderTradeResponse] (reset_timeDiffOfExchange)" << timeDiffOfExchange);
    }

    //  KF_LOG_INFO(logger, "[GetAndHandleOrderTradeResponse] (timeDiffOfExchange)" << timeDiffOfExchange);
}


void TDEngineUpbit::moveNewtoPending(AccountUnitUpbit& unit)
{
    std::lock_guard<std::mutex> guard_mutex(*mutex_order_and_trade);

    for(auto it = unit.mapNewCancelOrders.begin() ; it != unit.mapNewCancelOrders.end();++it)
    {
        unit.mapCancelOrders.insert(*it);
    }
    unit.mapNewCancelOrders.clear();

    std::vector<PendingUpbitOrderStatus>::iterator newOrderStatusIterator;
    for(newOrderStatusIterator = unit.newOrderStatus.begin(); newOrderStatusIterator != unit.newOrderStatus.end();)
    {
        unit.pendingOrderStatus.push_back(*newOrderStatusIterator);
        KF_LOG_INFO(logger,"[moveNewtoPending] add order (ordrRef)" << newOrderStatusIterator->OrderRef);
        newOrderStatusIterator = unit.newOrderStatus.erase(newOrderStatusIterator);
    }
}

bool TDEngineUpbit::retrieveOrderStatus(AccountUnitUpbit& unit,Document& orderResult,PendingUpbitOrderStatus& pendingOrderStatus)
{
    char_21 strOrderRef;
    std::string uuid = orderResult["uuid"].GetString();
    strncpy(strOrderRef, findKey(unit.mapOrderRef2OrderInfo,uuid).c_str(), 21); 
    auto stOrderInfo = findValue(unit.mapOrderRef2OrderInfo,strOrderRef); 

    KF_LOG_INFO(logger,"[retrieveOrderStatus] (orderRef)" << strOrderRef << "(requestId)" << stOrderInfo.nRequestID);

    //parse order status
    if(orderResult.IsObject())
    {
        LFRtnOrderField rtn_order;
        memset(&rtn_order, 0, sizeof(LFRtnOrderField));
        rtn_order.OrderStatus = convertOrderStatus(orderResult["state"].GetString(),orderResult["trades_count"].GetInt64());
        rtn_order.VolumeTraded = std::round(stod(orderResult["executed_volume"].GetString()) * scale_offset);
        strncpy(rtn_order.OrderRef, strOrderRef, 21);

        //if status changed or LF_CHAR_PartTradedQueueing but traded valume changes, emit onRtnOrder
        if(pendingOrderStatus.OrderStatus != rtn_order.OrderStatus ||
                (LF_CHAR_PartTradedQueueing == rtn_order.OrderStatus
                 && rtn_order.VolumeTraded != pendingOrderStatus.VolumeTraded))
        {
            strcpy(rtn_order.ExchangeID, "upbit");
            strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);       
            rtn_order.Direction = GetDirection(orderResult["side"].GetString());
            rtn_order.TimeCondition = LF_CHAR_GTC;
            rtn_order.OrderPriceType = GetPriceType(orderResult["ord_type"].GetString());
            strncpy(rtn_order.InstrumentID, pendingOrderStatus.InstrumentID, 31);
            rtn_order.VolumeTotalOriginal = std::round(stod(orderResult["volume"].GetString()) * scale_offset);
            rtn_order.LimitPrice = std::round(stod(orderResult["price"].GetString()) * scale_offset);
            rtn_order.VolumeTotal = rtn_order.VolumeTotalOriginal - rtn_order.VolumeTraded;
            rtn_order.RequestID  = stOrderInfo.nRequestID;
            strncpy(rtn_order.BusinessUnit,uuid.c_str(),21);
            on_rtn_order(&rtn_order);
            raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                    source_id, MSG_TYPE_LF_RTN_ORDER_UPBIT,
                    1/*islast*/, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);
            //update last status
            pendingOrderStatus.OrderStatus = rtn_order.OrderStatus;
            pendingOrderStatus.VolumeTraded = rtn_order.VolumeTraded;
            KF_LOG_INFO(logger, "[TDEngineUpbit::onRspNewOrderFULL]:on_rtn_order (orderRef)" << rtn_order.OrderRef
                    << "(requestId)" << stOrderInfo.nRequestID << "(status)" << rtn_order.OrderStatus);
        }
    } else 
    {
        KF_LOG_ERROR(logger, "[retrieveOrderStatus] get_order fail." << " (symbol)" << pendingOrderStatus.InstrumentID
                << " (OrderRef)" << pendingOrderStatus.OrderRef);
    }

    if(pendingOrderStatus.OrderStatus == LF_CHAR_Canceled)
    {
        unit.mapCancelOrders.erase(strOrderRef);
        KF_LOG_INFO(logger, "[retrieveOrderStatus] erase cancel order (OrderRef)" << pendingOrderStatus.OrderRef
                << "(status)" << pendingOrderStatus.OrderStatus);
    }
    //remove order when finish
    if(pendingOrderStatus.OrderStatus == LF_CHAR_AllTraded  || pendingOrderStatus.OrderStatus == LF_CHAR_Canceled
            || pendingOrderStatus.OrderStatus == LF_CHAR_Error)
    {
        KF_LOG_INFO(logger, "[retrieveOrderStatus] order all traded  or cancel. (OrderRef)" << pendingOrderStatus.OrderRef
                << "(status)" << pendingOrderStatus.OrderStatus);
        return true;
    }
    KF_LOG_INFO(logger, "[retrieveOrderStatus] get status end. (OrderRef)" << pendingOrderStatus.OrderRef
            << "(status)" << pendingOrderStatus.OrderStatus);
    return false;
}

void TDEngineUpbit::retrieveTradeStatus(AccountUnitUpbit& unit,Document& resultTrade,std::vector<std::string>& sentTradeIds)
{
    KF_LOG_INFO(logger, "[retrieveTradeStatus] (unit.pendingOrderStatus.size())"
            << unit.pendingOrderStatus.size() /*<< " (unit.pendingOnRtnTrades.size()) " << unit.pendingOnRtnTrades.size()*/);
    //if 'ours' order is finished, and ours trade is finished too , dont get trade info anymore.
    if(unit.pendingOrderStatus.size() == 0 /*&& unit.pendingOnRtnTrades.size() == 0*/) return;

    //KF_LOG_INFO(logger, "[retrieveTradeStatus] get_my_trades 3 (last_trade_id)" << tradeStatusIterator->last_trade_id << " (InstrumentID)" << tradeStatusIterator->InstrumentID);
    LFRtnTradeField rtn_trade;
    memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
    strcpy(rtn_trade.ExchangeID, "upbit");
    strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
    std::string strMarket = resultTrade["market"].GetString();
    std::string strInstrumentID = unit.coinPairWhiteList.GetKeyByValue(strMarket);
    strncpy(rtn_trade.InstrumentID, strInstrumentID.c_str(), 31);
    std::string side = resultTrade["side"].GetString();
    rtn_trade.Direction = GetDirection(side);
    std::string uuid = resultTrade["uuid"].GetString();
    strncpy(rtn_trade.OrderRef, findKey(unit.mapOrderRef2OrderInfo,uuid).c_str(), 13);
    //must be Array
    int len = resultTrade["trades"].Size();


    for(int i = 0 ; i < len; i++)
    {
        std::string newtradeId = resultTrade["trades"].GetArray()[i]["uuid"].GetString();
        bool hasSendThisTradeId = false;
        std::vector<std::string>::iterator sentTradeIdsIterator;
        for(sentTradeIdsIterator = sentTradeIds.begin(); sentTradeIdsIterator !=  sentTradeIds.end(); sentTradeIdsIterator++) {
            if((*sentTradeIdsIterator) == newtradeId) {
                hasSendThisTradeId = true;
                break;
            }
        }
        if(hasSendThisTradeId) {
            KF_LOG_INFO(logger, "[retrieveTradeStatus]  (hasSendThisTradeId)" << hasSendThisTradeId << " (newtradeId)" << newtradeId);
            continue;
        }
        strncpy(rtn_trade.TradeID,newtradeId.c_str(),21);
        strncpy(rtn_trade.OrderSysID,uuid.c_str(),31);
        // KF_LOG_INFO(logger, "[retrieveTradeStatus] get_my_trades 4 (for_i)" << i << "  (last_trade_id)" << tradeStatusIterator->last_trade_id << " (InstrumentID)" << tradeStatusIterator->InstrumentID);
        rtn_trade.Volume = std::round(stod(resultTrade["trades"].GetArray()[i]["volume"].GetString()) * scale_offset);
        rtn_trade.Price = std::round(stod(resultTrade["trades"].GetArray()[i]["price"].GetString()) * scale_offset);

        on_rtn_trade(&rtn_trade);
        raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
                source_id, MSG_TYPE_LF_RTN_TRADE_UPBIT, 1/*islast*/, -1/*invalidRid*/);  
        sentTradeIds.push_back(newtradeId);
        KF_LOG_INFO(logger, "[retrieveTradeStatus]  on_rtn_trade (orderRef)" << rtn_trade.OrderRef);
    }
}

void TDEngineUpbit::addNewQueryOrdersAndTrades(AccountUnitUpbit& unit, const char_31 InstrumentID,
        const char_21 OrderRef, const LfOrderStatusType OrderStatus,
        const uint64_t VolumeTraded, LfDirectionType Direction, int64_t UpbitOrderId)
{
    //add new orderId for GetAndHandleOrderTradeResponse
    std::lock_guard<std::mutex> guard_mutex(*mutex_order_and_trade);

    PendingUpbitOrderStatus status;
    memset(&status, 0, sizeof(PendingUpbitOrderStatus));
    strncpy(status.InstrumentID, InstrumentID, 31);
    strncpy(status.OrderRef, OrderRef, 21);
    status.OrderStatus = OrderStatus;
    status.VolumeTraded = VolumeTraded;
    unit.newOrderStatus.push_back(status);
    KF_LOG_INFO(logger, "[addNewQueryOrdersAndTrades] (OrderRef)" << OrderRef);
}

//bool TDEngineUpbit::isExistSymbolInPendingTradeStatus(AccountUnitUpbit& unit, const char_31 InstrumentID)
//{
//    std::vector<PendingUpbitTradeStatus>::iterator tradeStatusIterator;

//    for(tradeStatusIterator = unit.pendingTradeStatus.begin(); tradeStatusIterator != unit.pendingTradeStatus.end(); ++tradeStatusIterator)
//    {
//       if(strcmp(tradeStatusIterator->InstrumentID, InstrumentID) == 0)
//        {
//           return true;
////        }
//   }
//    return false;
//}


bool TDEngineUpbit::isExistSymbolInPendingUpbitOrderStatus(AccountUnitUpbit& unit, const char_31 InstrumentID, const char_21 OrderRef)
{
    std::vector<PendingUpbitOrderStatus>::iterator orderStatusIterator;

    for(orderStatusIterator = unit.pendingOrderStatus.begin(); orderStatusIterator != unit.pendingOrderStatus.end(); orderStatusIterator++) {
        if (strcmp(orderStatusIterator->InstrumentID, InstrumentID) == 0 && strcmp(orderStatusIterator->OrderRef, OrderRef) == 0) {
            return true;
        }
    }
    return false;
}

void TDEngineUpbit::set_reader_thread()
{
    ITDEngine::set_reader_thread();

    KF_LOG_INFO(logger, "[set_reader_thread] rest_thread start on AccountUnitUpbit::loop");
    rest_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineUpbit::loop, this)));
}

void TDEngineUpbit::loop()
{
    KF_LOG_INFO(logger, "[loop] (isRunning) " << isRunning);
    while(isRunning)
    {
        using namespace std::chrono;
        auto current_ms = duration_cast< milliseconds>(system_clock::now().time_since_epoch()).count();
        if(last_rest_get_ts != 0 && (current_ms - last_rest_get_ts) < rest_get_interval_ms)
        {
            continue;
        }

        last_rest_get_ts = current_ms;
        //GetAndHandleOrderTradeResponse();
    }
}


std::vector<std::string> TDEngineUpbit::split(std::string str, std::string token)
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

int32_t TDEngineUpbit::send_order(AccountUnitUpbit& unit, const char *symbol,
                const char *side,
                const char *type,
                const char *timeInForce,
                double quantity,
                double price,
                const char *newClientOrderId,
                double stopPrice,
                double icebergQty,
                Document& json)//quest5v3 fxw 
{
    KF_LOG_INFO(logger, "[send_order]");

    int retry_times = 0;
    cpr::Response response;
    bool should_retry = false;
    do {
        should_retry = false;

        long recvWindow = order_insert_recvwindow_ms;
        std::string Method = "POST";
        std::string requestPath = "https://api.upbit.com/v1/orders";
        std::string queryString("");
        std::string body = "";

        body.append( "market=" );
        body.append( symbol );

        body.append("&side=");
        body.append( side );

        body.append("&volume=");
        body.append( to_string( quantity) );

        if(strcmp("MARKET", type) != 0)
        {
            body.append("&price=");
            std::string priceStr;
            std::stringstream convertStream;
            convertStream <<std::fixed << std::setprecision(8) << price;
            convertStream >> priceStr;

            KF_LOG_INFO(logger, "[send_order] (priceStr)" << priceStr);

            body.append( priceStr );
        }

        body.append("&ord_type=");
        body.append( type );
     
       //if ( strlen( newClientOrderId ) > 0 ) {
        //    body.append("&identifier=");
       //     body.append( newClientOrderId );
       // }
       queryString = getEncode(body);
       std::string strAuthorization  = getAuthorization(unit,queryString);
       const  std::string& url = requestPath;

        response = Post(Url{url},
                                  Header{{"Authorization", strAuthorization}},
                                  Body{body}, Timeout{100000});

        KF_LOG_INFO(logger, "[send_order] (url) " << url << 
                " (strAuthorization) "<<strAuthorization<<
                " (body) "<<body<<
                " (response.status_code) " << response.status_code <<
                " (response.error.message) " << response.error.message <<
                " (response.text) " << response.text.c_str());

        if(shouldRetry(response.status_code, response.error.message, response.text)) {
            should_retry = true;//quest5v3  不做重新发单，只尝试一次
            retry_times++;
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_milliseconds));//失败等待一秒不变
        }
    } while(should_retry && retry_times < max_rest_retry_times);

    KF_LOG_INFO(logger, "[send_order] out_retry (response.status_code) " << response.status_code <<
                                                                         " (response.error.message) " << response.error.message <<
                                                                         " (response.text) " << response.text.c_str() );
    
    getResponse(response.status_code, response.text, response.error.message, json);
    return response.status_code;
}

/*
 * https://github.com/Upbit-exchange/Upbit-official-api-docs/blob/master/errors.md
 -1021 INVALID_TIMESTAMP
 Timestamp for this request is outside of the recvWindow.
 Timestamp for this request was 1000ms ahead of the server's time.


 *  (response.status_code) 400 (response.error.message)  (response.text) {"code":-1021,"msg":"Timestamp for this request is outside of the recvWindow."}
 * */

bool TDEngineUpbit::shouldRetry(int http_status_code, std::string errorMsg, std::string text)//quest5v3 fxw
{
    /*
    if( http_status_code == 201)
    {
        return false;
    }
    else
    {
        
        return true;
    }
    */
    if (http_status_code > 299 || http_status_code < 200)
    {
        KF_LOG_DEBUG(logger, "[shouldRetry] (errorMsg) " << errorMsg);
        return true;
    }
    else
    {
        KF_LOG_DEBUG(logger, "[shouldRetry] (text) " << text);
        return false;
    }

}

std::int32_t TDEngineUpbit::get_order(AccountUnitUpbit& unit, const char *uuid, Document& json)
{
    KF_LOG_INFO(logger, "[get_order]");
    long recvWindow = 5000;
    std::string Method = "GET";
    std::string requestPath = "https://api.upbit.com/v1/order?";
    std::string queryString("");
    std::string body = "";

    queryString.append( "uuid=" );
    queryString.append( uuid );
    queryString  = getEncode(queryString);
    string url = requestPath + queryString;

    int retry_times = 0;
    cpr::Response response;
    bool should_retry = false;
    do {
        should_retry = false;
        std::string strAuthorization = getAuthorization(unit,queryString);
        response = Get(Url{url},
                Header{{"Authorization", strAuthorization}},
                Body{body}, Timeout{100000});

        KF_LOG_INFO(logger, "[get_order] (url) " << url << 
                "(Authorization)"<<strAuthorization<<
                " (response.status_code) " << response.status_code <<
                " (response.error.message) " << response.error.message <<
                " (response.text) " << response.text.c_str());
        if(response.status_code != 200) {
            should_retry = true;
            retry_times++;
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_milliseconds));
        }
    } while(should_retry && retry_times < max_rest_retry_times);

    getResponse(response.status_code, response.text, response.error.message, json);
    return response.status_code;
}

std::int32_t  TDEngineUpbit::cancel_order(AccountUnitUpbit& unit, const char *symbol,
                  const char *uuid,  Document &json)
{
    int64_t start=getTimestamp();
    KF_LOG_INFO(logger, "[cancel_order]");
    int retry_times = 0;
    cpr::Response response;
    bool should_retry = false;
    do {
        should_retry = false;
        
        long recvWindow = order_action_recvwindow_ms;
        std::string Method = "DELETE";
        std::string requestPath = "https://api.upbit.com/v1/order?";
        std::string queryString("");
        std::string body = "";

        queryString.append( "uuid=" );
        queryString.append( uuid );
        queryString = getEncode(queryString);
        std::string strAuthorization = getAuthorization(unit,queryString);
        string url = requestPath + queryString;

        response = Delete(Url{url},
                                  Header{{"Authorization", strAuthorization}},
                                  Body{body}, Timeout{100000});

        KF_LOG_INFO(logger,"(retry_times)"<<retry_times<< "[cancel_order] (url) " << url <<
                                                 " (strAuthorization) "<<strAuthorization<<
                                                 " (body) "<<body<<
                                                 " (response.status_code) " << response.status_code <<
                                                 " (response.error.message) " << response.error.message <<
                                                 " (response.text) " << response.text.c_str()<<
                                                 "(timeConsumed)"<<(getTimestamp()-start)<<
                                                 "ms");


        getResponse(response.status_code, response.text, response.error.message, json);
        if(response.status_code != 200 && response.status_code != 404) {//撤单操作没有成功
            should_retry = true;
            retry_times++;
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_milliseconds));
        }
        else if(response.status_code==404)
        {
            break;
        }
        else if(strcmp(json["state"].GetString(),"cancel")&&strcmp(json["state"].GetString(),"done"))//撤单动作未起效
        {
            should_retry = true;
            retry_times++;
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_milliseconds/2));
            KF_LOG_INFO(logger,"(retry_times)"<<retry_times<<"(timeConsumed)"<<(getTimestamp()-start)<<"ms");
            get_order(unit,uuid,json);
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_milliseconds/2));
        }
        if (retry_times >= max_rest_retry_times) break;
    } while(should_retry);
    if(response.status_code!=404)
    {
        KF_LOG_INFO(logger, "[cancel_order] out_retry (response.status_code) " << response.status_code <<
                " (response.error.message) " << response.error.message <<
                " (response.text) " << response.text.c_str()<<
                "(timeConsumed)"<<(getTimestamp()-start)<<
                "ms");
    }
    else
    {
        KF_LOG_INFO(logger,"(retry_times)"<<retry_times<<"(timeConsumed)"<<(getTimestamp()-start)<<"ms");
        get_order(unit,uuid,json);
    }
    return response.status_code;
}

void TDEngineUpbit::get_open_orders(AccountUnitUpbit& unit, Document &json)
{
    KF_LOG_INFO(logger, "[get_open_orders]");
    long recvWindow = 5000;
    std::string Method = "GET";
    std::string requestPath = "https://api.upbit.com/v1/orders?";
    std::string queryString("");
    std::string body = "";

    queryString = "state=wait&page=1";
    queryString  = getEncode(queryString);

    int retry_times = 0;
    cpr::Response response;
    bool should_retry = false;
    do {
        should_retry = false;
        std::string strAuthorization = getAuthorization(unit,queryString);

        string url = requestPath + queryString;

        response = Get(Url{url},
                Header{{"Authorization", strAuthorization}},
                Body{body}, Timeout{100000});

        KF_LOG_INFO(logger, "[get_open_orders] (url) " << url << " (response.status_code) " << response.status_code <<
                " (response.error.message) " << response.error.message <<
                " (response.text) " << response.text.c_str());
        if(response.status_code != 200) {
            should_retry = true;
            retry_times++;
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_milliseconds));
        }
    } while(should_retry && retry_times < max_rest_retry_times);
    return getResponse(response.status_code, response.text, response.error.message, json);
}


void TDEngineUpbit::get_exchange_time(AccountUnitUpbit& unit, Document &json)
{
    KF_LOG_INFO(logger, "[get_exchange_time]");
    long recvWindow = 5000;
    std::string Timestamp = std::to_string(getTimestamp());
    std::string Method = "GET";
    std::string requestPath = "https://api.upbit.com/api/v1/time";
    std::string queryString("");
    std::string body = "";

    string url = requestPath + queryString;

    const auto response = Get(Url{url},
            Header{{"X-MBX-APIKEY", unit.api_key}},
            Body{body}, Timeout{100000});

    KF_LOG_INFO(logger, "[get_exchange_time] (url) " << url << " (response.status_code) " << response.status_code <<
            " (response.error.message) " << response.error.message <<
            " (response.text) " << response.text.c_str());
    return getResponse(response.status_code, response.text, response.error.message, json);
}

OrderInfo TDEngineUpbit::findValue(const std::map<std::string,OrderInfo>& mapSrc,const std::string& strKey)
{
    auto it =mapSrc.find(strKey);
    if(it != mapSrc.end())
    {
        return it->second;
    }
    else{
        return OrderInfo();
    }
}

std::string TDEngineUpbit::findKey(const std::map<std::string,OrderInfo>& mapSrc,const std::string& strValue)
{
    for(auto it = mapSrc.begin();it!=mapSrc.end();++it)
    {
        if(it->second.strRemoteUUID == strValue)
        {
            return it->first;
        }
    }
    return "";
}

void TDEngineUpbit::filterMarkets(std::vector<std::string>& vstrMarkets)
{
    for(auto it = vstrMarkets.begin(); it != vstrMarkets.end(); )
    {
        bool inWhiteList = false;

        for (size_t idx = 0; idx < account_units.size(); idx++)
        {
            AccountUnitUpbit& unit = account_units[idx];
            std::string ticker = unit.coinPairWhiteList.GetKeyByValue(*it);
            if(ticker.length() > 0) 
            {
                inWhiteList = true;
                break;
            }
        }

        if(inWhiteList)
        {
            ++it;
        }
        else
        {
            it = vstrMarkets.erase(it);
        }
    }
}

void TDEngineUpbit::getAllMarkets(std::vector<std::string>& vstrMarkets)
{
    KF_LOG_INFO(logger, "[getAllMarkets]");
    long recvWindow = 5000;
    std::string Timestamp = getTimestampString();
    std::string Method = "GET";
    std::string requestPath = "https://api.upbit.com/v1/market/all";
    std::string queryString("");
    std::string body = "";

    string url = requestPath + queryString;

    const auto response = cpr::Get(Url{url});
    KF_LOG_INFO(logger, "[getAllMarkets] (url) " << url << " (response.status_code) " << response.status_code <<
            " (response.error.message) " << response.error.message <<
            " (response.text) " << response.text.c_str());
    Document json;
    json.Parse(response.text.c_str());
    if(json.IsArray())
    {
        int nSize = json.Size();
        for(int nPos = 0 ;nPos < nSize; ++nPos)
        {
            auto& marketInfo = json.GetArray()[nPos];
            if(marketInfo.HasMember("market"))
            {
                vstrMarkets.push_back(marketInfo["market"].GetString());
            }  else {  KF_LOG_INFO(logger, "[getAllMarkets] respon not member market");}
        } 
    }
    else {  KF_LOG_INFO(logger, "[getAllMarkets] respon not a array");}
}

std::string TDEngineUpbit::getEncode(const std::string& str)
{
    return str;
    //return  base64_encode((unsigned char const*)str.c_str(),str.size());
}

std::string TDEngineUpbit::getUUID()
{
    /*uuid_t uuid;
    //The UUID is 16 bytes (128 bits) long
    uuid_generate(uuid);
    return string((char*)uuid);
    */
    const std::string CHARS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string uuid = std::string(36,' ');
    int rnd = 0;
    int r = 0;

    uuid[8] = '-';
    uuid[13] = '-';
    uuid[18] = '-';
    uuid[23] = '-';

    uuid[14] = '4';

    for(int i=0;i<36;i++){
        if (i != 8 && i != 13 && i != 18 && i != 14 && i != 23) {
            if (rnd <= 0x02) {
                rnd = 0x2000000 + (std::rand() * 0x1000000) | 0;
            }
            rnd >>= 4;
            uuid[i] = CHARS[(i == 19) ? ((rnd & 0xf) & 0x3) | 0x8 : rnd & 0xf];
        }
    }
    return uuid;
}

std::string TDEngineUpbit::getAuthorization(const AccountUnitUpbit& unit,const std::string& strQuery)
{
    KF_LOG_INFO(logger, "[getAuthorization] strQuery:" << strQuery);
    std::string strPayLoad;
    //std::string str;
    //str=utils::crypto::base64_encode((const unsigned char*)strQuery.c_str(),strQuery.length());
    //str =utils::crypto::jwt_hash_sha512(strQuery);
    if(strQuery == "")
    {
        strPayLoad = R"({"access_key": ")" + unit.api_key + R"(","nonce": ")" +getUUID() + R"("})";
    }
    else
    {
        //uuid=e8eeedea-b495-49da-9cf9-ec3e2909ef16
        strPayLoad = R"({"access_key":")" + unit.api_key + R"(","nonce":")" +getTimestampString()
            + R"(","query":")" + strQuery
            + R"("})";
        /*strPayLoad = R"({"access_key":")" + unit.api_key + R"(","nonce":")" +getUUID() + R"(","query_hash":")" + str  
            +R"(","query_hash_alg":"SHA512)"
            +R"("})";      */
    }
    std::string strJWT = utils::crypto::jwt_hs256_create(strPayLoad,unit.secret_key);
    std::string strAuthorization = "Bearer ";
    strAuthorization += strJWT;

    KF_LOG_INFO(logger, "[getAuthorization] strPayLoad:" << strPayLoad);

    return strAuthorization;
}

void TDEngineUpbit::getChanceResponce(const AccountUnitUpbit& unit, const std::string& strMarket,Document& d)
{
    long recvWindow = 5000;
    std::string Method = "GET";
    std::string strQueryString = "market=";
    std::string requestPath = "https://api.upbit.com/v1/orders/chance?";
    std::string body = "";

    std::string strParamEncode = getEncode(strQueryString + strMarket);
    std::string url = requestPath + strParamEncode;

    int retry_times = 0;
    cpr::Response response;
    bool should_retry = false;
    do {
        should_retry = false;
        std::string Authorization = getAuthorization(unit,strParamEncode);

        response = Get(Url{url},
                Header{{  "Authorization", Authorization}},
                Body{body}, Timeout{100000});
        KF_LOG_INFO(logger, "[getChanceResponce] (url) " << url <<
                "(Authorization)"<<Authorization<<
                " (response.status_code) " << response.status_code <<
                " (response.error.message) " << response.error.message <<
                " (response.text) " << response.text.c_str());
        if(response.status_code != 200) {
            should_retry = true;
            retry_times++;
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_milliseconds));
        }
    } while(should_retry && retry_times < max_rest_retry_times);


    d.Parse(response.text.c_str());
}

bool TDEngineUpbit::loadMarketsInfo(AccountUnitUpbit& unit, const std::vector<std::string>& vstrMarkets)
{
    for(auto& strMarket : vstrMarkets)
    {
        Document doc;
        getChanceResponce(unit,strMarket,doc);

        KF_LOG_INFO(logger, "[loadExchangeOrderFilters]");
        if(doc.HasParseError() || !doc.IsObject())
        {
            return false;
        }

        std::map<std::string, SendOrderFilter>::iterator it;
        if(doc.HasMember("market") && doc.HasMember("id"))
        {
            //std::string strMarket = doc["market"]["id"].GetString();
            it = unit.sendOrderFilters.insert(std::make_pair(strMarket,SendOrderFilter())).first;
            if(doc.HasMember("bid") && doc["bid"].HasMember("currency") && doc["bid"].HasMember("min_total"))
            {
                it->second.strBidCurrency = doc["bid"]["currency"].GetString();
                it->second.nBidMinTotal =  atoi(doc["bid"]["min_total"].GetString());
            }
            if(doc.HasMember("bid") && doc["bid"].HasMember("price_unit") )
            {
                std::string strBidUnit = doc["bid"]["price_unit"].GetString();
                auto nBegin = strBidUnit.find(".",0);
                auto nEnd = strBidUnit.find("1",0);
                if(nBegin != std::string::npos && nEnd != std::string::npos)
                {
                    it->second.nBidTickSize = nEnd - nBegin;
                }
            }
            if(doc.HasMember("ask") && doc["ask"].HasMember("currency") && doc["ask"].HasMember("min_total"))
            {
                it->second.strAskCurrency = doc["ask"]["currency"].GetString();
                it->second.nAskMinTotal =  atoi(doc["ask"]["min_total"].GetString());
            }
            if(doc.HasMember("ask") && doc["ask"].HasMember("price_unit") )
            {
                std::string strAskUnit = doc["ask"]["price_unit"].GetString();
                auto nBegin = strAskUnit.find(".",0);
                auto nEnd = strAskUnit.find("1",0);
                if(nBegin != std::string::npos && nEnd != std::string::npos)
                {
                    it->second.nAskTickSize = nEnd - nBegin;
                }
            }
            if(doc.HasMember("max_total"))
            {
                it->second.nMaxTotal = atoi(doc["max_total"].GetString());
            }
            if(doc.HasMember("state"))
            {
                it->second.strState = doc["state"].GetString();
            }
        }
    }
    return true;
}

void TDEngineUpbit::printResponse(const Document& d)
{
    if(d.IsObject() && d.HasMember("code") && d.HasMember("msg")) {
        KF_LOG_INFO(logger, "[printResponse] error (code) " << d["code"].GetInt() << " (msg) " << d["msg"].GetString());
    } else {
        StringBuffer buffer;
        Writer<StringBuffer> writer(buffer);
        d.Accept(writer);
        KF_LOG_INFO(logger, "[printResponse] ok (text) " << buffer.GetString());
    }
}

void TDEngineUpbit::getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& json)
{
    json.Parse(responseText.c_str());
}

inline int64_t TDEngineUpbit::getTimestamp()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp ;
}

std::string TDEngineUpbit::getTimestampString()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    KF_LOG_DEBUG(logger, "[getTimestampString] (timestamp)" << timestamp << " (timeDiffOfExchange)" << timeDiffOfExchange << " (exchange_shift_ms)" << exchange_shift_ms);
    timestamp =  timestamp - timeDiffOfExchange + exchange_shift_ms;
    KF_LOG_INFO(logger, "[getTimestampString] (new timestamp)" << timestamp);
    std::string timestampStr;
    std::stringstream convertStream;
    convertStream << timestamp;
    convertStream >> timestampStr;
    return timestampStr;
}


int64_t TDEngineUpbit::getTimeDiffOfExchange(AccountUnitUpbit& unit)
{
    KF_LOG_INFO(logger, "[getTimeDiffOfExchange] ");
    //reset to 0
    int64_t timeDiffOfExchange = 0;
    //
    //    int calculateTimes = 3;
    //    int64_t accumulationDiffTime = 0;
    //    bool hasResponse = false;
    //    for(int i = 0 ; i < calculateTimes; i++)
    //    {
    //        Document d;
    //        int64_t start_time = getTimestamp();
    //        int64_t exchangeTime = start_time;
    //        KF_LOG_INFO(logger, "[getTimeDiffOfExchange] (i) " << i << " (start_time) " << start_time);
    //        get_exchange_time(unit, d);
    //        if(!d.HasParseError() && d.HasMember("serverTime")) {//Upbit serverTime
    //            exchangeTime = d["serverTime"].GetInt64();
    //            KF_LOG_INFO(logger, "[getTimeDiffOfExchange] (i) " << i << " (exchangeTime) " << exchangeTime);
    //            hasResponse = true;
    //        }
    //        int64_t finish_time = getTimestamp();
    //        KF_LOG_INFO(logger, "[getTimeDiffOfExchange] (i) " << i << " (finish_time) " << finish_time);
    //        int64_t tripTime = (finish_time - start_time) / 2;
    //        KF_LOG_INFO(logger, "[getTimeDiffOfExchange] (i) " << i << " (tripTime) " << tripTime);
    //        accumulationDiffTime += start_time + tripTime - exchangeTime;
    //        KF_LOG_INFO(logger, "[getTimeDiffOfExchange] (i) " << i << " (accumulationDiffTime) " << accumulationDiffTime);
    //    }
    //    //set the diff
    //    if(hasResponse)
    //    {
    //        timeDiffOfExchange = accumulationDiffTime / calculateTimes;
    //    }
    //    KF_LOG_INFO(logger, "[getTimeDiffOfExchange] (timeDiffOfExchange) " << timeDiffOfExchange);
    return timeDiffOfExchange;
}

#define GBK2UTF8(msg) kungfu::yijinjing::gbk2utf8(string(msg))

BOOST_PYTHON_MODULE(libupbittd)
{
    using namespace boost::python;
    class_<TDEngineUpbit, boost::shared_ptr<TDEngineUpbit> >("Engine")
        .def(init<>())
        .def("init", &TDEngineUpbit::initialize)
        .def("start", &TDEngineUpbit::start)
        .def("stop", &TDEngineUpbit::stop)
        .def("logout", &TDEngineUpbit::logout)
        .def("wait_for_stop", &TDEngineUpbit::wait_for_stop);
}
