#include "TDEngineDaybit.h"
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
#define TOPIC_MARKET "/subscription:markets"
#define TOPIC_TRADE "/subscription:my_trades"
#define TOPIC_ORDER "/subscription:my_orders"
#define TOPIC_API "/api"
USING_WC_NAMESPACE

int g_RequestGap=5*60;

static TDEngineDaybit* global_td = nullptr;
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
                global_td->on_lws_write(wsi);
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
                        "td-protocol",
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



TDEngineDaybit::TDEngineDaybit(): ITDEngine(SOURCE_DAYBIT)
{
    logger = yijinjing::KfLog::getLogger("TradeEngine.Daybit");
    KF_LOG_INFO(logger, "[TDEngineDaybit]");
  
}

TDEngineDaybit::~TDEngineDaybit()
{
}
int64_t TDEngineDaybit::makeRef(){ return ++m_ref;}
int64_t TDEngineDaybit::getRef(){ return m_ref;}
//int64_t TDEngineDaybit::makeJoinRef(){return ++m_joinRef;}
//int64_t TDEngineDaybit::getJoinRef(){ return m_joinRef;}
void TDEngineDaybit::init()
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

void TDEngineDaybit::pre_load(const json& j_config)
{
    KF_LOG_INFO(logger, "[pre_load]");
}

void TDEngineDaybit::resize_accounts(int account_num)
{
    account_units.resize(account_num);
    KF_LOG_INFO(logger, "[resize_accounts]");
}

TradeAccount TDEngineDaybit::load_account(int idx, const json& j_config)
{
    KF_LOG_INFO(logger, "[load_account]");
    // internal load
    string api_key = j_config["APIKey"].get<string>();
    string secret_key = j_config["SecretKey"].get<string>();

    string baseUrl = j_config["baseUrl"].get<string>();
    string path = j_config["path"].get<string>();
    base_interval_ms = j_config["rest_get_interval_ms"].get<int>();
    base_interval_ms = std::max(base_interval_ms,(int64_t)500);

    AccountUnitDaybit& unit = account_units[idx];
    unit.api_key = api_key;
    unit.secret_key = secret_key;
    unit.baseUrl = baseUrl;
    unit.path = path;
    KF_LOG_INFO(logger, "[load_account] (api_key)" << api_key << " (baseUrl)" << unit.baseUrl);

    unit.coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
    unit.coinPairWhiteList.Debug_print();

    unit.positionWhiteList.ReadWhiteLists(j_config, "positionWhiteLists");
    unit.positionWhiteList.Debug_print();

    //display usage:
    if(unit.coinPairWhiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "TDEngineDaybit::load_account: please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"BTC_USDT\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"ETC_ETH\"");
        KF_LOG_ERROR(logger, "},");
    }

    //cancel all openning orders on TD startup
    //Document d;
    //cancel_all_orders(unit, d);

    // set up
    TradeAccount account = {};
    //partly copy this fields
    strncpy(account.UserID, api_key.c_str(), 16);
    strncpy(account.Password, secret_key.c_str(), 21);
    return account;
}

void TDEngineDaybit::InitSubscribeMsg(AccountUnitDaybit& unit,bool only_api_topic)
{
    std::lock_guard<std::mutex> lck(unit_mutex);
    
    unit.listMessageToSend = std::queue<std::string>(); 
    unit.listMessageToSend.push(createJoinReq(0,TOPIC_API));
    unit.mapSubscribeRef.insert(std::make_pair(TOPIC_API,getRef()));
    if(only_api_topic)
    {   
        isSyncServerTime = false;         
    }
    else
    {
        unit.listMessageToSend.push(createJoinReq(0,TOPIC_MARKET));
        unit.mapSubscribeRef.insert(std::make_pair(TOPIC_MARKET,getRef()));
        unit.listMessageToSend.push(createJoinReq(0,TOPIC_ORDER));
        unit.mapSubscribeRef.insert(std::make_pair(TOPIC_ORDER,getRef()));
        unit.listMessageToSend.push(createJoinReq(0,TOPIC_TRADE));
        unit.mapSubscribeRef.insert(std::make_pair(TOPIC_TRADE,getRef()));
    }
    //std::cout << "InitSubscribeMsg, ref test " << getRef() << " hhhhh " <<getRef() << std::endl;
}
void TDEngineDaybit::heartbeat_loop()
{

    while(isRunning)
    {
        std::this_thread::sleep_for(std::chrono::seconds(30));
       
        for (size_t idx = 0; idx < account_units.size(); idx++) 
        {
            AccountUnitDaybit &unit = account_units[idx];
            std::unique_lock<std::mutex> lck(unit_mutex);
            unit.listMessageToSend.push(createHeartBeatReq());   
            lck.unlock();
            lws_callback_on_writable(unit.websocketConn);        
        }                   
    }
}
void TDEngineDaybit::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[connect]");
    for (int idx = 0; idx < account_units.size(); idx ++)
    {
        AccountUnitDaybit& unit = account_units[idx];
        KF_LOG_INFO(logger, "[connect] (api_key)" << unit.api_key);
        if (!unit.logged_in)
        {
            //exchange infos
            Document doc;
            //TODO
            //get_products(unit, doc);
            //KF_LOG_INFO(logger, "[connect] get_products");
            //printResponse(doc);

            //if(loadExchangeOrderFilters(unit, doc))
            //{
            //    unit.logged_in = true;
            //} else {
            //    KF_LOG_ERROR(logger, "[connect] logged_in = false for loadExchangeOrderFilters return false");
            //}
            //debug_print(unit.sendOrderFilters);
            InitSubscribeMsg(unit);
			lws_login(unit, 0);
            
            unit.logged_in = true;
        }
    }
}

