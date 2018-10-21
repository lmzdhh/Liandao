#include "TDEngineOceanEx.h"
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
using utils::crypto::rsa256_private_encrypt;
using utils::crypto::rsa256_pub_decrypt;
USING_WC_NAMESPACE


TDEngineOceanEx::TDEngineOceanEx(): ITDEngine(SOURCE_OCEANEX)
{
    logger = yijinjing::KfLog::getLogger("TradeEngine.OceanEx");
    KF_LOG_INFO(logger, "[TDEngineOceanEx]");

    mutex_order_and_trade = new std::mutex();
    mutex_response_order_status = new std::mutex();
    mutex_orderaction_waiting_response = new std::mutex();
}

TDEngineOceanEx::~TDEngineOceanEx()
{
    if(mutex_order_and_trade != nullptr) delete mutex_order_and_trade;
    if(mutex_response_order_status != nullptr) delete mutex_response_order_status;
    if(mutex_orderaction_waiting_response != nullptr) delete mutex_orderaction_waiting_response;
}

void TDEngineOceanEx::init()
{
    ITDEngine::init();
    JournalPair tdRawPair = getTdRawJournalPair(source_id);
    raw_writer = yijinjing::JournalSafeWriter::create(tdRawPair.first, tdRawPair.second, "RAW_" + name());
    KF_LOG_INFO(logger, "[init]");
}

void TDEngineOceanEx::pre_load(const json& j_config)
{
    KF_LOG_INFO(logger, "[pre_load]");
}

void TDEngineOceanEx::resize_accounts(int account_num)
{
    account_units.resize(account_num);
    KF_LOG_INFO(logger, "[resize_accounts]");
}

TradeAccount TDEngineOceanEx::load_account(int idx, const json& j_config)
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

    AccountUnitOceanEx& unit = account_units[idx];
    unit.api_key = api_key;
    unit.secret_key = secret_key;
    unit.passphrase = passphrase;
    unit.baseUrl = baseUrl;

    KF_LOG_INFO(logger, "[load_account] (api_key)" << api_key << " (baseUrl)" << unit.baseUrl);

//test rs256
    std::string data ="{}"
    std::string signature = rsa256_private_sign(data, "-----BEGIN RSA PRIVATE KEY-----\nMIIEowIBAAKCAQEAzXu8DWmbHds0EOiBwgmYEGwayYIM75EJNd9R0HJHfTpfCl8h\nQ1r6M6/MtX9L8kviEup6jk7S0N2NZu8Xh6nk+SsUbJTOAm4c/9D1fM6IqXlYDmss\nU8zcLSzm72WTbC7HM8St2Ky5V4eCLHJsqCB/Je1Q/F6/K+pMMzPumornUpDgr6El\nUjjOgroRNnl5mgqB466Op1Xfnl/nLsHXetDPZ2Ekp4iQmCl5zR7sYMY0tUviVbjE\nGEQ6VobnkkZDH/pnjrdjWKW+Un6cO/WLDKKdsgloCBnFRH8jyAiifwTItTP+ejmK\nuqsjLUWcNJ/MtGvhTyxPd4z18SsgQ3g6Goc+swIBAwKCAQEAiP0oCPESE+d4C0Wr\n1rEQCvK8hlazSmCwzpThNaGE/ibqBuoWLOdRd8qIeP+H9t1BYfGnCYnh4JOzmfS6\nWnFDUMdi8w3erEloqotOUzRbG6Y6tEdy4oiSyMiZ9O5iSB8vd9hz5ch7j6+sHaGd\nxWr/bp41/ZR/cpwyzM1JvFyaNwoOJgA81SDUZjmZpfZYH7tc52JhlBJroJ3rQJFx\nO1yvLvMnM5akWhdVDtsRy2WUo5ToVbTFYOxqevstxwKNTpECxvl4+Rn8bIuzjo1b\nvQLWIepb6v9CFb0QNgP6IodxUg4vaNni/NCaD9Mc2mJiriFgpBKKwcdNgFIpveHP\nF9n6awKBgQDy4annCKGYJa+tD3IoQDDrugzxTZQ8eEXMRct1HUS9nhz4aI1Q8rF6\nBLbbrtNG6tVWfw45GdOMCQIVXTEkuoZkLHBx/Yr1mNyfNtE/3ej11pk8Lfr7K5Ie\nQaOX4ckpD6cI2t+4yI1DH2DNwYe17W5bcRSMpXIhRwUSJL9DQqZ50QKBgQDYlPbj\nCeX3w7P9rhXNKkCKzo4K+6YBtS06CBw4hIELAtdxcZlJHlUAMh92ANqO1RcvVhti\n7Q4OlQwNipFKb5p/N9C75XPOFtBvr1BBkzVmqJCh+Z/m+FFtNV8TaXB1qneughL9\nduT49igjK4SCwct05/vyr2/gaarPgeZANBnNQwKBgQCh68aaBcEQGR/ItPbFgCCd\nJrNLiQ19pYPdg9z4vi3Tvr368F419yD8AySSdIzZ8eOO/17Qu+JdW1a46Mtt0a7t\ncvWhU7H5EJMUzzYqk/Cj5GYoHqdSHQwUK8JlQTDGCm9bPJUl2wjXakCJK6/OnkmS\nS2MIbkwWL1i2wyos1xmmiwKBgQCQY09CBplP181TyWPeHCsHNF6x/RlWeMjRWr17\nAwCyAeT2S7uGFDiqzBT5VecJ42TKOWeXSLQJuLKzsbYxn7xUz+B9Q6KJZIr1H4rW\nYiOZxbXBURVEpYueI5S3m6BOcaUfAWH+T0NQpBrCHQMB1oejRVKhykqVm8c1AUQq\nzWaI1wKBgB6TWnnVGhx2jTei8YnD//IYplv8/kErxwHaC2yz7qvdBQB+ljuimGzm\nxefSDq993EWmKGYJ/IiiRoue2x6IX4EcrnG2hZ2sBfgjvjxGSm1s0w81XLMcMnL2\n+ItII2MKryk0lMyRyfVyaMr52wbXSo7Lali5wweXvxUCU1CGGUJD\n-----END RSA PRIVATE KEY-----");
    std::string sign = base64_encode((unsigned char*)signature.c_str(), signature.size());
    std::cout  << "[TDEngineOceanEx] (test rs256-base64-sign)" << sign << std::endl;

    std::string decodeStr = rsa256_pub_verify(data,signature, "-----BEGIN RSA PUBLIC KEY-----\nMIIBCAKCAQEAzXu8DWmbHds0EOiBwgmYEGwayYIM75EJNd9R0HJHfTpfCl8hQ1r6\nM6/MtX9L8kviEup6jk7S0N2NZu8Xh6nk+SsUbJTOAm4c/9D1fM6IqXlYDmssU8zc\nLSzm72WTbC7HM8St2Ky5V4eCLHJsqCB/Je1Q/F6/K+pMMzPumornUpDgr6ElUjjO\ngroRNnl5mgqB466Op1Xfnl/nLsHXetDPZ2Ekp4iQmCl5zR7sYMY0tUviVbjEGEQ6\nVobnkkZDH/pnjrdjWKW+Un6cO/WLDKKdsgloCBnFRH8jyAiifwTItTP+ejmKuqsj\nLUWcNJ/MtGvhTyxPd4z18SsgQ3g6Goc+swIBAw==\n-----END RSA PUBLIC KEY-----");
    std::cout  << "[TDEngineOceanEx] (test rs256-verify)" << decodeStr.empty()?"yes":"no" << std::endl;

    unit.coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
    unit.coinPairWhiteList.Debug_print();

    unit.positionWhiteList.ReadWhiteLists(j_config, "positionWhiteLists");
    unit.positionWhiteList.Debug_print();

    //display usage:
    if(unit.coinPairWhiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "TDEngineOceanEx::load_account: please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"btcusdt\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"etceth\"");
        KF_LOG_ERROR(logger, "},");
    }

    //test
    Document json;
    get_account(unit, json);
    printResponse(json);
    cancel_all_orders(unit, "etc_eth", json);
    printResponse(json);

    // set up
    TradeAccount account = {};
    //partly copy this fields
    strncpy(account.UserID, api_key.c_str(), 16);
    strncpy(account.Password, secret_key.c_str(), 21);
    return account;
}

