#include "TDEngineBithumb.h"
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
#include <mutex>
#include <chrono>
#include "../../utils/crypto/openssl_util.h"

using cpr::Delete;
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
using utils::crypto::hmac_sha512;
using utils::crypto::base64_encode;
USING_WC_NAMESPACE
std::mutex g_unit_mutex;
TDEngineBithumb::TDEngineBithumb(): ITDEngine(SOURCE_BITHUMB)
{
    logger = yijinjing::KfLog::getLogger("TradeEngine.Bithumb");
    KF_LOG_INFO(logger, "[TDEngineBithumb]");

    mutex_order_and_trade = new std::mutex();
    mutex_response_order_status = new std::mutex();
    mutex_orderaction_waiting_response = new std::mutex();
}

TDEngineBithumb::~TDEngineBithumb()
{
    if(mutex_order_and_trade != nullptr) delete mutex_order_and_trade;
    if(mutex_response_order_status != nullptr) delete mutex_response_order_status;
    if(mutex_orderaction_waiting_response != nullptr) delete mutex_orderaction_waiting_response;
}
bool isStatusOK(const std::string& status)
{
    return status == "0000";
}
std::pair<std::string,std::string> SplitCoinPair(const std::string& coinpair)
{
    auto pos = coinpair.find('-');
    if(pos == std::string::npos)
    {
        return std::make_pair("","");
    }
    return std::make_pair(coinpair.substr(0,pos),coinpair.substr(pos+1));
}

std::mutex g_httpMutex;
cpr::Response TDEngineBithumb::Get(const std::string& method_url,const std::string& body, AccountUnitBithumb& unit)
{
    /*
    std::string strTimeStamp = std::to_string(getTimestamp());
    std::string strPostBody = "endpoint="+endPoint+"&"+body;
    std::string reqbody =  utils::crypto::url_encode(strPostBody.c_str());
    std::string strSign = construct_request_body(unit,method_url,strPostBodyEncode,strTimeStamp);
    
    std::string queryString= "?" + construct_request_body(unit,body);
    string url = unit.baseUrl + method_url + queryString;

    std::unique_lock<std::mutex> lock(g_httpMutex);
    const auto response = cpr::Get(Url{url}, cpr::VerifySsl{false},
                              Header{{"Content-Type", "application/x-www-form-urlencoded"},
                                {"Content-Length", to_string(reqbody.size())},
                                {"Api-Key", unit.api_key},
                                {"Api-Sign", strSign},
                                {"Api-Nonce", strTimeStamp}
                                }, Timeout{10000} );
    lock.unlock();
    KF_LOG_INFO(logger, "[get] (url) " << url << " (sign)" << strSign << " (timestamp)" <<strTimeStamp <<" (response.status_code) " << response.status_code <<
                                               " (response.error.message) " << response.error.message <<
                                               " (response.text) " << response.text.c_str());
    return response;
    */
    return cpr::Response();
}

cpr::Response TDEngineBithumb::Post(const std::string& method_url,const std::string& body, AccountUnitBithumb& unit)
{
    std::string strTimeStamp = std::to_string(getTimestamp());
    std::string strPostBody = "endpoint="+endPoint+"&"+body;
    std::string reqbody =  utils::crypto::url_encode(strPostBody.c_str());
    std::string strSign = construct_request_body(unit,method_url,strPostBodyEncode,strTimeStamp);

    string url = unit.baseUrl + method_url;
    std::unique_lock<std::mutex> lock(g_httpMutex);
    auto response = cpr::Post(Url{url}, cpr::VerifySsl{false},
                    Header{{"Content-Type", "application/x-www-form-urlencoded"},
                           {"Content-Length", to_string(reqbody.size())},
                           {"Api-Key", unit.api_key},
                           {"Api-Sign", strSign},
                           {"Api-Nonce", strTimeStamp}
                           },
                    Body{reqbody}, Timeout{30000});
    lock.unlock();
    KF_LOG_INFO(logger, "[post] (url) " << url <<"(body) "<< reqbody << " (sign)" << strSign << " (timestamp)" <<strTimeStamp << " (response.status_code) " << response.status_code <<
                                       " (response.error.message) " << response.error.message <<
                                       " (response.text) " << response.text);
    return response;
}

void TDEngineBithumb::init()
{
    ITDEngine::init();
    JournalPair tdRawPair = getTdRawJournalPair(source_id);
    raw_writer = yijinjing::JournalSafeWriter::create(tdRawPair.first, tdRawPair.second, "RAW_" + name());
    KF_LOG_INFO(logger, "[init]");
}

void TDEngineBithumb::pre_load(const json& j_config)
{
    KF_LOG_INFO(logger, "[pre_load]");
}

void TDEngineBithumb::resize_accounts(int account_num)
{
    account_units.resize(account_num);
    KF_LOG_INFO(logger, "[resize_accounts]");
}

TradeAccount TDEngineBithumb::load_account(int idx, const json& j_config)
{
    KF_LOG_INFO(logger, "[load_account]");
    // internal load
    string api_key = j_config["APIKey"].get<string>();
    string secret_key = j_config["SecretKey"].get<string>();
    string passphrase = j_config["passphrase"].get<string>();
    string baseUrl = j_config["baseUrl"].get<string>();
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();

    if(j_config.find("orderaction_max_waiting_seconds") != j_config.end()) {
        orderaction_max_waiting_seconds = j_config["orderaction_max_waiting_seconds"].get<int>();
    }
    KF_LOG_INFO(logger, "[load_account] (orderaction_max_waiting_seconds)" << orderaction_max_waiting_seconds);

    if(j_config.find("max_rest_retry_times") != j_config.end()) {
        max_rest_retry_times = j_config["max_rest_retry_times"].get<int>();
    }
    KF_LOG_INFO(logger, "[load_account] (max_rest_retry_times)" << max_rest_retry_times);


    if(j_config.find("retry_interval_milliseconds") != j_config.end()) {
        retry_interval_milliseconds = j_config["retry_interval_milliseconds"].get<int>();
    }
    KF_LOG_INFO(logger, "[load_account] (retry_interval_milliseconds)" << retry_interval_milliseconds);

    AccountUnitBithumb& unit = account_units[idx];
    unit.api_key = api_key;
    unit.secret_key = secret_key;
    unit.passphrase = passphrase;
    unit.baseUrl = baseUrl;

    KF_LOG_INFO(logger, "[load_account] (api_key)" << api_key << " (baseUrl)" << unit.baseUrl);

    unit.coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
    unit.coinPairWhiteList.Debug_print();

    unit.positionWhiteList.ReadWhiteLists(j_config, "positionWhiteLists");
    unit.positionWhiteList.Debug_print();

    //display usage:
    if(unit.coinPairWhiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "TDEngineBithumb::load_account: please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"btcusdt\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"etceth\"");
        KF_LOG_ERROR(logger, "},");
    }

    //cancel_all_orders(unit, "etc_eth", json);

    // set up
    TradeAccount account = {};
    //partly copy this fields
    strncpy(account.UserID, api_key.c_str(), 16);
    strncpy(account.Password, secret_key.c_str(), 21);
    return account;
}

