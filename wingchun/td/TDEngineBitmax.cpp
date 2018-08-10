#include "TDEngineBitmax.h"
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
using utils::crypto::base64_decode;

USING_WC_NAMESPACE

TDEngineBitmax::TDEngineBitmax(): ITDEngine(SOURCE_BITMAX)
{
    logger = yijinjing::KfLog::getLogger("TradeEngine.Bitmax");
    KF_LOG_INFO(logger, "[TDEngineBitmax]");

    mutex_order_and_trade = new std::mutex();
}

TDEngineBitmax::~TDEngineBitmax()
{
    if(mutex_order_and_trade != nullptr) delete mutex_order_and_trade;
}

void TDEngineBitmax::init()
{
    ITDEngine::init();
    JournalPair tdRawPair = getTdRawJournalPair(source_id);
    raw_writer = yijinjing::JournalWriter::create(tdRawPair.first, tdRawPair.second, "RAW_" + name());
    KF_LOG_INFO(logger, "[init]");
}

void TDEngineBitmax::pre_load(const json& j_config)
{
    KF_LOG_INFO(logger, "[pre_load]");
}

void TDEngineBitmax::resize_accounts(int account_num)
{
    account_units.resize(account_num);
    KF_LOG_INFO(logger, "[resize_accounts]");
}

TradeAccount TDEngineBitmax::load_account(int idx, const json& j_config)
{
    KF_LOG_INFO(logger, "[load_account]");
    // internal load
    string api_key = j_config["APIKey"].get<string>();
    string secret_key = j_config["SecretKey"].get<string>();

    string baseUrl = j_config["baseUrl"].get<string>();
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();

    AccountUnitCoinmex& unit = account_units[idx];
    unit.api_key = api_key;
    unit.secret_key = secret_key;
    unit.baseUrl = baseUrl;

    KF_LOG_INFO(logger, "[load_account] (api_key)" << api_key << " (baseUrl)" << unit.baseUrl);

    unit.whiteList.ReadWhiteLists(j_config);

    unit.whiteList.Debug_print();

    //display usage:
    if(unit.whiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "TDEngineBitmax::load_account: subscribeCoinmexBaseQuote is empty. please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"BTC/USDT\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"ETC/ETH\"");
        KF_LOG_ERROR(logger, "},");
    }

    //cancel all openning orders on TD startup

    Document d;
    query_orders(unit, d);
    KF_LOG_INFO(logger, "[load_account] print get_open_orders");
    printResponse(d);

    if(!d.HasParseError() && d.IsObject() && d.HasMember("data") && d["data"].IsArray()) { // expected success response is array
        size_t len = d["data"].Size();
        KF_LOG_INFO(logger, "[load_account][get_open_orders] (length)" << len);
        for (int i = 0; i < len; i++) {
            if(d["data"].GetArray()[i].IsObject() && d["data"].GetArray()[i].HasMember("symbol") && d["data"].GetArray()[i].HasMember("coid"))
            {
                if(d.GetArray()[i]["symbol"].IsString() && d.GetArray()[i]["clientOrderId"].IsString())
                {
                    std::string symbol = d["data"].GetArray()[i]["symbol"].GetString();
                    std::string orderRef = d["data"].GetArray()[i]["coid"].GetString();
                    Document cancelResponse;
                    cancel_order(unit, symbol.c_str(), 0, orderRef.c_str(), cancelResponse);

                    KF_LOG_INFO(logger, "[load_account] cancel_order:");
                    printResponse(cancelResponse);
                    int errorId = 0;
                    std::string errorMsg = "";
                    if(d.HasParseError() )
                    {
                        errorId = 100;
                        errorMsg = "cancel_order http response has parse error. please check the log";
                        KF_LOG_ERROR(logger, "[load_account] cancel_order error! (rid)  -1 (errorId)" << errorId << " (errorMsg) " << errorMsg);
                    }
                    if(!cancelResponse.HasParseError() && cancelResponse.IsObject() && cancelResponse.HasMember("status") && strcmp("error", cancelResponse["status"].GetString() ) == 0)
                    {
                        errorId = 100;
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
    
    //test
    Document accountJson;
    get_account(unit, accountJson);
    KF_LOG_INFO(logger, "[load_account] get_account:");
    printResponse(accountJson);


    // set up
    TradeAccount account = {};
    //partly copy this fields
    strncpy(account.UserID, api_key.c_str(), 16);
    strncpy(account.Password, secret_key.c_str(), 21);
    return account;
}


void TDEngineBitmax::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[connect]");
    for (int idx = 0; idx < account_units.size(); idx ++)
    {
        AccountUnitCoinmex& unit = account_units[idx];
        KF_LOG_INFO(logger, "[connect] (api_key)" << unit.api_key);
        if (!unit.logged_in)
        {
            //exchange infos
            Document doc;
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
            unit.logged_in = true;
        }
    }

    KF_LOG_INFO(logger, "[connect] rest_thread start on TDEngineBitmax::loop");
    rest_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineBitmax::loop, this)));
}

