#include "TDEngineCoinmex.h"
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

TDEngineCoinmex::TDEngineCoinmex(): ITDEngine(SOURCE_COINMEX)
{
    logger = yijinjing::KfLog::getLogger("TradeEngine.Coinmex");
    KF_LOG_INFO(logger, "[TDEngineCoinmex]");

    mutex_order_and_trade = new std::mutex();
}

TDEngineCoinmex::~TDEngineCoinmex()
{
    if(mutex_order_and_trade != nullptr) delete mutex_order_and_trade;
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

    KF_LOG_INFO(logger, "[load_account] (api_key)" << api_key << " (baseUrl)" << unit.baseUrl);
    KF_LOG_INFO(logger, "[load_account] (api_key)" << api_key << " (is whiteListInstrumentIDs exist?)" << (j_config.find("whiteListInstrumentIDs") != j_config.end()));

    if(j_config.find("whiteListInstrumentIDs") != j_config.end()) {
        string whiteListInstrumentIDs = j_config["whiteListInstrumentIDs"].get<string>();
        if(whiteListInstrumentIDs.length() > 0)
        {
            KF_LOG_INFO(logger, "[load_account] (api_key)" << api_key << " (whiteListInstrumentIDs)" << whiteListInstrumentIDs);
            unit.whiteListInstrumentIDs = split(whiteListInstrumentIDs, ",");
            if(unit.whiteListInstrumentIDs.size() > 0)
            {
                for(int i=0; i < unit.whiteListInstrumentIDs.size(); i++)
                {
                    KF_LOG_INFO(logger, "[load_account] (api_key)" << api_key << " (cancel_all_orders of instrumentID)" << unit.whiteListInstrumentIDs[i]);
                    Document d;
                    cancel_all_orders(unit, unit.whiteListInstrumentIDs[i], d);
                    printResponse(d);
                }
            }
        }
    }
//test
    /*
    Document d;
    get_exchange_time(unit, d);
    KF_LOG_INFO(logger, "[print get_exchange_time]");
    printResponse(d);

    get_account(unit, d);
    KF_LOG_INFO(logger, "[print get_account]");
    printResponse(d);

    get_products(unit, d);
    KF_LOG_INFO(logger, "[print get_products]");
    printResponse(d);

    get_depth(unit, "LTC_BTC", d);
    KF_LOG_INFO(logger, "[print get_depth]");
    printResponse(d);

    std::string code="LTC_BTC";
    std::string side="buy";
    std:string type="market";
    double size=0;
    double price=0.0000;
    double funds=0;
    send_order(unit, code.c_str(), side.c_str(), type.c_str(), size, price, funds, d);
    KF_LOG_INFO(logger, "[print send_order]");
    printResponse(d);

    query_orders(unit, "LTC_BTC", "open", d);
    KF_LOG_INFO(logger, "[print query_orders]");
    printResponse(d);

//----------------------
    query_order(unit, "LTC_BTC", 20269511L, d);
    KF_LOG_INFO(logger, "[print query_order]");
    printResponse(d);

    cancel_order(unit, "LTC_BTC", 20269511L, d);
    KF_LOG_INFO(logger, "[print cancel_order]");
    printResponse(d);

    query_order(unit, "LTC_BTC", 20269511L, d);
    KF_LOG_INFO(logger, "[print query_order]");
    printResponse(d);
//----------------------
    query_order(unit, "LTC_BTC", 20321247L, d);
    KF_LOG_INFO(logger, "[print query_order]");
    printResponse(d);

    cancel_order(unit, "LTC_BTC", 20321247L, d);
    KF_LOG_INFO(logger, "[print cancel_order]");
    printResponse(d);

    query_order(unit, "LTC_BTC", 20321247L, d);
    KF_LOG_INFO(logger, "[print query_order]");
    printResponse(d);
//----------------------
*/
   /*
    Document d;
    cancel_all_orders(unit, "MVP_BTC", d);
    KF_LOG_INFO(logger, "[print cancel_all_orders]");
    printResponse(d);
    */
/*
    query_orders(unit, "LTC_BTC", "open", d);
    KF_LOG_INFO(logger, "[print query_orders]");
    printResponse(d);
*/

//test end

    // set up
    TradeAccount account = {};
    //partly copy this fields
    strncpy(account.UserID, api_key.c_str(), 16);
    strncpy(account.Password, secret_key.c_str(), 21);
    return account;
}

void TDEngineCoinmex::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[connect]");
    for (int idx = 0; idx < account_units.size(); idx ++)
    {
        AccountUnitCoinmex& unit = account_units[idx];
        KF_LOG_INFO(logger, "[connect] (api_key)" << unit.api_key);
        if (!unit.logged_in)
        {
            Document d;
            get_exchange_time(unit, d);
            if(d.HasMember("timestamp")) {
                Value& s = d["timestamp"];
                KF_LOG_INFO(logger, "[connect] (response.timestamp.type) " << s.GetType() << " (response.timestamp) " << d["timestamp"].GetInt64());
                unit.logged_in = true;
            }
        }
    }

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
        return "buy";
    } else if (LF_CHAR_Sell == input) {
        return "sell";
    } else {
        return "";
    }
}