void TDEngineBithumb::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[connect]");
    for (size_t idx = 0; idx < account_units.size(); idx++)
    {
        AccountUnitBithumb& unit = account_units[idx];
        KF_LOG_INFO(logger, "[connect] (api_key)" << unit.api_key);
        Document doc;
        //
        std::string requestPath = "/Info/Account";
        std::string params = "apiKey="+unit.api_key+"&secretKey"+unit.secret_key;
        const auto response = Post(requestPath,params,unit);

        getResponse(response.status_code, response.text, response.error.message, doc);

        if ( !unit.logged_in && doc.HasMember("status"))
        {
            std::string status = doc["status"].GetString();
            unit.logged_in = isStatusOK(status);
        }
    }
}


void TDEngineBithumb::login(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[login]");
    connect(timeout_nsec);
}

void TDEngineBithumb::logout()
{
    KF_LOG_INFO(logger, "[logout]");
}

void TDEngineBithumb::release_api()
{
    KF_LOG_INFO(logger, "[release_api]");
}

bool TDEngineBithumb::is_logged_in() const
{
    KF_LOG_INFO(logger, "[is_logged_in]");
    for (auto& unit: account_units)
    {
        if (!unit.logged_in)
            return false;
    }
    return true;
}

bool TDEngineBithumb::is_connected() const
{
    KF_LOG_INFO(logger, "[is_connected]");
    return is_logged_in();
}


std::string TDEngineBithumb::GetSide(const LfDirectionType& input) {
    if (LF_CHAR_Buy == input) {
        return "bid";
    } else if (LF_CHAR_Sell == input) {
        return "ask";
    } else {
        return "";
    }
}

LfDirectionType TDEngineBithumb::GetDirection(std::string input) {
    if ("bid" == input) {
        return LF_CHAR_Buy;
    } else if ("ask" == input) {
        return LF_CHAR_Sell;
    } else {
        return LF_CHAR_Buy;
    }
}

std::string TDEngineBithumb::GetType(const LfOrderPriceTypeType& input) {
    if (LF_CHAR_LimitPrice == input) {
        return "limit";
    } else if (LF_CHAR_AnyPrice == input) {
        return "market";
    } else {
        return "";
    }
}

LfOrderPriceTypeType TDEngineBithumb::GetPriceType(std::string input) {
    if ("limit" == input) {
        return LF_CHAR_LimitPrice;
    } else if ("market" == input) {
        return LF_CHAR_AnyPrice;
    } else {
        return '0';
    }
}
//订单状态，﻿open（未成交）、filled（已完成）、canceled（已撤销）、cancel（撤销中）、partially-filled（部分成交）
LfOrderStatusType TDEngineBithumb::GetOrderStatus(std::string input) {
    if ("placed" == input) {
        return LF_CHAR_NotTouched;
    } else if ("done" == input) {
        return LF_CHAR_AllTraded;
    } else if ("cancel" == input) {
        return LF_CHAR_Canceled;
    } else {
        return LF_CHAR_NotTouched;
    }
}

/**
 * req functions
 */
void TDEngineBithumb::req_investor_position(const LFQryPositionField* data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId);

    AccountUnitBithumb& unit = account_units[account_index];
    KF_LOG_INFO(logger, "[req_investor_position] (api_key)" << unit.api_key << " (InstrumentID) " << data->InstrumentID);

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

    int errorId = 0;
    std::string errorMsg = "";
    Document d;
    get_account(unit, d);

    if(!d.HasParseError() && d.IsObject() && d.HasMember("status"))
    {
        std::string statusCode = d["status"].GetString();
        if(!isStatusOK(statusCode)) {
            if (d.HasMember("message") && d["message"].IsString()) {
                errorMsg = d["message"].GetString();
            }
            KF_LOG_ERROR(logger, "[req_investor_position] failed!" << " (rid)" << requestId << " (errorId)" << statusCode
                                                                   << " (errorMsg) " << errorMsg);
            raw_writer->write_error_frame(&pos, sizeof(LFRspPositionField), source_id, MSG_TYPE_LF_RSP_POS_BITHUMB, 1, requestId, atoi(statusCode.c_str()), errorMsg.c_str());
        }
    }
    send_writer->write_frame(data, sizeof(LFQryPositionField), source_id, MSG_TYPE_LF_QRY_POS_BITHUMB, 1, requestId);




/*
{
    "status"    : "0000",
    "data"      : {
        "created"       : 1388118018000,
        "account_id"    : "A000105A",
        "trade_fee"     : "0.000",
        "balance"       : "665.40127447"
    }
}
* */
    if(!d.HasParseError() && d.IsObject() && d.HasMember("data") && d["data"].IsObject())
    {
        auto& data = d["data"];
        if(data.HasMember("balance"))
        {
            strncpy(pos.InstrumentID, "btc", 31);
            pos.Position = std::round(std::stod(data["balance"].GetString()) * scale_offset);
            KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId << " (symbol) " << symbol << " (position) " << pos.Position);
            on_rsp_position(&pos, 1, requestId, errorId, errorMsg.c_str());
            send_writer->write_frame(&pos, sizeof(LFRspPositionField), source_id, MSG_TYPE_LF_RSP_POS_BITHUMB, 1, requestId);
        }
    }
}