bool TDEngineBitmax::loadExchangeOrderFilters(AccountUnitCoinmex& unit, Document &doc)
{
    KF_LOG_INFO(logger, "[loadExchangeOrderFilters]");

    if(doc.HasParseError() || doc.IsObject())
    {
        return false;
    }
    if(doc.IsArray())
    {
        size_t productCount = doc.Size();
        for (size_t i = 0; i < productCount; i++) {
            const rapidjson::Value& prod = doc.GetArray()[i];
            if(prod.IsObject() && prod.HasMember("product") && prod["product"].IsObject()) {
                std::string symbol = prod["product"]["symbol"].GetString();
                int tickSize = prod["product"]["pricePrecision"].GetInt();

                KF_LOG_INFO(logger, "[loadExchangeOrderFilters] sendOrderFilters (symbol)" << symbol <<
                                                                                           " (tickSize)" << tickSize);
                SendOrderFilter afilter;
                strncpy(afilter.InstrumentID, symbol.c_str(), 31);
                afilter.ticksize = tickSize;
                unit.sendOrderFilters.insert(std::make_pair(symbol, afilter));
            }
        }
    }
    return true;
}

void TDEngineBitmax::debug_print(std::map<std::string, SendOrderFilter> &sendOrderFilters)
{
    std::map<std::string, SendOrderFilter>::iterator map_itr = sendOrderFilters.begin();
    while(map_itr != sendOrderFilters.end())
    {
        KF_LOG_INFO(logger, "[debug_print] sendOrderFilters (symbol)" << map_itr->first <<
                                                                      " (tickSize)" << map_itr->second.ticksize);
        map_itr++;
    }
}

SendOrderFilter TDEngineBitmax::getSendOrderFilter(AccountUnitCoinmex& unit, const char *symbol)
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
    defaultFilter.ticksize = 8;
    strcpy(defaultFilter.InstrumentID, "notfound");
    return defaultFilter;
}

void TDEngineBitmax::login(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[login]");
    connect(timeout_nsec);
}

void TDEngineBitmax::logout()
{
    KF_LOG_INFO(logger, "[logout]");
}

void TDEngineBitmax::release_api()
{
    KF_LOG_INFO(logger, "[release_api]");
}

bool TDEngineBitmax::is_logged_in() const
{
    KF_LOG_INFO(logger, "[is_logged_in]");
    for (auto& unit: account_units)
    {
        if (!unit.logged_in)
            return false;
    }
    return true;
}

bool TDEngineBitmax::is_connected() const
{
    KF_LOG_INFO(logger, "[is_connected]");
    return is_logged_in();
}



std::string TDEngineBitmax::GetSide(const LfDirectionType& input) {
    if (LF_CHAR_Buy == input) {
        return "buy";
    } else if (LF_CHAR_Sell == input) {
        return "sell";
    } else {
        return "";
    }
}