bool TDEngineDaybit::loadExchangeOrderFilters(AccountUnitDaybit& unit, Value &doc) {
    KF_LOG_INFO(logger, "[loadExchangeOrderFilters]");
    SizeType size = doc.Size();
    for (SizeType index =0;index < size;++index) 
    {            
        auto& data = doc[index];
	//std::cout << "index:" << index <<"size:"<< size <<std::endl;
        if(data.HasMember("data") && data["data"].IsArray())
        {
	        auto& dataInner = data["data"];
            SizeType innerSize = dataInner.Size();
            for(SizeType i = 0;i < innerSize;++i)
            {
		    //std::cout << "i:" << i <<"innersize:"<< innerSize <<std::endl;	
		    auto& item = dataInner[i];
                if (item.HasMember("tick_price") && item.HasMember("quote") && item.HasMember("base")) 
                {              
                    double tickSize = atof(item["tick_price"].GetString());
                    std::string symbol = item["quote"].GetString()+std::string("-")+item["base"].GetString();
                    KF_LOG_INFO(logger, "[loadExchangeOrderFilters] sendOrderFilters (symbol)" << symbol << " (tickSize)"<< tickSize);
                    //0.0000100; 0.001;  1; 10
                    SendOrderFilter afilter;
                    afilter.InstrumentID = symbol;
                    afilter.ticksize = tickSize;
                    unit.sendOrderFilters.insert(std::make_pair(afilter.InstrumentID, afilter));
                }
            }
        }
    }
    return true;
}

void TDEngineDaybit::debug_print(std::map<std::string, SendOrderFilter> &sendOrderFilters)
{
    std::map<std::string, SendOrderFilter>::iterator map_itr = sendOrderFilters.begin();
    while(map_itr != sendOrderFilters.end())
    {
        KF_LOG_DEBUG(logger, "[debug_print] sendOrderFilters (symbol)" << map_itr->first <<
                                                                      " (tickSize)" << map_itr->second.ticksize);
        map_itr++;
    }
}

SendOrderFilter TDEngineDaybit::getSendOrderFilter(AccountUnitDaybit& unit, const std::string& symbol) {
    std::map<std::string, SendOrderFilter>::iterator map_itr = unit.sendOrderFilters.find(symbol);
    if (map_itr != unit.sendOrderFilters.end()) {
        return map_itr->second;
    }
    SendOrderFilter defaultFilter;
    defaultFilter.ticksize = 0.00000001;
    defaultFilter.InstrumentID = "";
    return defaultFilter;
}

void TDEngineDaybit::login(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[login]");
    connect(timeout_nsec);
}

void TDEngineDaybit::logout()
{
    KF_LOG_INFO(logger, "[logout]");
}

void TDEngineDaybit::release_api()
{
    KF_LOG_INFO(logger, "[release_api]");
}

bool TDEngineDaybit::is_logged_in() const
{
    KF_LOG_INFO(logger, "[is_logged_in]");
    for (auto& unit: account_units)
    {
        if (!unit.logged_in)
	{
	    std::cout << "not log in" << std::endl;
            return false;
	}
    }
    std::cout << "is log in" << std::endl;
    return true;
}

bool TDEngineDaybit::is_connected() const
{
    KF_LOG_INFO(logger, "[is_connected]");
    return is_logged_in();
}

LfDirectionType TDEngineDaybit::GetDirection(bool isSell) {
    if (isSell) {
        return LF_CHAR_Sell;
    } else {
        return LF_CHAR_Buy;
    }
}

std::string TDEngineDaybit::GetType(const LfOrderPriceTypeType& input) {
    if (LF_CHAR_LimitPrice == input) {
        return "Limit";
    } else if (LF_CHAR_AnyPrice == input) {
        return "Market";
    } else {
        return "";
    }
}

LfOrderPriceTypeType TDEngineDaybit::GetPriceType(std::string input) {
    if ("Limit" == input) {
        return LF_CHAR_LimitPrice;
    } else if ("Market" == input) {
        return LF_CHAR_AnyPrice;
    } else {
        return '0';
    }
}
//订单状态
LfOrderStatusType TDEngineDaybit::GetOrderStatus(std::string input) {

    if("received" == input){
        return LF_CHAR_Unknown;
    }
    else if("placed" == input){
        return LF_CHAR_NotTouched;
    } 
    else if ("filled" == input) {
        return LF_CHAR_AllTraded;
    }
    else if ("canceled" == input) {
        return LF_CHAR_Canceled;
    }
    else if ("rejected" == input) {
        return LF_CHAR_NoTradeNotQueueing;
    } 
    else {
        return LF_CHAR_NotTouched;
    }
}

/**
 * req functions
 */
void TDEngineDaybit::req_investor_position(const LFQryPositionField* data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId);

    AccountUnitDaybit& unit = account_units[account_index];
    KF_LOG_INFO(logger, "[req_investor_position] (api_key)" << unit.api_key << " (InstrumentID) " << data->InstrumentID);

    int errorId = 0;
    std::string errorMsg = "";
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
    send_writer->write_frame(data, sizeof(LFQryPositionField), source_id, MSG_TYPE_LF_QRY_POS_DAYBIT, 1, requestId);
    on_rsp_position(&pos, 1, requestId, errorId, errorMsg.c_str());
   /*  int errorId = 0;
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
    send_writer->write_frame(data, sizeof(LFQryPositionField), source_id, MSG_TYPE_LF_QRY_POS_DAYBIT, 1, requestId);

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

    if(!findSymbolInResult)
    {
        KF_LOG_INFO(logger, "[req_investor_position] (!findSymbolInResult) (requestId)" << requestId);
        on_rsp_position(&pos, 1, requestId, errorId, errorMsg.c_str());
    }
    if(errorId != 0)
    {
        raw_writer->write_error_frame(&pos, sizeof(LFRspPositionField), source_id, MSG_TYPE_LF_RSP_POS_DAYBIT, 1, requestId, errorId, errorMsg.c_str());
    } */
}