void TDEngineOceanEx::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[connect]");
    for (size_t idx = 0; idx < account_units.size(); idx++)
    {
        AccountUnitOceanEx& unit = account_units[idx];
        KF_LOG_INFO(logger, "[connect] (api_key)" << unit.api_key);
        Document doc;
        //cancel_all_orders(unit, doc);

        if (!unit.logged_in)
        {
            unit.logged_in = true;
        }
    }
}


void TDEngineOceanEx::login(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[login]");
    connect(timeout_nsec);
}

void TDEngineOceanEx::logout()
{
    KF_LOG_INFO(logger, "[logout]");
}

void TDEngineOceanEx::release_api()
{
    KF_LOG_INFO(logger, "[release_api]");
}

bool TDEngineOceanEx::is_logged_in() const
{
    KF_LOG_INFO(logger, "[is_logged_in]");
    for (auto& unit: account_units)
    {
        if (!unit.logged_in)
            return false;
    }
    return true;
}

bool TDEngineOceanEx::is_connected() const
{
    KF_LOG_INFO(logger, "[is_connected]");
    return is_logged_in();
}


std::string TDEngineOceanEx::GetSide(const LfDirectionType& input) {
    if (LF_CHAR_Buy == input) {
        return "buy";
    } else if (LF_CHAR_Sell == input) {
        return "sell";
    } else {
        return "";
    }
}

LfDirectionType TDEngineOceanEx::GetDirection(std::string input) {
    if ("buy" == input) {
        return LF_CHAR_Buy;
    } else if ("sell" == input) {
        return LF_CHAR_Sell;
    } else {
        return LF_CHAR_Buy;
    }
}

std::string TDEngineOceanEx::GetType(const LfOrderPriceTypeType& input) {
    if (LF_CHAR_LimitPrice == input) {
        return "limit";
    } else if (LF_CHAR_AnyPrice == input) {
        return "market";
    } else {
        return "";
    }
}

