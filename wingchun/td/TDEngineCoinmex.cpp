#include "TDEngineCoinmex.h"
#include "TypeConvert.hpp"
#include "Timer.h"
#include "longfist/LFUtils.h"
#include "longfist/LFDataStruct.h"


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

TDEngineCoinmex::TDEngineCoinmex(): ITDEngine(SOURCE_COINMEX)
{
    logger = yijinjing::KfLog::getLogger("TradeEngine.Coinmex");
    KF_LOG_INFO(logger, "[TDEngineCoinmex]");
}

void TDEngineCoinmex::init()
{
    ITDEngine::init();
    JournalPair tdRawPair = getTdRawJournalPair(source_id);
    raw_writer = yijinjing::JournalWriter::create(tdRawPair.first, tdRawPair.second, "RAW_" + name());
    KF_LOG_INFO(logger, "[init]");
}

void TDEngineCoinmex::pre_load(const json& j_config)
{
    KF_LOG_INFO(logger, "[pre_load]");
}

void TDEngineCoinmex::resize_accounts(int account_num)
{
    account_units.resize(account_num);
    KF_LOG_INFO(logger, "[resize_accounts]");
}

TradeAccount TDEngineCoinmex::load_account(int idx, const json& j_config)
{
    KF_LOG_INFO(logger, "[load_account]");
    // internal load
    string api_key = j_config["APIKey"].get<string>();
    string secret_key = j_config["SecretKey"].get<string>();
    string passphrase = j_config["passphrase"].get<string>();
    string baseUrl = j_config["baseUrl"].get<string>();
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();

    AccountUnitCoinmex& unit = account_units[idx];
    unit.api_key = api_key;
    unit.secret_key = secret_key;
    unit.passphrase = passphrase;
    unit.baseUrl = baseUrl;

    KF_LOG_INFO(logger, "[load_account] (api_key)" << api_key << " (baseUrl)" << baseUrl);

    Document d = get_exchange_time(unit);

    //d = get_account(unit);

    d = send_order(unit);
    KF_LOG_INFO(logger, "[send_order] ( send_order)");
    //d = cancel_all_orders(unit, "LTC_BTC");

    //cancel_order(unit, "LTC_BTC", 123456L);

    //d = query_orders(unit, "LTC_BTC", "open");

    //d = query_order(unit, "LTC_BTC", 123456L);

    // set up
    TradeAccount account = {};
    //partly copy this fields
    strncpy(account.UserID, api_key.c_str(), 16);
    strncpy(account.Password, secret_key.c_str(), 21);
    return account;
}

void TDEngineCoinmex::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[connect]" << timeout_nsec);
    KF_LOG_INFO(logger, "[connect] rest_thread start on TDEngineCoinmex::loop");
    rest_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineCoinmex::loop, this)));
}

void TDEngineCoinmex::login(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[login]");
    connect(timeout_nsec);
}

void TDEngineCoinmex::logout()
{
    KF_LOG_INFO(logger, "[logout]");
}

void TDEngineCoinmex::release_api()
{
    KF_LOG_INFO(logger, "[release_api]");
}

bool TDEngineCoinmex::is_logged_in() const
{
    KF_LOG_INFO(logger, "[is_logged_in]");
    for (auto& unit: account_units)
    {
        if (!unit.logged_in)
            return false;
    }
    return true;
}

bool TDEngineCoinmex::is_connected() const
{
    KF_LOG_INFO(logger, "[is_connected]");
    return is_logged_in();
}



std::string TDEngineCoinmex::GetSide(const LfDirectionType& input) {
    if (LF_CHAR_Buy == input) {
        return "BUY";
    } else if (LF_CHAR_Sell == input) {
        return "SELL";
    } else {
        return "UNKNOWN";
    }
}

 LfDirectionType TDEngineCoinmex::GetDirection(std::string input) {
    if ("BUY" == input) {
        return LF_CHAR_Buy;
    } else if ("SELL" == input) {
        return LF_CHAR_Sell;
    } else {
        return LF_CHAR_Buy;
    }
}

std::string TDEngineCoinmex::GetType(const LfOrderPriceTypeType& input) {
    if (LF_CHAR_LimitPrice == input) {
        return "LIMIT";
    } else if (LF_CHAR_AnyPrice == input) {
        return "MARKET";
    } else {
        return "UNKNOWN";
    }
}