LfDirectionType TDEngineBitmax::GetDirection(std::string input) {
    if ("buy" == input) {
        return LF_CHAR_Buy;
    } else if ("sell" == input) {
        return LF_CHAR_Sell;
    } else {
        return LF_CHAR_Buy;
    }
}

std::string TDEngineBitmax::GetType(const LfOrderPriceTypeType& input) {
    if (LF_CHAR_LimitPrice == input) {
        return "limit";
    } else if (LF_CHAR_AnyPrice == input) {
        return "market";
    } else {
        return "";
    }
}

LfOrderPriceTypeType TDEngineBitmax::GetPriceType(std::string input) {
    if ("limit" == input) {
        return LF_CHAR_LimitPrice;
    } else if ("market" == input) {
        return LF_CHAR_AnyPrice;
    } else {
        return '0';
    }
}

LfOrderStatusType TDEngineBitmax::GetOrderStatus(std::string input) {
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
void TDEngineBitmax::req_investor_position(const LFQryPositionField* data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId);

    AccountUnitCoinmex& unit = account_units[account_index];
    KF_LOG_INFO(logger, "[req_investor_position] (api_key)" << unit.api_key << " (InstrumentID) " << data->InstrumentID);

    int errorId = 0;
    std::string errorMsg = "";
    Document d;
    get_account(unit, d);
/*
 {
	"email": "spencer.fan@sino-danish.com",
	"status": "success",
	"data": [{
		"assetCode": "LTC",
		"assetName": "Litecoin",
		"totalAmount": "0",
		"availableAmount": "0",
		"inOrderAmount": "0",
		"maxWithdrawAmount": "0",
		"btcValue": "0"
	}, {
		"assetCode": "BTC",
		"assetName": "Bitcoin",
		"totalAmount": "0",
 * */
    if(d.HasParseError() || !d.IsObject() )
    {
        errorId = 100;
        errorMsg = " response HasParseError or is not a json";
        KF_LOG_ERROR(logger, "[req_investor_position] failed!" << " (rid)" << requestId << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
    } else if(d.HasMember("status") || strcmp("error", d["status"].GetString()) == 0 )
    {
        errorId = 100;
        errorMsg = "response status error";
        KF_LOG_ERROR(logger, "[req_investor_position] failed!" << " (rid)" << requestId << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }
    send_writer->write_frame(data, sizeof(LFQryPositionField), source_id, MSG_TYPE_LF_QRY_POS_BITMAX, 1, requestId);

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

    std::vector<LFRspPositionField> tmp_vector;
    if(!d.HasParseError() && d.HasMember("data") && d["data"].IsArray())
    {
        size_t len = d["data"].Size();
        KF_LOG_INFO(logger, "[req_investor_position] (asset.length)" << len);
        for(int i = 0; i < len; i++)
        {
            std::string symbol = d["data"].GetArray()[i]["assetCode"].GetString();
            if(unit.whiteList.HasSymbolInWhiteList(symbol))
            {
                strncpy(pos.InstrumentID, symbol.c_str(), 31);
                KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId << " (symbol) " << symbol
                                                                          << " availableAmount:" << d["data"].GetArray()[i]["availableAmount"].GetString()
                                                                          << " totalAmount: " << d["data"].GetArray()[i]["totalAmount"].GetString()
                                                                          << " maxWithdrawAmount: " << d["data"].GetArray()[i]["maxWithdrawAmount"].GetString());
                pos.Position = std::round(std::stod(d["data"].GetArray()[i]["availableAmount"].GetString()) * scale_offset);
                tmp_vector.push_back(pos);
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
        raw_writer->write_error_frame(&pos, sizeof(LFRspPositionField), source_id, MSG_TYPE_LF_RSP_POS_BITMAX, 1, requestId, errorId, errorMsg.c_str());
    }
}

void TDEngineBitmax::req_qry_account(const LFQryAccountField *data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_qry_account]");
}

int64_t TDEngineBitmax::fixPriceTickSize(int keepPrecision, int64_t price, bool isBuy) {
    if(keepPrecision == 8) return price;

    int removePrecisions = (8 - keepPrecision);
    double cutter = pow(10, removePrecisions);

    KF_LOG_INFO(logger, "[fixPriceTickSize input]" << " 1(price)" << std::fixed  << std::setprecision(9) << price);
    double new_price = price/cutter;
    KF_LOG_INFO(logger, "[fixPriceTickSize input]" << " 2(price/cutter)" << std::fixed  << std::setprecision(9) << new_price);
    if(isBuy){
        new_price += 0.9;
        new_price = std::floor(new_price);
        KF_LOG_INFO(logger, "[fixPriceTickSize input]" << " 3(price is buy)" << std::fixed  << std::setprecision(9) << new_price);
    } else {
        new_price = std::floor(new_price);
        KF_LOG_INFO(logger, "[fixPriceTickSize input]" << " 3(price is sell)" << std::fixed  << std::setprecision(9) << new_price);
    }
    int64_t  ret_price = new_price * cutter;
    KF_LOG_INFO(logger, "[fixPriceTickSize input]" << " 4(new_price * cutter)" << std::fixed  << std::setprecision(9) << new_price);
    return ret_price;
}


void TDEngineBitmax::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitCoinmex& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_insert]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Tid)" << data->InstrumentID
                                              << " (Volume)" << data->Volume
                                              << " (LimitPrice)" << data->LimitPrice
                                              << " (OrderRef)" << data->OrderRef);
    send_writer->write_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BITMAX, 1/*ISLAST*/, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.whiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_insert]: not in WhiteList, ignore it  (rid)" << requestId <<
                                                                                      " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BITMAX, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_insert] (exchange_ticker)" << ticker);

    Document d;

    SendOrderFilter filter = getSendOrderFilter(unit, ticker.c_str());

    int64_t fixedPrice = fixPriceTickSize(filter.ticksize, data->LimitPrice, LF_CHAR_Buy == data->Direction);

    KF_LOG_DEBUG(logger, "[req_order_insert] SendOrderFilter  (Tid)" << ticker <<
                                                                     " (LimitPrice)" << data->LimitPrice <<
                                                                     " (ticksize)" << filter.ticksize <<
                                                                     " (fixedPrice)" << fixedPrice);
    send_order(unit, ticker.c_str(), data->OrderRef, GetSide(data->Direction).c_str(),
               GetType(data->OrderPriceType).c_str(), data->Volume*1.0/scale_offset, fixedPrice*1.0/scale_offset, d);

    //not expected response
    if(d.HasParseError() || !d.IsObject())
    {
        errorId = 100;
        errorMsg = "send_order http response has parse error or is not json. please check the log";
        KF_LOG_ERROR(logger, "[req_order_insert] send_order error!  (rid)" << requestId << " (errorId)" <<
                                                                           errorId << " (errorMsg) " << errorMsg);
    } else if(d.HasMember("status") && strcmp("error", d["status"].GetString()) == 0)
    {
        errorId = 100;
        errorMsg = d["msg"].GetString();
        KF_LOG_ERROR(logger, "[req_order_insert] send_order error!  (rid)" << requestId << " (errorId)" <<
                                                                           errorId << " (errorMsg) " << errorMsg);
    } else if(d.HasMember("status") && strcmp("success", d["status"].GetString()) == 0)
    {
        //success
    }

    if(errorId != 0)
    {
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
    }
    raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BITMAX, 1, requestId, errorId, errorMsg.c_str());
}