LfOrderPriceTypeType TDEngineOceanEx::GetPriceType(std::string input) {
    if ("limit" == input) {
        return LF_CHAR_LimitPrice;
    } else if ("market" == input) {
        return LF_CHAR_AnyPrice;
    } else {
        return '0';
    }
}
//订单状态，﻿open（未成交）、filled（已完成）、canceled（已撤销）、cancel（撤销中）、partially-filled（部分成交）
LfOrderStatusType TDEngineOceanEx::GetOrderStatus(std::string input) {
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
void TDEngineOceanEx::req_investor_position(const LFQryPositionField* data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId);

    AccountUnitOceanEx& unit = account_units[account_index];
    KF_LOG_INFO(logger, "[req_investor_position] (api_key)" << unit.api_key << " (InstrumentID) " << data->InstrumentID);

    int errorId = 0;
    std::string errorMsg = "";
    Document d;
    get_account(unit, d);

    if(!d.HasParseError() && d.IsObject() && d.HasMember("code"))
    {
        errorId = d["code"].GetInt();
        if(d.HasMember("message") && d["message"].IsString())
        {
            errorMsg = d["message"].GetString();
        }
        KF_LOG_ERROR(logger, "[req_investor_position] failed!" << " (rid)" << requestId << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }
    send_writer->write_frame(data, sizeof(LFQryPositionField), source_id, MSG_TYPE_LF_QRY_POS_OCEANEX, 1, requestId);

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
{
  "code": 0,
  "data": {
    "accounts": [
      {"locked": "0.0", "balance": "99.9", "currency": "oce"},
      {"locked": "0.0", "balance": "99.9", "currency": "usd"},
      {"locked": "0.0", "balance": "99.9", "currency": "vet"},
      {"locked": "0.0", "balance": "99.9", "currency": "vtho"}
      ]
    },
  "message": "Operation is successful"
}
* */
    std::vector<LFRspPositionField> tmp_vector;
    if(!d.HasParseError() && d.IsObject() && d.HasMember("data") && d["data"].IsObject() && d["data"].HasMember("accounts")
        && d["data"]["accounts"].IsArray())
    {
        size_t len = d["data"]["accounts"].Size();
        auto& accounts = d["data"]["accounts"];

        KF_LOG_INFO(logger, "[req_investor_position] (accounts.length)" << len);
        for(size_t i = 0; i < len; i++)
        {
            std::string symbol = accounts.GetArray()[i]["currency"].GetString();
            std::string ticker = unit.positionWhiteList.GetKeyByValue(symbol);
            if(ticker.length() > 0) {
                strncpy(pos.InstrumentID, ticker.c_str(), 31);
                pos.Position = std::round(std::stod(accounts.GetArray()[i]["balance"].GetString()) * scale_offset);
                tmp_vector.push_back(pos);
                KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId << " (symbol) " << symbol
                                                                          << " balance:" << accounts.GetArray()[i]["balance"].GetString()
                                                                          << " locked: " << accounts.GetArray()[i]["locked"].GetString());
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
        KF_LOG_INFO(logger, "[req_investor_position] (!findSymbolInResult) (requestId)" << requestId);
        on_rsp_position(&pos, 1, requestId, errorId, errorMsg.c_str());
    }
    if(errorId != 0)
    {
        raw_writer->write_error_frame(&pos, sizeof(LFRspPositionField), source_id, MSG_TYPE_LF_RSP_POS_OCEANEX, 1, requestId, errorId, errorMsg.c_str());
    }
}

void TDEngineOceanEx::req_qry_account(const LFQryAccountField *data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_qry_account]");
}


void TDEngineOceanEx::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitOceanEx& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_insert]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Tid)" << data->InstrumentID
                                              << " (Volume)" << data->Volume
                                              << " (LimitPrice)" << data->LimitPrice
                                              << " (OrderRef)" << data->OrderRef);
    send_writer->write_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_OCEANEX, 1/*ISLAST*/, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_insert]: not in WhiteList, ignore it  (rid)" << requestId <<
                                                                                      " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_OCEANEX, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_insert] (exchange_ticker)" << ticker);

    double funds = 0;
    Document d;

    int64_t fixedPrice = data->LimitPrice;

    send_order(unit, ticker.c_str(), GetSide(data->Direction).c_str(),
               GetType(data->OrderPriceType).c_str(), data->Volume*1.0/scale_offset, fixedPrice*1.0/scale_offset, funds, d);
    //d.Parse("{\"orderId\":19319936159776,\"result\":true}");
    //not expected response
    if(d.HasParseError() || !d.IsObject())
    {
        errorId = 100;
        errorMsg = "send_order http response has parse error or is not json. please check the log";
        KF_LOG_ERROR(logger, "[req_order_insert] send_order error!  (rid)" << requestId << " (errorId)" <<
                                                                           errorId << " (errorMsg) " << errorMsg);
    } else  if(d.HasMember("code") && d.HasMember("data") && d["data"].IsObject())
    {
        if(d["code"].GetInt() == 0)
        {
            /*
             {
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
            //if send successful and the exchange has received ok, then add to  pending query order list
            int64_t remoteOrderId = d["data"]["id"].GetInt64();
            //fix defect of use the old value
            localOrderRefRemoteOrderId[std::string(data->OrderRef)] = remoteOrderId;
            KF_LOG_INFO(logger, "[req_order_insert] after send  (rid)" << requestId << " (OrderRef) " <<
                                                                       data->OrderRef << " (remoteOrderId) " << remoteOrderId);


            //TODO:   onRtn order/on rtn trade




            char noneStatus = '\0';
            addNewQueryOrdersAndTrades(unit, data->InstrumentID, data->OrderRef, noneStatus, 0, remoteOrderId);

            //success, only record raw data
            raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_OCEANEX, 1, requestId, errorId, errorMsg.c_str());

            return;

        } else {
            errorId = d["code"].GetInt();
            if(d.HasMember("message") && d["message"].IsString())
            {
                errorMsg = d["message"].GetString();
            }
            KF_LOG_ERROR(logger, "[req_order_insert] send_order error!  (rid)" << requestId << " (errorId)" <<
                                                                               errorId << " (errorMsg) " << errorMsg);
        }
    } else if (d.HasMember("code") && d["code"].IsNumber()) {
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
    }
    raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_OCEANEX, 1, requestId, errorId, errorMsg.c_str());
}

//websocket的消息通常回来的比restful快，这时候因为消息里面有OrderId却找不到OrderRef，会先放入responsedOrderStatusNoOrderRef，
//当sendOrder返回OrderId信息之后,再处理这个信息
void TDEngineOceanEx::handlerResponsedOrderStatus(AccountUnitOceanEx& unit)
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

void TDEngineOceanEx::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitOceanEx& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_action]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Iid)" << data->InvestorID
                                              << " (OrderRef)" << data->OrderRef
                                              << " (KfOrderID)" << data->KfOrderID);

    send_writer->write_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_OCEANEX, 1, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_action]: not in WhiteList , ignore it: (rid)" << requestId << " (errorId)" <<
                                                                                       errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_OCEANEX, 1, requestId, errorId, errorMsg.c_str());
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
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_OCEANEX, 1, requestId, errorId, errorMsg.c_str());
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
    if(!d.HasParseError() && d.HasMember("code") && d["code"].IsNumber()) {
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
    } else {
        //addRemoteOrderIdOrderActionSentTime( data, requestId, remoteOrderId);



        //TODO:   onRtn order/on rtn trade




    }
    raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_OCEANEX, 1, requestId, errorId, errorMsg.c_str());
}

//对于每个撤单指令发出后30秒（可配置）内，如果没有收到回报，就给策略报错（撤单被拒绝，pls retry)
void TDEngineOceanEx::addRemoteOrderIdOrderActionSentTime(const LFOrderActionField* data, int requestId, int64_t remoteOrderId)
{
    std::lock_guard<std::mutex> guard_mutex_order_action(*mutex_orderaction_waiting_response);

    OrderActionSentTime newOrderActionSent;
    newOrderActionSent.requestId = requestId;
    newOrderActionSent.sentNameTime = getTimestamp();
    memcpy(&newOrderActionSent.data, data, sizeof(LFOrderActionField));
    remoteOrderIdOrderActionSentTime[remoteOrderId] = newOrderActionSent;
}

void TDEngineOceanEx::GetAndHandleOrderTradeResponse()
{
    //every account
    for (size_t idx = 0; idx < account_units.size(); idx++)
    {
        AccountUnitOceanEx& unit = account_units[idx];
        if (!unit.logged_in)
        {
            continue;
        }
        moveNewOrderStatusToPending(unit);
        retrieveOrderStatus(unit);
    }//end every account
}


void TDEngineOceanEx::retrieveOrderStatus(AccountUnitOceanEx& unit)
{
    KF_LOG_INFO(logger, "[retrieveOrderStatus] ");
    std::lock_guard<std::mutex> guard_mutex(*mutex_response_order_status);
    std::lock_guard<std::mutex> guard_mutex_order_action(*mutex_orderaction_waiting_response);


    std::vector<PendingOrderStatus>::iterator orderStatusIterator;

    for(orderStatusIterator = unit.pendingOrderStatus.begin(); orderStatusIterator != unit.pendingOrderStatus.end();)
    {
        KF_LOG_INFO(logger, "[retrieveOrderStatus] get_order " << "( account.api_key) "<< unit.api_key
                                                               << "  (account.pendingOrderStatus.InstrumentID) "<< orderStatusIterator->InstrumentID
                                                               <<"  (account.pendingOrderStatus.OrderRef) " << orderStatusIterator->OrderRef
                                                               <<"  (account.pendingOrderStatus.remoteOrderId) " << orderStatusIterator->remoteOrderId
                                                               <<"  (account.pendingOrderStatus.OrderStatus) " << orderStatusIterator->OrderStatus
        );

        std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(orderStatusIterator->InstrumentID));
        if(ticker.length() == 0) {
            KF_LOG_INFO(logger, "[retrieveOrderStatus]: not in WhiteList , ignore it:" << orderStatusIterator->InstrumentID);
            continue;
        }
        KF_LOG_DEBUG(logger, "[retrieveOrderStatus] (exchange_ticker)" << ticker);

        Document d;
        query_order(unit, ticker, std::to_string(orderStatusIterator->remoteOrderId), d);

        /*

{
  "code": 0,
  "data": [
    {
      "remaining_volume": "0.25",
      "trades_count": 0,
      "created_at": "2018-09-19T03:02:40Z",
      "side": "buy",
      "id": 7,
      "volume": "0.25",
      "ord_type": "limit",
      "price": "10.0",
      "avg_price": "0.0",
      "state": "wait",
      "executed_volume": "0.0",
      "market": "vetusd"
    }
  ],
  "message": "Operation is successful"
}
        */
        //parse order status
        //订单状态，﻿open（未成交）、filled（已完成）、canceled（已撤销）、cancel（撤销中）、partially-filled（部分成交）
        if(d.HasParseError()) {
            //HasParseError, skip
            KF_LOG_ERROR(logger, "[retrieveOrderStatus] get_order response HasParseError " << " (symbol)" << orderStatusIterator->InstrumentID
                                                                                           << " (orderRef)" << orderStatusIterator->OrderRef
                                                                                           << " (remoteOrderId) " << orderStatusIterator->remoteOrderId);
            continue;
        }
        if(d.HasMember("code") && d["code"].GetInt() == 0)
        {
            ResponsedOrderStatus responsedOrderStatus;
            responsedOrderStatus.ticker = ticker;
            responsedOrderStatus.averagePrice = std::round(std::stod(d["averagePrice"].GetString()) * scale_offset);
            responsedOrderStatus.orderId = orderStatusIterator->remoteOrderId;
            //报单价格条件
            responsedOrderStatus.OrderPriceType = GetPriceType(d["ord_type"].GetString());
            //买卖方向
            responsedOrderStatus.Direction = GetDirection(d["side"].GetString());
            //报单状态
            responsedOrderStatus.OrderStatus = GetOrderStatus(d["state"].GetString());
            responsedOrderStatus.price = std::round(std::stod(d["price"].GetString()) * scale_offset);
            responsedOrderStatus.volume = std::round(std::stod(d["volume"].GetString()) * scale_offset);
            //今成交数量
            responsedOrderStatus.VolumeTraded = std::round(std::stod(d["executed_volume"].GetString()) * scale_offset);
            responsedOrderStatus.openVolume = std::round(std::stod(d["remaining_volume"].GetString()) * scale_offset);

            handlerResponseOrderStatus(unit, orderStatusIterator, responsedOrderStatus);

            //OrderAction发出以后，有状态回来，就清空这次OrderAction的发送状态，不必制造超时提醒信息
            remoteOrderIdOrderActionSentTime.erase(orderStatusIterator->remoteOrderId);
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
        KF_LOG_INFO(logger, "[retrieveOrderStatus] move to next pendingOrderStatus.");
    }
}

void TDEngineOceanEx::addNewQueryOrdersAndTrades(AccountUnitOceanEx& unit, const char_31 InstrumentID,
                                                 const char_21 OrderRef, const LfOrderStatusType OrderStatus,
                                                 const uint64_t VolumeTraded, int64_t remoteOrderId)
{
    //add new orderId for GetAndHandleOrderTradeResponse
    std::lock_guard<std::mutex> guard_mutex(*mutex_order_and_trade);

    PendingOrderStatus status;
    memset(&status, 0, sizeof(PendingOrderStatus));
    strncpy(status.InstrumentID, InstrumentID, 31);
    strncpy(status.OrderRef, OrderRef, 21);
    status.OrderStatus = OrderStatus;
    status.VolumeTraded = VolumeTraded;
    status.averagePrice = 0.0;
    status.remoteOrderId = remoteOrderId;
    unit.newOrderStatus.push_back(status);
    KF_LOG_INFO(logger, "[addNewQueryOrdersAndTrades] (InstrumentID) " << status.InstrumentID
                                                                       << " (OrderRef) " << status.OrderRef
                                                                       << " (remoteOrderId) " << status.remoteOrderId
                                                                       << "(VolumeTraded)" << VolumeTraded);
}


void TDEngineOceanEx::moveNewOrderStatusToPending(AccountUnitOceanEx& unit)
{
    std::lock_guard<std::mutex> pending_guard_mutex(*mutex_order_and_trade);
    std::lock_guard<std::mutex> response_guard_mutex(*mutex_response_order_status);


    std::vector<PendingOrderStatus>::iterator newOrderStatusIterator;
    for(newOrderStatusIterator = unit.newOrderStatus.begin(); newOrderStatusIterator != unit.newOrderStatus.end();)
    {
        unit.pendingOrderStatus.push_back(*newOrderStatusIterator);
        newOrderStatusIterator = unit.newOrderStatus.erase(newOrderStatusIterator);
    }
}

void TDEngineOceanEx::set_reader_thread()
{
    ITDEngine::set_reader_thread();

    KF_LOG_INFO(logger, "[set_reader_thread] rest_thread start on TDEngineOceanEx::loop");
    rest_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineOceanEx::loop, this)));


    KF_LOG_INFO(logger, "[set_reader_thread] orderaction_timeout_thread start on TDEngineOceanEx::loopOrderActionNoResponseTimeOut");
    orderaction_timeout_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineOceanEx::loopOrderActionNoResponseTimeOut, this)));
}