LfOrderPriceTypeType TDEngineCoinmex::GetPriceType(std::string input) {
    if ("LIMIT" == input) {
        return LF_CHAR_LimitPrice;
    } else if ("MARKET" == input) {
        return LF_CHAR_AnyPrice;
    } else {
        return '0';
    }
}

std::string TDEngineCoinmex::GetTimeInForce(const LfTimeConditionType& input) {
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

LfTimeConditionType TDEngineCoinmex::GetTimeCondition(std::string input) {
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

LfOrderStatusType TDEngineCoinmex::GetOrderStatus(std::string input) {
    if ("NEW" == input) {
      return LF_CHAR_NotTouched;
    } else if ("PARTIALLY_FILLED" == input) {
      return LF_CHAR_PartTradedNotQueueing;
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
      return LF_CHAR_AllTraded;
    }
}


std::string TDEngineCoinmex::GetInputOrderData(const LFInputOrderField* order, int recvWindow) {
    std::stringstream ss;
    ss << "symbol=" << order->InstrumentID << "&";
    ss << "side=" << GetSide(order->Direction) << "&";
    ss << "type=" << GetType(order->OrderPriceType) << "&";
    ss << "timeInForce=" << GetTimeInForce(order->TimeCondition) << "&";
    ss << "quantity=" << order->Volume << "&";
    ss << "price=" << order->LimitPrice << "&";
    ss << "recvWindow="<< recvWindow <<"&timestamp=" << yijinjing::getNanoTime();
    return ss.str();
}

/**
 * req functions
 */
void TDEngineCoinmex::req_investor_position(const LFQryPositionField* data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_investor_position]");

    KF_LOG_DEBUG(logger, "[req_investor_position] (Mock EmptyData)" << " (Bid)" << data->BrokerID
                                     << " (Iid)" << data->InvestorID
                                     << " (Tid)" << data->InstrumentID);
    send_writer->write_frame(data, sizeof(LFQryPositionField), source_id, MSG_TYPE_LF_QRY_POS_COINMEX, 1, requestId);
    std::string msg = "";
    LFRspPositionField pos;
    strncpy(pos.BrokerID, data->BrokerID, 11);
    strncpy(pos.InvestorID, data->InvestorID, 19);
    strncpy(pos.InstrumentID, data->InstrumentID, 31);
    on_rsp_position(&pos, 1, requestId, 0, msg.c_str());
}

void TDEngineCoinmex::req_qry_account(const LFQryAccountField *data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_qry_account]");
}


void TDEngineCoinmex::GetAndHandleTradeResponse(const std::string& symbol, int limit)
{
    //rhese codes is just for test
    std::vector<std::string> symbols;
    uint64_t last_trade_id = 0;
    const auto static url = "https://api.binance.com/api/v1/trades";
    const auto response = cpr::Get(cpr::Url{url}, cpr::Parameters{{"symbol", symbol},
                                                   {"limit",  std::to_string(limit)}});
    rapidjson::Document d;
    d.Parse(response.text.c_str());
    if(d.IsArray())
    {
        LFL2TradeField trade;
        memset(&trade, 0, sizeof(trade));
        strcpy(trade.InstrumentID, symbols[0].c_str());
        strcpy(trade.ExchangeID, "binance");

        for(int i = 0; i < d.Size(); ++i)
        {
            const auto& ele = d[i];
            if(!ele.HasMember("id"))
            {
                continue;
            }

            const auto trade_id = ele["id"].GetUint64();
            if(trade_id <= last_trade_id)
            {
                continue;
            }

            last_trade_id = trade_id;
            if(ele.HasMember("price") && ele.HasMember("qty") && ele.HasMember("isBuyerMaker") && ele.HasMember("isBestMatch"))
            {
                trade.Price = std::stod(ele["price"].GetString()) * scale_offset;
                trade.Volume = std::stod(ele["qty"].GetString()) * scale_offset;
                trade.OrderKind[0] = ele["isBestMatch"].GetBool() ? 'B' : 'N';
                trade.OrderBSFlag[0] = ele["isBuyerMaker"].GetBool() ? 'B' : 'S';

            }
        }
    }
}


void TDEngineCoinmex::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time)
{

}

void TDEngineCoinmex::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time)
{

}

void TDEngineCoinmex::GetAndHandleOrderResponse()
{
    KF_LOG_INFO(logger, "[GetAndHandleOrderResponse]");
}

void TDEngineCoinmex::loop()
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
        GetAndHandleOrderResponse();
    }
}