void TDEngineBithumb::req_qry_account(const LFQryAccountField *data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_qry_account]");
}


void TDEngineBithumb::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitBithumb& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_insert]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Tid)" << data->InstrumentID
                                              << " (Volume)" << data->Volume
                                              << " (LimitPrice)" << data->LimitPrice
                                              << " (OrderRef)" << data->OrderRef);
    send_writer->write_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BITHUMB, 1/*ISLAST*/, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_insert]: not in WhiteList, ignore it  (rid)" << requestId <<
                                                                                      " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BITHUMB, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_insert] (exchange_ticker)" << ticker);

    Document d;
    send_order(unit, ticker.c_str(), GetSide(data->Direction).c_str(), data->Volume*1.0/scale_offset, data->LimitPrice*1.0/scale_offset,data->OrderPriceType == LF_CHAR_LimitPrice , d);
    //d.Parse("{\"orderId\":19319936159776,\"result\":true}");
    //not expected response
    if(d.HasParseError() || !d.IsObject())
    {
        errorId = 100;
        errorMsg = "send_order http response has parse error or is not json. please check the log";
        KF_LOG_ERROR(logger, "[req_order_insert] send_order error!  (rid)" << requestId << " (errorId)" <<
                                                                           errorId << " (errorMsg) " << errorMsg);
    } else  if(d.HasMember("status"))
    {
        std::string  status = d["status"].GetString();
        if(isStatusOK(status)) {
            /*
                {
                    "status"      : "0000",
                    "order_id"  : "1428646963419",
                    "data": [
                        {
                            "cont_id"    : "15313",
                            "units"        : "0.61460000",
                            "price"        : "284000",
                            "total"        : 174546,
                            "fee"           : "0.00061460"
                        },
                        {
                            "cont_id"   : "15314",
                            "units"        : "0.18540000",
                            "price"        : "289000",
                            "total"         : 53581,
                            "fee"          : "0.00018540"
                        }
                    ]
                }
             * */
            //if send successful and the exchange has received ok, then add to  pending query order list
            if(d.HadMember("data") && d.HasMember("order_id"))
            {
                std::string remoteOrderId = d["order_id"].GetString();
                //fix defect of use the old value
                localOrderRefRemoteOrderId[std::string(data->OrderRef)] = remoteOrderId;
                KF_LOG_INFO(logger, "[req_order_insert] after send  (rid)" << requestId << " (OrderRef) " <<
                                                                        data->OrderRef << " (remoteOrderId) "
                                                                        << remoteOrderId);
                //on_rtn_oder
                rapidjson::Value &dataRsp = d["data"];
                LFRtnOrderField rtn_order;
                memset(&rtn_order, 0, sizeof(LFRtnOrderField));

                rtn_order.OrderStatus = LF_CHAR_NotTouched;
                rtn_order.VolumeTraded = 0;
                strcpy(rtn_order.ExchangeID, "bithumb");
                strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
                strncpy(rtn_order.InstrumentID, ticker.c_str(), 31);
                rtn_order.Direction = data->Direction;
                rtn_order.TimeCondition = LF_CHAR_GTC;
                rtn_order.OrderPriceType = data->OrderPriceType;
                strncpy(rtn_order.OrderRef, data->OrderRef, 13);
                rtn_order.VolumeTotalOriginal = data->Volume;
                rtn_order.LimitPrice = data->LimitPrice;
                rtn_order.VolumeTotal = data->Volume;

                on_rtn_order(&rtn_order);
                raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                        source_id, MSG_TYPE_LF_RTN_ORDER_BITHUMB,
                                        1, (rtn_order.RequestID > 0) ? rtn_order.RequestID : -1);
                std::unique_lock<std::mutex> lck(g_unit_mutex);
                unit.mapOrders.insert(std::make_pair(remoteOrderId,rtn_order));
                lck.unlock();
                //char noneStatus = LF_CHAR_NotTouched;
                //addNewQueryOrdersAndTrades(unit, data->InstrumentID, data->OrderRef, noneStatus, 0, remoteOrderId);

                //success, only record raw data
                raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BITHUMB, 1,
                                            requestId, errorId, errorMsg.c_str());

            }

        }else {
            errorId = atoi(status.c_str());
            if(d.HasMember("message") && d["message"].IsString())
            {
                errorMsg = d["message"].GetString();
            }
            KF_LOG_ERROR(logger, "[req_order_insert] send_order error!  (rid)" << requestId << " (errorId)" <<
                                                                               errorId << " (errorMsg) " << errorMsg);
        }
    }
    if(errorId != 0)
    {
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BITHUMB, 1, requestId, errorId, errorMsg.c_str());
    }
}