void TDEngineOceanEx::loop()
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


void TDEngineOceanEx::loopOrderActionNoResponseTimeOut()
{
    KF_LOG_INFO(logger, "[loopOrderActionNoResponseTimeOut] (isRunning) " << isRunning);
    while(isRunning)
    {
        orderActionNoResponseTimeOut();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void TDEngineOceanEx::orderActionNoResponseTimeOut()
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

void TDEngineOceanEx::printResponse(const Document& d)
{
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);
    KF_LOG_INFO(logger, "[printResponse] ok (text) " << buffer.GetString());
}

void TDEngineOceanEx::getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& json)
{
    if(http_status_code == HTTP_RESPONSE_OK)
    {
        json.Parse(responseText.c_str());
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

        rapidjson::Value val;
        val.SetString(errorMsg.c_str(), errorMsg.length(), allocator);
        json.AddMember("message", val, allocator);
    }
}

std::string TDEngineOceanEx::construct_request_body(AccountUnitOceanEx& unit, std::string data)
{
    std::string header = "{\"typ\":\"JWT\",\"alg\":\"RS256\"}";
    string header_base64 = base64_encode((const unsigned char*)header.c_str(), header.length());
    std::string pay_load = "{\"uid\":\"" + unit.api_key + "\",\"data\":" + data + "}";
    string pay_load_base64 = base64_encode((const unsigned char*)pay_load.c_str(), pay_load.length());

    std::string signature = rsa256_private_encrypt(header_base64 + "." + pay_load_base64, unit.secret_key);

    std::cout  << "[construct_request_body] (signature)" << signature << std::endl;
    std::string sign = base64_encode((unsigned char*)signature.c_str(), signature.size());
    std::cout  << "[construct_request_body] (sign)" << sign << std::endl;

    std::string request_body = header_base64 + "." + pay_load_base64 + "." + sign;
    std::cout  << "[construct_request_body] (request_body)" << request_body << std::endl;
    return request_body;
}


