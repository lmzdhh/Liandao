#include "TDEngineBinance.h"
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
#include <queue>
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
using cpr::Interface;
using cpr::Put;

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


#define HTTP_CONNECT_REFUSED 429
#define HTTP_CONNECT_BANS	418

USING_WC_NAMESPACE



static TDEngineBinance* global_td = nullptr;
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
            if(global_td) {
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

AccountUnitBinance::AccountUnitBinance()
{
    mutex_weight = new std::mutex();
    mutex_handle_429 = new std::mutex();
    mutex_order_and_trade = new std::mutex();
}
AccountUnitBinance::~AccountUnitBinance()
{
    if(nullptr != mutex_handle_429)
        delete mutex_handle_429;
    if(nullptr != mutex_handle_429)
        delete mutex_weight;
    if(nullptr != mutex_handle_429)
        delete mutex_order_and_trade;
}
AccountUnitBinance::AccountUnitBinance(const AccountUnitBinance& source)
{
    api_key =source.api_key;
    secret_key = source.secret_key;
    listenKey= source.listenKey;
    // internal flags
    logged_in = source.logged_in;
    newOrderStatus = source.newOrderStatus;
    pendingOrderStatus= source.pendingOrderStatus;
    newTradeStatus= source.newTradeStatus;
    pendingTradeStatus= source.pendingTradeStatus;

    newOnRtnTrades= source.newOnRtnTrades;
    pendingOnRtnTrades= source.pendingOnRtnTrades;
    whiteListInstrumentIDs= source.whiteListInstrumentIDs;
    sendOrderFilters= source.sendOrderFilters;
    ordersMap= source.ordersMap;
    // the trade id that has been called on_rtn_trade. Do not send it again.
    newSentTradeIds= source.newSentTradeIds;
    sentTradeIds= source.sentTradeIds;

    coinPairWhiteList= source.coinPairWhiteList;
    positionWhiteList= source.positionWhiteList;

    
    order_total_count = source.order_total_count;

    weight_count = source.weight_count;
    mutex_weight = new std::mutex();
    weight_data_queue = source.weight_data_queue;

    time_queue= source.time_queue;
    bHandle_429 = source.bHandle_429;
    mutex_handle_429 = new std::mutex();
    startTime_429 = source.startTime_429;
    mutex_order_and_trade = new std::mutex();
    context = source.context;
    websocketConn= source.websocketConn;
}
std::mutex http_mutex;
std::mutex account_mutex;
TDEngineBinance::TDEngineBinance(): ITDEngine(SOURCE_BINANCE)
{
    logger = yijinjing::KfLog::getLogger("TradeEngine.Binance");
    KF_LOG_INFO(logger, "[ATTENTION] default to confirm settlement and no authentication!");

    
    
}

TDEngineBinance::~TDEngineBinance()
{
}

void TDEngineBinance::init()
{
    ITDEngine::init();
    JournalPair tdRawPair = getTdRawJournalPair(source_id);
    raw_writer = yijinjing::JournalSafeWriter::create(tdRawPair.first, tdRawPair.second, "RAW_" + name());
}

void TDEngineBinance::pre_load(const json& j_config)
{
    KF_LOG_INFO(logger, "[pre_load]");
}

void TDEngineBinance::resize_accounts(int account_num)
{
    
}

TradeAccount TDEngineBinance::load_account(int idx, const json& j_config)
{
    KF_LOG_INFO(logger, "[load_account]");

	string interfaces;
	int interface_timeout = 300000;
	if(j_config.find("interfaces") != j_config.end()) {
		interfaces = j_config["interfaces"].get<string>();
	}

	if(j_config.find("interface_timeout_ms") != j_config.end()) {
		interface_timeout = j_config["interface_timeout_ms"].get<int>();
	}

	if(j_config.find("interface_switch") != j_config.end()) {
		m_interface_switch = j_config["interface_switch"].get<int>();
	}

	KF_LOG_INFO(logger, "[load_account] interface switch: " << m_interface_switch);
	if (m_interface_switch > 0) {
		m_interfaceMgr.init(interfaces, interface_timeout);
		m_interfaceMgr.print();
	}
	
    //string api_key = j_config["APIKey"].get<string>();
    //string secret_key = j_config["SecretKey"].get<string>();
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();

    if(j_config.find("baseUrl") != j_config.end()) {
        restBaseUrl = j_config["baseUrl"].get<string>();
    }
    if(j_config.find("wsUrl") != j_config.end()) {
        wsBaseUrl = j_config["wsUrl"].get<string>();
    }
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

    if(j_config.find("order_count_per_second") != j_config.end()) {
        order_count_per_second = j_config["order_count_per_second"].get<int>();
    }
    KF_LOG_INFO(logger, "[load_account] (order_count_per_second)" << order_count_per_second);

    if(j_config.find("request_weight_per_minute") != j_config.end()) {
        request_weight_per_minute = j_config["request_weight_per_minute"].get<int>();
    }
    KF_LOG_INFO(logger, "[load_account] (request_weight_per_minute)" << request_weight_per_minute);

    if(j_config.find("prohibit_order_ms") != j_config.end()) {
        prohibit_order_ms = j_config["prohibit_order_ms"].get<int>();
    }
    KF_LOG_INFO(logger, "[load_account] (prohibit_order_ms)" << prohibit_order_ms);

    if(j_config.find("default_429_rest_interval_ms") != j_config.end()) {
        default_429_rest_interval_ms = j_config["default_429_rest_interval_ms"].get<int>();
    }
    KF_LOG_INFO(logger, "[load_account] (default_429_rest_interval_ms)" << default_429_rest_interval_ms);

    if(j_config.find("max_rest_retry_times") != j_config.end()) {
        max_rest_retry_times = j_config["max_rest_retry_times"].get<int>();
    }
    KF_LOG_INFO(logger, "[load_account] (max_rest_retry_times)" << max_rest_retry_times);


    if(j_config.find("retry_interval_milliseconds") != j_config.end()) {
        retry_interval_milliseconds = j_config["retry_interval_milliseconds"].get<int>();
    }
    KF_LOG_INFO(logger, "[load_account] (retry_interval_milliseconds)" << retry_interval_milliseconds);

    if(j_config.find("cancel_timeout_ms") != j_config.end()) {
        cancel_timeout_milliseconds = j_config["cancel_timeout_ms"].get<int>();
    }
    KF_LOG_INFO(logger, "[load_account] (cancel_timeout_ms)" << cancel_timeout_milliseconds);

    
    // internal load
    auto iter = j_config.find("users");
    if (iter != j_config.end() && iter.value().size() > 0)
    { 
        for (auto& j_account: iter.value())
        {
            AccountUnitBinance unit;
            unit.api_key = j_account["APIKey"].get<string>();
            unit.secret_key = j_account["SecretKey"].get<string>();

            unit.coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
            //unit.coinPairWhiteList.Debug_print();

            unit.positionWhiteList.ReadWhiteLists(j_config, "positionWhiteLists");
            //unit.positionWhiteList.Debug_print();
        
            if(unit.coinPairWhiteList.Size() == 0) {
                //display usage:
                KF_LOG_ERROR(logger, "TDEngineBinance::load_account: subscribeCoinBaseQuote is empty. please add whiteLists in kungfu.json like this :");
                KF_LOG_ERROR(logger, "\"whiteLists\":{");
                KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
                KF_LOG_ERROR(logger, "    \"btc_usdt\": \"BTCUSDT\",");
                KF_LOG_ERROR(logger, "     \"etc_eth\": \"ETCETH\"");
                KF_LOG_ERROR(logger, "},");
            }
            else
            {
                //cancel all openning orders on TD startup
                std::unordered_map<std::string, std::string>::iterator map_itr;
                map_itr = unit.coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().begin();
                while(map_itr != unit.coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().end())
                {
                    KF_LOG_INFO(logger, "[load_account] (api_key)" << unit.api_key << " (cancel_all_orders of instrumentID) of exchange coinpair: " << map_itr->second);

                    Document d;
                    get_open_orders(unit, map_itr->second.c_str(), d);
                    KF_LOG_INFO(logger, "[load_account] print get_open_orders");
                    printResponse(d);

                    if(!d.HasParseError() && d.IsArray()) { // expected success response is array
                        size_t len = d.Size();
                        KF_LOG_INFO(logger, "[load_account][get_open_orders] (length)" << len);
                        for (size_t i = 0; i < len; i++) {
                            if(d.GetArray()[i].IsObject() && d.GetArray()[i].HasMember("symbol") && d.GetArray()[i].HasMember("clientOrderId"))
                            {
                                if(d.GetArray()[i]["symbol"].IsString() && d.GetArray()[i]["clientOrderId"].IsString())
                                {
                                    std::string symbol = d.GetArray()[i]["symbol"].GetString();
                                    std::string orderRef = d.GetArray()[i]["clientOrderId"].GetString();
                                    Document cancelResponse;
                                    cancel_order(unit, symbol.c_str(), 0, orderRef.c_str(), "", cancelResponse);

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

                    map_itr++;
                }
            }
            account_units.emplace_back(unit);
        }
    }
    else
    {
        KF_LOG_ERROR(logger, "[load_account] no tarde account info !");
    }
    
    // set up
    TradeAccount account = {};
    //partly copy this fields
    strncpy(account.UserID, account_units[0].api_key.c_str(), 15);
    strncpy(account.Password, account_units[0].secret_key.c_str(), 20);
    KF_LOG_INFO(logger, "[load_account] SUCCESS !");
    return account;
}


void TDEngineBinance::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[connect]");
    for (size_t idx = 0; idx < account_units.size(); idx++)
    {
        AccountUnitBinance& unit = account_units[idx];

        KF_LOG_INFO(logger, "[connect] (api_key)" << unit.api_key);
        if (!unit.logged_in)
        {
            //exchange infos
            Document doc;
            get_exchange_infos(unit, doc);
            KF_LOG_INFO(logger, "[connect] get_exchange_infos");

            if(loadExchangeOrderFilters(unit, doc))
            {
                unit.logged_in = true;
                lws_login(unit, timeout_nsec);
            } else {
                KF_LOG_ERROR(logger, "[connect] logged_in = false for loadExchangeOrderFilters return false");
            }
            debug_print(unit.sendOrderFilters);
        }
    }
    //sync time of exchange
    getTimeDiffOfExchange(account_units[0]);
}

bool TDEngineBinance::loadExchangeOrderFilters(AccountUnitBinance& unit, Document &doc)
{
    KF_LOG_INFO(logger, "[loadExchangeOrderFilters]");
    if(doc.HasParseError() || !doc.IsObject())
    {
        return false;
    }
    if(doc.HasMember("symbols") && doc["symbols"].IsArray())
    {
        int symbolsCount = doc["symbols"].Size();
        for (int i = 0; i < symbolsCount; i++) {
            const rapidjson::Value& sym = doc["symbols"].GetArray()[i];
            std::string symbol = sym["symbol"].GetString();
            if(sym.IsObject() && sym.HasMember("filters") && sym["filters"].IsArray()) {
                int filtersCount = sym["filters"].Size();
                for (int j = 0; j < filtersCount; j++) {
                    const rapidjson::Value& filter = sym["filters"].GetArray()[j];
                    if (strcmp("PRICE_FILTER", filter["filterType"].GetString()) == 0) {
                        std::string tickSizeStr =  filter["tickSize"].GetString();
                        //KF_LOG_INFO(logger, "[loadExchangeOrderFilters] sendOrderFilters (symbol)" << symbol <<
                        //                                                                           " (tickSizeStr)" << tickSizeStr);
                        //0.0000100; 0.001;
                        unsigned int locStart = tickSizeStr.find( ".", 0 );
                        unsigned int locEnd = tickSizeStr.find( "1", 0 );
                        if( locStart != string::npos  && locEnd != string::npos ){
                            int num = locEnd - locStart;
                            SendOrderFilter afilter;
                            strncpy(afilter.InstrumentID, symbol.c_str(), 31);
                            afilter.ticksize = num;
                            unit.sendOrderFilters.insert(std::make_pair(symbol, afilter));
                            //KF_LOG_INFO(logger, "[loadExchangeOrderFilters] sendOrderFilters (symbol)" << symbol <<
                            //                                                                          " (tickSizeStr)" << tickSizeStr
                            //                                                                           <<" (tickSize)" << afilter.ticksize);
                        }
                    }
                }
            }
        }
    }
    return true;
}

void TDEngineBinance::debug_print(std::map<std::string, SendOrderFilter> &sendOrderFilters)
{
    std::map<std::string, SendOrderFilter>::iterator map_itr = sendOrderFilters.begin();
    while(map_itr != sendOrderFilters.end())
    {
        KF_LOG_INFO(logger, "[debug_print] sendOrderFilters (symbol)" << map_itr->first <<
                                                                                   " (tickSize)" << map_itr->second.ticksize);
        map_itr++;
    }
}

SendOrderFilter TDEngineBinance::getSendOrderFilter(AccountUnitBinance& unit, const char *symbol)
{
    auto map_itr = unit.sendOrderFilters.find(symbol);
    if(map_itr != unit.sendOrderFilters.end())
    {
        return map_itr->second;
    }
    SendOrderFilter defaultFilter;
    defaultFilter.ticksize = 8;
    strcpy(defaultFilter.InstrumentID, "notfound");
    return defaultFilter;
}

void TDEngineBinance::login(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[login]");
    connect(timeout_nsec);
}

void TDEngineBinance::logout()
{
    KF_LOG_INFO(logger, "[logout]");
}

void TDEngineBinance::release_api()
{
    KF_LOG_INFO(logger, "[release_api]");
}

bool TDEngineBinance::is_logged_in() const
{
    KF_LOG_INFO(logger, "[is_logged_in]");
    for (auto& unit: account_units)
    {
        if (!unit.logged_in)
            return false;
    }
    return true;
}

bool TDEngineBinance::is_connected() const
{
    KF_LOG_INFO(logger, "[is_connected]");
    return is_logged_in();
}



std::string TDEngineBinance::GetSide(const LfDirectionType& input) {
    if (LF_CHAR_Buy == input) {
        return "BUY";
    } else if (LF_CHAR_Sell == input) {
        return "SELL";
    } else {
        return "UNKNOWN";
    }
}

 LfDirectionType TDEngineBinance::GetDirection(std::string input) {
    if ("BUY" == input) {
        return LF_CHAR_Buy;
    } else if ("SELL" == input) {
        return LF_CHAR_Sell;
    } else {
        return LF_CHAR_Buy;
    }
}

std::string TDEngineBinance::GetType(const LfOrderPriceTypeType& input) {
    if (LF_CHAR_LimitPrice == input) {
        return "LIMIT";
    } else if (LF_CHAR_AnyPrice == input) {
        return "MARKET";
    } else {
        return "UNKNOWN";
    }
}

LfOrderPriceTypeType TDEngineBinance::GetPriceType(std::string input) {
    if ("LIMIT" == input) {
        return LF_CHAR_LimitPrice;
    } else if ("MARKET" == input) {
        return LF_CHAR_AnyPrice;
    } else {
        return '0';
    }
}

std::string TDEngineBinance::GetTimeInForce(const LfTimeConditionType& input) {
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

LfTimeConditionType TDEngineBinance::GetTimeCondition(std::string input) {
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

LfOrderStatusType TDEngineBinance::GetOrderStatus(std::string input) {
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
size_t current_account_idx = -1;
AccountUnitBinance& TDEngineBinance::get_current_account()
{
    current_account_idx++;
    current_account_idx %= account_units.size();
    return account_units[current_account_idx];
}
AccountUnitBinance& get_account_from_orderref(std::string& ref)
{

}

/**
 * req functions
 */
void TDEngineBinance::req_investor_position(const LFQryPositionField* data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_investor_position]");
    AccountUnitBinance& unit = account_units[0];
    KF_LOG_INFO(logger, "[req_investor_position] (api_key)" << unit.api_key);

    // User Balance
    Document d;
    get_account(unit, d);
    KF_LOG_INFO(logger, "[req_investor_position] get_account");
    printResponse(d);

    int errorId = 0;
    std::string errorMsg = "";
    if(d.HasParseError() )
    {
        errorId=100;
        errorMsg= "get_account http response has parse error. please check the log";
        KF_LOG_ERROR(logger, "[req_investor_position] get_account error! (rid)  -1 (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }

    if(!d.HasParseError() && d.IsObject() && d.HasMember("code") && d["code"].IsNumber())
    {
        errorId = d["code"].GetInt();
        if(d.HasMember("msg") && d["msg"].IsString())
        {
            errorMsg = d["msg"].GetString();
        }

        KF_LOG_ERROR(logger, "[req_investor_position] get_account failed! (rid)  -1 (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }
    send_writer->write_frame(data, sizeof(LFQryPositionField), source_id, MSG_TYPE_LF_QRY_POS_BINANCE, 1, requestId);

    LFRspPositionField pos;
    memset(&pos, 0, sizeof(LFRspPositionField));
    strncpy(pos.BrokerID, data->BrokerID, 11);
    strncpy(pos.InvestorID, data->InvestorID, 19);
    strncpy(pos.InstrumentID, data->InstrumentID, 31);
    pos.PosiDirection = LF_CHAR_Long;
    pos.Position = 0;

    std::vector<LFRspPositionField> tmp_vector;

    if(!d.HasParseError() && d.IsObject() && d.HasMember("balances"))
    {
        int len = d["balances"].Size();
        for ( int i  = 0 ; i < len ; i++ ) {
            std::string symbol = d["balances"].GetArray()[i]["asset"].GetString();
            std::string ticker = unit.positionWhiteList.GetKeyByValue(symbol);
            if(ticker.length() > 0) {
                strncpy(pos.InstrumentID, ticker.c_str(), 31);
                pos.Position = std::round(stod(d["balances"].GetArray()[i]["free"].GetString()) * scale_offset);
                tmp_vector.push_back(pos);
                KF_LOG_INFO(logger,  "[connect] (symbol)" << symbol << " (free)" <<  d["balances"].GetArray()[i]["free"].GetString()
                                                          << " (locked)" << d["balances"].GetArray()[i]["locked"].GetString());
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
        raw_writer->write_error_frame(&pos, sizeof(LFRspPositionField), source_id, MSG_TYPE_LF_RSP_POS_BINANCE, 1, requestId, errorId, errorMsg.c_str());
    }
}

void TDEngineBinance::req_qry_account(const LFQryAccountField *data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_qry_account]");
}


int64_t TDEngineBinance::fixPriceTickSize(int keepPrecision, int64_t price, bool isBuy)
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

void TDEngineBinance::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitBinance& unit = get_current_account();
    KF_LOG_DEBUG(logger, "[req_order_insert]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Tid)" << data->InstrumentID
                                              << " (OrderRef)" << data->OrderRef);
    send_writer->write_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BINANCE, 1/*ISLAST*/, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_insert]: not in WhiteList , ignore it: (rid)" << requestId << " (errorId)" <<
                errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BINANCE, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_insert] (exchange_ticker)" << ticker);

    double stopPrice = 0;
    double icebergQty = 0;
    Document d;

    SendOrderFilter filter = getSendOrderFilter(unit, ticker.c_str());

    int64_t fixedPrice = fixPriceTickSize(filter.ticksize, data->LimitPrice, LF_CHAR_Buy == data->Direction);

    KF_LOG_DEBUG(logger, "[req_order_insert] SendOrderFilter  (Tid)" << ticker <<
                                                                     " (LimitPrice)" << data->LimitPrice <<
                                                                     " (ticksize)" << filter.ticksize <<
                                                                     " (fixedPrice)" << fixedPrice);

    send_order(unit, ticker.c_str(), GetSide(data->Direction).c_str(), GetType(data->OrderPriceType).c_str(),
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
    if(!d.HasParseError() && d.IsObject() && d.HasMember("code") && d["code"].IsNumber())
    {
        errorId = d["code"].GetInt();
        if(d.HasMember("msg") && d["msg"].IsString())
        {
            errorMsg = d["msg"].GetString();
        }
        KF_LOG_ERROR(logger, "[req_order_insert] send_order failed! (rid)  -1 (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }

    //if(errorId != 0)
    {
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
    }
    raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BINANCE, 1, requestId, errorId, errorMsg.c_str());

    //paser the order/trade info in the response result
    if(!d.HasParseError() && d.IsObject() && !d.HasMember("code")&&d.HasMember("orderId"))
    {
        std::unique_lock<std::mutex> lck(account_mutex);
        mapInsertOrders.insert(std::make_pair(data->OrderRef,&unit));
        lck.unlock();
        std::string orderId=std::to_string(d["orderId"].GetInt64());
        strcpy(data->BusinessUnit,orderId.c_str());
        //order insert success,on_rtn_order with NotTouched status first
        onRtnNewOrder(data, unit, requestId);
        /*
        if(!d.HasMember("status"))
        {//no status, it is ACK
            onRspNewOrderACK(data, unit, d, requestId);
        } else {
            if(!d.HasMember("fills"))
            {
                // it is RESULT
                onRspNewOrderRESULT(data, unit, d, requestId);
            } else {
                // it is FULL
                onRspNewOrderFULL(data, unit, d, requestId);
            }
        }
        */
    }
}

void TDEngineBinance::onRtnNewOrder(const LFInputOrderField* data, AccountUnitBinance& unit, int requestId)
{
    LFRtnOrderField rtn_order;
    memset(&rtn_order, 0, sizeof(LFRtnOrderField));
    strcpy(rtn_order.ExchangeID, "binance");
    strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
    strncpy(rtn_order.InstrumentID, data->InstrumentID, 31);
    rtn_order.Direction = data->Direction;
    rtn_order.TimeCondition = data->TimeCondition;
    rtn_order.OrderPriceType = data->OrderPriceType;
    strncpy(rtn_order.OrderRef, data->OrderRef, 13);
    rtn_order.VolumeTraded = 0;
    rtn_order.VolumeTotalOriginal = data->Volume;
    rtn_order.VolumeTotal = data->Volume;
    rtn_order.LimitPrice = data->LimitPrice;
    rtn_order.RequestID = requestId;
    rtn_order.OrderStatus = LF_CHAR_NotTouched;
    on_rtn_order(&rtn_order);
    raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                            source_id, MSG_TYPE_LF_RTN_ORDER_BINANCE,
                            1/*islast*/, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);
    std::unique_lock<std::mutex> lck(*unit.mutex_order_and_trade);
    unit.ordersMap.insert(std::make_pair(data->BusinessUnit,rtn_order));
    lck.unlock();
    std::vector<std::string>::iterator it;
    for(it=unit.wsOrderStatus.begin();it!=unit.wsOrderStatus.end();it++){
        Document json;
        json.Parse((*it).c_str());
        string remoteOrderId=data->BusinessUnit;
        if(json.HasMember("i")){
            string wsOrderId=std::to_string(json["i"].GetInt64());
            if(remoteOrderId==wsOrderId){
                onOrder(unit,json);
                unit.wsOrderStatus.erase(it);
                it--;
            }
        }
    }
}
void TDEngineBinance::onRspNewOrderACK(const LFInputOrderField* data, AccountUnitBinance& unit, Document& result, int requestId)
{
    /*Response ACK:
                    {
                      "symbol": "BTCUSDT",
                      "orderId": 28,
                      "clientOrderId": "6gCrw2kRUAF9CvJDGP16IP",
                      "transactTime": 1507725176595
                    }
    */

    //if not Traded, add pendingOrderStatus for GetAndHandleOrderTradeResponse
    char noneStatus = LF_CHAR_NotTouched;
    int64_t binanceOrderId =  result["orderId"].GetInt64();
    addNewQueryOrdersAndTrades(unit, data->InstrumentID,data->LimitPrice, data->OrderRef, noneStatus, 0, data->Direction, binanceOrderId);
}


void TDEngineBinance::onRspNewOrderRESULT(const LFInputOrderField* data, AccountUnitBinance& unit, Document& result, int requestId)
{
    /*Response RESULT:
                    {
                      "symbol": "BTCUSDT",
                      "orderId": 28,
                      "clientOrderId": "6gCrw2kRUAF9CvJDGP16IP",
                      "transactTime": 1507725176595,
                      "price": "0.00000000",
                      "origQty": "10.00000000",
                      "executedQty": "10.00000000",
                      "status": "FILLED",
                      "timeInForce": "GTC",
                      "type": "MARKET",
                      "side": "SELL"
                    }

    */
    KF_LOG_DEBUG(logger, "TDEngineBinance::onRspNewOrderRESULT:");
    printResponse(result);

    // no strike price, dont emit OnRtnTrade
    LFRtnOrderField rtn_order;
    memset(&rtn_order, 0, sizeof(LFRtnOrderField));
    strcpy(rtn_order.ExchangeID, "binance");
    strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
    strncpy(rtn_order.InstrumentID, data->InstrumentID, 31);
    rtn_order.Direction = data->Direction;
    rtn_order.TimeCondition = data->TimeCondition;
    rtn_order.OrderPriceType = data->OrderPriceType;
    strncpy(rtn_order.OrderRef, result["clientOrderId"].GetString(), 13);
    rtn_order.VolumeTraded = std::round(stod(result["executedQty"].GetString()) * scale_offset);
    rtn_order.VolumeTotalOriginal = std::round(stod(result["origQty"].GetString()) * scale_offset);
    rtn_order.VolumeTotal = rtn_order.VolumeTotalOriginal - rtn_order.VolumeTraded;
    rtn_order.LimitPrice = data->LimitPrice;//std::round(stod(result["price"].GetString()) * scale_offset);
    rtn_order.RequestID = requestId;
    rtn_order.OrderStatus = GetOrderStatus(result["status"].GetString());
    if(rtn_order.OrderStatus != LF_CHAR_NotTouched)
    {
        on_rtn_order(&rtn_order);
        raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                source_id, MSG_TYPE_LF_RTN_ORDER_BINANCE,
                                1/*islast*/, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);
    }

    //if All Traded, emit OnRtnTrade
    if(rtn_order.OrderStatus == LF_CHAR_AllTraded)
    {
        LFRtnTradeField rtn_trade;
        memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
        strcpy(rtn_trade.ExchangeID, "binance");
        strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
        strncpy(rtn_trade.InstrumentID, data->InstrumentID, 31);
        strncpy(rtn_trade.OrderRef, result["clientOrderId"].GetString(), 13);
        rtn_trade.Direction = data->Direction;
        rtn_trade.Volume = std::round(stod(result["executedQty"].GetString()) * scale_offset);
        rtn_trade.Price = std::round(stod(result["price"].GetString()) * scale_offset);

        on_rtn_trade(&rtn_trade);
        raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
                                source_id, MSG_TYPE_LF_RTN_TRADE_BINANCE, 1/*islast*/, -1/*invalidRid*/);

        //this response has no tradeId, so dont call unit.newSentTradeIds.push_back(tradeid)
    }

    //if not All Traded, add pendingOrderStatus for GetAndHandleOrderTradeResponse
    if(rtn_order.VolumeTraded  < rtn_order.VolumeTotalOriginal )
    {
        int64_t binanceOrderId =  result["orderId"].GetInt64();
        addNewQueryOrdersAndTrades(unit, data->InstrumentID,data->LimitPrice,
                                       rtn_order.OrderRef, rtn_order.OrderStatus, rtn_order.VolumeTraded, data->Direction, binanceOrderId);
    }
}

void TDEngineBinance::onRspNewOrderFULL(const LFInputOrderField* data, AccountUnitBinance& unit, Document& result, int requestId)
{
    /*Response FULL:
                {
	"symbol": "BTCUSDT",
	"orderId": 144678401,
	"clientOrderId": "25",
	"transactTime": 1533717358045,
	"price": "0.00000000",
	"origQty": "0.01512200",
	"executedQty": "0.01512200",
	"cummulativeQuoteQty": "97.90379616",
	"status": "FILLED",
	"timeInForce": "GTC",
	"type": "MARKET",
	"side": "SELL",
	"fills": [{
		"price": "6474.49000000",
		"qty": "0.00957100",
		"commission": "0.00372804",
		"commissionAsset": "BNB",
		"tradeId": 61800504
	}, {
		"price": "6473.87000000",
		"qty": "0.00555100",
		"commission": "0.00216198",
		"commissionAsset": "BNB",
		"tradeId": 61800505
	}]
    }

    */

    LFRtnOrderField rtn_order;
    memset(&rtn_order, 0, sizeof(LFRtnOrderField));
    strcpy(rtn_order.ExchangeID, "binance");
    strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
    strncpy(rtn_order.InstrumentID, data->InstrumentID, 31);
    rtn_order.Direction = data->Direction;
    rtn_order.TimeCondition = data->TimeCondition;
    rtn_order.OrderPriceType = data->OrderPriceType;
    strncpy(rtn_order.OrderRef, result["clientOrderId"].GetString(), 13);
    rtn_order.RequestID = requestId;
    rtn_order.OrderStatus = GetOrderStatus(result["status"].GetString());

    uint64_t volumeTotalOriginal = std::round(stod(result["origQty"].GetString()) * scale_offset);
    //数量
    rtn_order.VolumeTotalOriginal = volumeTotalOriginal;

    bool isAllTraded = false;
    if (rtn_order.OrderStatus == LF_CHAR_AllTraded)
    {
        isAllTraded = true;
    }

    LFRtnTradeField rtn_trade;
    memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
    strcpy(rtn_trade.ExchangeID, "binance");
    strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
    strncpy(rtn_trade.InstrumentID, data->InstrumentID, 31);
    strncpy(rtn_trade.OrderRef, result["clientOrderId"].GetString(), 13);
    rtn_trade.Direction = data->Direction;
    rtn_order.LimitPrice = data->LimitPrice;
    //we have strike price, emit OnRtnTrade
    int fills_size = result["fills"].Size();

    for(int i = 0; i < fills_size; ++i)
    {
        uint64_t volume = std::round(stod(result["fills"].GetArray()[i]["qty"].GetString()) * scale_offset);
        int64_t price = std::round(stod(result["fills"].GetArray()[i]["price"].GetString()) * scale_offset);
        //今成交数量
        rtn_order.VolumeTraded += volume;
        //剩余数量
        //volumeTotalOriginal = volumeTotalOriginal - volume;
        rtn_order.VolumeTotal = volumeTotalOriginal - rtn_order.VolumeTraded;

        if(isAllTraded)
        {
            if(i == fills_size - 1) {
                //the last one
                rtn_order.OrderStatus = LF_CHAR_AllTraded;
            } else {
                rtn_order.OrderStatus = LF_CHAR_PartTradedQueueing;
            }
        }
        on_rtn_order(&rtn_order);
        raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                source_id, MSG_TYPE_LF_RTN_ORDER_BINANCE,
                                1/*islast*/, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);

        rtn_trade.Volume = volume;
        rtn_trade.Price = price;
        on_rtn_trade(&rtn_trade);
        raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
                                source_id, MSG_TYPE_LF_RTN_TRADE_BINANCE, 1/*islast*/, -1/*invalidRid*/);

        //this trade id has been send on_rtn_trade
        int64_t newTradeid = result["fills"].GetArray()[i]["tradeId"].GetInt64();
        addNewSentTradeIds(unit, newTradeid);
    }

    //if not All Traded, add pendingOrderStatus for GetAndHandleOrderTradeResponse
    if(rtn_order.VolumeTraded  < rtn_order.VolumeTotalOriginal )
    {
        int64_t binanceOrderId =  result["orderId"].GetInt64();
        LfOrderStatusType status =  rtn_order.OrderStatus;
        if( fills_size <= 0)
            status = LF_CHAR_NotTouched;
        addNewQueryOrdersAndTrades(unit, data->InstrumentID,data->LimitPrice,
                                       rtn_order.OrderRef,status, rtn_order.VolumeTraded, data->Direction, binanceOrderId);
    }
}


void TDEngineBinance::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time)
{
    std::unique_lock<std::mutex> lck(account_mutex);
    int errorId = 0;
    std::string errorMsg = "";
    auto it  = mapInsertOrders.find(data->OrderRef);
    if(it == mapInsertOrders.end())
    {
        errorId = 200;
        errorMsg = std::string(data->OrderRef) + "is not found, ignore it";
        KF_LOG_ERROR(logger, errorMsg << " (rid)" << requestId << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BINANCE, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    AccountUnitBinance& unit = *(it->second);
    lck.unlock();
    KF_LOG_DEBUG(logger, "[req_order_action]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Iid)" << data->InvestorID
                                              << " (OrderRef)" << data->OrderRef << " (KfOrderID)" << data->KfOrderID);

    send_writer->write_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BINANCE, 1, requestId);



    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + "not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_action]: not in WhiteList , ignore it. (rid)" << requestId << " (errorId)" <<
                                                                                      errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BINANCE, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_action] (exchange_ticker)" << ticker);

    Document d;
	cancel_order(unit, ticker.c_str(), 0, data->OrderRef, "", d);
    //    KF_LOG_INFO(logger, "[req_order_action] cancel_order");
    //    printResponse(d);

    if(d.HasParseError() )
    {
        errorId=100;
        errorMsg= "cancel_order http response has parse error. please check the log";
        KF_LOG_ERROR(logger, "[req_order_action] cancel_order error! (rid)" << requestId << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }
    if(!d.HasParseError() && d.IsObject() && d.HasMember("code") && d["code"].IsNumber())
    {
        errorId = d["code"].GetInt();
        if(d.HasMember("msg") && d["msg"].IsString())
        {
            errorMsg = d["msg"].GetString();
        }
        KF_LOG_ERROR(logger, "[req_order_action] cancel_order failed! (rid)  -1 (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }
    if(errorId != 0)
    {
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
    }
    else
    {
        mapCancelOrder.insert(std::make_pair(data->OrderRef,OrderActionInfo{getTimestamp(),*data,requestId}));
    }
    raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BINANCE, 1, requestId, errorId, errorMsg.c_str());
}

void TDEngineBinance::GetAndHandleOrderTradeResponse(AccountUnitBinance& unit)
{
    KF_LOG_INFO(logger, "[GetAndHandleOrderTradeResponse] (api_key)" << unit.api_key);
    if (!unit.logged_in)
    {
        //continue;
        return;
    }

    moveNewtoPending(unit);
    retrieveOrderStatus(unit);
    retrieveTradeStatus(unit);

    

    KF_LOG_INFO(logger, "[GetAndHandleOrderTradeResponse] (timeDiffOfExchange)" << timeDiffOfExchange);
}


void TDEngineBinance::moveNewtoPending(AccountUnitBinance& unit)
{
    std::lock_guard<std::mutex> guard_mutex(*unit.mutex_order_and_trade);

    std::vector<PendingBinanceOrderStatus>::iterator newOrderStatusIterator;
    for(newOrderStatusIterator = unit.newOrderStatus.begin(); newOrderStatusIterator != unit.newOrderStatus.end();)
    {
        unit.pendingOrderStatus.push_back(*newOrderStatusIterator);
        newOrderStatusIterator = unit.newOrderStatus.erase(newOrderStatusIterator);
    }

    std::vector<OnRtnOrderDoneAndWaitingOnRtnTrade>::iterator tradeIterator;
    for(tradeIterator = unit.newOnRtnTrades.begin(); tradeIterator != unit.newOnRtnTrades.end();)
    {
        unit.pendingOnRtnTrades.push_back(*tradeIterator);
        tradeIterator = unit.newOnRtnTrades.erase(tradeIterator);
    }

    std::vector<PendingBinanceTradeStatus>::iterator newTradeStatusIterator;
    for(newTradeStatusIterator = unit.newTradeStatus.begin(); newTradeStatusIterator != unit.newTradeStatus.end();) {
        unit.pendingTradeStatus.push_back(*newTradeStatusIterator);
        newTradeStatusIterator = unit.newTradeStatus.erase(newTradeStatusIterator);
    }

    std::vector<int64_t>::iterator newSentTradeIdsIterator;
    for(newSentTradeIdsIterator = unit.newSentTradeIds.begin(); newSentTradeIdsIterator != unit.newSentTradeIds.end();) {
        unit.sentTradeIds.push_back(*newSentTradeIdsIterator);
        newSentTradeIdsIterator = unit.newSentTradeIds.erase(newSentTradeIdsIterator);
    }
}

void TDEngineBinance::retrieveOrderStatus(AccountUnitBinance& unit)
{
    KF_LOG_INFO(logger, "[retrieveOrderStatus] (unit.pendingOrderStatus.size())" << unit.pendingOrderStatus.size());
    std::vector<PendingBinanceOrderStatus>::iterator orderStatusIterator;

    for(orderStatusIterator = unit.pendingOrderStatus.begin(); orderStatusIterator != unit.pendingOrderStatus.end();)
    {
        KF_LOG_INFO(logger, "[retrieveOrderStatus] get_order " << "account.api_key:"<< unit.api_key
                                                                          << "  account.pendingOrderStatus.InstrumentID: "<< orderStatusIterator->InstrumentID
                                                                          <<"  account.pendingOrderStatus.OrderRef: " << orderStatusIterator->OrderRef
                                                                          <<"  account.pendingOrderStatus.OrderStatus: " << orderStatusIterator->OrderStatus
        );

        std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(orderStatusIterator->InstrumentID));
        if(ticker.length() == 0) {
            KF_LOG_ERROR(logger, "[retrieveOrderStatus]: not in WhiteList , ignore it:" << orderStatusIterator->InstrumentID);
            continue;
        }


        Document orderResult;
        get_order(unit, ticker.c_str(), 0, orderStatusIterator->OrderRef, orderResult);
        KF_LOG_INFO(logger, "[retrieveOrderStatus] get_order " << " (symbol)" << orderStatusIterator->InstrumentID
                                                                          << " (orderId)" << orderStatusIterator->OrderRef);
        printResponse(orderResult);
        if(orderResult.HasParseError()) {
            KF_LOG_INFO(logger, "[retrieveOrderStatus] get_order HasParseError, call continue");
            continue;
        }
        /*
           {
            "symbol": "BTCUSDT",
            "orderId": 144674450,
            "clientOrderId": "1",
            "price": "0.00000000",
            "origQty": "0.00370000",
            "executedQty": "0.00370000",
            "cummulativeQuoteQty": "24.02137156",
            "status": "FILLED",
            "timeInForce": "GTC",
            "type": "MARKET",
            "side": "SELL",
            "stopPrice": "0.00000000",
            "icebergQty": "0.00000000",
            "time": 1533716765364,
            "updateTime": 1533716765364,
            "isWorking": true
        }
        */
        //parse order status
        if(orderResult.IsObject() && !orderResult.HasMember("code"))
        {
            LFRtnOrderField rtn_order;
            memset(&rtn_order, 0, sizeof(LFRtnOrderField));
            rtn_order.OrderStatus = GetOrderStatus(orderResult["status"].GetString());
            rtn_order.VolumeTraded = std::round(stod(orderResult["executedQty"].GetString()) * scale_offset);

            //if status changed or LF_CHAR_PartTradedQueueing but traded valume changes, emit onRtnOrder
            if(orderStatusIterator->OrderStatus != rtn_order.OrderStatus ||
               (LF_CHAR_PartTradedQueueing == rtn_order.OrderStatus
                && rtn_order.VolumeTraded != orderStatusIterator->VolumeTraded))
            {
                strcpy(rtn_order.ExchangeID, "binance");
                strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
                strncpy(rtn_order.InstrumentID, orderStatusIterator->InstrumentID, 31);
                rtn_order.Direction = GetDirection(orderResult["side"].GetString());
                rtn_order.TimeCondition = GetTimeCondition(orderResult["timeInForce"].GetString());
                rtn_order.OrderPriceType = GetPriceType(orderResult["type"].GetString());
                strncpy(rtn_order.OrderRef, orderResult["clientOrderId"].GetString(), 13);
                rtn_order.VolumeTotalOriginal = std::round(stod(orderResult["origQty"].GetString()) * scale_offset);
                rtn_order.LimitPrice =orderStatusIterator->LimitPrice;// std::round(stod(orderResult["price"].GetString()) * scale_offset);
                rtn_order.VolumeTotal = rtn_order.VolumeTotalOriginal - rtn_order.VolumeTraded;
                on_rtn_order(&rtn_order);
                raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                        source_id, MSG_TYPE_LF_RTN_ORDER_BINANCE,
                                        1/*islast*/, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);
                //update last status
                orderStatusIterator->OrderStatus = rtn_order.OrderStatus;
                orderStatusIterator->VolumeTraded = rtn_order.VolumeTraded;

                //is status is canceled, dont need get the orderref's trade info
                if(orderStatusIterator->OrderStatus == LF_CHAR_Canceled) {
                    int64_t binanceOrderId =  orderResult["orderId"].GetInt64();
                    if(removeBinanceOrderIdFromPendingOnRtnTrades(unit, binanceOrderId))
                    {
                        KF_LOG_INFO(logger, "[retrieveTradeStatus] the (OrderRef) "
                                            << orderStatusIterator->OrderRef
                                            << " is LF_CHAR_Canceled. done need get this (binanceOrderId)"
                                               << binanceOrderId << " trade info");
                    }
                }
            }
        } else {
            KF_LOG_ERROR(logger, "[retrieveOrderStatus] get_order fail." << " (symbol)" << orderStatusIterator->InstrumentID
                                                                                    << " (orderId)" << orderStatusIterator->OrderRef);
        }
        //remove order when finish
        if(orderStatusIterator->OrderStatus == LF_CHAR_AllTraded  || orderStatusIterator->OrderStatus == LF_CHAR_Canceled
           || orderStatusIterator->OrderStatus == LF_CHAR_Error)
        {
            KF_LOG_INFO(logger, "[retrieveOrderStatus] remove a pendingOrderStatus.");
            orderStatusIterator = unit.pendingOrderStatus.erase(orderStatusIterator);
            //
            auto it = mapCancelOrder.find(orderStatusIterator->OrderRef);
            if(it != mapCancelOrder.end())
            {
                mapCancelOrder.erase(it);
            }
            std::unique_lock<std::mutex> lck(account_mutex);
            auto it2 = mapInsertOrders.find(orderStatusIterator->OrderRef);
            if(it2 != mapInsertOrders.end())
            {
                mapInsertOrders.erase(it2);
            }
        }
        else {
           
            //
            auto it = mapCancelOrder.find(orderStatusIterator->OrderRef);
            if(it != mapCancelOrder.end() and (getTimestamp() - it->second.rcv_time) > cancel_timeout_milliseconds)
            {
                on_rsp_order_action(&(it->second.data),it->second.request_id , 101, "no response after cancel order for a long time");
            }
            //
            ++orderStatusIterator;
        }
        //KF_LOG_INFO(logger, "[retrieveOrderStatus] move to next pendingOrderStatus.");
    }
}

void TDEngineBinance::retrieveTradeStatus(AccountUnitBinance& unit)
{
    KF_LOG_INFO(logger, "[retrieveTradeStatus] (unit.pendingOrderStatus.size())"
                        << unit.pendingOrderStatus.size() << " (unit.pendingOnRtnTrades.size()) " << unit.pendingOnRtnTrades.size());
    //if 'ours' order is finished, and ours trade is finished too , dont get trade info anymore.
    if(unit.pendingOrderStatus.size() == 0 && unit.pendingOnRtnTrades.size() == 0) return;
    std::vector<PendingBinanceTradeStatus>::iterator tradeStatusIterator;
    int indexNum = 0;
    for(tradeStatusIterator = unit.pendingTradeStatus.begin(); tradeStatusIterator != unit.pendingTradeStatus.end(); ++tradeStatusIterator)
    {
        KF_LOG_INFO(logger, "[retrieveTradeStatus] pendingTradeStatus [" << indexNum <<"]"
                                                                << "  (InstrumentID) "<< tradeStatusIterator->InstrumentID
                                                                <<"  (last_trade_id) " << tradeStatusIterator->last_trade_id);
        indexNum++;
    }

    Document resultTrade;


    for(tradeStatusIterator = unit.pendingTradeStatus.begin(); tradeStatusIterator != unit.pendingTradeStatus.end(); ++tradeStatusIterator)
    {
        KF_LOG_INFO(logger, "[retrieveTradeStatus] get_my_trades 1 (last_trade_id)" << tradeStatusIterator->last_trade_id << " (InstrumentID)" << tradeStatusIterator->InstrumentID);

        std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(tradeStatusIterator->InstrumentID));
        if(ticker.length() == 0) {
            KF_LOG_INFO(logger, "[retrieveTradeStatus]: not in WhiteList , ignore it:" << tradeStatusIterator->InstrumentID);
            continue;
        }
        KF_LOG_DEBUG(logger, "[retrieveTradeStatus] (exchange_ticker)" << ticker);
        KF_LOG_INFO(logger, "[retrieveTradeStatus] get_my_trades 2 (last_trade_id)" << tradeStatusIterator->last_trade_id << " (InstrumentID)" << tradeStatusIterator->InstrumentID);
        get_my_trades(unit, ticker.c_str(), 500, tradeStatusIterator->last_trade_id, resultTrade);
        if(resultTrade.HasParseError()) {
            KF_LOG_INFO(logger, "[retrieveTradeStatus] get_my_trades HasParseError, call continue");
            continue;
        }
        /*
         [
          {
            "id": 28457,
            "orderId": 100234,
            "price": "4.00000100",
            "qty": "12.00000000",
            "commission": "10.10000000",
            "commissionAsset": "BNB",
            "time": 1499865549590,
            "isBuyer": true,
            "isMaker": false,
            "isBestMatch": true
          }
        ]

         example2, isBuyer=isMaker=true:
         [{
                "commission" : "0.00137879",
                "commissionAsset" : "BNB",
                "id" : 30801527,
                "isBestMatch" : true,
                "isBuyer" : true,
                "isMaker" : true,
                "orderId" : 61311309,
                "price" : "0.00000603",
                "qty" : "1000.00000000",
                "time" : 1530541407016
        }]
        */
        if(resultTrade.IsObject()) {
            //expected is array,but get object, it must be the error response json:
            /*
             {
            "code" : -1021,
            "msg" : "Timestamp for this request is outside of the recvWindow."
            }
             * */
            int errorId = 0;
            std::string errorMsg = "";
            if(resultTrade.HasMember("code"))
            {
                errorId = resultTrade["code"].GetInt();
                if(resultTrade.HasMember("msg") && resultTrade["msg"].IsString())
                {
                    errorMsg = resultTrade["msg"].GetString();
                }
                KF_LOG_ERROR(logger, "[retrieveTradeStatus] get_my_trades failed!" << " (errorId)" << errorId << " (errorMsg)" << errorMsg);

            }
            continue;
        }
        KF_LOG_INFO(logger, "[retrieveTradeStatus] get_my_trades 3 (last_trade_id)" << tradeStatusIterator->last_trade_id << " (InstrumentID)" << tradeStatusIterator->InstrumentID);
        LFRtnTradeField rtn_trade;
        memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
        strcpy(rtn_trade.ExchangeID, "binance");
        strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
        strncpy(rtn_trade.InstrumentID, tradeStatusIterator->InstrumentID, 31);
        //must be Array
        if (!resultTrade.IsArray()) {
			KF_LOG_INFO(logger, "[retrieveTradeStatus] Binance interface bans or refused!");
			continue;
		}
        int len = resultTrade.Size();
        for(int i = 0 ; i < len; i++)
        {
            int64_t newtradeId = resultTrade.GetArray()[i]["id"].GetInt64();
            bool hasSendThisTradeId = false;
            std::vector<int64_t>::iterator sentTradeIdsIterator;
            for(sentTradeIdsIterator = unit.sentTradeIds.begin(); sentTradeIdsIterator != unit.sentTradeIds.end(); sentTradeIdsIterator++) {
                if((*sentTradeIdsIterator) == newtradeId) {
                    hasSendThisTradeId = true;
                }
            }
            if(hasSendThisTradeId) {
                KF_LOG_INFO(logger, "[retrieveTradeStatus] get_my_trades 4 (for_i)" << i << "  (hasSendThisTradeId)" << hasSendThisTradeId << " (newtradeId)" << newtradeId);
                continue;
            }

            KF_LOG_INFO(logger, "[retrieveTradeStatus] get_my_trades 4 (for_i)" << i << "  (last_trade_id)" << tradeStatusIterator->last_trade_id << " (InstrumentID)" << tradeStatusIterator->InstrumentID);
            rtn_trade.Volume = std::round(stod(resultTrade.GetArray()[i]["qty"].GetString()) * scale_offset);
            rtn_trade.Price = std::round(stod(resultTrade.GetArray()[i]["price"].GetString()) * scale_offset);

            //apply the direction of the OrderRef
            int64_t binanceOrderId =  resultTrade.GetArray()[i]["orderId"].GetInt64();
            std::vector<OnRtnOrderDoneAndWaitingOnRtnTrade>::iterator tradeIterator;

            bool match_one = false;
            for(tradeIterator = unit.pendingOnRtnTrades.begin(); tradeIterator != unit.pendingOnRtnTrades.end(); ++tradeIterator)
            {
                if(tradeIterator->binanceOrderId == binanceOrderId)
                {
                    rtn_trade.Direction = tradeIterator->Direction;
                    strncpy(rtn_trade.OrderRef, tradeIterator->OrderRef, 13);
                    match_one = true;
                }
            }
            if(match_one) {
                on_rtn_trade(&rtn_trade);
                raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
                                        source_id, MSG_TYPE_LF_RTN_TRADE_BINANCE, 1/*islast*/, -1/*invalidRid*/);
            } else {
                //this order is not me sent out, maybe other strategy's order or manuelly open orders.
            }

            KF_LOG_INFO(logger, "[retrieveTradeStatus] get_my_trades 5 (last_trade_id)" << tradeStatusIterator->last_trade_id << " (InstrumentID)" << tradeStatusIterator->InstrumentID);


            KF_LOG_INFO(logger, "[retrieveTradeStatus] get_my_trades (newtradeId)" << newtradeId);
            if(newtradeId >= tradeStatusIterator->last_trade_id) {
                tradeStatusIterator->last_trade_id = newtradeId + 1;// for new trade
            }
            KF_LOG_INFO(logger, "[retrieveTradeStatus] get_my_trades (last_trade_id)" << tradeStatusIterator->last_trade_id << " (for_i)" << i);
        }

        //here, use another for-loop is for there maybe more than one trades on the same orderRef:
        //if the first one remove pendingOnRtnTrades, the second one could not get the Direction.

        for(int i = 0 ; i < len; i++)
        {
            if(! isExistSymbolInPendingBinanceOrderStatus(unit, tradeStatusIterator->InstrumentID, rtn_trade.OrderRef)) {
                //all the OnRtnOrder is finished.
                int64_t binanceOrderId =  resultTrade.GetArray()[i]["orderId"].GetInt64();
                if(removeBinanceOrderIdFromPendingOnRtnTrades(unit, binanceOrderId))
                {
                    KF_LOG_INFO(logger, "[retrieveTradeStatus] there is no pendingOrderStatus(LF_CHAR_AllTraded/LF_CHAR_Canceled/LF_CHAR_Error occur). this is the last turn of get_myTrades on (symbol)" << tradeStatusIterator->InstrumentID);
                }
            }
        }

        KF_LOG_INFO(logger, "[retrieveTradeStatus] get_my_trades 6 (last_trade_id)" << tradeStatusIterator->last_trade_id << " (InstrumentID)" << tradeStatusIterator->InstrumentID);
    }
}

bool TDEngineBinance::removeBinanceOrderIdFromPendingOnRtnTrades(AccountUnitBinance& unit, int64_t binanceOrderId)
{
    bool removedOne = false;
    std::vector<OnRtnOrderDoneAndWaitingOnRtnTrade>::iterator tradeIterator;
    for(tradeIterator = unit.pendingOnRtnTrades.begin(); tradeIterator != unit.pendingOnRtnTrades.end(); )
    {
        if(tradeIterator->binanceOrderId == binanceOrderId)
        {
            tradeIterator = unit.pendingOnRtnTrades.erase(tradeIterator);
            removedOne = true;
        } else {
            ++tradeIterator;
        }
    }
    return removedOne;
}


void TDEngineBinance::addNewSentTradeIds(AccountUnitBinance& unit, int64_t newSentTradeIds)
{
    std::lock_guard<std::mutex> guard_mutex(*unit.mutex_order_and_trade);

    unit.newSentTradeIds.push_back(newSentTradeIds);
    KF_LOG_DEBUG(logger, "[addNewSentTradeIds]" << " (newSentTradeIds)" << newSentTradeIds);
}

void TDEngineBinance::addNewQueryOrdersAndTrades(AccountUnitBinance& unit, const char_31 InstrumentID,int64_t limitPrice,
                                                     const char_21 OrderRef, const LfOrderStatusType OrderStatus,
                                                 const uint64_t VolumeTraded, LfDirectionType Direction, int64_t binanceOrderId)
{
    //add new orderId for GetAndHandleOrderTradeResponse
    std::lock_guard<std::mutex> guard_mutex(*unit.mutex_order_and_trade);

    PendingBinanceOrderStatus status;
    memset(&status, 0, sizeof(PendingBinanceOrderStatus));
    strncpy(status.InstrumentID, InstrumentID, 31);
    strncpy(status.OrderRef, OrderRef, 21);
    status.OrderStatus = OrderStatus;
    status.VolumeTraded = VolumeTraded;
    status.LimitPrice = limitPrice;
    unit.newOrderStatus.push_back(status);

    OnRtnOrderDoneAndWaitingOnRtnTrade waitingTrade;
    strncpy(waitingTrade.OrderRef, OrderRef, 21);
    waitingTrade.binanceOrderId = binanceOrderId;
    waitingTrade.Direction = Direction;
    unit.newOnRtnTrades.push_back(waitingTrade);

    //add new symbol for GetAndHandleOrderTradeResponse if had no this symbol before
    if(!isExistSymbolInPendingTradeStatus(unit, InstrumentID))
    {
        PendingBinanceTradeStatus tradeStatus;
        memset(&tradeStatus, 0, sizeof(PendingBinanceTradeStatus));
        strncpy(tradeStatus.InstrumentID, InstrumentID, 31);
        tradeStatus.last_trade_id = 0;
        unit.newTradeStatus.push_back(tradeStatus);
    }
}

bool TDEngineBinance::isExistSymbolInPendingTradeStatus(AccountUnitBinance& unit, const char_31 InstrumentID)
{
    std::vector<PendingBinanceTradeStatus>::iterator tradeStatusIterator;

    for(tradeStatusIterator = unit.pendingTradeStatus.begin(); tradeStatusIterator != unit.pendingTradeStatus.end(); ++tradeStatusIterator)
    {
        if(strcmp(tradeStatusIterator->InstrumentID, InstrumentID) == 0)
        {
            return true;
        }
    }
    return false;
}


bool TDEngineBinance::isExistSymbolInPendingBinanceOrderStatus(AccountUnitBinance& unit, const char_31 InstrumentID, const char_21 OrderRef)
{
    std::vector<PendingBinanceOrderStatus>::iterator orderStatusIterator;

    for(orderStatusIterator = unit.pendingOrderStatus.begin(); orderStatusIterator != unit.pendingOrderStatus.end(); orderStatusIterator++) {
        if (strcmp(orderStatusIterator->InstrumentID, InstrumentID) == 0 && strcmp(orderStatusIterator->OrderRef, OrderRef) == 0) {
            return true;
        }
    }
    return false;
}

void TDEngineBinance::set_reader_thread()
{
    ITDEngine::set_reader_thread();

    KF_LOG_INFO(logger, "[set_reader_thread] rest_thread start on AccountUnitBinance::loop");
    rest_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineBinance::loop, this)));
}
int64_t last_put_time = 0;
void TDEngineBinance::loop()
{
    KF_LOG_INFO(logger, "[loop] (isRunning) " << isRunning);
    while(isRunning)
    {
        
        auto current_ms = getTimestamp();
        //uint64_t tmp_rest_get_interval_ms = rest_get_interval_ms;
        
        for (size_t idx = 0; idx < account_units.size(); idx++)
        {
            AccountUnitBinance& unit = account_units[idx];
            if(last_put_time != 0 && current_ms - last_put_time > 1800000)
            {
                Document json;
                put_listen_key(unit,json);
                last_put_time = getTimestamp();
            }
            lws_service( unit.context, rest_get_interval_ms );
            /*
            if (unit.bHandle_429)
            {
                tmp_rest_get_interval_ms = default_429_rest_interval_ms;
            }
            if(last_rest_get_ts != 0 && (current_ms - last_rest_get_ts) < tmp_rest_get_interval_ms)
            {
                continue;
            }
            GetAndHandleOrderTradeResponse(unit);
            
            */
        }
        if( current_ms - last_rest_get_ts > SYNC_TIME_DEFAULT_INTERVAL) {
            //reset
            //sync_time_interval = SYNC_TIME_DEFAULT_INTERVAL;
            getTimeDiffOfExchange(account_units[0]);
            //KF_LOG_INFO(logger, "[GetAndHandleOrderTradeResponse] (reset_timeDiffOfExchange)" << timeDiffOfExchange);
            last_rest_get_ts = current_ms;
        }
        
        
    }
}


