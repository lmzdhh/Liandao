#include "TDEngineMock.h"
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

TDEngineMock::TDEngineMock(): ITDEngine(SOURCE_MOCK)
{
    logger = yijinjing::KfLog::getLogger("TradeEngine.Mock");
    KF_LOG_INFO(logger, "[TDEngineMock]");

    mutex_order_and_trade = new std::mutex();
}

TDEngineMock::~TDEngineMock()
{
    if(mutex_order_and_trade != nullptr) delete mutex_order_and_trade;
}

void TDEngineMock::init()
{
    ITDEngine::init();
    JournalPair tdRawPair = getTdRawJournalPair(source_id);
    raw_writer = yijinjing::JournalSafeWriter::create(tdRawPair.first, tdRawPair.second, "RAW_" + name());
    KF_LOG_INFO(logger, "[init]");
}

void TDEngineMock::pre_load(const json& j_config)
{
    KF_LOG_INFO(logger, "[pre_load]");
}

void TDEngineMock::resize_accounts(int account_num)
{
    account_units.resize(account_num);
    KF_LOG_INFO(logger, "[resize_accounts]");
}

TradeAccount TDEngineMock::load_account(int idx, const json& j_config)
{
    KF_LOG_INFO(logger, "[load_account]");
    // internal load
    string api_key = j_config["APIKey"].get<string>();
    string secret_key = j_config["SecretKey"].get<string>();
    string passphrase = j_config["passphrase"].get<string>();
    string baseUrl = j_config["baseUrl"].get<string>();
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();

    if(j_config.find("sync_time_interval") != j_config.end()) {
        SYNC_TIME_DEFAULT_INTERVAL = j_config["sync_time_interval"].get<int>();
    }
    KF_LOG_INFO(logger, "[load_account] (SYNC_TIME_DEFAULT_INTERVAL)" << SYNC_TIME_DEFAULT_INTERVAL);

    AccountUnitCoinmex& unit = account_units[idx];
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
    if(unit.coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().size() == 0) {
        KF_LOG_ERROR(logger, "TDEngineMock::load_account: subscribeCoinmexBaseQuote is empty. please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"btcusdt\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"etceth\"");
        KF_LOG_ERROR(logger, "},");
    }

    //cancel all openning orders on TD startup

    // set up
    TradeAccount account = {};
    //partly copy this fields
    strncpy(account.UserID, api_key.c_str(), 16);
    strncpy(account.Password, secret_key.c_str(), 21);
    return account;
}


void TDEngineMock::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[connect]");
    for (int idx = 0; idx < account_units.size(); idx ++)
    {
        AccountUnitCoinmex& unit = account_units[idx];
        KF_LOG_INFO(logger, "[connect] (api_key)" << unit.api_key);
        if (!unit.logged_in)
        {
            unit.logged_in = true;
        }
    }

    KF_LOG_INFO(logger, "[connect] rest_thread start on TDEngineMock::loop");
    rest_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineMock::loop, this)));
}


void TDEngineMock::login(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[login]");
    connect(timeout_nsec);
}

void TDEngineMock::logout()
{
    KF_LOG_INFO(logger, "[logout]");
}

void TDEngineMock::release_api()
{
    KF_LOG_INFO(logger, "[release_api]");
}

bool TDEngineMock::is_logged_in() const
{
    KF_LOG_INFO(logger, "[is_logged_in]");
    for (auto& unit: account_units)
    {
        if (!unit.logged_in)
            return false;
    }
    return true;
}

bool TDEngineMock::is_connected() const
{
    KF_LOG_INFO(logger, "[is_connected]");
    return is_logged_in();
}



std::string TDEngineMock::GetSide(const LfDirectionType& input) {
    if (LF_CHAR_Buy == input) {
        return "buy";
    } else if (LF_CHAR_Sell == input) {
        return "sell";
    } else {
        return "";
    }
}

LfDirectionType TDEngineMock::GetDirection(std::string input) {
    if ("buy" == input) {
        return LF_CHAR_Buy;
    } else if ("sell" == input) {
        return LF_CHAR_Sell;
    } else {
        return LF_CHAR_Buy;
    }
}

std::string TDEngineMock::GetType(const LfOrderPriceTypeType& input) {
    if (LF_CHAR_LimitPrice == input) {
        return "limit";
    } else if (LF_CHAR_AnyPrice == input) {
        return "market";
    } else {
        return "";
    }
}

LfOrderPriceTypeType TDEngineMock::GetPriceType(std::string input) {
    if ("limit" == input) {
        return LF_CHAR_LimitPrice;
    } else if ("market" == input) {
        return LF_CHAR_AnyPrice;
    } else {
        return '0';
    }
}
//订单状态，﻿open（未成交）、filled（已完成）、canceled（已撤销）、cancel（撤销中）、partially-filled（部分成交）
LfOrderStatusType TDEngineMock::GetOrderStatus(std::string input) {
    if ("open" == input) {
        return LF_CHAR_NotTouched;
    } else if ("partially-filled" == input) {
        return LF_CHAR_PartTradedQueueing;
    } else if ("filled" == input) {
        return LF_CHAR_AllTraded;
    } else if ("canceled" == input) {
        return LF_CHAR_Canceled;
    } else if ("cancel" == input) {
        return LF_CHAR_NotTouched;
    } else {
        return LF_CHAR_NotTouched;
    }
}

/**
 * req functions
 */