void TDEngineDaybit::req_qry_account(const LFQryAccountField *data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_qry_account]");
}

int64_t TDEngineDaybit::fixPriceTickSize(double keepPrecision, int64_t price, bool isBuy) {


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

int TDEngineDaybit::Round(std::string tickSizeStr) {
    size_t docAt = tickSizeStr.find( ".", 0 );
    size_t oneAt = tickSizeStr.find( "1", 0 );

    if(docAt == string::npos) {
        //not ".", it must be "1" or "10"..."100"
        return -1 * (tickSizeStr.length() -  1);
    }
    //there must exist 1 in the string.
    return oneAt - docAt;
}

bool TDEngineDaybit::ShouldRetry(const Document& json)
{

	/* std::lock_guard<std::mutex> lck(unit_mutex);
    if(json.IsObject() && json.HasMember("code") && json["code"].IsNumber())
    {
        int code = json["code"].GetInt();
		if (code == 503 || code == 429)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(rest_get_interval_ms));
			return true;
		}
          
    } */
    return false;
}
void TDEngineDaybit::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitDaybit& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_insert]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Tid)" << data->InstrumentID
                                              << " (Volume)" << data->Volume
                                              << " (LimitPrice)" << data->LimitPrice
                                              << " (OrderRef)" << data->OrderRef);
    send_writer->write_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_DAYBIT, 1/*ISLAST*/, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_insert]: not in WhiteList, ignore it  (rid)" << requestId <<
                                                                                      " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_DAYBIT, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_insert] (exchange_ticker)" << ticker);


    SendOrderFilter filter = getSendOrderFilter(unit, ticker.c_str());

    int64_t fixedPrice = fixPriceTickSize(filter.ticksize, data->LimitPrice, LF_CHAR_Buy == data->Direction);

    KF_LOG_DEBUG(logger, "[req_order_insert] SendOrderFilter  (Tid)" << ticker <<
                                                                     " (LimitPrice)" << data->LimitPrice <<
                                                                     " (ticksize)" << filter.ticksize <<
                                                                     " (fixedPrice)" << fixedPrice);
    std::lock_guard<std::mutex> guard_mutex(unit_mutex);                                                            
    //unit.listMessageToSend.push(createJoinReq(getJoinRef(),"/api"));
    unit.listMessageToSend.push(createNewOrderReq(unit.mapSubscribeRef[TOPIC_API],data->Volume*1.0/scale_offset,fixedPrice*1.0/scale_offset,ticker,LF_CHAR_Sell == data->Direction));
	addNewOrder(unit, data->InstrumentID, data->OrderRef,data->Direction, LF_CHAR_Unknown, data->Volume, requestId,getRef(),*data);
    //on_rsp_order_insert(data, requestId,errorId, errorMsg.c_str());
    raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_DAYBIT, 1, requestId, errorId, errorMsg.c_str());
}


void TDEngineDaybit::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitDaybit& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_action]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Iid)" << data->InvestorID
                                              << " (OrderRef)" << data->OrderRef
                                              << " (KfOrderID)" << data->KfOrderID);

    send_writer->write_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_DAYBIT, 1, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_action]: not in WhiteList , ignore it: (rid)" << requestId << " (errorId)" <<
                                                                                       errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_DAYBIT, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_action] (exchange_ticker)" << ticker);
	std::unique_lock<std::mutex> lck(unit_mutex);
    auto itr = localOrderRefRemoteOrderId.find(data->OrderRef);
    int64_t remoteOrderId;
    if(itr == localOrderRefRemoteOrderId.end()) {
        errorId = 1;
        std::stringstream ss;
        ss << "[req_order_action] not found in localOrderRefRemoteOrderId map (orderRef) " << data->OrderRef;
        errorMsg = ss.str();
        KF_LOG_ERROR(logger, "[req_order_action] not found in localOrderRefRemoteOrderId map. "
                             << " (rid)" << requestId << " (orderRef)" << data->OrderRef << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_DAYBIT, 1, requestId, errorId, errorMsg.c_str());
        return;
    } else {
        remoteOrderId = itr->second;
        KF_LOG_DEBUG(logger, "[req_order_action] found in localOrderRefRemoteOrderId map (orderRef) "
                             << data->OrderRef << " (remoteOrderId) " << remoteOrderId);
    }
    cancel_order(unit,remoteOrderId);
    if(errorId != 0)
    {
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
    }
    raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_DAYBIT, 1, requestId, errorId, errorMsg.c_str());
}