LfDirectionType TDEngineCoinmex::GetDirection(std::string input) {
    if ("buy" == input) {
        return LF_CHAR_Buy;
    } else if ("sell" == input) {
        return LF_CHAR_Sell;
    } else {
        return LF_CHAR_Buy;
    }
}

std::string TDEngineCoinmex::GetType(const LfOrderPriceTypeType& input) {
    if (LF_CHAR_LimitPrice == input) {
        return "limit";
    } else if (LF_CHAR_AnyPrice == input) {
        return "market";
    } else {
        return "";
    }
}

LfOrderPriceTypeType TDEngineCoinmex::GetPriceType(std::string input) {
    if ("limit" == input) {
        return LF_CHAR_LimitPrice;
    } else if ("market" == input) {
        return LF_CHAR_AnyPrice;
    } else {
        return '0';
    }
}
//订单状态，﻿open（未成交）、filled（已完成）、canceled（已撤销）、cancel（撤销中）、partially-filled（部分成交）
LfOrderStatusType TDEngineCoinmex::GetOrderStatus(std::string input) {
    if ("open" == input) {
        return LF_CHAR_NotTouched;
    } else if ("partially-filled" == input) {
        return LF_CHAR_PartTradedNotQueueing;
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
void TDEngineCoinmex::req_investor_position(const LFQryPositionField* data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId);

    AccountUnitCoinmex& unit = account_units[account_index];
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
    send_writer->write_frame(data, sizeof(LFQryPositionField), source_id, MSG_TYPE_LF_QRY_POS_COINMEX, 1, requestId);

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
    bool findSymbolInResult = false;

/*
 # Response
    [{"available":"0.099","balance":"0.099","currencyCode":"BTC","hold":"0","id":83906},{"available":"188","balance":"188","currencyCode":"MVP","hold":"0","id":83906}]
 * */
    if(d.IsArray())
    {
        size_t len = d.Size();
        KF_LOG_INFO(logger, "[req_investor_position] (asset.length)" << len);
        for(int i = 0; i < len; i++)
        {
            std::string symbol = d.GetArray()[i]["currencyCode"].GetString();
            strncpy(pos.InstrumentID, symbol.c_str(), 31);
            KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId << " (symbol) " << symbol
                                                                   << " available:" << d.GetArray()[i]["available"].GetString()
                                                                   << " balance: " << d.GetArray()[i]["balance"].GetString()
                                                                   << " hold: " << d.GetArray()[i]["hold"].GetString());
            pos.Position = std::stod(d.GetArray()[i]["available"].GetString()) * scale_offset;
            on_rsp_position(&pos, i == (len - 1), requestId, errorId, errorMsg.c_str());
            KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId << " (symbol) " << symbol << " (position) " << pos.Position);
            findSymbolInResult = true;
        }
    }

    if(!findSymbolInResult)
    {
        KF_LOG_INFO(logger, "[req_investor_position] (!findSymbolInResult) (requestId)" << requestId);
        on_rsp_position(&pos, 1, requestId, errorId, errorMsg.c_str());
    }
    raw_writer->write_error_frame(&pos, sizeof(LFRspPositionField), source_id, MSG_TYPE_LF_RSP_POS_COINMEX, 1, requestId, errorId, errorMsg.c_str());
}

void TDEngineCoinmex::req_qry_account(const LFQryAccountField *data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_qry_account]");
}

void TDEngineCoinmex::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitCoinmex& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_insert]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Tid)" << data->InstrumentID
                                              << " (Volume)" << data->Volume
                                              << " (LimitPrice)" << data->LimitPrice
                                              << " (OrderRef)" << data->OrderRef);

    double funds = 0;
    Document d;
    send_order(unit, data->InstrumentID, GetSide(data->Direction).c_str(),
            GetType(data->OrderPriceType).c_str(), data->Volume*1.0/scale_offset, data->LimitPrice*1.0/scale_offset, funds, d);

    int errorId = 0;
    std::string errorMsg = "";


    if(d.HasMember("orderId") && d.HasMember("result"))
    {
        std::string remoteOrderId = std::to_string(d["orderId"].GetInt64());
        localOrderRefRemoteOrderId.insert(std::make_pair(std::string(data->OrderRef), remoteOrderId));
        KF_LOG_INFO(logger, "[req_order_insert] after send (OrderRef) " << data->OrderRef << " (remoteOrderId) " << remoteOrderId);
        if(d.HasMember("result") && d["result"].IsBool())
        {
            if(d["result"].GetBool()){
                /*
                 * # Response OK
                    {
                        "result": true,
                        "order_id": 123456
                    }
                 * */
                //if send successful and the exchange has received ok, then add to  pending query order list
                char noneStatus = '\0';//none
                addNewQueryOrdersAndTrades(unit, data->InstrumentID, data->OrderRef, noneStatus, 0);
            } else {
                /*
                 * # Response error
                    {
                        "result": false,
                        "order_id": 123456
                    }
                 * */
                //send successful BUT the exchange has received fail
                errorId = 200;
                errorMsg = "http.code is 200, but result is false";
                on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
                raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_COINMEX, 1, requestId, errorId, errorMsg.c_str());
            }
        }
    }

    send_writer->write_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_COINMEX, 1/*ISLAST*/, requestId);

    if (errorId == 0 && d.HasMember("code")) {
        //send error, examle: http timeout.
        {
            errorId = d["code"].GetInt();
            if(d.HasMember("message") && d["message"].IsString())
            {
                errorMsg = d["message"].GetString();
            }
            KF_LOG_ERROR(logger, "[req_order_insert] failed!" << " (rid)" << requestId << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        }
    }

    if(errorId != 0)
    {
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_COINMEX, 1, requestId, errorId, errorMsg.c_str());
    }

}