void TDEngineMock::req_investor_position(const LFQryPositionField* data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId);

    AccountUnitCoinmex& unit = account_units[account_index];
    KF_LOG_INFO(logger, "[req_investor_position] (api_key)" << unit.api_key << " (InstrumentID) " << data->InstrumentID);

    int errorId = 0;
    std::string errorMsg = "";

    send_writer->write_frame(data, sizeof(LFQryPositionField), source_id, MSG_TYPE_LF_QRY_POS_MOCK, 1, requestId);

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

    KF_LOG_INFO(logger, "[req_investor_position] (!findSymbolInResult) (requestId)" << requestId);
    on_rsp_position(&pos, 1, requestId, errorId, errorMsg.c_str());

}

void TDEngineMock::req_qry_account(const LFQryAccountField *data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_qry_account]");
}



void TDEngineMock::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitCoinmex& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_insert999]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Tid)" << data->InstrumentID
                                              << " (Volume)" << data->Volume
                                              << " (LimitPrice)" << data->LimitPrice
                                              << " (OrderRef)" << data->OrderRef);
    send_writer->write_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_MOCK, 1/*ISLAST*/, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_insert]: not in WhiteList, ignore it  (rid)" << requestId <<
                                                                                      " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_COINMEX, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_insert] (exchange_ticker)" << ticker);

    if(errorId != 0)
    {
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
    }
    raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_MOCK, 1, requestId, errorId, errorMsg.c_str());


    LFRtnOrderField rtn_order;
    memset(&rtn_order, 0, sizeof(LFRtnOrderField));
    rtn_order.OrderStatus = LF_CHAR_OrderInserted;
    rtn_order.VolumeTraded = 0;

    //first send onRtnOrder about the status change or VolumeTraded change
    strcpy(rtn_order.ExchangeID, "mock");
    strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
    strncpy(rtn_order.InstrumentID, data->InstrumentID, 31);
    rtn_order.Direction = LF_CHAR_Buy;
    //No this setting on coinmex
    rtn_order.TimeCondition = LF_CHAR_GTC;
    rtn_order.OrderPriceType = LF_CHAR_AnyPrice;
    strncpy(rtn_order.OrderRef, data->OrderRef, 13);
    rtn_order.VolumeTotalOriginal = data->Volume;
    rtn_order.LimitPrice = std::round(data->LimitPrice);
    rtn_order.VolumeTotal = rtn_order.VolumeTotalOriginal - rtn_order.VolumeTraded;

    on_rtn_order(&rtn_order);
    raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                            source_id, MSG_TYPE_LF_RTN_ORDER_COINMEX,
                            1, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);

}


void TDEngineMock::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitCoinmex& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_action999]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Iid)" << data->InvestorID
                                              << " (OrderRef)" << data->OrderRef
                                              << " (KfOrderID)" << data->KfOrderID);

    send_writer->write_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_MOCK, 1, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_action]: not in WhiteList , ignore it: (rid)" << requestId << " (errorId)" <<
                                                                                       errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_COINMEX, 1, requestId, errorId, errorMsg.c_str());
        return;
    }

    KF_LOG_DEBUG(logger, "[req_order_action] (exchange_ticker)" << ticker);


    if(errorId != 0)
    {
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
    }
    raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_MOCK, 1, requestId, errorId, errorMsg.c_str());
}


void TDEngineMock::loop()
{
    while(isRunning)
    {
        using namespace std::chrono;
        auto current_ms = duration_cast< milliseconds>(system_clock::now().time_since_epoch()).count();
        if(last_rest_get_ts != 0 && (current_ms - last_rest_get_ts) < rest_get_interval_ms)
        {
            continue;
        }

        last_rest_get_ts = current_ms;
    }
}


void TDEngineMock::printResponse(const Document& d)
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

/*
 * https://github.com/coinmex/coinmex-official-api-docs/blob/master/README_ZH_CN.md
 *
 *成功
HTTP状态码200表示成功响应，并可能包含内容。如果响应含有内容，则将显示在相应的返回内容里面。
常见错误码
    400 Bad Request – Invalid request forma 请求格式无效
    401 Unauthorized – Invalid API Key 无效的API Key
    403 Forbidden – You do not have access to the requested resource 请求无权限
    404 Not Found 没有找到请求
    429 Too Many Requests 请求太频繁被系统限流
    500 Internal Server Error – We had a problem with our server 服务器内部阻碍
 * */
//当出错时，返回http error code和出错信息message
//当不出错时，返回结果信息
void TDEngineMock::getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& json)
{
    if(http_status_code == HTTP_RESPONSE_OK)
    {
        //KF_LOG_INFO(logger, "[getResponse] (http_status_code == 200) (responseText)" << responseText << " (errorMsg) " << errorMsg);
        json.Parse(responseText.c_str());
        //KF_LOG_INFO(logger, "[getResponse] (http_status_code == 200) (HasParseError)" << json.HasParseError());
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
            if( d.HasMember("msg")) {
                //KF_LOG_INFO(logger, "[getResponse] (err) (errorMsg)" << d["msg"].GetString());
                std::string message = d["msg"].GetString();
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


#define GBK2UTF8(msg) kungfu::yijinjing::gbk2utf8(string(msg))

BOOST_PYTHON_MODULE(libmocktd)
{
    using namespace boost::python;
    class_<TDEngineMock, boost::shared_ptr<TDEngineMock> >("Engine")
            .def(init<>())
            .def("init", &TDEngineMock::initialize)
            .def("start", &TDEngineMock::start)
            .def("stop", &TDEngineMock::stop)
            .def("logout", &TDEngineMock::logout)
            .def("wait_for_stop", &TDEngineMock::wait_for_stop);
}