void TDEngineBitmax::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitCoinmex& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_action]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Iid)" << data->InvestorID
                                              << " (OrderRef)" << data->OrderRef
                                              << " (KfOrderID)" << data->KfOrderID);

    send_writer->write_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BITMAX, 1, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.whiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_action]: not in WhiteList , ignore it: (rid)" << requestId << " (errorId)" <<
                                                                                       errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BITMAX, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_action] (exchange_ticker)" << ticker);

    Document d;
    cancel_order(unit, ticker.c_str(), getTimestampString().c_str(), data->OrderRef, d);

    //not expected response
    if(d.HasParseError() || !d.IsObject()) {
        errorId = 100;
        errorMsg = "cancel_order http response has parse error or is not json. please check the log";
        KF_LOG_ERROR(logger, "[req_order_action] cancel_order error!  (rid)" << requestId << " (errorId)" <<
                                                                           errorId << " (errorMsg) " << errorMsg);
    } else if(d.HasMember("status") && strcmp("error", d["status"].GetString()) == 0)
    {
        errorId = 100;
        errorMsg = d["msg"].GetString();
        KF_LOG_ERROR(logger, "[req_order_action] send_order error!  (rid)" << requestId << " (errorId)" <<
                                                                           errorId << " (errorMsg) " << errorMsg);
    } else if(d.HasMember("status") && strcmp("success", d["status"].GetString()) == 0)
    {
        //success
    }

    if(errorId != 0)
    {
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
    }
    raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BITMAX, 1, requestId, errorId, errorMsg.c_str());
}