void TDEngineDaybit::addNewOrder(AccountUnitDaybit& unit, const char_31 InstrumentID,
                                                 const char_21 OrderRef, LfDirectionType direction, const LfOrderStatusType OrderStatus,const uint64_t VolumeTotal,int reqID,int64_t ref,LFInputOrderField input)
{
    //add new orderId for GetAndHandleOrderTradeResponse
   
     KF_LOG_INFO(logger, "[addNewOrder]");
	LFRtnOrderField order;
    memset(&order, 0, sizeof(LFRtnOrderField));
	order.OrderStatus = OrderStatus;
    order.VolumeTotalOriginal = VolumeTotal;
    order.VolumeTotal = VolumeTotal;
	strncpy(order.OrderRef, OrderRef, 21);
	strncpy(order.InstrumentID, InstrumentID, 31);
	order.RequestID = reqID;
	strcpy(order.ExchangeID, "Daybit");
	strncpy(order.UserID, unit.api_key.c_str(), 16);
	order.TimeCondition = LF_CHAR_GTC;
	order.Direction = direction;
    order.OrderPriceType = LF_CHAR_LimitPrice;
	unit.ordersLocalMap.insert(std::make_pair(ref, std::make_pair(order,input)));
    KF_LOG_INFO(logger, "[addNewOrder] (InstrumentID) " << InstrumentID
                                                                       << " (OrderRef) " << OrderRef
                                                                       << "(VolumeTraded)" << VolumeTotal);
}


void TDEngineDaybit::set_reader_thread()
{
    ITDEngine::set_reader_thread();

    KF_LOG_INFO(logger, "[set_reader_thread] ws_thread start on TDEngineDaybit::wsloop");
    ws_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineDaybit::wsloop, this)));
    heartbeat_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineDaybit::heartbeat_loop, this)));
}


void TDEngineDaybit::wsloop()
{
    KF_LOG_INFO(logger, "[loop] (isRunning) " << isRunning);
    while(isRunning)
    {
        int n = lws_service( context, base_interval_ms );
        //std::cout << " 3.1415 loop() lws_service (n)" << n << std::endl;
    }
}

void TDEngineDaybit::printResponse(const Value& d)
{
    if(d.IsObject())
    {
        StringBuffer buffer;
        Writer<StringBuffer> writer(buffer);
        d.Accept(writer);
        KF_LOG_INFO(logger, "[printResponse] (text) " << buffer.GetString());
    }
}



std::string TDEngineDaybit::getResponse(Value& payload, Value& response)
{
     std::string  retMsg ="";
     if (payload.IsObject()) 
     {
         if(payload.HasMember("status") && payload.HasMember("response"))
        {
            std::string status = payload["status"].GetString();
            response = payload["response"].GetObject();
            if(status != "ok")
            {
                if(response.HasMember("error_code"))
                {
                    retMsg = response["error_code"].GetString();
                    KF_LOG_ERROR(logger, "[getResponse] error (code)"<< retMsg);
                }
                else
                {
                    retMsg = "unkown error";
                    KF_LOG_ERROR(logger, "[getResponse] error (message) unkown error" );
                }
            }
            else if(!response.HasMember("data"))
            {
                retMsg = "join ok";
                KF_LOG_ERROR(logger, "[getResponse]  (message)" << retMsg);
            }
            else
            {
                response = response["data"];//.GetObject();
            }
        } 
        else if(payload.HasMember("data"))
        {
            response = payload["data"];
        }
     }
     printResponse(payload);
     return retMsg;
}


void TDEngineDaybit::get_account(AccountUnitDaybit& unit, Document& json)
{
    KF_LOG_INFO(logger, "[get_account]");
    
}


void TDEngineDaybit::cancel_all_orders(AccountUnitDaybit& unit)
{
    KF_LOG_INFO(logger, "[cancel_all_orders]");
    
    auto req = createCancelAllOrdersReq(unit.mapSubscribeRef[TOPIC_API]);
    unit.listMessageToSend.push(req);
    KF_LOG_INFO(logger, "[cancel_all_orders] (joinref) " << unit.mapSubscribeRef[TOPIC_API] << " (ref)" << getRef() << "(msg) " << req);
}


void TDEngineDaybit::cancel_order(AccountUnitDaybit& unit, int64_t orderId)
{
    KF_LOG_INFO(logger, "[cancel_order]");  
    //unit.listMessageToSend.push(createJoinReq(getJoinRef(),"/api"));
    auto req = createCancelOrderReq(unit.mapSubscribeRef[TOPIC_API],orderId);
    unit.listMessageToSend.push(req);
    KF_LOG_INFO(logger, "[cancel_order] (joinref) " << unit.mapSubscribeRef[TOPIC_API] << " (ref)" << getRef() << "(msg) " << req);	
}



int64_t TDEngineDaybit::getTimestamp()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp;
}




void TDEngineDaybit::on_lws_connection_error(struct lws* conn)
{
    KF_LOG_ERROR(logger, "TDEngineDaybit::on_lws_connection_error.");
    //market logged_in false;
    AccountUnitDaybit& unit = findAccountUnitByWebsocketConn(conn);
    unit.logged_in = false;
    KF_LOG_ERROR(logger, "TDEngineDaybit::on_lws_connection_error. login again.");

    InitSubscribeMsg(unit);
    long timeout_nsec = 0;
    lws_login(unit, timeout_nsec);
}