void TDEngineOceanEx::get_account(AccountUnitOceanEx& unit, Document& json)
{
    KF_LOG_INFO(logger, "[get_account]");

    std::string requestPath = "/members/me";
    std::string queryString= "?user_jwt=RkTgU1lne1aWSBnC171j0eJe__fILSclRpUJ7SWDDulWd4QvLa0-WVRTeyloJOsjyUtduuF0K0SdkYqXR-ibuULqXEDGCGSHSed8WaNtHpvf-AyCI-JKucLH7bgQxT1yPtrJC6W31W5dQ2Spp3IEpXFS49pMD3FRFeHF4HAImo9VlPUM_bP-1kZt0l9RbzWjxVtaYbx3L8msXXyr_wqacNnIV6X9m8eie_DqZHYzGrN_25PfAFgKmghfpL-jmu53kgSyTw5v-rfZRP9VMAuryRIMvOf9LBuMaxcuFn7PjVJx8F7fcEPBCd0roMTLKhHjFidi6QxZNUO1WKSkoSbRxA"
            ;//construct_request_body(unit, "{}");

    string url = unit.baseUrl + requestPath + queryString;

    const auto response = Get(Url{url}, cpr::VerifySsl{false},
                              Header{{"Content-Type", "application/json"}}, Timeout{10000} );

    KF_LOG_INFO(logger, "[get_account] (url) " << url << " (response.status_code) " << response.status_code <<
                                              " (response.error.message) " << response.error.message <<
                                              " (response.text) " << response.text.c_str());


    return getResponse(response.status_code, response.text, response.error.message, json);
}