void TDEngineCoinmex::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitCoinmex& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_action]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Iid)" << data->InvestorID
                                              << " (OrderRef)" << data->OrderRef
                                              << " (KfOrderID)" << data->KfOrderID);
    int errorId = 0;
    std::string errorMsg = "";

    std::map<std::string, std::string>::iterator itr = localOrderRefRemoteOrderId.find(data->OrderRef);
    std::string remoteOrderId;
    if(itr == localOrderRefRemoteOrderId.end()) {
        KF_LOG_ERROR(logger, "[req_order_action] not found in localOrderRefRemoteOrderId map (orderRef) " << data->OrderRef);
        errorId = 1;
        std::stringstream ss;
        ss << "[req_order_action] not found in localOrderRefRemoteOrderId map (orderRef) " << data->OrderRef;
        errorMsg = ss.str();
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_COINMEX, 1, requestId, errorId, errorMsg.c_str());
        return;
    } else {
        remoteOrderId = itr->second;
        KF_LOG_ERROR(logger, "[req_order_action] found in localOrderRefRemoteOrderId map (orderRef) " << data->OrderRef << " (remoteOrderId) " << remoteOrderId);
    }


    Document d;
    cancel_order(unit, data->InstrumentID, stod(remoteOrderId), d);
    send_writer->write_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_COINMEX, 1, requestId);

    if(!d.HasParseError() && d.HasMember("code") && d["code"].IsNumber())
    {
        errorId = d["code"].GetInt();
        if(d.HasMember("message") && d["message"].IsString())
        {
            errorMsg = d["message"].GetString();
        }
        KF_LOG_ERROR(logger, "[req_order_action] cancel_order failed!" << " (rid)" << requestId << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }
    if(errorId != 0)
    {
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_COINMEX, 1, requestId, errorId, errorMsg.c_str());
    }
}

void TDEngineCoinmex::GetAndHandleOrderTradeResponse()
{
    //every account
    for (int idx = 0; idx < account_units.size(); idx ++)
    {
        AccountUnitCoinmex& unit = account_units[idx];
        if (!unit.logged_in)
        {
            continue;
        }
        moveNewtoPending(unit);
        retrieveOrderStatus(unit);
    }//end every account
}


void TDEngineCoinmex::moveNewtoPending(AccountUnitCoinmex& unit)
{
    std::lock_guard<std::mutex> guard_mutex(*mutex_order_and_trade);

    std::vector<PendingCoinmexOrderStatus>::iterator newOrderStatusIterator;
    for(newOrderStatusIterator = unit.newOrderStatus.begin(); newOrderStatusIterator != unit.newOrderStatus.end();)
    {
        unit.pendingOrderStatus.push_back(*newOrderStatusIterator);
        newOrderStatusIterator = unit.newOrderStatus.erase(newOrderStatusIterator);
    }
}

