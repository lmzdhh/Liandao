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
#include "/liandao/utils/rapidjson/include/document.h"  
#include "/liandao/utils/rapidjson/include/filewritestream.h"  
#include "/liandao/utils/rapidjson/include/prettywriter.h"  
#include "/liandao/utils/rapidjson/include/stringbuffer.h"  

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

using namespace std;  
using namespace rapidjson;
USING_WC_NAMESPACE
USING_YJJ_NAMESPACE

#define PROBIT_BTC_USDT      "BTC-USDT"
#define PROBIT_ETH_USDT      "ETH-USDT"
#define PROBIT_EOS_USDT      "EOS-USDT"
#define PARTIAL_TRADED       "partialtraded"

std::mutex  g_orderMutex;
std::mutex  g_postMutex;
std::mutex  g_requestMutex;
std::condition_variable g_requestCond;

const int HTTP_RESPONSE_OK = 200;
const int g_RequestGap = 5*60;
const int scale_offset = 1e8;
const int g_gpTimes = 24 * 60 * 60*1000;

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
{
    isRunning = false;
    if(m_wsLoopThread)
    {
        if (m_wsLoopThread->joinable())
        {
            m_wsLoopThread->join();
        }
    }
    if(m_requestThread)
    {
        g_requestCond.notify_all();
        if (m_requestThread->joinable())
        {
            m_requestThread->join();
        }
    }
}

void TDEngineProbit::init()
{
    ITDEngine::init();
    JournalPair td_raw_pair = getTdRawJournalPair(source_id);
    raw_writer = yijinjing::JournalWriter::create(td_raw_pair.first, td_raw_pair.second, "RAW_" + name());
    genUniqueKey();
    KF_LOG_INFO(logger, "[init], uniqueKey:" << m_uniqueKey << ",engineIndex:" << m_engineIndex);
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
    if (j_config.find("retryCounts") != j_config.end())
    {
        m_retryCounts = j_config["retryCounts"].get<int>();
    }
    if (j_config.find("retryIntervalMs") != j_config.end())
    {
        m_retryIntervalMs = j_config["retryIntervalMs"].get<int>();
    }
    m_restIntervalms = j_config["rest_get_interval_ms"].get<int>();
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
    if(unit.coinPairWhiteList.Size() == 0)
    {
        KF_LOG_ERROR(logger, "[load_account] CoinPairWhiteList is empty");
        exit(0);
    }
    // set up
    TradeAccount account {};
    //partly copy this fields
    strncpy(account.UserID, unit.api_key .c_str(), sizeof(account.UserID)-1);
    strncpy(account.Password, unit.secret_key.c_str(), sizeof(account.Password)-1);
    return account;
}

void TDEngineProbit::connect(long timeout_nsec)
{
    for (int idx = 0; idx < account_units.size();  ++idx)
    {
        AccountUnitProbit& unit = account_units[idx];
        KF_LOG_INFO(logger, "[connect] (api_key)" << unit.api_key);
        if (!unit.logged_in)
        {
            /*Document doc;
            get_products(unit, doc);
            printResponse(doc);*/
            Document doc;
            get_exchange_infos(unit, doc);
            loadExchangeOrderFilters(unit, doc);
            debug_print(m_sendOrderFilters);
            lws_login(unit, 0);
        }
    }
}

void TDEngineProbit::get_exchange_infos(AccountUnitProbit& unit, Document &json)
{
    KF_LOG_INFO(logger, "[get_exchange_infos]");
    long recvWindow = 5000;
//    std::string Timestamp = getTimestampString();
    std::string Method = "GET";
    std::string requestPath = "https://api.probit.com/api/exchange/v1/market";
    std::string queryString("");
    std::string body = "";

    string url = requestPath + queryString;

    const auto response = Get(Url{url},
                              //Header{{"X-MBX-APIKEY", unit.api_key}},
                              Body{body}, Timeout{100000});

    KF_LOG_INFO(logger, "[get_exchange_infos] (url) " << url << " (response.status_code) " << response.status_code <<
                                                   " (response.error.message) " << response.error.message <<
                                                   " (response.text) " << response.text.c_str());
    return getResponse(response.status_code, response.text, response.error.message, json);
}