std::vector<std::string> TDEngineCoinmex::split(std::string str, std::string token)
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

Document TDEngineCoinmex::get_exchange_time(AccountUnitCoinmex& unit)
{
    KF_LOG_INFO(logger, "[get_exchange_time]");
    std::string Timestamp = getTimestampString();
    std::string Method = "GET";
    std::string requestPath = "/api/v1/spot/public/time";
    std::string queryString= "";
    std::string body = "";
    string Message = Timestamp + Method + requestPath + queryString + body;

    string url = unit.baseUrl + requestPath + queryString;
    KF_LOG_INFO(logger, "[get_exchange_time] (url) " << url << " (timestamp) " << Timestamp << " (Message) " << Message);
    const auto response = Get(Url{url}, Parameters{});
    Document d;
    d.Parse(response.text.c_str());
    KF_LOG_INFO(logger, "[get_exchange_time] (url) " << url << " (response) " << response.text.c_str());
    if(d.HasMember("timestamp")) {
        Value& s = d["timestamp"];
        KF_LOG_INFO(logger, "[get_exchange_time] (response.timestamp.type) " << s.GetType());
        KF_LOG_INFO(logger, "[get_exchange_time] (response.timestamp) " << d["timestamp"].GetInt64());
    }
    if(d.HasMember("iso")) {
        Value& s = d["iso"];
        KF_LOG_INFO(logger, "[get_exchange_time] (response.iso.type) " << s.GetType());
        KF_LOG_INFO(logger, "[get_exchange_time] (response.iso) " << d["iso"].GetString());
    }
    KF_LOG_INFO(logger, "[get_exchange_time] (done) ");
    return d;
}

Document TDEngineCoinmex::send_order(AccountUnitCoinmex& unit)
{
    KF_LOG_INFO(logger, "[send_order]");
    std::string code="LTC_BTC";
    std::string side="buy";
    std:string type="limit";
    std::string size="1";
    std::string price="0.0001";
    std::string funds="1";

    if("limit" == type)
    {
        if(price.length() == 0 || size.length() == 0) {
            KF_LOG_ERROR(logger, "[send_order] limit order , price or size cannot be null");
        }

    } else if("market" == type) {
        if("buy" == side &&  funds.length() == 0) {
            KF_LOG_ERROR(logger, "[send_order] market order , type is buy, the funds cannot be null");
        }
        if("sell" == side &&  size.length() == 0) {
            KF_LOG_ERROR(logger, "[send_order] market order , type is buy, the size cannot be null");
        }
    }

    rapidjson::Document document;
    document.SetObject();
    rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
    document.AddMember("code", StringRef(code.c_str()), allocator);
    document.AddMember("side", StringRef(side.c_str()), allocator);
    document.AddMember("type", StringRef(type.c_str()), allocator);
    document.AddMember("size", StringRef(size.c_str()), allocator);
    document.AddMember("price", StringRef(price.c_str()), allocator);
    document.AddMember("funds", StringRef(funds.c_str()), allocator);
    StringBuffer jsonStr;
    Writer<StringBuffer> writer(jsonStr);
    document.Accept(writer);

    std::string Timestamp = getTimestampString();
    std::string Method = "POST";
    std::string requestPath = "/api/v1/spot/ccex/orders";
    std::string queryString= "";
    std::string body = jsonStr.GetString();
    KF_LOG_INFO(logger, "[send_order] (body) " << body);

    string Message = Timestamp + Method + requestPath + queryString + body;
    unsigned char* signature = hmac_sha256_byte(unit.secret_key.c_str(), Message.c_str());
    string url = unit.baseUrl + requestPath + queryString;
    KF_LOG_INFO(logger, "[send_order] (url) " << url << " (timestamp) " << Timestamp << " (Message) " << Message);

    std::string sign = base64_encode(signature, strlen((char*)signature));
    KF_LOG_INFO(logger, "[send_order] (1sign) " << sign);

    KF_LOG_INFO(logger, "[send_order] (url) " << url << " (preHash) " << Message);
    KF_LOG_INFO(logger, "[send_order] (url) " << url << " (ACCESS-KEY) " << unit.api_key);
    KF_LOG_INFO(logger, "[send_order] (url) " << url << " (ACCESS-PASSPHRASE) " << unit.passphrase);
    KF_LOG_INFO(logger, "[send_order] (url) " << url << " (ACCESS-SIGN) " << sign);
    KF_LOG_INFO(logger, "[send_order] (url) " << url << " (ACCESS-TIMESTAMP) " << Timestamp);

    const auto response = Post(Url{url},
                              Header{{"ACCESS-KEY", unit.api_key}, {"ACCESS-PASSPHRASE", unit.passphrase},
                                     {"Content-Type", "application/json; charset=UTF-8"},
                                     {"Content-Length", to_string(body.size())},
                                     {"ACCESS-SIGN", sign},
                                     {"ACCESS-TIMESTAMP",  Timestamp}},
                              Body{body});

    KF_LOG_INFO(logger, "[send_order] (url) " << url << " (response.status_code) " << response.status_code << " (response.error.message) " << response.error.message << " (response.text) " << response.text.c_str());
    Document d;
    d.Parse(response.text.c_str());
    return d;
}