//websocket的消息通常回来的比restful快，这时候因为消息里面有OrderId却找不到OrderRef，会先放入responsedOrderStatusNoOrderRef，
//当sendOrder返回OrderId信息之后,再处理这个信息
void TDEngineBithumb::handlerResponsedOrderStatus(AccountUnitBithumb& unit)
{
    std::lock_guard<std::mutex> guard_mutex(*mutex_response_order_status);

    std::vector<ResponsedOrderStatus>::iterator noOrderRefOrserStatusItr;
    for(noOrderRefOrserStatusItr = responsedOrderStatusNoOrderRef.begin(); noOrderRefOrserStatusItr != responsedOrderStatusNoOrderRef.end(); ) {

        //has no orderRed Order status, should link this OrderRef and handler it.
        ResponsedOrderStatus responsedOrderStatus = (*noOrderRefOrserStatusItr);

        std::vector<PendingOrderStatus>::iterator orderStatusIterator;
        for(orderStatusIterator = unit.pendingOrderStatus.begin(); orderStatusIterator != unit.pendingOrderStatus.end(); ++orderStatusIterator)
        {
//            KF_LOG_INFO(logger, "[handlerResponsedOrderStatus] (orderStatusIterator->remoteOrderId)"<< orderStatusIterator->remoteOrderId << " (orderId)" << responsedOrderStatus.orderId);
            if(orderStatusIterator->remoteOrderId == responsedOrderStatus.orderId)
            {
                break;
            }
        }

        if(orderStatusIterator == unit.pendingOrderStatus.end()) {
            KF_LOG_INFO(logger, "[handlerResponsedOrderStatus] not find this pendingOrderStatus of order id, ignore it.(orderId)"<< responsedOrderStatus.orderId);
            ++noOrderRefOrserStatusItr;
        } else {
            KF_LOG_INFO(logger, "[handlerResponsedOrderStatus] handlerResponseOrderStatus (responsedOrderStatus.orderId)"<< responsedOrderStatus.orderId);
            handlerResponseOrderStatus(unit, orderStatusIterator, responsedOrderStatus);

            //remove order when finish
            if(orderStatusIterator->OrderStatus == LF_CHAR_AllTraded  || orderStatusIterator->OrderStatus == LF_CHAR_Canceled
               || orderStatusIterator->OrderStatus == LF_CHAR_Error)
            {
                KF_LOG_INFO(logger, "[handlerResponsedOrderStatus] remove a pendingOrderStatus. (orderStatusIterator->remoteOrderId)" << orderStatusIterator->remoteOrderId);
                orderStatusIterator = unit.pendingOrderStatus.erase(orderStatusIterator);
            }

            KF_LOG_INFO(logger, "[handlerResponsedOrderStatus] responsedOrderStatusNoOrderRef erase(noOrderRefOrserStatusItr)"<< noOrderRefOrserStatusItr->orderId);
            noOrderRefOrserStatusItr = responsedOrderStatusNoOrderRef.erase(noOrderRefOrserStatusItr);
        }
    }
}

void TDEngineBithumb::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitBithumb& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_action]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Iid)" << data->InvestorID
                                              << " (OrderRef)" << data->OrderRef
                                              << " (KfOrderID)" << data->KfOrderID);

    send_writer->write_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BITHUMB, 1, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_action]: not in WhiteList , ignore it: (rid)" << requestId << " (errorId)" <<
                                                                                       errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BITHUMB, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_action] (exchange_ticker)" << ticker);

    std::map<std::string, int64_t>::iterator itr = localOrderRefRemoteOrderId.find(data->OrderRef);
    int64_t remoteOrderId = 0;
    if(itr == localOrderRefRemoteOrderId.end()) {
        errorId = 1;
        std::stringstream ss;
        ss << "[req_order_action] not found in localOrderRefRemoteOrderId map (orderRef) " << data->OrderRef;
        errorMsg = ss.str();
        KF_LOG_ERROR(logger, "[req_order_action] not found in localOrderRefRemoteOrderId map. "
                << " (rid)" << requestId << " (orderRef)" << data->OrderRef << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BITHUMB, 1, requestId, errorId, errorMsg.c_str());
        return;
    } else {
        remoteOrderId = itr->second;
        KF_LOG_DEBUG(logger, "[req_order_action] found in localOrderRefRemoteOrderId map (orderRef) "
                << data->OrderRef << " (remoteOrderId) " << remoteOrderId);
    }

    Document d;
    cancel_order(unit, ticker, std::to_string(remoteOrderId), d);

/*
 * {
  "code": 0,
  "data": {
    "remaining_volume": "0.25",
    "trades_count": 0,
    "created_at": "2018-09-19T02:31:45Z",
    "side": "buy",
    "id": 1,
    "volume": "0.25",
    "ord_type": "limit",
    "price": "10.0",
    "avg_price": "0.0",
    "state": "wait",
    "executed_volume": "0.0",
    "market": "vetusd"
    },
  "message": "Operation is successful"
}
 * */
    if(!d.HasParseError() && d.HasMember("code") && d["code"].GetInt() != 0) {
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
	raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BITHUMB, 1, requestId, errorId, errorMsg.c_str());

    } else {
        //addRemoteOrderIdOrderActionSentTime( data, requestId, remoteOrderId);

       // addRemoteOrderIdOrderActionSentTime( data, requestId, remoteOrderId);

        //TODO:   onRtn order/on rtn trade

    }
    }

//对于每个撤单指令发出后30秒（可配置）内，如果没有收到回报，就给策略报错（撤单被拒绝，pls retry)
void TDEngineBithumb::addRemoteOrderIdOrderActionSentTime(const LFOrderActionField* data, int requestId, int64_t remoteOrderId)
{
    std::lock_guard<std::mutex> guard_mutex_order_action(*mutex_orderaction_waiting_response);

    OrderActionSentTime newOrderActionSent;
    newOrderActionSent.requestId = requestId;
    newOrderActionSent.sentNameTime = getTimestamp();
    memcpy(&newOrderActionSent.data, data, sizeof(LFOrderActionField));
    remoteOrderIdOrderActionSentTime[remoteOrderId] = newOrderActionSent;
}

void TDEngineBithumb::GetAndHandleOrderTradeResponse()
{
    //every account
    for (size_t idx = 0; idx < account_units.size(); idx++)
    {
        AccountUnitBithumb& unit = account_units[idx];
        if (!unit.logged_in)
        {
            continue;
        }
        retrieveOrderStatus(unit);
    }//end every account
}