bool TDEngineProbit::loadExchangeOrderFilters(AccountUnitProbit& unit, Document &doc)
{
    SendOrderFilter filter;
    m_sendOrderFilters[PROBIT_BTC_USDT] = {PROBIT_BTC_USDT, 1};
    m_sendOrderFilters[PROBIT_ETH_USDT] = {PROBIT_ETH_USDT, 2};
    m_sendOrderFilters[PROBIT_EOS_USDT] = {PROBIT_EOS_USDT, 4};

    if(doc["data"].IsArray())
    {
        int len = doc["data"].Size();
        Value &node = doc["data"];       
        for (int i = 0; i < len; i++) {
/*            const rapidjson::Value& sym = doc["data"].GetArray()[i];
            std::string symbol = sym["data"].GetString();*/
            std::string symbol = node.GetArray()[i]["id"].GetString();
            SendOrderFilter afilter;
            std::string tickSizeStr = node.GetArray()[i]["price_increment"].GetString();
            unsigned int locStart = tickSizeStr.find( ".", 0 );
            unsigned int locEnd = tickSizeStr.find( "1", 0 );
            if( locStart != string::npos  && locEnd != string::npos ){
            int num = locEnd - locStart;
            afilter.ticksize = num;
            afilter.stepsize = node.GetArray()[i]["quantity_precision"].GetInt();
            afilter.costsize = node.GetArray()[i]["cost_precision"].GetInt();
            unit.sendOrderFilters.insert(std::make_pair(symbol, afilter));
            }
            else{
                int num = 0;
                afilter.ticksize = num;
                afilter.stepsize = node.GetArray()[i]["quantity_precision"].GetInt();
                afilter.costsize = node.GetArray()[i]["cost_precision"].GetInt();
                unit.sendOrderFilters.insert(std::make_pair(symbol, afilter));
            }
            //afilter.stepsize = node.GetArray()[i]["quantity_precision"].GetInt();
            //afilter.costsize = node.GetArray()[i]["cost_precision"].GetInt();
//            unit.sendOrderFilters.insert(std::make_pair(symbol, afilter));
        
        
    }
    }


    return true;
}

SendOrderFilter TDEngineProbit::getSendOrderFilter1(AccountUnitProbit& unit, const char *symbol)
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
   /* strcpy(defaultFilter.InstrumentID, "notfound");
    return defaultFilter;*/
}

void TDEngineProbit::debug_print(const std::map<std::string, SendOrderFilter> &sendOrderFilters)
{
    for(auto filterIter = sendOrderFilters.begin(); filterIter != sendOrderFilters.end();  ++filterIter)
    {
        KF_LOG_INFO(logger, "[debug_print] sendOrderFilters (symbol)" << filterIter->first << " (tickSize)" << filterIter->second.ticksize);
    }
}

SendOrderFilter TDEngineProbit::getSendOrderFilter(const std::string& symbol)
{
    auto filterIter = m_sendOrderFilters.find(symbol);
    if (filterIter != m_sendOrderFilters.end())
    {
        return filterIter->second;
    }
    return SendOrderFilter {"DefaultTicker", 8};
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
    for (auto& unit: account_units)
    {
        if (!unit.logged_in)
        {
            KF_LOG_DEBUG(logger, "[is_logged_in] TD logout");
            return false;
        }
    }
    KF_LOG_DEBUG(logger, "[is_logged_in] TD login");
    return true;
}

bool TDEngineProbit::is_connected() const
{
    KF_LOG_INFO(logger, "[is_connected]");
    return is_logged_in();
}