Document TDEngineCoinmex::cancel_all_orders(AccountUnitCoinmex& unit, std::string code)
{
    KF_LOG_INFO(logger, "[cancel_all_orders]");
    rapidjson::Document document;
    document.SetObject();
    rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
    document.AddMember("code", StringRef(code.c_str()), allocator);
    StringBuffer jsonStr;
    Writer<StringBuffer> writer(jsonStr);
    document.Accept(writer);

    std::string Timestamp = getTimestampString();
    std::string Method = "DELETE";
    std::string requestPath = "/api/v1/spot/ccex/orders";
    std::string queryString= "";
    std::string body = jsonStr.GetString();
    string Message = Timestamp + Method + requestPath + queryString + body;

    unsigned char* signature = hmac_sha256_byte(unit.secret_key.c_str(), Message.c_str());
    string url = unit.baseUrl + requestPath + queryString;
    KF_LOG_INFO(logger, "[cancel_all_orders] (url) " << url << " (timestamp) " << Timestamp << " (Message) " << Message);

    std::string sign = base64_encode(signature, strlen((char*)signature));

    const auto response = Delete(Url{url},
                                 Header{{"ACCESS-KEY", unit.api_key}, {"ACCESS-PASSPHRASE", unit.passphrase},
                                        {"Content-Type", "application/json; charset=UTF-8"},
                                        {"Content-Length", to_string(body.size())},
                                        {"ACCESS-SIGN", sign},
                                        {"ACCESS-TIMESTAMP",  Timestamp}},
                                 Body{body});

    KF_LOG_INFO(logger, "[cancel_all_orders] (url) " << url << " (response.status_code) " << response.status_code << " (response.error.message) " << response.error.message << " (response.text) " << response.text.c_str());
    // (response.status_code) 200 (response.error.message)  (response.text)
    Document d;
    d.Parse(response.text.c_str());
    return d;
}

Document TDEngineCoinmex::cancel_order(AccountUnitCoinmex& unit, std::string code, long orderId)
{
    KF_LOG_INFO(logger, "[cancel_order]");
    rapidjson::Document document;
    document.SetObject();
    rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
    document.AddMember("code", StringRef(code.c_str()), allocator);
    StringBuffer jsonStr;
    Writer<StringBuffer> writer(jsonStr);
    document.Accept(writer);

    std::string Timestamp = getTimestampString();
    std::string Method = "DELETE";
    std::string requestPath = "/api/v1/spot/ccex/orders/" + std::to_string(orderId);
    std::string queryString= "";
    std::string body = jsonStr.GetString();
    string Message = Timestamp + Method + requestPath + queryString + body;

    unsigned char* signature = hmac_sha256_byte(unit.secret_key.c_str(), Message.c_str());
    string url = unit.baseUrl + requestPath + queryString;
    KF_LOG_INFO(logger, "[cancel_order] (url) " << url << " (timestamp) " << Timestamp << " (Message) " << Message);

    std::string sign = base64_encode(signature, strlen((char*)signature));
    KF_LOG_INFO(logger, "[cancel_order] (1sign) " << sign);
    KF_LOG_INFO(logger, "[cancel_order] (url) " << url << " (preHash) " << Message);
    KF_LOG_INFO(logger, "[cancel_order] (url) " << url << " (ACCESS-KEY) " << unit.api_key);
    KF_LOG_INFO(logger, "[cancel_order] (url) " << url << " (ACCESS-PASSPHRASE) " << unit.passphrase);
    KF_LOG_INFO(logger, "[cancel_order] (url) " << url << " (ACCESS-SIGN) " << sign);
    KF_LOG_INFO(logger, "[cancel_order] (url) " << url << " (ACCESS-TIMESTAMP) " << Timestamp);

    const auto response = Delete(Url{url},
                                 Header{{"ACCESS-KEY", unit.api_key}, {"ACCESS-PASSPHRASE", unit.passphrase},
                                        {"Content-Type", "application/json; charset=UTF-8"},
                                        {"Content-Length", to_string(body.size())},
                                        {"ACCESS-SIGN", sign},
                                        {"ACCESS-TIMESTAMP",  Timestamp}},
                                 Body{body});

    KF_LOG_INFO(logger, "[cancel_order] (url) " << url << " (response.status_code) " << response.status_code << " (response.error.message) " << response.error.message << " (response.text) " << response.text.c_str());
    //(response.status_code) 200 (response.error.message)  (response.text)
    Document d;
    d.Parse(response.text.c_str());
    return d;
}