/*
 * {
    "market": "vetusd",
    "side": "buy",
    "volume": 0.25,
    "price": 10,
    "ord_type": "limit"
}
 * */
std::string TDEngineOceanEx::createInsertOrdertring(const char *code,
                                                    const char *side, const char *type, double size, double price)
{
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();
    writer.Key("market");
    writer.String(code);

    writer.Key("side");
    writer.String(side);

    writer.Key("volume");
    writer.Double(size);

    writer.Key("price");
    writer.Double(price);

    writer.Key("ord_type");
    writer.String(type);

    writer.EndObject();
    return s.GetString();
}

void TDEngineOceanEx::send_order(AccountUnitOceanEx& unit, const char *code,
                                 const char *side, const char *type, double size, double price, double funds, Document& json)
{
    KF_LOG_INFO(logger, "[send_order]");

    int retry_times = 0;
    cpr::Response response;
    bool should_retry = false;
    do {
        should_retry = false;

        std::string requestPath = "/orders";
        std::string queryString= "";
        std::string body = "?user_jwt=" + construct_request_body(unit, createInsertOrdertring(code, side, type, size, price));

        string url = unit.baseUrl + requestPath + queryString;

        response = Post(Url{url}, cpr::VerifySsl{false},
                        Header{{"Content-Type", "application/json; charset=UTF-8"},
                               {"Content-Length", to_string(body.size())}},
                        Body{body}, Timeout{30000});

        KF_LOG_INFO(logger, "[send_order] (url) " << url << " (body) "<< body << " (response.status_code) " << response.status_code <<
                                                  " (response.error.message) " << response.error.message <<
                                                  " (response.text) " << response.text.c_str() << " (retry_times)" << retry_times);

        //has error and find the 'error setting certificate verify locations' error, should retry
        if(shouldRetry(response.status_code, response.error.message)) {
            should_retry = true;
            retry_times++;
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_milliseconds));
        }
    } while(should_retry && retry_times < max_rest_retry_times);



    KF_LOG_INFO(logger, "[send_order] out_retry (response.status_code) " << response.status_code <<
                                                                         " (response.error.message) " << response.error.message <<
                                                                         " (response.text) " << response.text.c_str() );

    getResponse(response.status_code, response.text, response.error.message, json);
}