std::string TDEngineProbit::GetSide(const LfDirectionType& input) {
    if (LF_CHAR_Buy == input)
    {
        return "buy";
    }
    else if (LF_CHAR_Sell == input)
    {
        return "sell";
    }
    else
    {
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
//鐠併垹宕熼悩鑸碘偓渚婄礉閿樼釜pen閿涘牊婀幋鎰唉閿涘鈧公illed閿涘牆鍑＄€瑰本鍨氶敍澶堚偓涔nceled閿涘牆鍑￠幘銈夋敘閿?
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

void TDEngineProbit::req_investor_position(const LFQryPositionField* data, int account_index, int requestId)
{
    KF_LOG_DEBUG(logger, "[req_investor_position] (requestId)" << requestId);

    AccountUnitProbit& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_investor_position] (api_key)" << unit.api_key << " (InstrumentID) " << data->InstrumentID);

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

    std::vector<LFRspPositionField> tmp_vector;
    if(d.HasMember("data") && d["data"].IsArray())
    {
        auto& dataArray = d["data"];
        size_t len = dataArray.Size();
        KF_LOG_DEBUG(logger, "[req_investor_position] (asset.length)" << len);
        for(size_t i = 0; i < len; i++)
        {
            std::string symbol = dataArray[i]["currency_id"].GetString();
            std::string ticker = unit.positionWhiteList.GetKeyByValue(symbol);
            if(ticker.length() > 0)
            {
                strncpy(pos.InstrumentID, ticker.c_str(), 31);
                pos.Position = std::round(std::stod(dataArray[i]["available"].GetString()) * scale_offset);
                tmp_vector.push_back(pos);
                KF_LOG_DEBUG(logger, "[req_investor_position] (requestId)" << requestId << " (symbol) " << symbol << " available:" << d.GetArray()[i]["available"].GetString() << " total: " << d.GetArray()[i]["total"].GetString());
                KF_LOG_DEBUG(logger, "[req_investor_position] (requestId)" << requestId << " (symbol) " << symbol << " (position) " << pos.Position);
            }
        }
    }

    bool findSymbolInResult = false;
    //send the filtered position
    for (size_t i = 0; i < tmp_vector.size(); ++i)
    {
        on_rsp_position(&tmp_vector[i], i == (tmp_vector.size()- 1), requestId, errorId, errorMsg.c_str());
        findSymbolInResult = true;
    }

    if(!findSymbolInResult)
    {
        KF_LOG_DEBUG(logger, "[req_investor_position] (!findSymbolInResult) (requestId)" << requestId);
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
/*
int64_t TDEngineProbit::fixPriceTickSize(const std::string& ticker, int64_t price, bool isBuy)
{
    auto filter = getSendOrderFilter(ticker);
    if(filter.ticksize == 8)
    {
        return price;
    }
    int divided = 8 - filter.ticksize;
    int64_t cutter = pow(10, divided);
    int64_t new_price = price / cutter;
    //if(!isBuy)
    //{
        new_price += 1;
    //}
    return new_price * cutter;
}*/

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
    tm utc_time{};
    time_t time = timestamp/1000;
    gmtime_r(&time, &utc_time);
    char timeStr[50]{};
    sprintf(timeStr, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", utc_time.tm_year + 1900, utc_time.tm_mon + 1, utc_time.tm_mday, utc_time.tm_hour, utc_time.tm_min, utc_time.tm_sec,ms);
    return std::string(timeStr);
}

int64_t TDEngineProbit::fixPrice(int keepPrecision, int64_t price, bool isBuy)
{
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

int64_t TDEngineProbit::fixVolume(int keepPrecision,int64_t volume, bool isBuy)
{
    if(keepPrecision == 8) return volume;
    int removePrecisions = (8 - keepPrecision);
    double cutter = pow(10, removePrecisions);
    int64_t new_volume = 0;
    new_volume = std::ceil(volume / cutter)* cutter;
    return new_volume;
}

int64_t TDEngineProbit::fixCost(int keepPrecision, int64_t price, bool isBuy)
{
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

void TDEngineProbit::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitProbit& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_insert]" << " (rid)" << requestId << " (APIKey)" << unit.api_key << " (Tid)" << data->InstrumentID << " (Volume)" << data->Volume << " (LimitPrice)" << data->LimitPrice << " (OrderRef)" << data->OrderRef);
    send_writer->write_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_PROBIT, 1/*ISLAST*/, requestId);
    int errorId = 0;
    std::string errorMsg {};
    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.empty())
    {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_insert]: not in WhiteList, ignore it  (rid)" << requestId << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_PROBIT, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_insert] (exchange_ticker)" << ticker);
    OrderFieldEx order {};
    order.RequestID = requestId;
    order.VolumeTotalOriginal = data->Volume;

    std::string clientId = genClinetid(data->OrderRef);
    {
        std::unique_lock<std::mutex> l(g_orderMutex);
        unit.ordersMap[clientId] = order;
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
    }
/*    int64_t fixedPrice = fixPriceTickSize(ticker, data->LimitPrice, LF_CHAR_Buy == data->Direction);
    KF_LOG_DEBUG(logger, "[req_order_insert] SendOrderFilter  (Tid)" << ticker <<" (LimitPrice)" << data->LimitPrice <<" (FixedPrice)" << fixedPrice);
*/
   // int64_t ExpectPrice=scale_offset;
    uint64_t  cost1;
    cost1 = (data->Volume*1.0)*(data->ExpectPrice*1.0);

    SendOrderFilter filter = getSendOrderFilter1(unit, ticker.c_str());
    int64_t fixedPrice = fixPrice(filter.ticksize, data->LimitPrice, LF_CHAR_Buy == data->Direction);
    int64_t fixedVolume = fixVolume(filter.stepsize, data->Volume, LF_CHAR_Buy == data->Direction);    
    int64_t fixedCost = fixCost(filter.costsize, cost1, LF_CHAR_Buy == data->Direction);

    Document rspjson;
    send_order(unit, ticker.c_str(), GetSide(data->Direction).c_str(),GetType(data->OrderPriceType).c_str(), fixedVolume*1.0 / scale_offset , fixedPrice*1.0 / scale_offset, fixedCost*1.0 / scale_offset/scale_offset, clientId, rspjson);
    if(rspjson.HasParseError() || !rspjson.IsObject())
    {
        errorId = 100;
        errorMsg = "send_order http response has parse error or is not json. please check the log";
        KF_LOG_ERROR(logger, "[req_order_insert] send_order error!  (rid)" << requestId << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }
    else if (rspjson.HasMember("data"))
    {
        auto& dataJson = rspjson["data"];
        std::string remoteOrderId = dataJson["id"].GetString();
        KF_LOG_DEBUG(logger, "[req_order_insert] rsp  (rid)" << requestId << " (OrderRef) " << data->OrderRef << " (remoteOrderId) " << remoteOrderId);
    }
    else if (rspjson.HasMember("code") && rspjson["code"].IsNumber())
    {
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
        std::unique_lock<std::mutex> l(g_orderMutex);
        unit.ordersMap.erase(clientId);
    }
    //raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_PROBIT, 1, requestId, errorId, errorMsg.c_str());
}

void TDEngineProbit::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time)
{
    CancelOrderReq req;
    req.data = *data;
    req.account_index = account_index;
    req.requestId = requestId;
    req.rcv_time = rcv_time;
    pushCancelTask(std::move(req));
}