//订单状态，﻿open（未成交）、filled（已完成）、canceled（已撤销）、cancel（撤销中）、partially-filled（部分成交）
Document TDEngineCoinmex::query_orders(AccountUnitCoinmex& unit, std::string code, std::string status)
{
    KF_LOG_INFO(logger, "[query_orders]");
/*
 # Response
    {
        "averagePrice": "0",
        "code": "chp-eth",
        "createdDate": 1526299182000,
        "filledVolume": "0",
        "funds": "0",
        "orderId": 9865872,
        "orderType": "limit",
        "price": "0.00001",
        "side": "buy",
        "status": "canceled",
        "volume": "1"
    }
 * */

    std::string Timestamp = getTimestampString();
    std::string Method = "GET";
    std::string requestPath = "/api/v1/spot/ccex/orders";
    std::string queryString= "?code=" + code + "&status=" + status;
    std::string body = "";
    string Message = Timestamp + Method + requestPath + queryString + body;

    unsigned char* signature = hmac_sha256_byte(unit.secret_key.c_str(), Message.c_str());
    string url = unit.baseUrl + requestPath + queryString;
    KF_LOG_INFO(logger, "[query_orders] (url) " << url << " (timestamp) " << Timestamp << " (Message) " << Message);

    std::string sign = base64_encode(signature, strlen((char*)signature));
    KF_LOG_INFO(logger, "[query_orders] (1sign) " << sign);
    KF_LOG_INFO(logger, "[query_orders] (url) " << url << " (preHash) " << Message);
    KF_LOG_INFO(logger, "[query_orders] (url) " << url << " (ACCESS-KEY) " << unit.api_key);
    KF_LOG_INFO(logger, "[query_orders] (url) " << url << " (ACCESS-PASSPHRASE) " << unit.passphrase);
    KF_LOG_INFO(logger, "[query_orders] (url) " << url << " (ACCESS-SIGN) " << sign);
    KF_LOG_INFO(logger, "[query_orders] (url) " << url << " (ACCESS-TIMESTAMP) " << Timestamp);

    const auto response = Get(Url{url},
                               Header{{"ACCESS-KEY", unit.api_key}, {"ACCESS-PASSPHRASE", unit.passphrase},
                                      {"Content-Type", "application/json"},
                                      {"ACCESS-SIGN", sign},
                                      {"ACCESS-TIMESTAMP",  Timestamp}},
                               Body{body});

    KF_LOG_INFO(logger, "[query_orders] (url) " << url << " (response.status_code) " << response.status_code << " (response.error.message) " << response.error.message << " (response.text) " << response.text.c_str());
    // (response.status_code) 200 (response.error.message)  (response.text) []
    Document d;
    d.Parse(response.text.c_str());
    return d;
}