void TDEngineBithumb::retrieveOrderStatus(AccountUnitBithumb& unit)
{
    //KF_LOG_INFO(logger, "[retrieveOrderStatus] order_size:"<< unit.pendingOrderStatus.size());
    std::lock_guard<std::mutex> lck(g_unit_mutex);
    for(auto& it : unit.mapOrders)
    {
        std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(it.second.InstrumentID));
        if(ticker.length() == 0) {
            KF_LOG_INFO(logger, "[retrieveOrderStatus]: not in WhiteList , ignore it:" << orderStatusIterator->InstrumentID);
            continue;
        }
        KF_LOG_INFO(logger, "[retrieveOrderStatus] get_order " << "( account.api_key) " << unit.api_key
                                                               << "  (account.pendingOrderStatus.InstrumentID) " << it.second.InstrumentID
                                                               << "  (account.pendingOrderStatus.OrderRef) " << it.second.OrderRef
                                                               << "  (account.pendingOrderStatus.remoteOrderId) " << it.first;
                                                               << "  (account.pendingOrderStatus.OrderStatus) " << it.second.OrderStatus
                                                               << "  (exchange_ticker)" << ticker
        );

        Document d;
        query_trade(unit, ticker, it.first,it.second.Direction == LF_CHAR_Buy, d);

        /*
        {
            "status"    : "0000",
            "data"      : [
                {
                    "transaction_date“     : "1428024598967",
                    "type"                           : "ask",
                    "order_currency"        : "BTC",
                    "payment_currency"  : "KRW",
                    "units_traded"             : "0.0017",
                    "price"                           : "264000",
                    "fee"                              : "0.0000017",
                    "total"                           : "449"
                }
            ]
        }
        */
        if(d.HasParseError()) {
            //HasParseError, skip
            KF_LOG_ERROR(logger, "[retrieveOrderStatus] get_order response HasParseError " << " (symbol)" << it.second.InstrumentID
                                                                                           << " (orderRef)" << it.second.OrderRef
                                                                                           << " (remoteOrderId) " << it.first);
            continue;
        }
        if(d.HasMember("status") && isStatusOK(d["status"].GetString()))
        {
            rapidjson::Value &dataArray = d["data"];
            if(dataArray.IsArray() && dataArray.Size() > 0) {
                rapidjson::Value &data = dataArray[0];
                ResponsedOrderStatus responsedOrderStatus;
                responsedOrderStatus.ticker = ticker;
                responsedOrderStatus.averagePrice = std::round(std::stod(data["avg_price"].GetString()) * scale_offset);
                responsedOrderStatus.orderId = orderStatusIterator->remoteOrderId;
                //报单价格条件
                responsedOrderStatus.OrderPriceType = GetPriceType(data["ord_type"].GetString());
                //买卖方向
                responsedOrderStatus.Direction = GetDirection(data["side"].GetString());
                //报单状态
                responsedOrderStatus.OrderStatus = GetOrderStatus(data["state"].GetString());
                responsedOrderStatus.price = std::round(std::stod(data["price"].GetString()) * scale_offset);
                responsedOrderStatus.volume = std::round(std::stod(data["volume"].GetString()) * scale_offset);
                //今成交数量
                responsedOrderStatus.VolumeTraded = std::round(
                        std::stod(data["executed_volume"].GetString()) * scale_offset);
                responsedOrderStatus.openVolume = std::round(
                        std::stod(data["remaining_volume"].GetString()) * scale_offset);

                handlerResponseOrderStatus(unit, orderStatusIterator, responsedOrderStatus);

                //OrderAction发出以后，有状态回来，就清空这次OrderAction的发送状态，不必制造超时提醒信息
                remoteOrderIdOrderActionSentTime.erase(orderStatusIterator->remoteOrderId);
            }
        } else {
            std::string errorMsg = "";

            int errorId = d["code"].GetInt();
            if(d.HasMember("message") && d["message"].IsString())
            {
                errorMsg = d["message"].GetString();
            }

            KF_LOG_ERROR(logger, "[retrieveOrderStatus] get_order fail." << " (symbol)" << orderStatusIterator->InstrumentID
                                                                         << " (orderRef)" << orderStatusIterator->OrderRef
                                                                         << " (errorId)" << errorId
                                                                         << " (errorMsg)" << errorMsg);
        }

        //remove order when finish
        if(orderStatusIterator->OrderStatus == LF_CHAR_AllTraded  || orderStatusIterator->OrderStatus == LF_CHAR_Canceled
           || orderStatusIterator->OrderStatus == LF_CHAR_Error)
        {
            KF_LOG_INFO(logger, "[retrieveOrderStatus] remove a pendingOrderStatus.");
            orderStatusIterator = unit.pendingOrderStatus.erase(orderStatusIterator);
        } else {
            ++orderStatusIterator;
        }
        //KF_LOG_INFO(logger, "[retrieveOrderStatus] move to next pendingOrderStatus.");
    }
}

void TDEngineBithumb::set_reader_thread()
{
    ITDEngine::set_reader_thread();

    KF_LOG_INFO(logger, "[set_reader_thread] rest_thread start on TDEngineBithumb::loop");
    rest_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineBithumb::loop, this)));


    KF_LOG_INFO(logger, "[set_reader_thread] orderaction_timeout_thread start on TDEngineBithumb::loopOrderActionNoResponseTimeOut");
    orderaction_timeout_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineBithumb::loopOrderActionNoResponseTimeOut, this)));
}


void TDEngineBithumb::loop()
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
        GetAndHandleOrderTradeResponse();
    }
}