bool TDEngineProbit::OpenOrderToLFOrder(AccountUnitProbit& unit, rapidjson::Value& json, LFRtnOrderField& order)
{
    //memset(&order, 0, sizeof(LFRtnOrderField));
    KF_LOG_DEBUG(logger, "[OpenOrderToLFOrder] " << json.IsObject());
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
        KF_LOG_DEBUG(logger, "[OpenOrderToLFOrder] (InstrumentID) " << ticker.c_str());
        return true;
    }
    return false;
}

void TDEngineProbit::set_reader_thread()
{
    ITDEngine::set_reader_thread();
    m_wsLoopThread = ThreadPtr(new std::thread(std::bind(&TDEngineProbit::wsloop, this)));
    m_requestThread =  ThreadPtr(new std::thread( [&]{
        KF_LOG_INFO(logger, "request thread start");
        while (isRunning)
        {
            {
                std::unique_lock<std::mutex> l(g_requestMutex);
                while (m_cancelOrders.empty())
                {
                    if(!isRunning)
                    {
                        break;
                    }
                    if (g_requestCond.wait_for(l,std::chrono::seconds(2)) == std::cv_status::timeout)
                    {
                        continue;
                    }
                }
            }

            if(isRunning)
            {
                //do action
                std::vector<CancelOrderReq> cancelOrders;
                getCancelOrder(cancelOrders);
                for(const auto& order : cancelOrders)
                {
                    doCancelOrder(order);
                }

            }
        }
        KF_LOG_INFO(logger, "request thread exit");
    }));

}

void TDEngineProbit::wsloop()
{
    KF_LOG_INFO(logger, "ws loop thread start");
    while(isRunning)
    {
        lws_service( context, m_restIntervalms);
    }
    KF_LOG_INFO(logger, "ws loop thread exit");
}

void TDEngineProbit::printResponse(const Document& d)
{
    if(d.IsObject() && d.HasMember("code"))
    {
        KF_LOG_DEBUG(logger, "[printResponse] error (code) " << d["code"].GetInt() << " (message) " << d["message"].GetString());
    }
    else
    {
        StringBuffer buffer;
        Writer<StringBuffer> writer(buffer);
        d.Accept(writer);
        KF_LOG_DEBUG(logger, "[printResponse] ok (text) " << buffer.GetString());
    }
}