void TDEngineBitmax::loop()
{
    while(isRunning) {
    }

}


std::vector<std::string> TDEngineBitmax::split(std::string str, std::string token)
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

void TDEngineBitmax::printResponse(const Document& d)
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


void TDEngineBitmax::getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& json)
{
    json.Parse(responseText.c_str());
}


void TDEngineBitmax::get_account(AccountUnitCoinmex& unit, Document& json)
{
    KF_LOG_INFO(logger, "[get_account]");
    std::string Timestamp = getTimestampString();
    std::string Method = "GET";
    std::string requestPath = "balances";
    std::string queryString= "";
    std::string body = "";
    string Message = Timestamp + "+" + requestPath;

    std::string secret_key_64decode = base64_decode(unit.secret_key);
    unsigned char* signature = hmac_sha256_byte(secret_key_64decode.c_str(), Message.c_str());
    string url = unit.baseUrl + requestPath;
    std::string sign = base64_encode(signature, 32);

    const auto response = Get(Url{url},
                              Header{{"x-auth-key", unit.api_key},
                                     {"Content-Type", "application/json"},
                                     {"x-auth-signature", sign},
                                     {"x-auth-timestamp",  Timestamp}}, Timeout{10000} );

    KF_LOG_INFO(logger, "[get_account] (url) " << url << " (response) " << response.text.c_str());
    //[]
    return getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineBitmax::get_products(AccountUnitCoinmex& unit, Document& json)
{
    /*
  [
     {
    "product" : {
      "symbol" : "BCH/BTC",
      "baseAsset" : "BCH",
      "quoteAsset" : "BTC",
      "statusCode" : "Normal",
      "statusMessage" : "",
      "qtyScale" : 5,
      "priceScale" : 6,
      "pricePrecision" : 6,
      "notionalScale" : 6,
      "minQty" : "0.01",
      "maxQty" : "10000.00",
      "minNotional" : "0.001",
      "maxNotional" : "1000.000"
    },
    "baseAsset" : {
      "assetId" : "iYROwQmWeUxxpjaIFuc5UM3GMLhJLlnr",
      "assetCode" : "BCH",
      "assetName" : "Bitcoin Cash",
      "unit" : "",
      "website" : "",
      "logoUrl" : "",
      "precisionScale" : 5,
      "numConfirmations" : 3,
      "withdrawalFee" : 0.01,
      "minWithdrawalAmt" : 0.01,
      "statusCode" : "Normal",
      "statusMessage" : ""
    },
    "quoteAsset" : {
      "assetId" : "hEbYqvUNuKeYKVKIaFIdUNkSxGnL4wjz",
      "assetCode" : "BTC",
      "assetName" : "Bitcoin",
      "unit" : "",
      "website" : "",
      "logoUrl" : "",
      "precisionScale" : 6,
      "numConfirmations" : 2,
      "withdrawalFee" : 0.001,
      "minWithdrawalAmt" : 0.01,
      "statusCode" : "Normal",
      "statusMessage" : ""
    }
  },
  {
    "product" : {
      "symbol" : "LTC/BTC",
      "baseAsset" : "LTC",



     * */
    KF_LOG_INFO(logger, "[get_products]");
//    std::string Timestamp = getTimestampString();
//    std::string Method = "GET";
    std::string requestPath = "products";
//    std::string queryString= "";
//    std::string body = "";
//    string Message = Timestamp + "+" + requestPath;

    string url = unit.baseUrl + requestPath;

    const auto response = Get(Url{url},
                              Header{
                                     {"Content-Type", "application/json"},
                                     }, Timeout{10000} );

    KF_LOG_INFO(logger, "[get_products] (url) " << url << " (response) " << response.text.c_str());
    return getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineBitmax::send_order(AccountUnitCoinmex& unit, const char *code, const char *coid,
                                     const char *side, const char *type, double size, double price, Document& json)
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
                                               type << " (size) "<< sizeStr << " (price) "<< priceStr << " (time) " << time);

    std::string Timestamp = getTimestampString();

    Document document;
    document.SetObject();
    Document::AllocatorType& allocator = document.GetAllocator();
    //used inner this method only.so  can use reference
    document.AddMember("coid", StringRef(coid), allocator);

    rapidjson::Value timeVal;
    timeVal.SetString(Timestamp.c_str(), Timestamp.length(), allocator);
    json.AddMember("time", timeVal, allocator);

    document.AddMember("symbol", StringRef(code), allocator);
    document.AddMember("side", StringRef(side), allocator);
    document.AddMember("orderType", StringRef(type), allocator);
    document.AddMember("orderQty", StringRef(sizeStr.c_str()), allocator);
    document.AddMember("orderPrice", StringRef(priceStr.c_str()), allocator);
    StringBuffer jsonStr;
    Writer<StringBuffer> writer(jsonStr);
    document.Accept(writer);


    std::string Method = "POST";
    std::string requestPath = "order/new";
    std::string queryString= "";
    std::string body = jsonStr.GetString();

    string Message = Timestamp + "+" + requestPath;
    std::string secret_key_64decode = base64_decode(unit.secret_key);
    unsigned char* signature = hmac_sha256_byte(secret_key_64decode.c_str(), Message.c_str());
    string url = unit.baseUrl + requestPath + queryString;
    std::string sign = base64_encode(signature, 32);

    const auto response = Post(Url{url},
                               Header{{"x-auth-key", unit.api_key},
                                      {"Content-Type", "application/json; charset=UTF-8"},
                                      {"Content-Length", to_string(body.size())},
                                      {"x-auth-signature", sign},
                                      {"x-auth-timestamp",  Timestamp}},
                               Body{body}, Timeout{30000});

    //an error:
    //(response.status_code) 0 (response.error.message) Failed to connect to www.bitmore.top port 443: Connection refused (response.text)
    KF_LOG_INFO(logger, "[send_order] (url) " << url << " (body) "<< body << " (response.status_code) " << response.status_code <<
                                              " (response.error.message) " << response.error.message <<
                                              " (response.text) " << response.text.c_str());
    getResponse(response.status_code, response.text, response.error.message, json);
}