std::vector<std::string> TDEngineBinance::split(std::string str, std::string token)
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

void TDEngineBinance::send_order(AccountUnitBinance& unit, const char *symbol,
                const char *side,
                const char *type,
                const char *timeInForce,
                double quantity,
                double price,
                const char *newClientOrderId,
                double stopPrice,
                double icebergQty,
                Document& json)
{
    KF_LOG_INFO(logger, "[send_order]");

	string interface;
    int retry_times = 0;
    cpr::Response response;
    bool should_retry = false;
    do {
        should_retry = false;

        long recvWindow = order_insert_recvwindow_ms;
        std::string Timestamp = getTimestampString();
        std::string Method = "POST";
        std::string requestPath = restBaseUrl + "/api/v3/order?";
        std::string queryString("");
        std::string body = "";

        queryString.append( "symbol=" );
        queryString.append( symbol );

        queryString.append("&side=");
        queryString.append( side );

        queryString.append("&type=");
        queryString.append( type );
        //if MARKET,not send price or timeInForce
        if(strcmp("MARKET", type) != 0)
        {
            queryString.append("&timeInForce=");
            queryString.append( timeInForce );
        }

        queryString.append("&quantity=");
        queryString.append( to_string( quantity) );

        if(strcmp("MARKET", type) != 0)
        {
            queryString.append("&price=");
            std::string priceStr;
            std::stringstream convertStream;
            convertStream <<std::fixed << std::setprecision(8) << price;
            convertStream >> priceStr;

            KF_LOG_INFO(logger, "[send_order] (priceStr)" << priceStr);

            queryString.append( priceStr );
        }

        if ( strlen( newClientOrderId ) > 0 ) {
            queryString.append("&newClientOrderId=");
            queryString.append( newClientOrderId );
        }

        if ( stopPrice > 0.0 ) {
            queryString.append("&stopPrice=");
            queryString.append( to_string( stopPrice ) );
        }

        if ( icebergQty > 0.0 ) {
            queryString.append("&icebergQty=");
            queryString.append( to_string( icebergQty ) );
        }

        if ( recvWindow > 0 ) {
            queryString.append("&recvWindow=");
            queryString.append( to_string( recvWindow) );
        }

        queryString.append("&timestamp=");
        queryString.append(Timestamp);

        std::string signature =  hmac_sha256( unit.secret_key.c_str(), queryString.c_str() );
        queryString.append( "&signature=");
        queryString.append( signature );

        string url = requestPath + queryString;

        if (order_count_over_limit(unit))
        {
            //send err msg to strategy
            std::string strErr = "{\"code\":-1429,\"msg\":\"order count over 100000 limit.\"}";
            json.Parse(strErr.c_str());
            return;
        }

        if (unit.bHandle_429)
        {
            if (isHandling(unit))
            {
                std::string strErr = "{\"code\":-1429,\"msg\":\"handle 429, prohibit send order.\"}";
                json.Parse(strErr.c_str());
                return;
            }
        }

        handle_request_weight(unit,SendOrder_Type);

		if (m_interface_switch > 0) {
	        interface = m_interfaceMgr.getActiveInterface();
	        KF_LOG_INFO(logger, "[send_order] interface: [" << interface << "].");
	        if (interface.empty()) {
	            KF_LOG_INFO(logger, "[send_order] interface is empty, decline message sending!");
	            std::string strRefused = "{\"code\":-1430,\"msg\":\"interface is empty.\"}";
	            json.Parse(strRefused.c_str());
	            return;
	        }
		}
        std::unique_lock<std::mutex> lck(http_mutex);
        response = Post(Url{url},
                                  Header{{"X-MBX-APIKEY", unit.api_key}}, cpr::VerifySsl{false},
                                  Body{body}, Timeout{100000}, Interface{interface});

        KF_LOG_INFO(logger, "[send_order] (url) " << url << " (response.status_code) " << response.status_code <<
                                                         " (response.error.message) " << response.error.message <<
                                                         " (response.text) " << response.text.c_str());
        lck.unlock();

        if (response.status_code == HTTP_CONNECT_REFUSED)
        {
            meet_429(unit);
            break;
        }

        if(shouldRetry(response.status_code, response.error.message, response.text)) {
            should_retry = true;
            retry_times++;
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_milliseconds));
        }
    } while(should_retry && retry_times < max_rest_retry_times);

    KF_LOG_INFO(logger, "[send_order] out_retry (response.status_code) " << response.status_code <<
																		 " interface [" << interface <<
                                                                         "] (response.error.message) " << response.error.message <<
                                                                         " (response.text) " << response.text.c_str() );
	if (response.status_code == HTTP_CONNECT_REFUSED || response.status_code == HTTP_CONNECT_BANS) {
		if (m_interface_switch > 0) {
			m_interfaceMgr.disable(interface);
			KF_LOG_INFO(logger, "[send_order] interface [" << interface << "] is disabled!");
		}
	}
	
    return getResponse(response.status_code, response.text, response.error.message, json);
}