void TDEngineBithumb::loopOrderActionNoResponseTimeOut()
{
    KF_LOG_INFO(logger, "[loopOrderActionNoResponseTimeOut] (isRunning) " << isRunning);
    while(isRunning)
    {
        orderActionNoResponseTimeOut();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void TDEngineBithumb::orderActionNoResponseTimeOut()
{
//    KF_LOG_DEBUG(logger, "[orderActionNoResponseTimeOut]");
    int errorId = 100;
    std::string errorMsg = "OrderAction has none response for a long time(" + std::to_string(orderaction_max_waiting_seconds) + " s), please send OrderAction again";

    std::lock_guard<std::mutex> guard_mutex_order_action(*mutex_orderaction_waiting_response);

    int64_t currentNano = getTimestamp();
    int64_t timeBeforeNano = currentNano - orderaction_max_waiting_seconds * 1000;
//    KF_LOG_DEBUG(logger, "[orderActionNoResponseTimeOut] (currentNano)" << currentNano << " (timeBeforeNano)" << timeBeforeNano);
    std::map<int64_t, OrderActionSentTime>::iterator itr;
    for(itr = remoteOrderIdOrderActionSentTime.begin(); itr != remoteOrderIdOrderActionSentTime.end();)
    {
        if(itr->second.sentNameTime < timeBeforeNano)
        {
            KF_LOG_DEBUG(logger, "[orderActionNoResponseTimeOut] (remoteOrderIdOrderActionSentTime.erase remoteOrderId)" << itr->first );
            on_rsp_order_action(&itr->second.data, itr->second.requestId, errorId, errorMsg.c_str());
            itr = remoteOrderIdOrderActionSentTime.erase(itr);
        } else {
            ++itr;
        }
    }
//    KF_LOG_DEBUG(logger, "[orderActionNoResponseTimeOut] (remoteOrderIdOrderActionSentTime.size)" << remoteOrderIdOrderActionSentTime.size());
}
void TDEngineBithumb::getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& json)
{
    KF_LOG_INFO(logger, "[getResponse] (http_status_code)" << http_status_code << " (text) " << responseText << " (errorMsg)" << errorMsg);
    if(http_status_code >= HTTP_RESPONSE_OK && http_status_code <= 299)
    {
        json.Parse(responseText.c_str());
    } else if(http_status_code == 0)
    {
        json.SetObject();
        Document::AllocatorType& allocator = json.GetAllocator();
        json.AddMember("status", rapidjson::StringRef("1"), allocator);
        json.AddMember("message", rapidjson::StringRef(errorMsg.c_str()), allocator);
    } else
    {
        Document d;
        d.Parse(responseText.c_str());
        json.SetObject();
        Document::AllocatorType& allocator = json.GetAllocator();
        std:;string status_code = std::to_string(http_status_code);
        json.AddMember("status", rapidjson::StringRef(status_code.c_str()), allocator);
        json.AddMember("message", rapidjson::StringRef(errorMsg.c_str()), allocator);
    }
}

std::string TDEngineBithumb::construct_request_body(const AccountUnitBithumb& unit,const std::string& endPoint,const  std::string& data,const std::string& timeStamp)
{
    //std::string strPost = "endpoint="+endPoint+"&"+data;
    //std::string strEncode = utils::crypto::url_encode(strPost.c_str());
    size_t nLen = data.length()+endPoint.length()+timeStamp.length()+3;
    char* strData = new char[nLen]{0};
    sprintf(strData,"%s%c%s%c%s",endPoint.c_str(),(char)0,data.c_str(),(char)0,timeStamp.c_str());
    std::string strSHASign = hmac_sha512(unit.secret_key,unit.secret_key.length(),strData,nLen);
    std::string strSign = base64_encode((const unsigned char*)strSHASign.c_str(),(unsigned long)strSHASign.length());
    delete[] strData;
    return strSign;
}


void TDEngineBithumb::get_account(AccountUnitBithumb& unit, Document& json)
{
    KF_LOG_INFO(logger, "[get_account]");

    std::string requestPath = "/Info/Account";
    std::string params = "apiKey="+unit.api_key+"&secretKey"+unit.secret_key;
    const auto response = Post(requestPath,params,unit); 
    return getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineBithumb::send_order(AccountUnitBithumb& unit, const char *code,
                                 const char *side, double size, double price,bool isLimit, Document& json)
{
    KF_LOG_INFO(logger, "[send_order]");

    int retry_times = 0;
    cpr::Response response;
    bool should_retry = false;
    do {
        should_retry = false;

        auto coinPair = SplitCoinPair(code);
        std::string requestPath = "/trade/place";
        std::string params="apiKey="+unit.api_key+"&secretKey="+unit.secret_key+"&order_currency="+coinPair.first+"&payment_currency="+coinPair.second+"&units="+size+"&price="+price+
                        "&type="+std::string(side);
        response = Post(requestPath,,unit);

        KF_LOG_INFO(logger, "[send_order] (url) " << requestPath << " (response.status_code) " << response.status_code <<
                                                  " (response.error.message) " << response.error.message <<
                                                  " (response.text) " << response.text.c_str() << " (retry_times)" << retry_times);

        //json.Clear();
        getResponse(response.status_code, response.text, response.error.message, json);
        //has error and find the 'error setting certificate verify locations' error, should retry
        if(shouldRetry(json)) {
            should_retry = true;
            retry_times++;
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_milliseconds));
        }
    } while(should_retry && retry_times < max_rest_retry_times);



    KF_LOG_INFO(logger, "[send_order] out_retry (response.status_code) " << response.status_code <<
                                                                         " (response.error.message) " << response.error.message <<
                                                                         " (response.text) " << response.text.c_str() );

    //getResponse(response.status_code, response.text, response.error.message, json);
}

bool TDEngineBithumb::shouldRetry(Document& doc)
{
    bool ret = false;
    if(!doc.IsObject() || !doc.HasMember("status") || !isStatusOK(doc["status"].GetString()))
    {
        ret = true;
    }
//    if( 502 == http_status_code
//        || (errorMsg.size() > 0 && errorMsg.find("error setting certificate verify locations") >= 0)
//        || (401 == http_status_code && errorMsg.size() > 0 && errorMsg.find("Auth error") >= 0) )
//    {
//        return true;
//    }
    return ret;
}

void TDEngineBithumb::cancel_all_orders(AccountUnitBithumb& unit, std::string code, Document& json)
{
    KF_LOG_INFO(logger, "[cancel_all_orders]");

    std::string requestPath = "/orders/clear";
    //std::string queryString= "?user_jwt=RkTgU1lne1aWSBnC171j0eJe__fILSclRpUJ7SWDDulWd4QvLa0-WVRTeyloJOsjyUtduuF0K0SdkYqXR-ibuULqXEDGCGSHSed8WaNtHpvf-AyCI-JKucLH7bgQxT1yPtrJC6W31W5dQ2Spp3IEpXFS49pMD3FRFeHF4HAImo9VlPUM_bP-1kZt0l9RbzWjxVtaYbx3L8msXXyr_wqacNnIV6X9m8eie_DqZHYzGrN_25PfAFgKmghfpL-jmu53kgSyTw5v-rfZRP9VMAuryRIMvOf9LBuMaxcuFn7PjVJx8F7fcEPBCd0roMTLKhHjFidi6QxZNUO1WKSkoSbRxA";//construct_request_body(unit, "{}");

    auto response = Post(requestPath,"{}",unit);

    getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineBithumb::cancel_order(AccountUnitBithumb& unit, std::string code, std::string orderId,bool isBuy, Document& json)
{
    KF_LOG_INFO(logger, "[cancel_order]");

    int retry_times = 0;
    cpr::Response response;
    bool should_retry = false;
    do {
        should_retry = false;

        std::string requestPath = "/trade/cancel";
        std::string params= "apiKey="+unit.api_key+"&secretKey="+unit.secret_key+"&order_id="+orderId+"&type="+(isBuy?std::string("bid"):std::string("ask"))+"&currency="+coinPair.first;
        response = Post(requestPath,params,unit);

        getResponse(response.status_code, response.text, response.error.message, json);
        //has error and find the 'error setting certificate verify locations' error, should retry
        if(shouldRetry(json)) {
            should_retry = true;
            retry_times++;
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_milliseconds));
        }
    } while(should_retry && retry_times < max_rest_retry_times);


    KF_LOG_INFO(logger, "[cancel_order] out_retry " << retry_times << " (response.status_code) " << response.status_code <<
                                                                           " (response.error.message) " << response.error.message <<
                                                                           " (response.text) " << response.text.c_str() );
}