bool TDEngineOceanEx::shouldRetry(int http_status_code, std::string errorMsg)
{
//    if( 502 == http_status_code
//        || (errorMsg.size() > 0 && errorMsg.find("error setting certificate verify locations") >= 0)
//        || (401 == http_status_code && errorMsg.size() > 0 && errorMsg.find("Auth error") >= 0) )
//    {
//        return true;
//    }
    return false;
}

void TDEngineOceanEx::cancel_all_orders(AccountUnitOceanEx& unit, std::string code, Document& json)
{
    KF_LOG_INFO(logger, "[cancel_all_orders]");

    std::string requestPath = "/orders/clear";
    std::string queryString= "?user_jwt=RkTgU1lne1aWSBnC171j0eJe__fILSclRpUJ7SWDDulWd4QvLa0-WVRTeyloJOsjyUtduuF0K0SdkYqXR-ibuULqXEDGCGSHSed8WaNtHpvf-AyCI-JKucLH7bgQxT1yPtrJC6W31W5dQ2Spp3IEpXFS49pMD3FRFeHF4HAImo9VlPUM_bP-1kZt0l9RbzWjxVtaYbx3L8msXXyr_wqacNnIV6X9m8eie_DqZHYzGrN_25PfAFgKmghfpL-jmu53kgSyTw5v-rfZRP9VMAuryRIMvOf9LBuMaxcuFn7PjVJx8F7fcEPBCd0roMTLKhHjFidi6QxZNUO1WKSkoSbRxA";//construct_request_body(unit, "{}");

    string url = unit.baseUrl + requestPath + queryString;

    const auto response = Get(Url{url}, cpr::VerifySsl{false},
                              Header{{"Content-Type", "application/json"}}, Timeout{30000} );


    KF_LOG_INFO(logger, "[cancel_all_orders] (url) " << url << " (response.status_code) " << response.status_code <<
                                                     " (response.error.message) " << response.error.message <<
                                                     " (response.text) " << response.text.c_str());

    getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineOceanEx::cancel_order(AccountUnitOceanEx& unit, std::string code, std::string orderId, Document& json)
{
    KF_LOG_INFO(logger, "[cancel_order]");

    int retry_times = 0;
    cpr::Response response;
    bool should_retry = false;
    do {
        should_retry = false;

        std::string requestPath = "/order/delete";
        std::string queryString= "?user_jwt=" + construct_request_body(unit, "{\"id\":" + orderId + "}");

        string url = unit.baseUrl + requestPath + queryString;

        const auto response = Get(Url{url}, cpr::VerifySsl{false},
                                  Header{{"Content-Type", "application/json"}}, Timeout{30000} );

        KF_LOG_INFO(logger, "[cancel_order] (url) " << url  << " (response.status_code) " << response.status_code <<
                                                    " (response.error.message) " << response.error.message <<
                                                    " (response.text) " << response.text.c_str() << " (retry_times)" << retry_times);

        //has error and find the 'error setting certificate verify locations' error, should retry
        if(shouldRetry(response.status_code, response.error.message)) {
            should_retry = true;
            retry_times++;
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_milliseconds));
        }
    } while(should_retry && retry_times < max_rest_retry_times);


    KF_LOG_INFO(logger, "[cancel_order] out_retry " << retry_times << " (response.status_code) " << response.status_code <<
                                                                           " (response.error.message) " << response.error.message <<
                                                                           " (response.text) " << response.text.c_str() );

    getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineOceanEx::query_order(AccountUnitOceanEx& unit, std::string code, std::string orderId, Document& json)
{
    KF_LOG_INFO(logger, "[query_order]");
    std::string requestPath = "/orders";
    std::string queryString= "?user_jwt=" + construct_request_body(unit, "{\"ids\": [" + orderId + "]}");

    string url = unit.baseUrl + requestPath + queryString;

    const auto response = Get(Url{url}, cpr::VerifySsl{false},
                              Header{{"Content-Type", "application/json"}}, Timeout{30000} );

    KF_LOG_INFO(logger, "[query_order] (url) " << url << " (response.status_code) " << response.status_code <<
                                               " (response.error.message) " << response.error.message <<
                                               " (response.text) " << response.text.c_str());

    getResponse(response.status_code, response.text, response.error.message, json);
}