void TDEngineCoinmex::retrieveOrderStatus(AccountUnitCoinmex& unit)
{
    KF_LOG_INFO(logger, "[retrieveOrderStatus] ");
    std::vector<PendingCoinmexOrderStatus>::iterator orderStatusIterator;
    int indexNum = 0;
    for(orderStatusIterator = unit.pendingOrderStatus.begin(); orderStatusIterator != unit.pendingOrderStatus.end(); orderStatusIterator++)
    {
        indexNum++;
        KF_LOG_INFO(logger, "[retrieveOrderStatus] get_order [" << indexNum <<"]    (account.api_key)"<< unit.api_key
                                                                << "  (account.pendingOrderStatus.InstrumentID) "<< orderStatusIterator->InstrumentID
                                                                <<"  (account.pendingOrderStatus.OrderRef) " << orderStatusIterator->OrderRef
                                                                <<"  (account.pendingOrderStatus.OrderStatus) " << orderStatusIterator->OrderStatus
        );
    }

    for(orderStatusIterator = unit.pendingOrderStatus.begin(); orderStatusIterator != unit.pendingOrderStatus.end();)
    {
        KF_LOG_INFO(logger, "[retrieveOrderStatus] get_order " << "( account.api_key) "<< unit.api_key
                                                               << "  (account.pendingOrderStatus.InstrumentID) "<< orderStatusIterator->InstrumentID
                                                               <<"  (account.pendingOrderStatus.OrderRef) " << orderStatusIterator->OrderRef
                                                               <<"  (account.pendingOrderStatus.OrderStatus) " << orderStatusIterator->OrderStatus
        );


        std::map<std::string, std::string>::iterator itr = localOrderRefRemoteOrderId.find(orderStatusIterator->OrderRef);
        std::string remoteOrderId;
        if(itr == localOrderRefRemoteOrderId.end()) {
            KF_LOG_ERROR(logger, "[retrieveOrderStatus] not found in localOrderRefRemoteOrderId map (orderRef) " << orderStatusIterator->OrderRef);
            continue;
        } else {
            remoteOrderId = itr->second;
            KF_LOG_INFO(logger, "[retrieveOrderStatus] found in localOrderRefRemoteOrderId map (orderRef) " << orderStatusIterator->OrderRef << " (remoteOrderId) " << remoteOrderId);
        }



        Document d;
        query_order(unit, orderStatusIterator->InstrumentID, stod(remoteOrderId), d);

        /*
 # Response

{
	"averagePrice": "0",
	"code": "MVP_BTC",
	"createdDate": 1530417365000,
	"filledVolume": "0",
	"funds": "0",
	"orderId": 20283535,
	"orderType": "limit",
	"price": "0.00000001",
	"side": "buy",
	"status": "open",
	"volume": "1"
}

返回值说明
返回字段 	字段说明
averagePrice 	订单已成交部分均价，如果未成交则为0
code 	币对如btc-usdt
createDate 	创建订单的时间戳
filledVolume 	订单已成交数量
funds 	订单已成交金额
orderId 	订单代码
price 	订单委托价
side 	订单交易方向
status 	订单状态
volume 	订单委托数量
        */
        //parse order status
        //订单状态，﻿open（未成交）、filled（已完成）、canceled（已撤销）、cancel（撤销中）、partially-filled（部分成交）
        if(d.HasParseError()) {
            //HasParseError, skip
            KF_LOG_ERROR(logger, "[retrieveOrderStatus] get_order response HasParseError " << " (symbol)" << orderStatusIterator->InstrumentID
                                                                   << " (orderRef)" << orderStatusIterator->OrderRef
                                                                   << " (remoteOrderId) " << remoteOrderId);
            continue;
        }
        if(d.HasMember("status"))
        {
            /*
             {
                "averagePrice": "0.00000148",
                "code": "MVP_BTC",
                "createdDate": 1530439964000,
                "filledVolume": "1",
                "funds": "0",
                "orderId": 20644648,
                "orderType": "limit",
                "price": "0.00001111",
                "side": "buy",
                "status": "filled",
                "volume": "1"
            }
             * */
            //parse success
            LFRtnOrderField rtn_order;
            memset(&rtn_order, 0, sizeof(LFRtnOrderField));
            rtn_order.OrderStatus = GetOrderStatus(d["status"].GetString());
            rtn_order.VolumeTraded = std::stod(d["filledVolume"].GetString()) * scale_offset;

            //if status changed or LF_CHAR_PartTradedNotQueueing but traded valume changes, emit onRtnOrder
            if(orderStatusIterator->OrderStatus != rtn_order.OrderStatus ||
               (LF_CHAR_PartTradedNotQueueing == rtn_order.OrderStatus
                && rtn_order.VolumeTraded != orderStatusIterator->VolumeTraded))
            {
                //first send onRtnOrder about the status change or VolumeTraded change
                strcpy(rtn_order.ExchangeID, "coinmex");
                strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
                strncpy(rtn_order.InstrumentID, d["code"].GetString(), 31);
                rtn_order.Direction = GetDirection(d["side"].GetString());
                //No this setting on coinmex
                rtn_order.TimeCondition = LF_CHAR_GFD;
                rtn_order.OrderPriceType = GetPriceType(d["orderType"].GetString());
                strncpy(rtn_order.OrderRef, orderStatusIterator->OrderRef, 13);
                rtn_order.VolumeTotalOriginal = std::stod(d["volume"].GetString()) * scale_offset;
                rtn_order.LimitPrice = std::stod(d["price"].GetString()) * scale_offset;
                rtn_order.VolumeTotal = rtn_order.VolumeTotalOriginal - rtn_order.VolumeTraded;

                on_rtn_order(&rtn_order);
                raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                        source_id, MSG_TYPE_LF_RTN_ORDER_COINMEX,
                                        1, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);

                uint64_t newAveragePrice = std::stod(d["averagePrice"].GetString()) * scale_offset;
                //second, if the status is PartTraded/AllTraded, send OnRtnTrade
                if(rtn_order.OrderStatus == LF_CHAR_AllTraded ||
                    (LF_CHAR_PartTradedNotQueueing == rtn_order.OrderStatus
                    && rtn_order.VolumeTraded != orderStatusIterator->VolumeTraded))
                {
                    LFRtnTradeField rtn_trade;
                    memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
                    strcpy(rtn_trade.ExchangeID, "coinmex");
                    strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
                    strncpy(rtn_trade.InstrumentID, d["code"].GetString(), 31);
                    strncpy(rtn_trade.OrderRef, orderStatusIterator->OrderRef, 13);
                    rtn_trade.Direction = rtn_order.Direction;
                    uint64_t oldAmount = orderStatusIterator->VolumeTraded * orderStatusIterator->averagePrice;
                    uint64_t newAmount = rtn_order.VolumeTraded * newAveragePrice;

                    //calculate the volumn and price (it is average too)
                    rtn_trade.Volume = rtn_order.VolumeTraded - orderStatusIterator->VolumeTraded;
                    rtn_trade.Price = (newAmount - oldAmount)/(rtn_order.VolumeTraded - orderStatusIterator->VolumeTraded);

                    on_rtn_trade(&rtn_trade);
                    raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
                                            source_id, MSG_TYPE_LF_RTN_TRADE_BINANCE, 1, -1);
                }
                //third, update last status for next query_order
                orderStatusIterator->OrderStatus = rtn_order.OrderStatus;
                orderStatusIterator->VolumeTraded = rtn_order.VolumeTraded;
                orderStatusIterator->averagePrice = newAveragePrice;
            }
        } else {
            int errorId = 0;
            std::string errorMsg = "";
            //no status, it must be a Error response. see details in getResponse(...)
            if(d.HasMember("code") && d["code"].IsInt()) {
                errorId = d["code"].GetInt();
            }
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


void TDEngineCoinmex::addNewQueryOrdersAndTrades(AccountUnitCoinmex& unit, const char_31 InstrumentID,
                                                 const char_21 OrderRef, const LfOrderStatusType OrderStatus, const uint64_t VolumeTraded)
{
    //add new orderId for GetAndHandleOrderTradeResponse
    std::lock_guard<std::mutex> guard_mutex(*mutex_order_and_trade);

    PendingCoinmexOrderStatus status;
    memset(&status, 0, sizeof(PendingCoinmexOrderStatus));
    strncpy(status.InstrumentID, InstrumentID, 31);
    strncpy(status.OrderRef, OrderRef, 21);
    status.OrderStatus = OrderStatus;
    status.VolumeTraded = VolumeTraded;
    status.averagePrice = 0.0;
    unit.newOrderStatus.push_back(status);
    KF_LOG_INFO(logger, "[addNewQueryOrdersAndTrades] (InstrumentID) " << InstrumentID
                                                                       << " (OrderRef) " << OrderRef
                                                                       << "(VolumeTraded)" << VolumeTraded);
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
        GetAndHandleOrderTradeResponse();
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

void TDEngineCoinmex::printResponse(const Document& d)
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
void TDEngineCoinmex::getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& json)
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
        if(d.IsObject()) {
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
        }
    }
}