void TDEngineBithumb::query_order(AccountUnitBithumb& unit, std::string code, std::string orderId, Document& json)
{
    KF_LOG_INFO(logger, "[query_order]");
    std::string requestPath = "/info/orders";
    std::string params= "apiKey="+unit.api_key+"&secretKey="+unit.secret_key+"&order_id="+orderId;
    auto response = Post(requestPath,params,unit);

    getResponse(response.status_code, response.text, response.error.message, json);
}
void TDEngineBithumb::query_trade(AccountUnitBithumb& unit, std::string code, std::string orderId,bool isBuy, Document& json)
{
    KF_LOG_INFO(logger, "[query_trade]");
    std::string coinPair = SplitCoinPair(code);
    std::string requestPath = "/info/order_detail";
    std::string params= "apiKey="+unit.api_key+"&secretKey="+unit.secret_key+"&order_id="+orderId+"&type="+(isBuy?std::string("bid"):std::string("ask"))+"&currency="+coinPair.first;
    auto response = Post(requestPath,params,unit);

    getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineBithumb::onRtnTrade(AccountUnitBithumb& unit,LFRtnOrderField& order,Value& json)
{
    
}
void TDEngineBithumb::onRtnOrder(AccountUnitBithumb& unit,LFRtnOrderField& order,Value& json)
{

}
void TDEngineBithumb::handlerResponseOrderStatus(AccountUnitBithumb& unit, std::vector<PendingOrderStatus>::iterator orderStatusIterator, ResponsedOrderStatus& responsedOrderStatus)
{
    if( (responsedOrderStatus.OrderStatus == 'b' && '1' == orderStatusIterator-> OrderStatus || responsedOrderStatus.OrderStatus == orderStatusIterator-> OrderStatus) && responsedOrderStatus.VolumeTraded == orderStatusIterator->VolumeTraded)
    {//no change
        return;
    }
    int64_t newAveragePrice = responsedOrderStatus.averagePrice;
    //cancel 需要特殊处理
    if(LF_CHAR_Canceled == responsedOrderStatus.OrderStatus)  {
        /*
         * 因为restful查询有间隔时间，订单可能会先经历过部分成交，然后才达到的cancnel，所以得到cancel不能只认为是cancel，还需要判断有没有部分成交过。
        这时候需要补状态，补一个on rtn order，一个on rtn trade。
        这种情况仅cancel才有, 部分成交和全成交没有此问题。
        当然，也要考虑，如果上一次部分成交已经被抓取到的并返回过 on rtn order/on rtn trade，那么就不需要补了
         //2018-09-12.  不清楚websocket会不会有这个问题，先做同样的处理
        */

        //虽然是撤单状态，但是已经成交的数量和上一次记录的数量不一样，期间一定发生了部分成交. 要补发 LF_CHAR_PartTradedQueueing
        if(responsedOrderStatus.VolumeTraded != orderStatusIterator->VolumeTraded) {
            //if status is LF_CHAR_Canceled but traded valume changes, emit onRtnOrder/onRtnTrade of LF_CHAR_PartTradedQueueing
            LFRtnOrderField rtn_order;
            memset(&rtn_order, 0, sizeof(LFRtnOrderField));
            rtn_order.OrderStatus = LF_CHAR_PartTradedNotQueueing;
            rtn_order.VolumeTraded = responsedOrderStatus.VolumeTraded;
            //first send onRtnOrder about the status change or VolumeTraded change
            strcpy(rtn_order.ExchangeID, "bithumb");
            strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
            strncpy(rtn_order.InstrumentID, orderStatusIterator->InstrumentID, 31);
            rtn_order.Direction = responsedOrderStatus.Direction;
            //No this setting on BITHUMB
            rtn_order.TimeCondition = LF_CHAR_GTC;
            rtn_order.OrderPriceType = responsedOrderStatus.OrderPriceType;
            strncpy(rtn_order.OrderRef, orderStatusIterator->OrderRef, 13);
            rtn_order.VolumeTotalOriginal = responsedOrderStatus.volume;
            rtn_order.LimitPrice = responsedOrderStatus.price;
            //剩余数量
            rtn_order.VolumeTotal = responsedOrderStatus.openVolume;

            //经过2018-08-20讨论，这个on rtn order 可以不必发送了, 只记录raw有这么回事就行了。只补发一个 on rtn trade 就行了。
            //on_rtn_order(&rtn_order);
            raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                    source_id, MSG_TYPE_LF_RTN_ORDER_BITHUMB,
                                    1, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);


            //send OnRtnTrade
            LFRtnTradeField rtn_trade;
            memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
            strcpy(rtn_trade.ExchangeID, "bithumb");
            strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
            strncpy(rtn_trade.InstrumentID, orderStatusIterator->InstrumentID, 31);
            strncpy(rtn_trade.OrderRef, orderStatusIterator->OrderRef, 13);
            rtn_trade.Direction = rtn_order.Direction;
            uint64_t oldAmount = orderStatusIterator->VolumeTraded * orderStatusIterator->averagePrice;
            uint64_t newAmount = rtn_order.VolumeTraded * newAveragePrice;

            //calculate the volumn and price (it is average too)
            rtn_trade.Volume = rtn_order.VolumeTraded - orderStatusIterator->VolumeTraded;
            rtn_trade.Price = (newAmount - oldAmount)/(rtn_trade.Volume);

            on_rtn_trade(&rtn_trade);
            raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
                                    source_id, MSG_TYPE_LF_RTN_TRADE_BITHUMB, 1, -1);

        }

        //emit the LF_CHAR_Canceled status
        LFRtnOrderField rtn_order;
        memset(&rtn_order, 0, sizeof(LFRtnOrderField));
        rtn_order.OrderStatus = LF_CHAR_Canceled;
        rtn_order.VolumeTraded = responsedOrderStatus.VolumeTraded;

        //first send onRtnOrder about the status change or VolumeTraded change
        strcpy(rtn_order.ExchangeID, "bithumb");
        strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
        strncpy(rtn_order.InstrumentID, orderStatusIterator->InstrumentID, 31);
        rtn_order.Direction = responsedOrderStatus.Direction;
        //BITHUMB has no this setting
        rtn_order.TimeCondition = LF_CHAR_GTC;
        rtn_order.OrderPriceType = responsedOrderStatus.OrderPriceType;
        strncpy(rtn_order.OrderRef, orderStatusIterator->OrderRef, 13);
        rtn_order.VolumeTotalOriginal = responsedOrderStatus.volume;
        rtn_order.LimitPrice = responsedOrderStatus.price;
        //剩余数量
        rtn_order.VolumeTotal = responsedOrderStatus.openVolume;

        on_rtn_order(&rtn_order);
        raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                source_id, MSG_TYPE_LF_RTN_ORDER_BITHUMB,
                                1, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);


        //third, update last status for next query_order
        orderStatusIterator->OrderStatus = rtn_order.OrderStatus;
        orderStatusIterator->VolumeTraded = rtn_order.VolumeTraded;
        orderStatusIterator->averagePrice = newAveragePrice;

    }
    else
    {
        //if status changed or LF_CHAR_PartTradedQueueing but traded valume changes, emit onRtnOrder
        LFRtnOrderField rtn_order;
        memset(&rtn_order, 0, sizeof(LFRtnOrderField));

        KF_LOG_INFO(logger, "[handlerResponseOrderStatus] VolumeTraded Change  LastOrderPsp:" << orderStatusIterator->VolumeTraded << ", NewOrderRsp: " << responsedOrderStatus.VolumeTraded  <<
                                                        " NewOrderRsp.Status " << responsedOrderStatus.OrderStatus);
        if(responsedOrderStatus.OrderStatus == LF_CHAR_NotTouched && responsedOrderStatus.VolumeTraded != orderStatusIterator->VolumeTraded) {
            rtn_order.OrderStatus = LF_CHAR_PartTradedQueueing;
        } else{
            rtn_order.OrderStatus = responsedOrderStatus.OrderStatus;
        }
        rtn_order.VolumeTraded = responsedOrderStatus.VolumeTraded;

        //first send onRtnOrder about the status change or VolumeTraded change
        strcpy(rtn_order.ExchangeID, "bithumb");
        strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
        strncpy(rtn_order.InstrumentID, orderStatusIterator->InstrumentID, 31);
        rtn_order.Direction = responsedOrderStatus.Direction;
        //No this setting on BITHUMB
        rtn_order.TimeCondition = LF_CHAR_GTC;
        rtn_order.OrderPriceType = responsedOrderStatus.OrderPriceType;
        strncpy(rtn_order.OrderRef, orderStatusIterator->OrderRef, 13);
        rtn_order.VolumeTotalOriginal = responsedOrderStatus.volume;
        rtn_order.LimitPrice = responsedOrderStatus.price;
        rtn_order.VolumeTotal = responsedOrderStatus.openVolume;

        on_rtn_order(&rtn_order);
        raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                source_id, MSG_TYPE_LF_RTN_ORDER_BITHUMB,
                                1, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);

        int64_t newAveragePrice = responsedOrderStatus.averagePrice;
        //second, if the status is PartTraded/AllTraded, send OnRtnTrade
        if(rtn_order.OrderStatus == LF_CHAR_AllTraded ||
           (LF_CHAR_PartTradedQueueing == rtn_order.OrderStatus
            && rtn_order.VolumeTraded != orderStatusIterator->VolumeTraded))
        {
            LFRtnTradeField rtn_trade;
            memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
            strcpy(rtn_trade.ExchangeID, "bithumb");
            strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
            strncpy(rtn_trade.InstrumentID, orderStatusIterator->InstrumentID, 31);
            strncpy(rtn_trade.OrderRef, orderStatusIterator->OrderRef, 13);
            rtn_trade.Direction = rtn_order.Direction;
            uint64_t oldAmount = orderStatusIterator->VolumeTraded * orderStatusIterator->averagePrice;
            uint64_t newAmount = rtn_order.VolumeTraded * newAveragePrice;

            //calculate the volumn and price (it is average too)
            rtn_trade.Volume = rtn_order.VolumeTraded - orderStatusIterator->VolumeTraded;
            rtn_trade.Price = (newAmount - oldAmount)/(rtn_trade.Volume);

            on_rtn_trade(&rtn_trade);
            raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
                                    source_id, MSG_TYPE_LF_RTN_TRADE_BITHUMB, 1, -1);
        }
        //third, update last status for next query_order
        orderStatusIterator->OrderStatus = rtn_order.OrderStatus;
        orderStatusIterator->VolumeTraded = rtn_order.VolumeTraded;
        orderStatusIterator->averagePrice = newAveragePrice;
    }
}

std::string TDEngineBithumb::parseJsonToString(Document &d)
{
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);

    return buffer.GetString();
}


inline int64_t TDEngineBithumb::getTimestamp()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp;
}

#define GBK2UTF8(msg) kungfu::yijinjing::gbk2utf8(string(msg))

BOOST_PYTHON_MODULE(libbithumbtd)
{
    using namespace boost::python;
    class_<TDEngineBithumb, boost::shared_ptr<TDEngineBithumb> >("Engine")
            .def(init<>())
            .def("init", &TDEngineBithumb::initialize)
            .def("start", &TDEngineBithumb::start)
            .def("stop", &TDEngineBithumb::stop)
            .def("logout", &TDEngineBithumb::logout)
            .def("wait_for_stop", &TDEngineBithumb::wait_for_stop);
}