void TDEngineProbit::getResponse(int http_status_code, const std::string& responseText, const std::string& errorMsg, Document& json)
{
    if(http_status_code == HTTP_RESPONSE_OK)
    {
        KF_LOG_DEBUG(logger, "[getResponse] (http_status_code == 200) (responseText)" << responseText << " (errorMsg) " << errorMsg);
        json.Parse(responseText.c_str());
        KF_LOG_DEBUG(logger, "[getResponse] (http_status_code == 200) (HasParseError)" << json.HasParseError());
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
    KF_LOG_DEBUG(logger, "[get_account]");
    std::string requestPath = "/api/exchange/v1/balance";

    std::string authToken =getAuthToken(unit);
    string url = unit.baseUrl + requestPath;

    const auto response = Get(Url{url},
                                 Header{
                                        {"Content-Type", "application/json"},
                                        { "authorization", "Bearer " + authToken }},
                                 Timeout{30000});

    KF_LOG_DEBUG(logger, "[get_account] (url) " << url  << " (response.status_code) " << response.status_code <<
                                                " (response.error.message) " << response.error.message <<
                                                " (response.text) " << response.text.c_str());

    return getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineProbit::get_products(const AccountUnitProbit& unit, Document& json)
{
    KF_LOG_DEBUG(logger, "[get_products]");
    return;
}

void TDEngineProbit::send_order(const AccountUnitProbit& unit, const char *code, const char *side, const char *type, double size, double price,double cost, const std::string& clientId,  Document& json)
{
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
    Document document;
    document.SetObject();
    Document::AllocatorType& allocator = document.GetAllocator();

    //Value document(kObjectType);

    document.AddMember("market_id", StringRef(code), allocator);
    document.AddMember("side", StringRef(side), allocator);
    document.AddMember("type", StringRef(type), allocator);
    rapidjson::Value nullObject(rapidjson::kNullType);
    if (strcmp(type, "limit") == 0)
    {
            document.AddMember("time_in_force", StringRef("gtc"), allocator);
        document.AddMember("quantity", StringRef(sizeStr.c_str()), allocator);
       // document.AddMember("cost", nullObject,allocator);
            document.AddMember("limit_price", StringRef(priceStr.c_str()), allocator);
    }
    else
    {
        document.AddMember("time_in_force", StringRef("ioc"), allocator);
        if (strcmp(side, "buy") == 0)
        {
            //document.AddMember("quantity", StringRef(""), allocator);
            document.AddMember("cost", StringRef(costStr.c_str()), allocator);
        }
        else
        {
            document.AddMember("quantity", StringRef(sizeStr.c_str()), allocator);
            document.AddMember("cost", StringRef(costStr.c_str()), allocator);
        }
    }
    //document.AddMember("limit_price", StringRef(priceStr.c_str()), allocator);
    std::string client_id = clientId;
    document.AddMember("client_order_id", StringRef(client_id.c_str()), allocator);
    //document1.AddMember("data",document,allocator); 

    StringBuffer jsonStr;
    Writer<StringBuffer> writer(jsonStr);
    document.Accept(writer);
    std::string body = jsonStr.GetString();
    std::string url = unit.baseUrl + std::string("/api/exchange/v1/new_order");
    std::string authToken("Bearer ");
    authToken += getAuthToken(unit);
    auto retryCounts = m_retryCounts;
    do
    {
    Document rspDoc;  
        if (PostRequest(url ,authToken , body, rspDoc))
        {
        json = std::move(rspDoc);
            break;
        }
    json = std::move(rspDoc);
        KF_LOG_DEBUG(logger, "[send_order] try "<<retryCounts << " times,client_order_id:" << client_id);
        std::this_thread::sleep_for(std::chrono::milliseconds(m_retryIntervalMs));
    }while(retryCounts--);
}

void TDEngineProbit::cancel_order(const AccountUnitProbit& unit, const std::string& orderId, const std::string& marketID, double quantity,  Document& json)
{
    KF_LOG_DEBUG(logger, "[cancel_order]");
    std::string requestPath = "/api/exchange/v1/cancel_order";
    char strQuantity[20]{};
    sprintf(strQuantity, "%.4f", quantity);
    std::string body = "{\"market_id\":\"" + marketID + "\",\"order_id\":\"" + orderId + "\",\"limit_open_quantity\":\"0\"}";
    std::string authToken("Bearer ");
    authToken += getAuthToken(unit);
    string url = unit.baseUrl + requestPath;
    auto retryCounts = m_retryCounts;
    do
    {
    Document rspDoc;  
        if (PostRequest(url , authToken , body, rspDoc))
        {
        json = std::move(rspDoc);
            break;
        }
    json = std::move(rspDoc);
        KF_LOG_DEBUG(logger, "[cancel_order] try "<<retryCounts << " times,remote_order_id:" << orderId);
        std::this_thread::sleep_for(std::chrono::milliseconds(m_retryIntervalMs));
    }while(retryCounts--);
}

int64_t TDEngineProbit::getTimestamp()
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
        memset( &info, 0x00, sizeof(info) );
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
    if (context == NULL)
    {
        KF_LOG_ERROR(logger, "TDEngineProbit::lws_login: context is NULL. return");
        return;
    }
    int logs = LLL_ERR | LLL_DEBUG | LLL_WARN;
    lws_set_log_level(logs, NULL);
    struct lws_client_connect_info ccinfo = { 0 };
    ccinfo.context  = context;
    ccinfo.address  = unit.wsUrl.c_str();
    ccinfo.port     = 443;
    ccinfo.path     = "/api/exchange/v1/ws";
    ccinfo.host     = unit.wsUrl.c_str();
    ccinfo.origin   = unit.wsUrl.c_str();
    ccinfo.ietf_version_or_minus_one = -1;
    ccinfo.protocol = "wss://";
    ccinfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
    unit.websocketConn = lws_client_connect_via_info(&ccinfo);
    KF_LOG_INFO(logger, "TDEngineProbit::lws_login: Connecting to "<< ccinfo.protocol <<  ccinfo.host << ":" << ccinfo.port << ":" << ccinfo.path);
    if (unit.websocketConn == NULL)
    {
        KF_LOG_ERROR(logger, "TDEngineProbit::lws_login: wsi create error.");
        return;
    }
    unit.logged_in = true;
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
        auto clientId = order["client_order_id"].GetString();
        auto orderIter = unit.ordersMap.find(clientId);
        if (orderIter == unit.ordersMap.end())
        {
            KF_LOG_DEBUG(logger, "TDEngineProbit::onOrder,ignore this order,ClientOrderId:" << clientId);
            continue;
        }
        if (!order.HasMember("id") || !order["id"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"id\"");
            return;
        }
        //kungfu order
        OrderFieldEx& rtnOrderEx = orderIter->second;
        rtnOrderEx.remoteOrderRef = order["id"].GetString();
        strncpy(rtnOrderEx.OrderRef, getOrderRef(clientId).c_str(), sizeof(rtnOrderEx.OrderRef) - 1);

        if (!order.HasMember("market_id") || !order["market_id"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"market_id\"");
        }
        strncpy(rtnOrderEx.InstrumentID, unit.coinPairWhiteList.GetKeyByValue(order["market_id"].GetString()).c_str(), sizeof(rtnOrderEx.InstrumentID) - 1);

        if (!order.HasMember("side") || !order["side"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"side\"");
            return;
        }
        rtnOrderEx.Direction = GetDirection(order["side"].GetString());
        if (!order.HasMember("type") || !order["type"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"type\"");
            return;
        }
        rtnOrderEx.OrderPriceType = GetPriceType(order["type"].GetString());
        if (!order.HasMember("open_quantity") || !order["open_quantity"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"open_quantity\"");
            return;
        }
        rtnOrderEx.VolumeTotal = (uint64_t)((std::atof(order["open_quantity"].GetString()) + 0.000000001) * scale_offset);
        rtnOrderEx.RequestID = orderIter->second.RequestID;
/*        if (!order.HasMember("cancelled_quantity") || !order["cancelled_quantity"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"cancelled_quantity\"");
            return;
        }
        auto cancelled_quantity = (uint64_t)((std::atof(order["cancelled_quantity"].GetString()) + 0.000000001) * scale_offset);
        if(cancelled_quantity > 0)
        {
            rtnOrderEx.OrderStatus = LF_CHAR_Canceled;
        }*/
        //price
        if (!order.HasMember("limit_price") || !order["limit_price"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"limit_price\"");
            return;
        }
        rtnOrderEx.LimitPrice = (int64_t)((std::atof(order["limit_price"].GetString()) + 0.000000001) * scale_offset);
        if (!order.HasMember("filled_cost") || !order["filled_cost"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"filled_cost\"");
            return;
        }
        double total_filledCost = std::atof(order["filled_cost"].GetString());
        double cur_filledCost = total_filledCost - rtnOrderEx.preFilledCost;
        KF_LOG_DEBUG(logger, "TDEngineProbit::onOrder, totalFilledCost:"<<total_filledCost<< ", preFilledCost:" << rtnOrderEx.preFilledCost<<", requestId:" << rtnOrderEx.RequestID);
        rtnOrderEx.preFilledCost = total_filledCost;
        //quantity
        if (!order.HasMember("filled_quantity") || !order["filled_quantity"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"filled_quantity\"");
            return;
        }
        double total_filled_quantity = std::atof(order["filled_quantity"].GetString());
        double cur_filled_quantity = total_filled_quantity - rtnOrderEx.preFilledQuantity;
        rtnOrderEx.VolumeTraded = (uint64_t)((total_filled_quantity + 0.000000001)*scale_offset);
        rtnOrderEx.preFilledQuantity =  total_filled_quantity;
        if (!order.HasMember("quantity") || !order["quantity"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"quantity\"");
           // return;
        }
        else if(order.HasMember("quantity") && order["quantity"].IsString()){
        double quantity = std::atof(order["quantity"].GetString()) + 0.000000001;
        rtnOrderEx.VolumeTotalOriginal = (uint64_t)(quantity*scale_offset);
        }
        //status
        if (!order.HasMember("status") || !order["status"].IsString())
        {
            KF_LOG_ERROR(logger, "TDEngineProbit::onOrder, parse json error:json string has no member \"status\"");
            return;
        }
        rtnOrderEx.OrderStatus = GetOrderStatus(order["status"].GetString());
        if (rtnOrderEx.OrderStatus == LF_CHAR_NotTouched)
        {
            if (cur_filled_quantity > 0.0000001)
            {
                rtnOrderEx.OrderStatus = LF_CHAR_PartTradedQueueing;
            }
        }
        // on_rtn_order
        auto rtnOrder = convert(rtnOrderEx);
        if (!rtnOrderEx.isSentNotTouched && rtnOrder.OrderStatus != LF_CHAR_NotTouched)
        {
            auto status = rtnOrder.OrderStatus;
            rtnOrder.OrderStatus = LF_CHAR_NotTouched;
            on_rtn_order(&rtnOrder);
            rtnOrderEx.isSentNotTouched = true;
            rtnOrder.OrderStatus = status;
        }
        on_rtn_order(&rtnOrder);
        raw_writer->write_frame(&rtnOrder, sizeof(LFRtnOrderField), source_id, MSG_TYPE_LF_RTN_ORDER_PROBIT, 1, (rtnOrderEx.RequestID > 0) ? rtnOrderEx.RequestID : -1);
        KF_LOG_DEBUG(logger, "TDEngineProbit::onOrder, ticker:" << rtnOrderEx.InstrumentID <<",curFilledCost:"<< cur_filledCost << ", curQuantity:" << cur_filled_quantity <<", requestId:" << rtnOrderEx.RequestID);
        if (cur_filled_quantity > 0.0000001)
        {
            double fixedPrice  = cur_filledCost / cur_filled_quantity;
            int64_t cur_price = convert(rtnOrderEx.InstrumentID, fixedPrice);
            // on_rtn_trade
            onTrade(conn, rtnOrderEx.OrderRef, unit.api_key.c_str(), rtnOrderEx.InstrumentID, rtnOrderEx.Direction, (uint64_t)((cur_filled_quantity + 0.000000001) * scale_offset), cur_price, rtnOrderEx.RequestID);
        }
        if(rtnOrderEx.OrderStatus == LF_CHAR_Canceled ||  rtnOrderEx.OrderStatus == LF_CHAR_AllTraded )
        {
            KF_LOG_DEBUG(logger, "TDEngineProbit::onOrder delete order, orderRef:"<< rtnOrderEx.OrderRef << ", remoteOrderRef:" << rtnOrderEx.remoteOrderRef <<", requestId:" << rtnOrderEx.RequestID);
            unit.ordersMap.erase(orderIter);
        }
    }
}