Document TDEngineCoinmex::query_order(AccountUnitCoinmex& unit, std::string code, long orderId)
{
    KF_LOG_INFO(logger, "[query_order]");
    std::string Timestamp = getTimestampString();
    std::string Method = "GET";
    std::string requestPath = "/api/v1/spot/ccex/orders/" + std::to_string(orderId);
    std::string queryString= "?code=" + code;
    std::string body = "";
    string Message = Timestamp + Method + requestPath + queryString + body;

    unsigned char* signature = hmac_sha256_byte(unit.secret_key.c_str(), Message.c_str());
    string url = unit.baseUrl + requestPath + queryString;
    KF_LOG_INFO(logger, "[query_order] (url) " << url << " (timestamp) " << Timestamp << " (Message) " << Message);

    std::string sign = base64_encode(signature, strlen((char*)signature));
    KF_LOG_INFO(logger, "[query_order] (1sign) " << sign);
    KF_LOG_INFO(logger, "[query_order] (url) " << url << " (preHash) " << Message);
    KF_LOG_INFO(logger, "[query_order] (url) " << url << " (ACCESS-KEY) " << unit.api_key);
    KF_LOG_INFO(logger, "[query_order] (url) " << url << " (ACCESS-PASSPHRASE) " << unit.passphrase);
    KF_LOG_INFO(logger, "[query_order] (url) " << url << " (ACCESS-SIGN) " << sign);
    KF_LOG_INFO(logger, "[query_order] (url) " << url << " (ACCESS-TIMESTAMP) " << Timestamp);

    const auto response = Get(Url{url},
                               Header{{"ACCESS-KEY", unit.api_key}, {"ACCESS-PASSPHRASE", unit.passphrase},
                                      {"Content-Type", "application/json"},
                                      {"ACCESS-SIGN", sign},
                                      {"ACCESS-TIMESTAMP",  Timestamp}},
                               Body{body});

    KF_LOG_INFO(logger, "[query_order] (url) " << url << " (response.status_code) " << response.status_code << " (response.error.message) " << response.error.message << " (response.text) " << response.text.c_str());
    //(response.status_code) 404 (response.error.message)  (response.text) {"message":"Order does not exist"}
    Document d;
    d.Parse(response.text.c_str());
    return d;
}

Document TDEngineCoinmex::get_account(AccountUnitCoinmex& unit)
{
    KF_LOG_INFO(logger, "[get_account]");
    std::string Timestamp = getTimestampString();
    std::string Method = "GET";
    std::string requestPath = "/api/v1/spot/ccex/account/assets";
    std::string queryString= "";
    std::string body = "";
    string Message = Timestamp + Method + requestPath + queryString + body;
    //Message ="1529669690.429GET/api/v1/spot/ccex/account/assets";

    unsigned char* signature = hmac_sha256_byte(unit.secret_key.c_str(), Message.c_str());
    string url = unit.baseUrl + requestPath;
    KF_LOG_INFO(logger, "[get_account] (url) " << url << " (timestamp) " << Timestamp << " (Message) " << Message);
    
    std::string sign1 = base64_encode(signature, strlen((char*)signature));
    KF_LOG_INFO(logger, "[get_account] (1sign) " << sign1);

    KF_LOG_INFO(logger, "[get_account] (url) " << url << " (preHash) " << Message);
    KF_LOG_INFO(logger, "[get_account] (url) " << url << " (ACCESS-KEY) " << unit.api_key);
    KF_LOG_INFO(logger, "[get_account] (url) " << url << " (ACCESS-PASSPHRASE) " << unit.passphrase);
    KF_LOG_INFO(logger, "[get_account] (url) " << url << " (ACCESS-SIGN) " << sign1);
    KF_LOG_INFO(logger, "[get_account] (url) " << url << " (ACCESS-TIMESTAMP) " << Timestamp);

    const auto response = Get(Url{url},
            Header{{"ACCESS-KEY", unit.api_key}, {"ACCESS-PASSPHRASE", unit.passphrase},
                   {"Content-Type", "application/json"},
                   {"ACCESS-SIGN", sign1},
                   {"ACCESS-TIMESTAMP",  Timestamp}} );

    KF_LOG_INFO(logger, "[get_account] (url) " << url << " (response) " << response.text.c_str());
    Document d;
    d.Parse(response.text.c_str());
    return d;
}

std::string TDEngineCoinmex::getTimestampString()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::string timestampStr;
    std::stringstream convertStream;
    convertStream <<std::fixed << std::setprecision(3) << (timestamp/1000.0);
    convertStream >> timestampStr;
    return timestampStr;
}


#define GBK2UTF8(msg) kungfu::yijinjing::gbk2utf8(string(msg))

BOOST_PYTHON_MODULE(libcoinmextd)
{
    using namespace boost::python;
    class_<TDEngineCoinmex, boost::shared_ptr<TDEngineCoinmex> >("Engine")
    .def(init<>())
    .def("init", &TDEngineCoinmex::initialize)
    .def("start", &TDEngineCoinmex::start)
    .def("stop", &TDEngineCoinmex::stop)
    .def("logout", &TDEngineCoinmex::logout)
    .def("wait_for_stop", &TDEngineCoinmex::wait_for_stop);
}