void TDEngineBitmax::cancel_order(AccountUnitCoinmex& unit, const char *code, const char *coid, const char *origCoid, Document& json)
{
    KF_LOG_INFO(logger, "[cancel_order]");
    std::string Timestamp = getTimestampString();

    rapidjson::Document document;
    document.SetObject();
    rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
    //used inner this method only.so  can use reference
    document.AddMember("coid", StringRef(coid), allocator);

    rapidjson::Value timeVal;
    timeVal.SetString(Timestamp.c_str(), Timestamp.length(), allocator);
    json.AddMember("time", timeVal, allocator);

    document.AddMember("origCoid", StringRef(origCoid), allocator);
    document.AddMember("symbol", StringRef(code), allocator);

    StringBuffer jsonStr;
    Writer<StringBuffer> writer(jsonStr);
    document.Accept(writer);


    std::string Method = "POST";
    std::string requestPath = "order/cancel";
    std::string queryString= "";
    std::string body = jsonStr.GetString();
    string Message = Timestamp + "+" + requestPath;

    std::string secret_key_64decode = base64_decode(unit.secret_key);
    unsigned char* signature = hmac_sha256_byte(secret_key_64decode.c_str(), Message.c_str());
    string url = unit.baseUrl + requestPath + queryString;
    std::string sign = base64_encode(signature, 32);
    const auto response = Post(Url{url},
                                 Header{{"x-auth-key", unit.api_key},
                                        {"Content-Type", "application/json; charset=UTF-8"},
                                        {"Content-Length", to_string(body.size())},
                                        {"x-auth-signature", sign},
                                        {"x-auth-timestamp",  Timestamp}},
                                 Body{body}, Timeout{30000});

    KF_LOG_INFO(logger, "[cancel_order] (url) " << url  << " (body) "<< body << " (response.status_code) " << response.status_code <<
                                                " (response.error.message) " << response.error.message <<
                                                " (response.text) " << response.text.c_str());
    getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineBitmax::query_orders(AccountUnitCoinmex& unit, Document& json)
{
    KF_LOG_INFO(logger, "[query_orders]");


    std::string Timestamp = getTimestampString();
    std::string Method = "GET";
    std::string requestPath = "orders/open";
    std::string queryString= "?page=1&pagesize=500";
    std::string body = "";
    string Message = Timestamp + "+" + requestPath;

    std::string secret_key_64decode = base64_decode(unit.secret_key);
    unsigned char* signature = hmac_sha256_byte(secret_key_64decode.c_str(), Message.c_str());
    string url = unit.baseUrl + requestPath + queryString;
    std::string sign = base64_encode(signature, 32);
    const auto response = Get(Url{url},
                              Header{{"x-auth-key", unit.api_key},
                                     //{"Content-Type", "application/json"},
                                     {"x-auth-signature", sign},
                                     {"x-auth-timestamp",  Timestamp}},
                              Body{body}, Timeout{10000});

    KF_LOG_INFO(logger, "[query_orders] (x-auth-timestamp) " << Timestamp);

    KF_LOG_INFO(logger, "[query_orders] (url) " << url << " (response.status_code) " << response.status_code <<
                                                " (response.error.message) " << response.error.message <<
                                                " (response.text) " << response.text.c_str());
    getResponse(response.status_code, response.text, response.error.message, json);
}

inline int64_t TDEngineBitmax::getTimestamp()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp;
}

std::string TDEngineBitmax::getTimestampString()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    //timestamp =  timestamp - timeDiffOfExchange;
    return std::to_string(timestamp);
}

#define GBK2UTF8(msg) kungfu::yijinjing::gbk2utf8(string(msg))

BOOST_PYTHON_MODULE(libbitmaxtd)
{
    using namespace boost::python;
    class_<TDEngineBitmax, boost::shared_ptr<TDEngineBitmax> >("Engine")
            .def(init<>())
            .def("init", &TDEngineBitmax::initialize)
            .def("start", &TDEngineBitmax::start)
            .def("stop", &TDEngineBitmax::stop)
            .def("logout", &TDEngineBitmax::logout)
            .def("wait_for_stop", &TDEngineBitmax::wait_for_stop);
}