void TDEngineProbit::onTrade(struct lws * conn, const char* orderRef, const char* api_key, const char* instrumentID, LfDirectionType direction, uint64_t volume, int64_t price, int requestid)
{
    KF_LOG_DEBUG(logger, "TDEngineProbit::onTrade, start,requestId:" << requestid);
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
    KF_LOG_DEBUG(logger, "TDEngineProbit::onTrade end,requestId:" << requestid);
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

bool TDEngineProbit::PostRequest(const std::string& url,const std::string& auth, const std::string& body, Document& json)
{
    std::lock_guard<std::mutex> lck(g_postMutex);
    const auto response = cpr::Post(Url{ url },cpr::VerifySsl(false),Header{{ "Content-Type", "application/json" },{ "authorization", auth }},Body{ body }, Timeout{ 30000 });
    KF_LOG_DEBUG(logger, "[Post] (url) " << url << \
              " (body) " << body << \
              " (msg) " << auth <<  \
              " (response.status_code) " << response.status_code << \
              " (response.error.message) " << response.error.message << \
              " (response.text) " << response.text.c_str());
    getResponse(response.status_code, response.text, response.error.message, json);
    return response.status_code == HTTP_RESPONSE_OK ? true : false;
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
        PostRequest(url, "Basic " + authEncode, body, json);
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

void TDEngineProbit::genUniqueKey()
{
    struct tm cur_time = getCurLocalTime();
    //SSMMHHDDN
    char key[11]{0};
    snprintf((char*)key, 11, "%02d%02d%02d%02d%1s", cur_time.tm_sec, cur_time.tm_min, cur_time.tm_hour, cur_time.tm_mday, m_engineIndex.c_str());
    m_uniqueKey = key;
}

//clientid =  m_uniqueKey+orderRef
std::string TDEngineProbit::genClinetid(const std::string &orderRef)
{
    return m_uniqueKey + orderRef;
}

//the first 9 bytes are m_uniqueKey,the last N bytes are orderRef
std::string TDEngineProbit::getOrderRef(const std::string &clinetID)
{
    if(clinetID.size() > 9)
    {
        return std::string(clinetID, 9);
    }
    return std::string{};
}

void TDEngineProbit::doCancelOrder(const CancelOrderReq & req)
{
    auto& account_index = req.account_index;
    auto  data          = &req.data;
    auto& requestId     = req.requestId;
    AccountUnitProbit& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[doCancelOrder]" << " (rid)" << requestId << " (APIKey)" << unit.api_key << " (Iid)" << data->InvestorID << " (OrderRef)" << data->OrderRef << " (KfOrderID)" << data->KfOrderID);
    send_writer->write_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_PROBIT, 1, requestId);
    int errorId = 0;
    std::string errorMsg {};
    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.empty())
    {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[doCancelOrder]: not in WhiteList , ignore it: (rid)" << requestId << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_PROBIT, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[doCancelOrder] (exchange_ticker)" << ticker);
    Document doc;
    cancel_order(unit, req.remoteOrderRef, ticker, req.cancelVolume*1.0/scale_offset, doc);
    if(doc.IsObject() && !doc.HasParseError() && doc.HasMember("code") && doc["code"].IsNumber())
    {
        errorId = doc["code"].GetInt();
        if(doc.HasMember("message") && doc["message"].IsString())
        {
            errorMsg = doc["message"].GetString();
        }
        KF_LOG_ERROR(logger, "[doCancelOrder] cancel_order failed!" << " (rid)" << requestId << " (errorId)" << errorId << " (errorMsg) " << errorMsg);;
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_PROBIT, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[doCancelOrder] cancel_order success!" << " (rid)" << requestId <<"(order ref)"<< data->OrderRef);
    //raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_PROBIT, 1, requestId, errorId, errorMsg.c_str());
}

void TDEngineProbit::pushCancelTask(CancelOrderReq && req)
{
    std::unique_lock<std::mutex> l(g_requestMutex);
    if(m_cancelOrders.size() > 1000)
    {
        m_cancelOrders.pop_front();
        KF_LOG_ERROR(logger, "[pushCancelTask] cancel order size > 1000," << "RequestId:" << req.requestId <<",OrderRef:"<< req.data.OrderRef);
    }
    KF_LOG_DEBUG(logger, "[pushCancelTask] push cancel task," << "RequestId:" << req.requestId <<",OrderRef:"<< req.data.OrderRef);
    m_cancelOrders.push_back(std::move(req));
}

void TDEngineProbit::getCancelOrder(std::vector<CancelOrderReq>& requests)
{
    std::vector<std::map<std::string/*client_order_id*/, OrderFieldEx>> curAccountOrders;
    {
        std::unique_lock<std::mutex> l(g_orderMutex);
        for(const auto& unit : account_units)
        {
            curAccountOrders.push_back(unit.ordersMap);
        }
    }
    std::unique_lock<std::mutex> l(g_requestMutex);
    for (auto reqIter = m_cancelOrders.begin(); reqIter != m_cancelOrders.end(); )
    {
        auto req = *reqIter;
        if( req.account_index >= curAccountOrders.size() ||  req.account_index < 0)
        {
            reqIter = m_cancelOrders.erase(reqIter);
            continue;
        }

        auto& order = curAccountOrders[req.account_index];
        std::string clientId = genClinetid(req.data.OrderRef);
        auto orderIter = order.find(clientId);
        if(orderIter == order.end())
        {
            ++reqIter;
            //KF_LOG_DEBUG(logger, "[getCancelOrder] orderMap can not find, ClientOrderId:" << clientId << ",RequestId:" << req.requestId << ",AccountIndex:" << req.account_index);
            continue;
        }
        if (!orderIter->second.remoteOrderRef.empty())
        {
            CancelOrderReq new_req {};
            new_req = req;
            new_req.cancelVolume = orderIter->second.VolumeTotal;
            new_req.remoteOrderRef = orderIter->second.remoteOrderRef;
            KF_LOG_DEBUG(logger, "[getCancelOrder] orderMap ClientOrderId:" << clientId << ",RequestId:" << req.requestId << ",AccountIndex:" << req.account_index << ",RemoteOrderRef:"<< new_req.remoteOrderRef);
            requests.push_back(std::move(new_req));
            reqIter = m_cancelOrders.erase(reqIter);
            continue;
        }
        ++reqIter;
        KF_LOG_DEBUG(logger, "[getCancelOrder] orderMap RemoteOrderRef is empty, ClientOrderId:" << clientId << ",RequestId:" << req.requestId << ",AccountIndex:" << req.account_index);
    }
}

int64_t TDEngineProbit::convert(const std::string &ticker, double price)
{
    auto filter = getSendOrderFilter(ticker);
    int  divisor = pow(10, 8 - filter.ticksize);
    price += pow(0.1, filter.ticksize+1);
    int64_t dividend = price * scale_offset;
    return dividend / divisor * divisor;
}

LFRtnOrderField TDEngineProbit::convert(const OrderFieldEx &ex)
{
    LFRtnOrderField ret{};
    strncpy(ret.BrokerID,ex.BrokerID, sizeof(ret.BrokerID)-1);
    strncpy(ret.UserID,ex.UserID, sizeof(ret.UserID)-1);
    strncpy(ret.ParticipantID,ex.ParticipantID, sizeof(ret.ParticipantID)-1);
    strncpy(ret.InvestorID,ex.InvestorID, sizeof(ret.InvestorID)-1);
    strncpy(ret.BusinessUnit,ex.BusinessUnit, sizeof(ret.BusinessUnit)-1);
    strncpy(ret.InstrumentID,ex.InstrumentID, sizeof(ret.InstrumentID)-1);
    strncpy(ret.OrderRef,ex.OrderRef, sizeof(ret.OrderRef)-1);
    strncpy(ret.ExchangeID,ex.ExchangeID, sizeof(ret.ExchangeID)-1);
    ret.LimitPrice = ex.LimitPrice;
    ret.VolumeTraded = ex.VolumeTraded;
    ret.VolumeTotal = ex.VolumeTotal;
    ret.VolumeTotalOriginal = ex.VolumeTotalOriginal;
    ret.TimeCondition = ex.TimeCondition;
    ret.VolumeCondition = ex.VolumeCondition;
    ret.OrderPriceType = ex.OrderPriceType;
    ret.Direction = ex.Direction;
    ret.OffsetFlag = ex.OffsetFlag;
    ret.HedgeFlag = ex.HedgeFlag;
    ret.OrderStatus = ex.OrderStatus;
    ret.RequestID = ex.RequestID;
    return std::move(ret);
}

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