int TDEngineDaybit::on_lws_write(struct lws* conn)
{
    
    KF_LOG_INFO(logger, "TDEngineDaybit::on_lws_write");
    std::lock_guard<std::mutex> lck(unit_mutex);
    //KF_LOG_INFO(logger,"TDEngineDaybit::lws_write_subscribe");
    auto& unit = findAccountUnitByWebsocketConn(conn);
    int ret = 0;
    if(unit.logged_in && unit.listMessageToSend.size() > 0) 
    {        
        auto reqMsg = unit.listMessageToSend.front();
        int length = reqMsg.length();
        unsigned char *msg  = new unsigned char[LWS_PRE+ length];
        memset(&msg[LWS_PRE], 0, length);
        KF_LOG_INFO(logger, "TDEngineDaybit::on_lws_write: " + reqMsg);
        strncpy((char *)msg+LWS_PRE, reqMsg.c_str(), length);
        ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);
        unit.listMessageToSend.pop();
        if(unit.listMessageToSend.size() > 0)
        {    //still has pending send data, emit a lws_callback_on_writable()
            lws_callback_on_writable( conn );
        }
    }  
    return ret;
}

void TDEngineDaybit::lws_login(AccountUnitDaybit& unit, long timeout_nsec) {
    KF_LOG_INFO(logger, "TDEngineDaybit::lws_login:");
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
        KF_LOG_INFO(logger, "TDEngineDaybit::lws_login: context created.");
    }

    if (context == NULL) {
        KF_LOG_ERROR(logger, "TDEngineDaybit::lws_login: context is NULL. return");
        return;
    }

    int logs = LLL_ERR | LLL_DEBUG | LLL_WARN;
    lws_set_log_level(logs, NULL);

    struct lws_client_connect_info ccinfo = {0};

    static std::string host  = unit.baseUrl;
    static std::string path = unit.path+ "?api_key="+unit.api_key+"&api_secret="+unit.secret_key;
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
    KF_LOG_INFO(logger, "TDEngineDaybit::lws_login: Connecting to " <<  ccinfo.host << ":" << ccinfo.port << ":" << ccinfo.path);

    if (unit.websocketConn == NULL) {
        KF_LOG_ERROR(logger, "TDEngineDaybit::lws_login: wsi create error.");
        return;
    }
    KF_LOG_INFO(logger, "TDEngineDaybit::lws_login: wsi create success.");
}


void TDEngineDaybit::on_lws_data(struct lws* conn, const char* data, size_t len) {
    AccountUnitDaybit &unit = findAccountUnitByWebsocketConn(conn);
    KF_LOG_INFO(logger, "TDEngineDaybit::on_lws_data: " << data);
    Document json;
    json.Parse(data,len);
    if (json.HasParseError() || !json.IsObject()) {
        KF_LOG_ERROR(logger, "TDEngineDaybit::on_lws_data. parse json error: " << data);        
    }
	else if(json.HasMember("topic") && json.HasMember("payload") && json.HasMember("ref"))
	{
		std::string  topic = json["topic"].GetString();
        Value payload = json["payload"].GetObject();
        int64_t ref = json["ref"].IsNull() ? -1 : atoll(json["ref"].GetString());
        Value response;
        auto errorMsg = getResponse(payload,response);
        if(errorMsg.empty())
        {           
            if (topic == TOPIC_ORDER)
            {               
                onRtnOrder(conn, response);
            }
            else if (topic == TOPIC_TRADE)
            {
                onRtnTrade(conn, response);
            }
            else if(topic == TOPIC_API)
            {
                if(response.IsObject() && response.HasMember("server_time"))
                {
                    m_time_diff_with_server = response["server_time"].GetInt64() - getTimestamp();
                    std::cout << "server_time:" <<response["server_time"].GetInt64() << " diff" << m_time_diff_with_server<<std::endl;
                    isSyncServerTime = true;
                    InitSubscribeMsg(unit,false);
                }
                else
                    onRspOrder(conn,response,ref);
            }
            else if(topic == TOPIC_MARKET)
            {
                onRtnMarket(conn,response);
            }
        }
        else if( errorMsg == "join ok")
        {
            std::lock_guard<std::mutex> lck(unit_mutex);
            auto it = unit.mapSubscribeRef.find(topic);
            if(it != unit.mapSubscribeRef.end())
            {
                if(!isSyncServerTime)
                {
                    if(topic == TOPIC_API)
                    {
                        auto req = createGetServerTimeReq(it->second);
                        unit.listMessageToSend.push(req);
                    }
                }
                else
                {
                    std::string req="";   
                    if (topic == TOPIC_ORDER)
                    {               
                        req = createSubscribeOrderReq(it->second);
                    }
                    else if (topic == TOPIC_TRADE)
                    {
                        req = createSubscribeTradeReq(it->second);
                    }
                    else if(topic == TOPIC_API)
                    {                      
                        req = createCancelAllOrdersReq(it->second);                    
                    }
                    else if(topic == TOPIC_MARKET)
                    {
                        req = createSubscribeMarketReq(it->second);
                    }
                    unit.listMessageToSend.push(req);
                }
                //unit.mapSubscribeRef.erase(it);    
            }
        }
        else
        {
            if(topic == TOPIC_API)
            {
                onRspError(conn,errorMsg,ref);
            }
        }
	}
}



AccountUnitDaybit& TDEngineDaybit::findAccountUnitByWebsocketConn(struct lws * websocketConn)
{
    for (size_t idx = 0; idx < account_units.size(); idx++) {
        AccountUnitDaybit &unit = account_units[idx];
        if(unit.websocketConn == websocketConn) {
            return unit;
        }
    }
    return account_units[0];
}