void TDEngineOceanEx::handlerResponseOrderStatus(AccountUnitOceanEx& unit, std::vector<PendingOrderStatus>::iterator orderStatusIterator, ResponsedOrderStatus& responsedOrderStatus)
{
    int64_t newAveragePrice = responsedOrderStatus.averagePrice;
    //cancel 需要特殊处理
    if(LF_CHAR_Canceled == responsedOrderStatus.OrderStatus) {
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
            rtn_order.OrderStatus = LF_CHAR_PartTradedQueueing;
            rtn_order.VolumeTraded = responsedOrderStatus.VolumeTraded;
            //first send onRtnOrder about the status change or VolumeTraded change
            strcpy(rtn_order.ExchangeID, "oceanex");
            strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
            strncpy(rtn_order.InstrumentID, orderStatusIterator->InstrumentID, 31);
            rtn_order.Direction = responsedOrderStatus.Direction;
            //No this setting on OceanEx
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
                                    source_id, MSG_TYPE_LF_RTN_ORDER_OCEANEX,
                                    1, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);


            //send OnRtnTrade
            LFRtnTradeField rtn_trade;
            memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
            strcpy(rtn_trade.ExchangeID, "oceanex");
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
                                    source_id, MSG_TYPE_LF_RTN_TRADE_OCEANEX, 1, -1);

        }

        //emit the LF_CHAR_Canceled status
        LFRtnOrderField rtn_order;
        memset(&rtn_order, 0, sizeof(LFRtnOrderField));
        rtn_order.OrderStatus = LF_CHAR_Canceled;
        rtn_order.VolumeTraded = responsedOrderStatus.VolumeTraded;

        //first send onRtnOrder about the status change or VolumeTraded change
        strcpy(rtn_order.ExchangeID, "oceanex");
        strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
        strncpy(rtn_order.InstrumentID, orderStatusIterator->InstrumentID, 31);
        rtn_order.Direction = responsedOrderStatus.Direction;
        //OceanEx has no this setting
        rtn_order.TimeCondition = LF_CHAR_GTC;
        rtn_order.OrderPriceType = responsedOrderStatus.OrderPriceType;
        strncpy(rtn_order.OrderRef, orderStatusIterator->OrderRef, 13);
        rtn_order.VolumeTotalOriginal = responsedOrderStatus.volume;
        rtn_order.LimitPrice = responsedOrderStatus.price;
        //剩余数量
        rtn_order.VolumeTotal = responsedOrderStatus.openVolume;

        on_rtn_order(&rtn_order);
        raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                source_id, MSG_TYPE_LF_RTN_ORDER_OCEANEX,
                                1, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);


        //third, update last status for next query_order
        orderStatusIterator->OrderStatus = rtn_order.OrderStatus;
        orderStatusIterator->VolumeTraded = rtn_order.VolumeTraded;
        orderStatusIterator->averagePrice = newAveragePrice;

    } else if(orderStatusIterator->OrderStatus != responsedOrderStatus.OrderStatus ||
              (LF_CHAR_PartTradedQueueing == responsedOrderStatus.OrderStatus
               && responsedOrderStatus.VolumeTraded != orderStatusIterator->VolumeTraded))
    {
        //if status changed or LF_CHAR_PartTradedQueueing but traded valume changes, emit onRtnOrder
        LFRtnOrderField rtn_order;
        memset(&rtn_order, 0, sizeof(LFRtnOrderField));
        rtn_order.OrderStatus = responsedOrderStatus.OrderStatus;
        rtn_order.VolumeTraded = responsedOrderStatus.VolumeTraded;

        //first send onRtnOrder about the status change or VolumeTraded change
        strcpy(rtn_order.ExchangeID, "oceanex");
        strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
        strncpy(rtn_order.InstrumentID, orderStatusIterator->InstrumentID, 31);
        rtn_order.Direction = responsedOrderStatus.Direction;
        //No this setting on OceanEx
        rtn_order.TimeCondition = LF_CHAR_GTC;
        rtn_order.OrderPriceType = responsedOrderStatus.OrderPriceType;
        strncpy(rtn_order.OrderRef, orderStatusIterator->OrderRef, 13);
        rtn_order.VolumeTotalOriginal = responsedOrderStatus.volume;
        rtn_order.LimitPrice = responsedOrderStatus.price;
        rtn_order.VolumeTotal = responsedOrderStatus.openVolume;

        on_rtn_order(&rtn_order);
        raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                source_id, MSG_TYPE_LF_RTN_ORDER_OCEANEX,
                                1, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);

        int64_t newAveragePrice = responsedOrderStatus.averagePrice;
        //second, if the status is PartTraded/AllTraded, send OnRtnTrade
        if(rtn_order.OrderStatus == LF_CHAR_AllTraded ||
           (LF_CHAR_PartTradedQueueing == rtn_order.OrderStatus
            && rtn_order.VolumeTraded != orderStatusIterator->VolumeTraded))
        {
            LFRtnTradeField rtn_trade;
            memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
            strcpy(rtn_trade.ExchangeID, "oceanex");
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
                                    source_id, MSG_TYPE_LF_RTN_TRADE_OCEANEX, 1, -1);
        }
        //third, update last status for next query_order
        orderStatusIterator->OrderStatus = rtn_order.OrderStatus;
        orderStatusIterator->VolumeTraded = rtn_order.VolumeTraded;
        orderStatusIterator->averagePrice = newAveragePrice;
    }
}

std::string TDEngineOceanEx::parseJsonToString(Document &d)
{
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);

    return buffer.GetString();
}


inline int64_t TDEngineOceanEx::getTimestamp()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp;
}

#define GBK2UTF8(msg) kungfu::yijinjing::gbk2utf8(string(msg))

BOOST_PYTHON_MODULE(liboceanextd)
{
    using namespace boost::python;
    class_<TDEngineOceanEx, boost::shared_ptr<TDEngineOceanEx> >("Engine")
            .def(init<>())
            .def("init", &TDEngineOceanEx::initialize)
            .def("start", &TDEngineOceanEx::start)
            .def("stop", &TDEngineOceanEx::stop)
            .def("logout", &TDEngineOceanEx::logout)
            .def("wait_for_stop", &TDEngineOceanEx::wait_for_stop);
}