void TDEngineCoinmex::get_exchange_time(AccountUnitCoinmex& unit, Document& json)
{
    KF_LOG_INFO(logger, "[get_exchange_time]");
    std::string Timestamp = getTimestampString();
    std::string Method = "GET";
    std::string requestPath = "/api/v1/spot/public/time";
    std::string queryString= "";
    std::string body = "";
    string Message = Timestamp + Method + requestPath + queryString + body;
    string url = unit.baseUrl + requestPath + queryString;
    const auto response = Get(Url{url}, Parameters{}, Timeout{10000});
    KF_LOG_INFO(logger, "[get_exchange_time] (url) " << url << " (response) " << response.text.c_str());
    return getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineCoinmex::get_account(AccountUnitCoinmex& unit, Document& json)
{
    KF_LOG_INFO(logger, "[get_account]");
    std::string Timestamp = getTimestampString();
    std::string Method = "GET";
    std::string requestPath = "/api/v1/spot/ccex/account/assets";
    std::string queryString= "";
    std::string body = "";
    string Message = Timestamp + Method + requestPath + queryString + body;

    unsigned char* signature = hmac_sha256_byte(unit.secret_key.c_str(), Message.c_str());
    string url = unit.baseUrl + requestPath;
    std::string sign = base64_encode(signature, 32);

    const auto response = Get(Url{url},
                              Header{{"ACCESS-KEY", unit.api_key}, {"ACCESS-PASSPHRASE", unit.passphrase},
                                     {"Content-Type", "application/json"},
                                     {"ACCESS-SIGN", sign},
                                     {"ACCESS-TIMESTAMP",  Timestamp}}, Timeout{10000} );

    KF_LOG_INFO(logger, "[get_account] (url) " << url << " (response) " << response.text.c_str());
    //[]
    return getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineCoinmex::get_products(AccountUnitCoinmex& unit, Document& json)
{
 /*
[{
	"baseCurrency": "LTC",
	"baseMaxSize": "100000.00",
	"baseMinSize": "0.001",
	"code": "LTC_BTC",
	"quoteCurrency": "BTC",
	"quoteIncrement": "8"
}, {
	"baseCurrency": "BCH",
	"baseMaxSize": "100000.00",
	"baseMinSize": "0.001",
	"code": "BCH_BTC",
	"quoteCurrency": "BTC",
	"quoteIncrement": "8"
}, {
	"baseCurrency": "ETH",
	"baseMaxSize": "100000.00",
	"baseMinSize": "0.001",
	"code": "ETH_BTC",
	"quoteCurrency": "BTC",
	"quoteIncrement": "8"
}.......]
  * */
    KF_LOG_INFO(logger, "[get_products]");
    std::string Timestamp = getTimestampString();
    std::string Method = "GET";
    std::string requestPath = "/api/v1/spot/public/products";
    std::string queryString= "";
    std::string body = "";
    string Message = Timestamp + Method + requestPath + queryString + body;

    unsigned char* signature = hmac_sha256_byte(unit.secret_key.c_str(), Message.c_str());
    string url = unit.baseUrl + requestPath;
    std::string sign = base64_encode(signature, 32);
    const auto response = Get(Url{url},
                              Header{{"ACCESS-KEY", unit.api_key}, {"ACCESS-PASSPHRASE", unit.passphrase},
                                     {"Content-Type", "application/json"},
                                     {"ACCESS-SIGN", sign},
                                     {"ACCESS-TIMESTAMP",  Timestamp}}, Timeout{10000} );

    KF_LOG_INFO(logger, "[get_products] (url) " << url << " (response) " << response.text.c_str());
    //
    return getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineCoinmex::send_order(AccountUnitCoinmex& unit, const char *code,
                                     const char *side, const char *type, double size, double price, double funds, Document& json)
{
    KF_LOG_INFO(logger, "[send_order]");

    //check funds
    if(strcmp("market", type) == 0 && strcmp("buy", side) == 0)
    {
/*
    {
        "asks": [
            ["0.01304566", "0.51385531"],
            ["0.01310131", "2.20822955"],
* */
        /*  DO NOT SUPPORT MARKET-BUY , for it required calculate funds.
        Document d = get_depth(unit, code);
        if(d.IsObject() && d.HasMember("asks"))
        {
            double currentPrice = 0;
            if(strcmp("buy", side) == 0) {
                currentPrice = std::stod(d["asks"].GetArray()[0][0].GetString());
            } else {
                currentPrice = std::stod(d["bids"].GetArray()[0][0].GetString());
            }
            KF_LOG_INFO(logger, "[send_order] (currentPrice) " << std::setprecision(8) << currentPrice);
            funds = size * price /currentPrice;

        } else {
            //error, return getResponse(0, "", "get_depth error");  ???
            return d;
        }
        */
        getResponse(0, "", "market buy is not supported!", json);
        return;
    }

    if(strcmp("limit", type) == 0)
    {
        if(price == 0 || size == 0) {
            KF_LOG_ERROR(logger, "[send_order] limit order, price or size cannot be null");
            getResponse(0, "", "price or size cannot be null", json);
            return;
        }

    } else if(strcmp("market", type) == 0) {
        if(strcmp("buy", side) == 0 &&  funds == 0) {
            KF_LOG_ERROR(logger, "[send_order] market order, type is buy, the funds cannot be null");
            getResponse(0, "", "market order, type is buy, the funds cannot be null", json);
            return;
        }
        if(strcmp("sell", side) == 0 &&  size == 0) {
            KF_LOG_ERROR(logger, "[send_order] market order, type is sell, the size cannot be null");
            getResponse(0, "", "market order, type is sell, the size cannot be null", json);
            return;
        }
    }

    std::string priceStr;
    std::stringstream convertPriceStream;
    convertPriceStream <<std::fixed << std::setprecision(8) << price;
    convertPriceStream >> priceStr;

    std::string sizeStr;
    std::stringstream convertSizeStream;
    convertSizeStream <<std::fixed << std::setprecision(8) << size;
    convertSizeStream >> sizeStr;

    std::string fundsStr;
    std::stringstream convertFundsStream;
    convertFundsStream <<std::fixed << std::setprecision(8) << funds;
    convertFundsStream >> fundsStr;

    KF_LOG_INFO(logger, "[send_order] (code) " << code << " (side) "<< side << " (type) " <<
                                               type << " (size) "<< sizeStr << " (price) "<< priceStr << " (funds) " << fundsStr);

    Document document;
    document.SetObject();
    Document::AllocatorType& allocator = document.GetAllocator();
    //used inner this method only.so  can use reference
    document.AddMember("code", StringRef(code), allocator);
    document.AddMember("side", StringRef(side), allocator);
    document.AddMember("type", StringRef(type), allocator);
    document.AddMember("size", StringRef(sizeStr.c_str()), allocator);
    document.AddMember("price", StringRef(priceStr.c_str()), allocator);
    document.AddMember("funds", StringRef(fundsStr.c_str()), allocator);
    StringBuffer jsonStr;
    Writer<StringBuffer> writer(jsonStr);
    document.Accept(writer);

    std::string Timestamp = getTimestampString();
    std::string Method = "POST";
    std::string requestPath = "/api/v1/spot/ccex/orders";
    std::string queryString= "";
    std::string body = jsonStr.GetString();

    string Message = Timestamp + Method + requestPath + queryString + body;
    unsigned char* signature = hmac_sha256_byte(unit.secret_key.c_str(), Message.c_str());
    string url = unit.baseUrl + requestPath + queryString;
    std::string sign = base64_encode(signature, 32);

    const auto response = Post(Url{url},
                               Header{{"ACCESS-KEY", unit.api_key}, {"ACCESS-PASSPHRASE", unit.passphrase},
                                      {"Content-Type", "application/json; charset=UTF-8"},
                                      {"Content-Length", to_string(body.size())},
                                      {"ACCESS-SIGN", sign},
                                      {"ACCESS-TIMESTAMP",  Timestamp}},
                               Body{body}, Timeout{10000});

    //an error:
    //(response.status_code) 0 (response.error.message) Failed to connect to www.bitmore.top port 443: Connection refused (response.text)
    KF_LOG_INFO(logger, "[send_order] (url) " << url << " (response.status_code) " << response.status_code <<
                                              " (response.error.message) " << response.error.message <<
                                              " (response.text) " << response.text.c_str());
    getResponse(response.status_code, response.text, response.error.message, json);
}


void TDEngineCoinmex::cancel_all_orders(AccountUnitCoinmex& unit, std::string code, Document& json)
{
    KF_LOG_INFO(logger, "[cancel_all_orders]");
    rapidjson::Document document;
    document.SetObject();
    rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
    //used inner this method only.so  can use reference
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
    std::string sign = base64_encode(signature, 32);
    const auto response = Delete(Url{url},
                                 Header{{"ACCESS-KEY", unit.api_key}, {"ACCESS-PASSPHRASE", unit.passphrase},
                                        {"Content-Type", "application/json; charset=UTF-8"},
                                        {"Content-Length", to_string(body.size())},
                                        {"ACCESS-SIGN", sign},
                                        {"ACCESS-TIMESTAMP",  Timestamp}},
                                 Body{body}, Timeout{10000});

    KF_LOG_INFO(logger, "[cancel_all_orders] (url) " << url << " (response.status_code) " << response.status_code <<
                                                     " (response.error.message) " << response.error.message <<
                                                     " (response.text) " << response.text.c_str());
    getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineCoinmex::get_depth(AccountUnitCoinmex& unit, std::string code, Document& json)
{
    KF_LOG_INFO(logger, "[get_depth]");
/*
返回字段 	字段说明
asks 	卖方深度  AskPrice1/AskVolume1
bids 	买方深度
 # Response
    {
	"asks": [
		["0.01304566", "0.51385531"],
		["0.01310131", "2.20822955"],
		["0.01312757", "1.92059042"],
		["0.01314095", "0.53782524"],
		["0.01315399", "0.4179756"],
		["0.01318462", "0.44793801"],
		["0.01323127", "0.90336663"],
		["0.01327756", "0.75954707"],
		["0.01332584", "0.63969743"],
		["0.01334785", "0.65168239"],
		["0.01338953", "0.53782524"],
		["0.0135845", "0.84943429"],
		["0.01371617", "0.00653215"],
		["0.0138774", "0.37602823"],
		["0.01393799", "0.83744933"],
		["0.01414", "0.51385531"],
		["0.014645", "0.73557714"],
		["0.0150692", "0.59775006"],
		["0.01515", "0.38202071"],
		["0.016059", "0.85542678"],
		["0.016261", "0.81947188"]
	],
	"bids": [
		["0.01276962", "0.67158993"],
		["0.01275003", "1.9113995"],
		["0.01272479", "2.26019503"],
		["0.01272478", "0.69061514"],
		["0.01272395", "0.63353951"],
		["0.01270817", "0.43694567"],
		["0.01266133", "0.9062342"],
		["0.01266132", "0.34816135"],
		["0.01261785", "0.57012214"],
		["0.0125765", "0.93794288"],
		["0.01257649", "0.93160115"],
		["0.01254665", "0.35450309"],
		["0.012375", "0.60183083"],
		["0.011979", "0.77939946"],
		["0.01188", "0.6145143"],
		["0.011682", "0.70964035"],
		["0.011385", "0.65890646"],
		["0.01088999", "0.59548909"],
		["0.0106623", "0.36084482"],
		["0.010494", "0.53207172"],
		["0.00000111", "0.07112221"]
	]
}
 * */
    std::string Timestamp = getTimestampString();
    std::string Method = "GET";
    std::string requestPath = "/api/v1/spot/public/products/" + code + "/orderbook";
    std::string queryString= "";
    std::string body = "";
    string Message = Timestamp + Method + requestPath + queryString + body;
    unsigned char* signature = hmac_sha256_byte(unit.secret_key.c_str(), Message.c_str());
    string url = unit.baseUrl + requestPath + queryString;
    std::string sign = base64_encode(signature, 32);
    const auto response = Get(Url{url},
                                 Header{{"ACCESS-KEY", unit.api_key}, {"ACCESS-PASSPHRASE", unit.passphrase},
                                        {"Content-Type", "application/json"},
                                        {"ACCESS-SIGN", sign},
                                        {"ACCESS-TIMESTAMP",  Timestamp}},
                                 Body{body}, Timeout{10000});

    KF_LOG_INFO(logger, "[get_depth] (url) " << url << " (response.status_code) " << response.status_code <<
                                             " (response.error.message) " << response.error.message <<
                                             " (response.text) " << response.text.c_str());
    //{"asks":[],"bids":[]}
    getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineCoinmex::cancel_order(AccountUnitCoinmex& unit, std::string code, long orderId, Document& json)
{
    KF_LOG_INFO(logger, "[cancel_order]");
    rapidjson::Document document;
    document.SetObject();
    rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
    //used inner this method only.so  can use reference
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
    std::string sign = base64_encode(signature, 32);
    const auto response = Delete(Url{url},
                                 Header{{"ACCESS-KEY", unit.api_key}, {"ACCESS-PASSPHRASE", unit.passphrase},
                                        {"Content-Type", "application/json; charset=UTF-8"},
                                        {"Content-Length", to_string(body.size())},
                                        {"ACCESS-SIGN", sign},
                                        {"ACCESS-TIMESTAMP",  Timestamp}},
                                 Body{body}, Timeout{10000});

    KF_LOG_INFO(logger, "[cancel_order] (url) " << url << " (response.status_code) " << response.status_code <<
                                                " (response.error.message) " << response.error.message <<
                                                " (response.text) " << response.text.c_str());
    getResponse(response.status_code, response.text, response.error.message, json);
}

//订单状态，﻿open（未成交）、filled（已完成）、canceled（已撤销）、cancel（撤销中）、partially-filled（部分成交）
void TDEngineCoinmex::query_orders(AccountUnitCoinmex& unit, std::string code, std::string status, Document& json)
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
    std::string sign = base64_encode(signature, 32);
    const auto response = Get(Url{url},
                              Header{{"ACCESS-KEY", unit.api_key}, {"ACCESS-PASSPHRASE", unit.passphrase},
                                     {"Content-Type", "application/json"},
                                     {"ACCESS-SIGN", sign},
                                     {"ACCESS-TIMESTAMP",  Timestamp}},
                              Body{body}, Timeout{10000});

    KF_LOG_INFO(logger, "[query_orders] (url) " << url << " (response.status_code) " << response.status_code <<
                                                " (response.error.message) " << response.error.message <<
                                                " (response.text) " << response.text.c_str());
    getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineCoinmex::query_order(AccountUnitCoinmex& unit, std::string code, long orderId, Document& json)
{
    KF_LOG_INFO(logger, "[query_order]");
/*
 # Response
    {
        "averagePrice":"0",
        "code":"chp-eth",
        "createdDate":9887828,
        "filledVolume":"0",
        "funds":"0",
        "orderId":9865872,
        "orderType":"limit",
        "price":"0.00001",
        "side":"buy",
        "status":"canceled",
        "volume":"1"
    }
* */
    std::string Timestamp = getTimestampString();
    std::string Method = "GET";
    std::string requestPath = "/api/v1/spot/ccex/orders/" + std::to_string(orderId);
    std::string queryString= "?code=" + code;
    std::string body = "";
    string Message = Timestamp + Method + requestPath + queryString + body;
    unsigned char* signature = hmac_sha256_byte(unit.secret_key.c_str(), Message.c_str());
    string url = unit.baseUrl + requestPath + queryString;
    std::string sign = base64_encode(signature, 32);
    const auto response = Get(Url{url},
                              Header{{"ACCESS-KEY", unit.api_key}, {"ACCESS-PASSPHRASE", unit.passphrase},
                                     {"Content-Type", "application/json"},
                                     {"ACCESS-SIGN", sign},
                                     {"ACCESS-TIMESTAMP",  Timestamp}},
                              Body{body}, Timeout{10000});

    KF_LOG_INFO(logger, "[query_order] (url) " << url << " (response.status_code) " << response.status_code <<
                                               " (response.error.message) " << response.error.message <<
                                               " (response.text) " << response.text.c_str());
    //(response.status_code) 404 (response.error.message)  (response.text) {"message":"Order does not exist"}
    getResponse(response.status_code, response.text, response.error.message, json);
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