void TDEngineDaybit::onRtnOrder(struct lws * websocketConn, Value& response)
{
    KF_LOG_INFO(logger, "TDEngineDaybit::onRtnOrder");
    AccountUnitDaybit &unit = findAccountUnitByWebsocketConn(websocketConn);
    std::lock_guard<std::mutex> lck(unit_mutex);
    if(response.IsArray())
    {
		for (SizeType index = 0; index < response.Size(); ++index)
		{
			auto& data = response[index];	
            if(data.HasMember("data") && data["data"].IsArray())
            {		
                auto& dataItems = data["data"];
		        for (SizeType i = 0; i < dataItems.Size(); ++i)
                {
                    auto& order = dataItems[i];
                    if(order.HasMember("id") && order.HasMember("status") && order.HasMember("close_type") && order.HasMember("unfilled")
                    && order.HasMember("sell") && order.HasMember("filled") && order.HasMember("filled_quote"))
                    {
                        int64_t OrderRef= order["id"].GetInt64();
                        auto it = unit.ordersMap.find(OrderRef);
                        if (it == unit.ordersMap.end())
                        { 
                            KF_LOG_ERROR(logger, "TDEngineDaybit::onRtnOrder,no order match");
                            continue;
                        }
                        LFRtnOrderField rtn_order = it->second;
                       
                        rtn_order.VolumeTotal = int64_t(order["unfilled"].GetDouble()*scale_offset);	                                                                       
                        if(order["sell"].GetBool())
                        {
                            rtn_order.VolumeTraded = int64_t(order["filled"].GetDouble()*scale_offset);
                        }
                        else
                        {
                            rtn_order.VolumeTraded = int64_t(order["filled_quote"].GetDouble()*scale_offset);
                        }

                        std::string status = order["status"].GetString();
                        if(status == "closed")
                        {
                            rtn_order.OrderStatus = GetOrderStatus(order["close_type"].GetString());
                        }
                        else if(rtn_order.VolumeTraded != it->second.VolumeTraded)
                        {
                            rtn_order.OrderStatus = LF_CHAR_PartTradedQueueing;
                        }
                        else
                        {
                            rtn_order.OrderStatus = GetOrderStatus(status);
                        }  
                        if(rtn_order.OrderStatus != it->second.OrderStatus || rtn_order.VolumeTraded != it->second.VolumeTraded)
                        {
                            KF_LOG_INFO(logger, "TDEngineDaybit::onRtnOrder,rtn_order");
                            on_rtn_order(&rtn_order);
                            raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                source_id, MSG_TYPE_LF_RTN_ORDER_DAYBIT,
                                1, (rtn_order.RequestID > 0) ? rtn_order.RequestID : -1);
                            if (rtn_order.OrderStatus == LF_CHAR_AllTraded || rtn_order.OrderStatus == LF_CHAR_PartTradedNotQueueing ||
                                rtn_order.OrderStatus == LF_CHAR_Canceled || rtn_order.OrderStatus == LF_CHAR_NoTradeNotQueueing || rtn_order.OrderStatus == LF_CHAR_Error)
                            {
                                unit.ordersMap.erase(it);
                            }
                        }
                        else
                        {
                             KF_LOG_INFO(logger, "TDEngineDaybit::onRtnOrder,order no change");
                        }
                    }
                }
            }
		}
       
    }    
    else
    {
        KF_LOG_ERROR(logger, "TDEngineDaybit::onRtnOrder unknown message");    
    }
}
void TDEngineDaybit::onRspOrder(struct lws* conn, Value& rsp,int64_t ref) 
{
	KF_LOG_INFO(logger, "TDEngineDaybit::onRspOrder");
    AccountUnitDaybit &unit = findAccountUnitByWebsocketConn(conn);
	std::lock_guard<std::mutex> lck(unit_mutex);
    
    auto it = unit.ordersLocalMap.find(ref);
    if(it == unit.ordersLocalMap.end())
    {
        KF_LOG_ERROR(logger, "TDEngineDaybit::onRspOrder,no order match (ref)" << ref);
        return;
    }
    if(rsp.IsObject() && rsp.HasMember("id"))
    {
        auto& rtnOrder = it->second.first;
        unit.ordersMap.insert(std::make_pair(rsp["id"].GetInt64(),rtnOrder));
        KF_LOG_INFO(logger, "TDEngineDaybit::onRspOrder,rtn_order");
		on_rtn_order(&rtnOrder);
		raw_writer->write_frame(&rtnOrder, sizeof(LFRtnOrderField),source_id, MSG_TYPE_LF_RTN_ORDER_DAYBIT,1, (rtnOrder.RequestID > 0) ? rtnOrder.RequestID : -1);
        unit.ordersLocalMap.erase(it);
    }
    else
    {
        KF_LOG_ERROR(logger, "TDEngineDaybit::onRspOrder unknown message");       
    }
    

}
void TDEngineDaybit::onRspError(struct lws * conn, std::string errorMsg,int64_t ref)
{
    KF_LOG_INFO(logger, "TDEngineDaybit::onRspError");
    AccountUnitDaybit &unit = findAccountUnitByWebsocketConn(conn);
	std::lock_guard<std::mutex> lck(unit_mutex);
    
    auto it = unit.ordersLocalMap.find(ref);
    if(it == unit.ordersLocalMap.end())
    {
        //KF_LOG_ERROR(logger, "TDEngineDaybit::onRspError,no order match (ref)" << ref);
        return;
    }
   
    else
    {
        auto& inputOrder = it->second.second;
        on_rsp_order_insert(&inputOrder, it->second.first.RequestID, -1, errorMsg.c_str());
        KF_LOG_ERROR(logger, "TDEngineDaybit::onRspError on_rsp_order_insert");   
        unit.ordersLocalMap.erase(it);    
    }
}
void TDEngineDaybit::onRtnTrade(struct lws * websocketConn, Value& response)
{ 
	KF_LOG_ERROR(logger, "TDEngineDaybit::onRtnTrade");
	AccountUnitDaybit &unit = findAccountUnitByWebsocketConn(websocketConn);
	std::lock_guard<std::mutex> lck(unit_mutex);
    if(response.IsArray())
    {
		for (SizeType index = 0; index < response.Size(); ++index)
		{
			auto& data = response[index];	
            if(data.HasMember("data") && data["data"].IsArray())
            {		
                auto& dataItems = data["data"];
		        for (SizeType i = 0; i < dataItems.Size(); ++i)
                {
                    auto& trade = dataItems[i];
                    if(trade.HasMember("price") && trade.HasMember("order_id") &&  trade.HasMember("sell") && trade.HasMember("quote_amount") 
                    && trade.HasMember("base_amount") )	
                    {	
			        LFRtnTradeField rtn_trade;
                        memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
                        int64_t id = trade["order_id"].GetInt64();
                        auto it = unit.ordersMap.find(id);
                        if (it == unit.ordersMap.end())
                        {	
                            KF_LOG_ERROR(logger, "TDEngineDaybit::onRtnTrade,not match" << rtn_trade.OrderRef);
                            continue;
                        }
                        auto& order = it->second;
                        strncpy(rtn_trade.OrderRef, order.OrderRef, 13);
                        strcpy(rtn_trade.ExchangeID, "Daybit");
                        strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
                        strncpy(rtn_trade.InstrumentID, order.InstrumentID, 31);
                        rtn_trade.Direction = order.Direction;	
                        if(trade["sell"].GetBool())
                        {
                            rtn_trade.Volume = int64_t(trade["base_amount"].GetDouble()*scale_offset);
                        }
                        else
                        {
                            rtn_trade.Volume = int64_t(trade["quote_amount"].GetDouble()*scale_offset);
                        }					        
                        rtn_trade.Price = int64_t(trade["price"].GetDouble()*scale_offset);
                        
                        KF_LOG_ERROR(logger, "TDEngineDaybit::onTrade,rtn_trade");
                        on_rtn_trade(&rtn_trade);
                        raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),source_id, MSG_TYPE_LF_RTN_TRADE_DAYBIT, 1, -1);
                    }
                }
            }
		}
	}
    else
    {
       KF_LOG_ERROR(logger, "TDEngineDaybit::onRtnTrade unknown message");     
    }
}
void TDEngineDaybit::onRtnMarket(struct lws * websocketConn, Value& response)
{
    AccountUnitDaybit &unit = findAccountUnitByWebsocketConn(websocketConn);
    if(response.IsArray() && !response.Empty())
    {
        loadExchangeOrderFilters(unit,response);
    }
    else
    {
        KF_LOG_ERROR(logger, "TDEngineDaybit::onRtnTrade unknown message");
    }
    
}
std::string TDEngineDaybit::createHeartBeatReq()
{
    Document doc;
    doc.SetObject();
    Document::AllocatorType& allocator = doc.GetAllocator();
    std::string strRef = std::to_string(makeRef());
    Value joinref(rapidjson::kNullType);
    Value payload_obj(rapidjson::kObjectType);
    doc.AddMember(StringRef("join_ref"),joinref,allocator);
    doc.AddMember(StringRef("ref"),StringRef(strRef.c_str()),allocator);
    doc.AddMember(StringRef("topic"),StringRef("phoenix"),allocator);
    doc.AddMember(StringRef("event"),StringRef("heartbeat"),allocator);
    doc.AddMember(StringRef("payload"),payload_obj,allocator);
    //doc.AddMember(StringRef("timeout"),3000,allocator);
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    doc.Accept(writer);
    return buffer.GetString();
}
std::string TDEngineDaybit::createJoinReq(int64_t joinref,const std::string& topic)
{
    Value obj(rapidjson::kObjectType);
    return createPhoenixMsg(joinref,topic,"phx_join",obj);
}
std::string TDEngineDaybit::createLeaveReq(int64_t joinref,const std::string& topic)
{
    Value obj(rapidjson::kObjectType);
    return createPhoenixMsg(joinref,topic,"phx_leave",obj);
}
std::string fToa(double src)
{
    char strTmp[20]{0};
    sprintf(strTmp,"%.8f",src+0.000000001);
    return strTmp;
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
std::string TDEngineDaybit::createPhoenixMsg(int64_t joinref,const std::string& topic,const std::string& event,rapidjson::Value& payload)
{
    Document doc;
    doc.SetObject();
    Document::AllocatorType& allocator = doc.GetAllocator();
    std::string strRef = std::to_string(makeRef());
    doc.AddMember(StringRef("join_ref"),StringRef(strRef.c_str()),allocator);
    doc.AddMember(StringRef("ref"),StringRef(strRef.c_str()),allocator);
    doc.AddMember(StringRef("topic"),StringRef(topic.c_str()),allocator);
    doc.AddMember(StringRef("event"),StringRef(event.c_str()),allocator);
    doc.AddMember(StringRef("payload"),payload,allocator);
    //doc.AddMember(StringRef("timeout"),3000,allocator);
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    doc.Accept(writer);
    return buffer.GetString();
}

std::string TDEngineDaybit::createNewOrderReq(int64_t joinref,double amount,double price,const std::string& symbol,bool isSell )
{
    Document doc;
    doc.SetObject();
    Document::AllocatorType& allocator = doc.GetAllocator();
    Value payload_obj(rapidjson::kObjectType);
    payload_obj.AddMember(StringRef("timestamp"),getTimestamp() + m_time_diff_with_server,allocator);
    payload_obj.AddMember(StringRef("cond_type"),StringRef("none"),allocator);
    payload_obj.AddMember(StringRef("role"),StringRef("both"),allocator);
    std::string strPrice = fToa(price);
    std::string strAmount = fToa(amount);
    payload_obj.AddMember(StringRef("price"),StringRef(strPrice.c_str()),allocator);
    payload_obj.AddMember(StringRef("amount"),StringRef(strAmount.c_str()),allocator);
    auto pairCoin = SplitCoinPair(symbol);
    payload_obj.AddMember(StringRef("base"),StringRef(pairCoin.second.c_str()),allocator);
    payload_obj.AddMember(StringRef("quote"),StringRef(pairCoin.first.c_str()),allocator);
    payload_obj.AddMember(StringRef("sell"),isSell,allocator);
    payload_obj.AddMember(StringRef("timeout"),-1,allocator);
    return createPhoenixMsg(joinref,TOPIC_API,"create_order",payload_obj);
}
std::string TDEngineDaybit::createCancelOrderReq(int64_t joinref,int64_t orderID)
{
    Document doc;
    doc.SetObject();
    Document::AllocatorType& allocator = doc.GetAllocator();
    Value payload_obj(rapidjson::kObjectType);
    payload_obj.AddMember(StringRef("timestamp"),getTimestamp(),allocator);
    payload_obj.AddMember(StringRef("order_id"),orderID,allocator);
    payload_obj.AddMember(StringRef("timeout"),-1,allocator);
    return createPhoenixMsg(joinref,TOPIC_API,"cancel_order",payload_obj);
}
std::string TDEngineDaybit::createCancelAllOrdersReq(int64_t joinref)
{
    Document doc;
    doc.SetObject();
    Document::AllocatorType& allocator = doc.GetAllocator();
    Value payload_obj(rapidjson::kObjectType);
    payload_obj.AddMember(StringRef("timestamp"),getTimestamp() + m_time_diff_with_server,allocator);
    //payload_obj.AddMember("order_id",orderID,allocator);
    payload_obj.AddMember(StringRef("timeout"),-1,allocator);
    std::cout << "server_time_diff" << m_time_diff_with_server<<std::endl;
    return createPhoenixMsg(joinref,TOPIC_API,"cancel_all_my_orders",payload_obj);
}
std::string TDEngineDaybit::createSubscribeOrderReq(int64_t joinref)
{
    Document doc;
    doc.SetObject();
    Document::AllocatorType& allocator = doc.GetAllocator();
    Value payload_obj(rapidjson::kObjectType);
    payload_obj.AddMember(StringRef("timestamp"),getTimestamp() + m_time_diff_with_server,allocator);
    //payload_obj.AddMember("closed",false,allocator);
    payload_obj.AddMember(StringRef("timeout"),-1,allocator);
    std::cout << "server_time_diff" << m_time_diff_with_server<<std::endl;
    return createPhoenixMsg(joinref,TOPIC_ORDER,"request",payload_obj);
}

std::string TDEngineDaybit::createSubscribeTradeReq(int64_t joinref)
{
    Document doc;
    doc.SetObject();
    Document::AllocatorType& allocator = doc.GetAllocator();
    Value payload_obj(rapidjson::kObjectType);
    payload_obj.AddMember(StringRef("timestamp"),getTimestamp() + m_time_diff_with_server,allocator);
    payload_obj.AddMember(StringRef("timeout"),-1,allocator);
    //payload_obj.AddMember("closed",false,allocator);
    std::cout << "server_time_diff" << m_time_diff_with_server<<std::endl;
    return createPhoenixMsg(joinref,TOPIC_TRADE,"request",payload_obj);
}
std::string TDEngineDaybit::createSubscribeMarketReq(int64_t joinref)
{
    Document doc;
    doc.SetObject();
    Document::AllocatorType& allocator = doc.GetAllocator();
    Value payload_obj(rapidjson::kObjectType);
    payload_obj.AddMember(StringRef("timestamp"),getTimestamp() + m_time_diff_with_server,allocator);
    payload_obj.AddMember(StringRef("timeout"),-1,allocator);
    //payload_obj.AddMember("closed",false,allocator);
    std::cout << "server_time_diff" << m_time_diff_with_server<<std::endl;
    return createPhoenixMsg(joinref,TOPIC_MARKET,"request",payload_obj);
}

std::string TDEngineDaybit::createGetServerTimeReq(int64_t joinref)
{
    Document doc;
    doc.SetObject();
    Document::AllocatorType& allocator = doc.GetAllocator();
    Value payload_obj(rapidjson::kObjectType);
    payload_obj.AddMember(StringRef("timestamp"),getTimestamp() + m_time_diff_with_server,allocator);
    //payload_obj.AddMember("closed",false,allocator);
    return createPhoenixMsg(joinref,TOPIC_API,"get_server_time",payload_obj);
}
#define GBK2UTF8(msg) kungfu::yijinjing::gbk2utf8(string(msg))

BOOST_PYTHON_MODULE(libdaybittd)
{
    using namespace boost::python;
    class_<TDEngineDaybit, boost::shared_ptr<TDEngineDaybit> >("Engine")
            .def(init<>())
            .def("init", &TDEngineDaybit::initialize)
            .def("start", &TDEngineDaybit::start)
            .def("stop", &TDEngineDaybit::stop)
            .def("logout", &TDEngineDaybit::logout)
            .def("wait_for_stop", &TDEngineDaybit::wait_for_stop);
}