/*
 * https://github.com/binance-exchange/binance-official-api-docs/blob/master/errors.md
-1021 INVALID_TIMESTAMP
Timestamp for this request is outside of the recvWindow.
Timestamp for this request was 1000ms ahead of the server's time.


 *  (response.status_code) 400 (response.error.message)  (response.text) {"code":-1021,"msg":"Timestamp for this request is outside of the recvWindow."}
 * */
bool TDEngineBinance::shouldRetry(int http_status_code, std::string errorMsg, std::string text)
{
    if( 400 == http_status_code && text.find(":-1021") != std::string::npos )
    {
        getTimeDiffOfExchange(account_units[0]);
        return true;
    }
    return false;
}

bool TDEngineBinance::order_count_over_limit(AccountUnitBinance& unit)
{
    if (unit.order_total_count >= 100000)
    {
        KF_LOG_DEBUG(logger, "[order_count_over_limit] (order_total_count)" << unit.order_total_count << " over 100000/day limit!");
        return true;
    }
    
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (unit.time_queue.size() <= 0)
    {
        unit.time_queue.push(timestamp);
        unit.order_total_count++;
        return false;
    }

    uint64_t startTime = unit.time_queue.front();
    int order_time_diff_ms = timestamp - startTime;
    KF_LOG_DEBUG(logger, "[order_count_over_limit] (order_time_diff_ms)" << order_time_diff_ms 
        << " (time_queue.size)" << unit.time_queue.size()
        << " (order_total_count)" << unit.order_total_count
        << " (order_count_per_second)" << order_count_per_second);
    
    const int order_ms = 1000;      //1s
    if (order_time_diff_ms < order_ms)
    {
        //in second        
        if(unit.time_queue.size() < order_count_per_second)
        {
            //do not reach limit in second
            unit.time_queue.push(timestamp);
            unit.order_total_count++;
            return false;
        }

        //reach limit in second/over limit count, sleep
        usleep((order_ms - order_time_diff_ms) * 1000);
        timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    //move receive window to next step
    unit.time_queue.pop();
    unit.time_queue.push(timestamp);
    unit.order_total_count++;

    //清理超过1秒的记录
    while(unit.time_queue.size() > 0)
    {
        uint64_t tmpTime = unit.time_queue.front();
        int tmp_time_diff_ms = timestamp - tmpTime;
        if (tmp_time_diff_ms <= order_ms)
        {
            break;
        }

        unit.time_queue.pop();
    }
    return false;
}

void TDEngineBinance::handle_request_weight(AccountUnitBinance& unit,RequestWeightType type)
{
    if (request_weight_per_minute <= 0)
    {
        //do nothing even meet 429
        return;
    }

    std::lock_guard<std::mutex> guard_mutex(*unit.mutex_weight);
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
     std::queue<weight_data>& weight_data_queue = unit.weight_data_queue;
    if (weight_data_queue.size() <= 0 || unit.weight_count <= 0)
    {
        weight_data wd;
        wd.time = timestamp;
        wd.addWeight(type);
        unit.weight_count += wd.weight;
        weight_data_queue.push(wd);
        return;
    }

    weight_data front_data = weight_data_queue.front();
    int time_diff_ms = timestamp - front_data.time;
    KF_LOG_DEBUG(logger, "[handle_request_weight] (time_diff_ms)" << time_diff_ms 
        << " (weight_data_queue.size)" << weight_data_queue.size()
        << " (weight_count)" << unit.weight_count
        << " (request_weight_per_minute)" << request_weight_per_minute);

    const int weight_ms = 60000;     //60s,1minute
    if (time_diff_ms < weight_ms)
    {
        //in minute
        if(unit.weight_count < request_weight_per_minute)
        {
            //do not reach limit in second
            weight_data wd;
            wd.time = timestamp;
            wd.addWeight(type);
            unit.weight_count += wd.weight;
            weight_data_queue.push(wd);
            return;
        }

        //reach limit in minute/over weight limit count, sleep
        usleep((weight_ms - time_diff_ms) * 1000);
        timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }
    
    unit.weight_count -= front_data.weight;
    weight_data_queue.pop();

    weight_data wd;
    wd.time = timestamp;
    wd.addWeight(type);
    unit.weight_count += wd.weight;
    weight_data_queue.push(wd);

    //清理时间超过60000ms(1分钟)的记录
    while(weight_data_queue.size() > 0)
    {
        weight_data tmp_data = weight_data_queue.front();
        int tmp_time_diff_ms = timestamp - tmp_data.time;
        if (tmp_time_diff_ms <= weight_ms)
        {
            break;
        }

        unit.weight_count -= tmp_data.weight;
        weight_data_queue.pop();
    }
}

void TDEngineBinance::meet_429(AccountUnitBinance& unit)
{
    std::lock_guard<std::mutex> guard_mutex(*unit.mutex_handle_429);
    if (request_weight_per_minute <= 0)
    {
        KF_LOG_INFO(logger, "[meet_429] request_weight_per_minute <= 0, return");
        return;
    }

    if (unit.bHandle_429)
    {
        KF_LOG_INFO(logger, "[meet_429] 418 prevention mechanism already activated, return");
        return;
    }

    unit.startTime_429 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    unit.bHandle_429 = true;
    KF_LOG_INFO(logger, "[meet_429] 429 warning received, current request_weight_per_minute: " << request_weight_per_minute);
}

bool TDEngineBinance::isHandling(AccountUnitBinance& unit)
{
    std::lock_guard<std::mutex> guard_mutex(*unit.mutex_handle_429);
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    int handle_429_time_diff_ms = timestamp - unit.startTime_429;
    if (handle_429_time_diff_ms > prohibit_order_ms)
    {
        //stop handle 429
        unit.startTime_429 = 0;
        unit.bHandle_429 = false;
        KF_LOG_INFO(logger, "[isHandling] handle_429_time_diff_ms > prohibit_order_ms, stop handle 429");
    }
    KF_LOG_INFO(logger, "[isHandling] " << " bHandle_429 " << unit.bHandle_429 << " request_weight_per_minute " << request_weight_per_minute);
    return unit.bHandle_429;
}

void TDEngineBinance::get_order(AccountUnitBinance& unit, const char *symbol, long orderId, const char *origClientOrderId, Document& json)
{
    KF_LOG_INFO(logger, "[get_order]");
    long recvWindow = order_insert_recvwindow_ms;//5000;
    std::string Timestamp = getTimestampString();
    std::string Method = "GET";
    std::string requestPath = restBaseUrl +  "/api/v3/order?";
    std::string queryString("");
    std::string body = "";

    queryString.append( "symbol=" );
    queryString.append( symbol );

    if ( orderId > 0 ) {
        queryString.append("&orderId=");
        queryString.append( to_string( orderId ) );
    }

    if ( strlen( origClientOrderId ) > 0 ) {
        queryString.append("&origClientOrderId=");
        queryString.append( origClientOrderId );
    }

    if ( recvWindow > 0 ) {
        queryString.append("&recvWindow=");
        queryString.append( to_string( recvWindow) );
    }

    queryString.append("&timestamp=");
    queryString.append( Timestamp );

    std::string signature =  hmac_sha256( unit.secret_key.c_str(), queryString.c_str() );
    queryString.append( "&signature=");
    queryString.append( signature );

    string url = requestPath + queryString;

    if (unit.bHandle_429)
    {
        isHandling(unit);
    }

    handle_request_weight(unit,GetOrder_Type);

	string interface;
	if (m_interface_switch > 0) {
		interface = m_interfaceMgr.getActiveInterface();
		KF_LOG_INFO(logger, "[get_order] interface: [" << interface << "].");
		if (interface.empty()) {
		    KF_LOG_INFO(logger, "[get_order] interface is empty, decline message sending!");
		    return;
		}
	}
    std::unique_lock<std::mutex> lck(http_mutex);
    const auto response = Get(Url{url},
                              Header{{"X-MBX-APIKEY", unit.api_key}}, cpr::VerifySsl{false},
                              Body{body}, Timeout{100000}, Interface{interface});

    KF_LOG_INFO(logger, "[get_order] (url) " << url << " (response.status_code) " << response.status_code <<
											  " interface [" << interface <<
                                              "] (response.error.message) " << response.error.message <<
                                              " (response.text) " << response.text.c_str());
    lck.unlock();
    if (response.status_code == HTTP_CONNECT_REFUSED)
    {
        meet_429(unit);
    }

	if (response.status_code == HTTP_CONNECT_REFUSED || response.status_code == HTTP_CONNECT_BANS) {
		if (m_interface_switch > 0) {
			m_interfaceMgr.disable(interface);
			KF_LOG_INFO(logger, "[get_order] interface [" << interface << "] is disabled!");
		}
	}
	
    return getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineBinance::cancel_order(AccountUnitBinance& unit, const char *symbol,
                  long orderId, const char *origClientOrderId, const char *newClientOrderId, Document &json)
{
    KF_LOG_INFO(logger, "[cancel_order]");
    int retry_times = 0;
    cpr::Response response;
    bool should_retry = false;
	string interface;
    do {
        should_retry = false;

        long recvWindow = order_action_recvwindow_ms;
        std::string Timestamp = getTimestampString();
        std::string Method = "DELETE";
        std::string requestPath =restBaseUrl + "/api/v3/order?";
        std::string queryString("");
        std::string body = "";

        queryString.append( "symbol=" );
        queryString.append( symbol );

        if ( orderId > 0 ) {
            queryString.append("&orderId=");
            queryString.append( to_string( orderId ) );
        }

        if ( strlen( origClientOrderId ) > 0 ) {
            queryString.append("&origClientOrderId=");
            queryString.append( origClientOrderId );
        }

        if ( strlen( newClientOrderId ) > 0 ) {
            queryString.append("&newClientOrderId=");
            queryString.append( newClientOrderId );
        }

        if ( recvWindow > 0 ) {
            queryString.append("&recvWindow=");
            queryString.append( std::to_string( recvWindow) );
        }

        queryString.append("&timestamp=");
        queryString.append( Timestamp );

        std::string signature =  hmac_sha256( unit.secret_key.c_str(), queryString.c_str() );
        queryString.append( "&signature=");
        queryString.append( signature );

        string url = requestPath + queryString;

        if (order_count_over_limit(unit))
        {
            std::string strErr = "{\"code\":-1429,\"msg\":\"order count over 100000 limit.\"}";
            json.Parse(strErr.c_str());
            return;
        }

        if (unit.bHandle_429)
        {
            if (isHandling(unit))
            {
                std::string strErr = "{\"code\":-1429,\"msg\":\"handle 429, prohibit cancel order.\"}";
                json.Parse(strErr.c_str());
                return;
            }
        }

        handle_request_weight(unit,CancelOrder_Type);

		if (m_interface_switch > 0) {
	        interface = m_interfaceMgr.getActiveInterface();
	        KF_LOG_INFO(logger, "[cancel_order] interface: [" << interface << "].");
	        if (interface.empty()) {
	            KF_LOG_INFO(logger, "[cancel_order] interface is empty, decline message sending!");
	            std::string strRefused = "{\"code\":-1430,\"msg\":\"interface is empty.\"}";
	            json.Parse(strRefused.c_str());
	            return;
	        }
		}
        std::unique_lock<std::mutex> lck(http_mutex);
        response = Delete(Url{url},
                                  Header{{"X-MBX-APIKEY", unit.api_key}}, cpr::VerifySsl{false},
                                  Body{body}, Timeout{100000}, Interface{interface});

        KF_LOG_INFO(logger, "[cancel_order] (url) " << url << " (response.status_code) " << response.status_code <<
                                                 " (response.error.message) " << response.error.message <<
                                                 " (response.text) " << response.text.c_str());
        lck.unlock();
        if (response.status_code == HTTP_CONNECT_REFUSED)
        {
            meet_429(unit);
            break;
        }

        if(shouldRetry(response.status_code, response.error.message, response.text)) {
            should_retry = true;
            retry_times++;
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_milliseconds));
        }
    } while(should_retry && retry_times < max_rest_retry_times);

    KF_LOG_INFO(logger, "[cancel_order] out_retry (response.status_code) " << response.status_code <<
                                                                         " interface [" << interface <<
                                                                         "] (response.error.message) " << response.error.message <<
                                                                         " (response.text) " << response.text.c_str() );
	if (response.status_code == HTTP_CONNECT_REFUSED || response.status_code == HTTP_CONNECT_BANS) {
		if (m_interface_switch > 0) {
			m_interfaceMgr.disable(interface);
			KF_LOG_INFO(logger, "[cancel_order] interface [" << interface << "] is disabled!");
		}
	}

    return getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineBinance::get_my_trades(AccountUnitBinance& unit, const char *symbol, int limit, int64_t fromId, Document &json)
{
    KF_LOG_INFO(logger, "[get_my_trades]");
    long recvWindow = order_insert_recvwindow_ms;//5000;
    std::string Timestamp = getTimestampString();
    std::string Method = "GET";
    std::string requestPath = restBaseUrl + "/api/v3/myTrades?";
    std::string queryString("");
    std::string body = "";

    queryString.append( "symbol=" );
    queryString.append( symbol );
    if ( limit > 0 ) {
        queryString.append("&limit=");
        queryString.append( std::to_string( limit ) );
    }
    if ( fromId > 0 ) {
        queryString.append("&fromId=");
        queryString.append( std::to_string( fromId ) );
    }

    if ( recvWindow > 0 ) {
        queryString.append("&recvWindow=");
        queryString.append( std::to_string( recvWindow ) );
    }

    queryString.append("&timestamp=");
    queryString.append( Timestamp );


    std::string signature =  hmac_sha256( unit.secret_key.c_str(), queryString.c_str() );
    queryString.append( "&signature=");
    queryString.append( signature );

    string url = requestPath + queryString;

    if (unit.bHandle_429)
    {
        isHandling(unit);
    }

    handle_request_weight(unit,TradeList_Type);

	string interface;
	if (m_interface_switch > 0) {
		interface = m_interfaceMgr.getActiveInterface();
		KF_LOG_INFO(logger, "[get_my_trades] interface: [" << interface << "].");
		if (interface.empty()) {
			KF_LOG_INFO(logger, "[get_my_trades] interface is empty, decline message sending!");
			return;
		}
	}
	std::unique_lock<std::mutex> lck(http_mutex);
    const auto response = Get(Url{url},
                              Header{{"X-MBX-APIKEY", unit.api_key}}, cpr::VerifySsl{false},
                              Body{body}, Timeout{100000}, Interface{interface});

    KF_LOG_INFO(logger, "[get_my_trades] (url) " << url << " (response.status_code) " << response.status_code <<
												" interface [" << interface <<
                                                "] (response.error.message) " << response.error.message <<
                                                " (response.text) " << response.text.c_str());
    lck.unlock();
    if (response.status_code == HTTP_CONNECT_REFUSED)
    {
        meet_429(unit);
    }

	if (response.status_code == HTTP_CONNECT_REFUSED || response.status_code == HTTP_CONNECT_BANS) {
		if (m_interface_switch > 0) {
			m_interfaceMgr.disable(interface);
			KF_LOG_INFO(logger, "[get_my_trades] interface [" << interface << "] is disabled!");
		}
	}

    return getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineBinance::get_open_orders(AccountUnitBinance& unit, const char *symbol, Document &json)
{
    KF_LOG_INFO(logger, "[get_open_orders]");
    long recvWindow = order_insert_recvwindow_ms;//5000;
    std::string Timestamp = getTimestampString();
    std::string Method = "GET";
    std::string requestPath = restBaseUrl + "/api/v3/openOrders?";
    std::string queryString("");
    std::string body = "";

    bool hasSetParameter = false;

    if(strlen(symbol) > 0) {
        queryString.append( "symbol=" );
        queryString.append( symbol );
        hasSetParameter = true;
    }

    if ( recvWindow > 0 ) {
        if(hasSetParameter)
        {
            queryString.append("&recvWindow=");
            queryString.append( to_string( recvWindow) );
        } else {
            queryString.append("recvWindow=");
            queryString.append( to_string( recvWindow) );
        }
        hasSetParameter = true;
    }

    if(hasSetParameter)
    {
        queryString.append("&timestamp=");
        queryString.append( Timestamp );
    } else {
        queryString.append("timestamp=");
        queryString.append( Timestamp );
    }

    std::string signature =  hmac_sha256( unit.secret_key.c_str(), queryString.c_str() );
    queryString.append( "&signature=");
    queryString.append( signature );

    string url = requestPath + queryString;

    handle_request_weight(unit,GetOpenOrder_Type);
    std::unique_lock<std::mutex> lck(http_mutex);
    const auto response = Get(Url{url},
                                 Header{{"X-MBX-APIKEY", unit.api_key}}, cpr::VerifySsl{false},
                                 Body{body}, Timeout{100000});

    KF_LOG_INFO(logger, "[get_open_orders] (url) " << url << " (response.status_code) " << response.status_code <<
                                                 " (response.error.message) " << response.error.message <<
                                                 " (response.text) " << response.text.c_str());
    lck.unlock();
    /*If the symbol is not sent, orders for all symbols will be returned in an array.
    [
      {
        "symbol": "LTCBTC",
        "orderId": 1,
        "clientOrderId": "myOrder1",
        "price": "0.1",
        "origQty": "1.0",
        "executedQty": "0.0",
        "status": "NEW",
        "timeInForce": "GTC",
        "type": "LIMIT",
        "side": "BUY",
        "stopPrice": "0.0",
        "icebergQty": "0.0",
        "time": 1499827319559,
        "isWorking": trueO
      }
    ]
     * */
     
    return getResponse(response.status_code, response.text, response.error.message, json);
}


void TDEngineBinance::get_exchange_time(AccountUnitBinance& unit, Document &json)
{
    KF_LOG_INFO(logger, "[get_exchange_time]");
    long recvWindow = order_insert_recvwindow_ms;//5000;
    std::string Timestamp = std::to_string(getTimestamp());
    std::string Method = "GET";
    std::string requestPath = restBaseUrl + "/api/v1/time";
    std::string queryString("");
    std::string body = "";

    string url = requestPath + queryString;
    std::unique_lock<std::mutex> lck(http_mutex);
    const auto response = Get(Url{url},
                              Header{{"X-MBX-APIKEY", unit.api_key}},
                              Body{body}, Timeout{100000});

    KF_LOG_INFO(logger, "[get_exchange_time] (url) " << url << " (response.status_code) " << response.status_code <<
                                                      " (response.error.message) " << response.error.message <<
                                                      " (response.text) " << response.text.c_str());
    return getResponse(response.status_code, response.text, response.error.message, json);
}


void TDEngineBinance::get_exchange_infos(AccountUnitBinance& unit, Document &json)
{
    KF_LOG_INFO(logger, "[get_exchange_infos]");
    long recvWindow = 5000;
    std::string Timestamp = getTimestampString();
    std::string Method = "GET";
    std::string requestPath =restBaseUrl +"/api/v1/exchangeInfo";
    std::string queryString("");
    std::string body = "";

    string url = requestPath + queryString;
    std::unique_lock<std::mutex> lck(http_mutex);
    const auto response = Get(Url{url},
                              Header{{"X-MBX-APIKEY", unit.api_key}},
                              Body{body}, Timeout{100000});

    KF_LOG_INFO(logger, "[get_exchange_infos] (url) " << url << " (response.status_code) " << response.status_code <<
                                                   " (response.error.message) " << response.error.message <<
                                                   " (response.text) " << response.text.c_str());
    return getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineBinance::get_account(AccountUnitBinance& unit, Document &json)
{
    KF_LOG_INFO(logger, "[get_account]");
    long recvWindow = order_insert_recvwindow_ms;//5000;
    std::string Timestamp = getTimestampString();
    std::string Method = "GET";
    std::string requestPath =restBaseUrl + "/api/v3/account?";
    std::string queryString("");
    std::string body = "";

    queryString.append("timestamp=");
    queryString.append( Timestamp );

    if ( recvWindow > 0 ) {
        queryString.append("&recvWindow=");
        queryString.append( std::to_string( recvWindow ) );
    }

    std::string signature =  hmac_sha256( unit.secret_key.c_str(), queryString.c_str() );
    queryString.append( "&signature=");
    queryString.append( signature );

    string url = requestPath + queryString;
    std::unique_lock<std::mutex> lck(http_mutex);
    const auto response = Get(Url{url},
                              Header{{"X-MBX-APIKEY", unit.api_key}},
                              Body{body}, Timeout{100000});

    KF_LOG_INFO(logger, "[get_account] (url) " << url << " (response.status_code) " << response.status_code <<
                                                      " (response.error.message) " << response.error.message <<
                                                      " (response.text) " << response.text.c_str());

    return getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineBinance::get_listen_key(AccountUnitBinance& unit, Document &json)
{
    KF_LOG_INFO(logger, "[get_listen_key]");
    std::string Timestamp = getTimestampString();
    std::string Method = "POST";
    std::string requestPath = restBaseUrl +"/api/v1/userDataStream";
    std::string queryString("");
    std::string body = "";

    string url = requestPath + queryString;
    std::unique_lock<std::mutex> lck(http_mutex);
    const auto response = Post(Url{url},
                              Header{{"X-MBX-APIKEY", unit.api_key}},
                              Body{body}, Timeout{100000});

    KF_LOG_INFO(logger, "[get_listen_key] (url) " << url << " (response.status_code) " << response.status_code <<
                                                   " (response.error.message) " << response.error.message <<
                                                   " (response.text) " << response.text.c_str());
    return getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineBinance::put_listen_key(AccountUnitBinance& unit, Document &json)
{
    KF_LOG_INFO(logger, "[put_listen_key]");
    std::string Timestamp = getTimestampString();
    std::string Method = "PUT";
    std::string requestPath = restBaseUrl +"/api/v1/userDataStream";
    std::string queryString("");
    std::string body ="{ \"listenKey\":"+ unit.listenKey + "}";

    string url = requestPath + queryString;
    std::unique_lock<std::mutex> lck(http_mutex);
    const auto response = Put(Url{url},
                              Header{{"X-MBX-APIKEY", unit.api_key}},
                              Body{body}, Timeout{100000});

    KF_LOG_INFO(logger, "[put_listen_key] (url) " << url << " (response.status_code) " << response.status_code <<
                                                   " (response.error.message) " << response.error.message <<
                                                   " (response.text) " << response.text.c_str());
    return getResponse(response.status_code, response.text, response.error.message, json);
}



void TDEngineBinance::printResponse(const Document& d)
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

void TDEngineBinance::getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& json)
{
    json.Parse(responseText.c_str());
}

inline int64_t TDEngineBinance::getTimestamp()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp;
}

std::string TDEngineBinance::getTimestampString()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    KF_LOG_DEBUG(logger, "[getTimestampString] (timestamp)" << timestamp << " (timeDiffOfExchange)" << timeDiffOfExchange << " (exchange_shift_ms)" << exchange_shift_ms);
    timestamp =  timestamp + timeDiffOfExchange + exchange_shift_ms;
    KF_LOG_INFO(logger, "[getTimestampString] (new timestamp)" << timestamp);
    std::string timestampStr;
    std::stringstream convertStream;
    convertStream << timestamp;
    convertStream >> timestampStr;
    return timestampStr;
}


int64_t TDEngineBinance::getTimeDiffOfExchange(AccountUnitBinance& unit)
{
    KF_LOG_INFO(logger, "[getTimeDiffOfExchange] ");
    //reset to 0
    Document d;
    int64_t start_time = getTimestamp();
    get_exchange_time(unit, d);
    if(!d.HasParseError() && d.HasMember("serverTime"))
    {//binance serverTime
        int64_t exchangeTime = d["serverTime"].GetInt64();
        //KF_LOG_INFO(logger, "[getTimeDiffOfExchange] (i) " << i << " (exchangeTime) " << exchangeTime);
        int64_t finish_time = getTimestamp();
        timeDiffOfExchange = exchangeTime-(finish_time+start_time)/2;
    }
    return timeDiffOfExchange;
}
void TDEngineBinance::on_lws_connection_error(struct lws* conn)
{
    KF_LOG_ERROR(logger, "TDEngineBinance::on_lws_connection_error.");
    AccountUnitBinance& unit = findAccountUnitByWebsocketConn(conn);
    std::map<std::string, LFRtnOrderField>::iterator it;
    for(it=unit.ordersMap.begin();it!=unit.ordersMap.end();it++){
        int errorId;string errorMsg;
        string remoteOrderId=it->first;
        LFRtnOrderField data=it->second;
        Document json;
        std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data.InstrumentID));
	    cancel_order(unit, ticker.c_str(), 0, data.OrderRef, "", json);
        if(json.HasParseError() ){
            errorId=100;
            errorMsg= "cancel_order http response has parse error. please check the log";
            KF_LOG_ERROR(logger, "[req_order_action] cancel_order error! (remoteOrderId)" << remoteOrderId << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        }
        if(!json.HasParseError() && json.IsObject() && json.HasMember("code") && json["code"].IsNumber()){
            errorId = json["code"].GetInt();
            if(json.HasMember("msg") && json["msg"].IsString()){
                errorMsg = json["msg"].GetString();
            }
            KF_LOG_ERROR(logger, "[req_order_action] cancel_order failed! (rid)  -1 (errorId)" << errorId << " (errorMsg) " << errorMsg);
        }else if(json.HasMember("status")){
            string status=json["status"].GetString();
            data.OrderStatus=GetOrderStatus(status);
            on_rtn_order(&data);
        }
    }
    unit.ordersMap.clear();
    unit.wsOrderStatus.clear();
    long timeout_nsec = 0;
    lws_login(unit, timeout_nsec);
}

int TDEngineBinance::lws_write_subscribe(struct lws* conn)
{
    return 0;
}

void TDEngineBinance::lws_login(AccountUnitBinance& unit, long timeout_nsec) {
    KF_LOG_INFO(logger, "TDEngineBinance::lws_login:");
    global_td = this;
    //
    int errorId = 0;
    string errorMsg = "";
    Document json;
    get_listen_key(unit,json);
    if(json.HasParseError() )
    {
        errorId=100;
        errorMsg= "get_listen_key http response has parse error. please check the log";
        KF_LOG_ERROR(logger, "[lws_login] get_listen_key error! (rid)  -1 (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }
    else 
    {
        if(json.IsObject() && json.HasMember("listenKey"))
        {
            unit.listenKey = json["listenKey"].GetString();
        }
        else
        {
            errorId  = 101;
            errorMsg = "unknown error";
            KF_LOG_ERROR(logger, "[lws_login] get_account failed! (rid)  -1 (errorId)" << errorId << " (errorMsg) " << errorMsg);
        }
        
    }
    if(errorId != 0) 
        return;


    if (unit.context == NULL) {
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

        unit.context = lws_create_context( &info );
        KF_LOG_INFO(logger, "TDEngineBinance::lws_login: context created:"<< unit.api_key);
    }

    if (unit.context == NULL) {
        KF_LOG_ERROR(logger, "TDEngineBinance::lws_login: context of" << unit.api_key <<" is NULL. return");
        return;
    }

    int logs = LLL_ERR | LLL_DEBUG | LLL_WARN;
    lws_set_log_level(logs, NULL);

    struct lws_client_connect_info ccinfo = {0};

    static std::string host  = wsBaseUrl;
    static std::string path = "/ws/"+unit.listenKey;
    static int port = 9443;

    ccinfo.context 	= unit.context;
    ccinfo.address 	= host.c_str();
    ccinfo.port 	= port;
    ccinfo.path 	= path.c_str();
    ccinfo.host 	= host.c_str();
    ccinfo.origin 	= host.c_str();
    ccinfo.ietf_version_or_minus_one = -1;
    ccinfo.protocol = protocols[0].name;
    ccinfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;

    unit.websocketConn = lws_client_connect_via_info(&ccinfo);
    KF_LOG_INFO(logger, "TDEngineBinance::lws_login: Connecting to " <<  ccinfo.host << ":" << ccinfo.port << ":" << ccinfo.path);

    if (unit.websocketConn == NULL) {
        KF_LOG_ERROR(logger, "TDEngineBinance::lws_login: wsi create error.");
        return;
    }
    last_put_time = getTimestamp();
    KF_LOG_INFO(logger, "TDEngineBinance::lws_login: wsi create success.");
}


void TDEngineBinance::on_lws_data(struct lws* conn, const char* data, size_t len) {
    AccountUnitBinance &unit = findAccountUnitByWebsocketConn(conn);
    KF_LOG_INFO(logger, "TDEngineBinance::on_lws_data: " << data);
    Document json;
    json.Parse(data,len);
    if (json.HasParseError() || !json.IsObject()) {
        KF_LOG_ERROR(logger, "TDEngineBinance::on_lws_data. parse json error: " << data);        
    }
	else if(json.HasMember("e"))
	{
		
        std::string eventType = json["e"].GetString();
        if(eventType == "executionReport")
        {
            KF_LOG_INFO(logger, "TDEngineBinance::on_lws_data. Order Update ");
            AccountUnitBinance& unit= findAccountUnitByWebsocketConn(conn);
            onOrder(unit,json);
        }
        else if(eventType == "outboundAccountInfo")
        {
            KF_LOG_INFO(logger, "TDEngineBinance::on_lws_data. Account Update ");
        }
        
	}
	
}



AccountUnitBinance& TDEngineBinance::findAccountUnitByWebsocketConn(struct lws * websocketConn)
{
    for (size_t idx = 0; idx < account_units.size(); idx++) {
        AccountUnitBinance &unit = account_units[idx];
        if(unit.websocketConn == websocketConn) {
            return unit;
        }
    }
    return account_units[0];
}

//cys add
std::string TDEngineBinance::parseJsonToString(Document &d){
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);

    return buffer.GetString();
}
void TDEngineBinance::onOrder(AccountUnitBinance& unit, Document& json) {
	KF_LOG_INFO(logger, "TDEngineBinance::onOrder");
    if (json.HasMember("c")&& json.HasMember("i") && json.HasMember("X")&& json.HasMember("l")&& json.HasMember("L")&& json.HasMember("z")&& json.HasMember("t")) {
		
		std::lock_guard<std::mutex> lck(*unit.mutex_order_and_trade);		
		std::string remoteOrderId= json["i"].GetString();      
		auto it = unit.ordersMap.find(remoteOrderId);
		if (it == unit.ordersMap.end())
		{ 
			KF_LOG_ERROR(logger, "TDEngineBinance::onOrder,no order match,save wsOrderStatus");
            string strJson=parseJsonToString(json);
            unit.wsOrderStatus.push_back(strJson.c_str());
			return;
		}
		LFRtnOrderField& rtn_order = it->second;
        char status = GetOrderStatus(json["X"].GetString());				
        if(status == LF_CHAR_NotTouched &&  rtn_order.OrderStatus == status)
        {
            KF_LOG_INFO(logger, "TDEngineBinance::onOrder,status is not changed");
            return;
        }
        rtn_order.OrderStatus = status;
        std::string strTradeVolume = json["l"].GetString();
        uint64_t volumeTraded = std::round(std::stod(strTradeVolume)*scale_offset);
		rtn_order.VolumeTraded += volumeTraded;
        rtn_order.VolumeTotal = rtn_order.VolumeTotalOriginal - rtn_order.VolumeTraded;
		KF_LOG_INFO(logger, "TDEngineBinance::onOrder,rtn_order");
		on_rtn_order(&rtn_order);
		raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),source_id, MSG_TYPE_LF_RTN_ORDER_BITMEX,1, (rtn_order.RequestID > 0) ? rtn_order.RequestID : -1);
		       
        LFRtnTradeField rtn_trade;
		memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
		strncpy(rtn_trade.OrderRef, rtn_order.OrderRef, 13);
		strcpy(rtn_trade.ExchangeID, rtn_order.ExchangeID);
		strncpy(rtn_trade.UserID, rtn_order.UserID, 16);
		strncpy(rtn_trade.InstrumentID, rtn_order.InstrumentID, 31);
		rtn_trade.Direction = rtn_order.Direction;	
		rtn_trade.Volume = volumeTraded;
        strcpy(rtn_trade.TradeID,rtn_order.BusinessUnit);
		std::string strTradePrice = json["L"].GetString();
        int64_t priceTraded = std::round(std::stod(strTradePrice)*scale_offset);
		rtn_trade.Price = priceTraded;
		
		KF_LOG_INFO(logger, "TDEngineBinance::onOrder,rtn_trade");
		on_rtn_trade(&rtn_trade);
		raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),source_id, MSG_TYPE_LF_RTN_TRADE_BITMEX, 1, -1);


        if (rtn_order.OrderStatus == LF_CHAR_AllTraded || rtn_order.OrderStatus == LF_CHAR_PartTradedNotQueueing ||
			rtn_order.OrderStatus == LF_CHAR_Canceled || rtn_order.OrderStatus == LF_CHAR_NoTradeNotQueueing || rtn_order.OrderStatus == LF_CHAR_Error)
		{
			unit.ordersMap.erase(it);

            std::unique_lock<std::mutex> lck(account_mutex);
            auto it2 = mapInsertOrders.find(rtn_order.OrderRef);
            if(it2 != mapInsertOrders.end())
            {
                mapInsertOrders.erase(it2);
            }
		}       
    }

}



#define GBK2UTF8(msg) kungfu::yijinjing::gbk2utf8(string(msg))

BOOST_PYTHON_MODULE(libbinancetd)
{
    using namespace boost::python;
    class_<TDEngineBinance, boost::shared_ptr<TDEngineBinance> >("Engine")
    .def(init<>())
    .def("init", &TDEngineBinance::initialize)
    .def("start", &TDEngineBinance::start)
    .def("stop", &TDEngineBinance::stop)
    .def("logout", &TDEngineBinance::logout)
    .def("wait_for_stop", &TDEngineBinance::wait_for_stop);
}
