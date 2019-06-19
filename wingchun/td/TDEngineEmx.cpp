#include "TDEngineEmx.h"
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
using utils::crypto::base64_decode;
USING_WC_NAMESPACE
std::mutex mutex_msg_queue;
std::mutex g_httpMutex;
TDEngineEmx::TDEngineEmx(): ITDEngine(SOURCE_EMX)
{
    logger = yijinjing::KfLog::getLogger("TradeEngine.EMX");
    KF_LOG_INFO(logger, "[TDEngineEmx]");

    m_mutexOrder = new std::mutex();
    mutex_order_and_trade = new std::mutex();
    mutex_response_order_status = new std::mutex();
    mutex_orderaction_waiting_response = new std::mutex();
}

TDEngineEmx::~TDEngineEmx()
{
    if(m_mutexOrder != nullptr) delete m_mutexOrder;
    if(mutex_order_and_trade != nullptr) delete mutex_order_and_trade;
    if(mutex_response_order_status != nullptr) delete mutex_response_order_status;
    if(mutex_orderaction_waiting_response != nullptr) delete mutex_orderaction_waiting_response;
}

static TDEngineEmx* global_md = nullptr;

static int ws_service_cb( struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len )
{
    std::stringstream ss;
    //ss << "lws_callback,reason=" << reason << ",";
	switch( reason )
	{
		case LWS_CALLBACK_CLIENT_ESTABLISHED:
		{
            ss << "LWS_CALLBACK_CLIENT_ESTABLISHED.";
            if(global_md) global_md->writeErrorLog(ss.str());
			lws_callback_on_writable( wsi );
			break;
		}
		case LWS_CALLBACK_PROTOCOL_INIT:
        {
			 ss << "LWS_CALLBACK_PROTOCOL_INIT.";
            if(global_md) global_md->writeErrorLog(ss.str());
			break;
		}
		case LWS_CALLBACK_CLIENT_RECEIVE:
		{
			if(global_md)
			{
				global_md->on_lws_data(wsi, (const char*)in, len);
			}
			break;
		}
		case LWS_CALLBACK_CLIENT_WRITEABLE:
		{
		    ss << "LWS_CALLBACK_CLIENT_WRITEABLE.";
            
            int ret = 0;
			if(global_md)
			{
                global_md->writeErrorLog(ss.str());
				ret = global_md->lws_write_msg(wsi);
			}
			break;
		}
		case LWS_CALLBACK_CLOSED:
        case LWS_CALLBACK_WSI_DESTROY:
		case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
		{
            ss << "lws_callback,reason=" << reason;
            
 			if(global_md)
			{
                global_md->writeErrorLog(ss.str());
				global_md->on_lws_connection_error(wsi);
			}
			break;
		}
		default:
              //if(global_md) global_md->writeErrorLog(ss.str());
			break;
	}

	return 0;
}

std::string TDEngineEmx::getTimestampStr()
{
    //long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return  std::to_string(getMSTime());
}


 void TDEngineEmx::onOrderChange(Document& msg)
 {
            if(msg.HasMember("type") && msg["type"].GetString() == std::string("update") && msg.HasMember("action") && msg.HasMember("data"))
            {
                std::string strAction = msg["action"].GetString();
                auto& data = msg["data"];
                if(strAction == "order-received" && data.HasMember("order_id") && data.HasMember("client_id"))
                {
                    std::string strOrderId = data["order_id"].GetString();
                    std::string strClientId = data["client_id"].GetString();
                    std::lock_guard<std::mutex> lck(*m_mutexOrder); 
                    auto it = m_mapNewOrder.find(strClientId);
                    if(it != m_mapNewOrder.end())
                    {
                        it->second.OrderStatus = LF_CHAR_NotTouched;
                        //on_rtn_order(&(it->second));
                        strncpy(it->second.BusinessUnit,strOrderId.c_str(),64);
                        m_mapOrder.insert(std::make_pair(strOrderId,it->second));
                        m_mapNewOrder.erase(it);
                    }

                    auto it2 = m_mapInputOrder.find(strClientId);
                    if(it2 != m_mapInputOrder.end())
                    { 
                        auto data = it2->second;
                        m_mapInputOrder.erase(it2);
                        m_mapInputOrder.insert(std::make_pair(strOrderId,data));
                        
                    }
                }
                else if(strAction == "accepted" )
                {
                    std::string strOrderId = data["order_id"].GetString();
                    std::lock_guard<std::mutex> lck(*m_mutexOrder); 
                    auto it = m_mapOrder.find(strOrderId);
                    if(it != m_mapOrder.end())
                    {
                        localOrderRefRemoteOrderId.insert(std::make_pair(it->second.OrderRef,strOrderId));
                        on_rtn_order(&(it->second));
                    }
                    //
                    //auto it2 = m_mapInputOrder.find(strOrderId);
                    //if(it2 != m_mapInputOrder.end())
                    //{
                    //    m_mapInputOrder.erase(it2);
                    //}
                }
                else if(strAction == "rejected")
                {
                    std::string strOrderId = data["order_id"].GetString();
                    std::string strError = data["message"].GetString();
                    std::lock_guard<std::mutex> lck(*m_mutexOrder); 
                    auto it = m_mapOrder.find(strOrderId);
                    if(it != m_mapOrder.end())
                    {
                        auto it2 = m_mapInputOrder.find(strOrderId);
                        if(it2 != m_mapInputOrder.end())
                        {
                            on_rsp_order_insert(&(it2->second),it->second.RequestID,100,strError.c_str());
                            m_mapInputOrder.erase(it2);
                        }
                        m_mapOrder.erase(it);
                    }
                    
                }
                else if(strAction == "cancel-rejected")
                {
                    std::string strOrderId = data["order_id"].GetString();
                    std::string strError = data["message"].GetString();
                    std::lock_guard<std::mutex> lck(*m_mutexOrder); 
                    auto it = m_mapOrder.find(strOrderId);
                    if(it != m_mapOrder.end())
                    {
                        auto it2 = m_mapOrderAction.find(strOrderId);
                        if(it2 != m_mapOrderAction.end())
                        {
                            on_rsp_order_action(&(it2->second),it->second.RequestID,101,strError.c_str());
                            m_mapOrderAction.erase(it2);
                        }
                    }
                }
                else if(strAction == "canceled")
                {
                    std::string strOrderId = data["order_id"].GetString();
                    std::lock_guard<std::mutex> lck(*m_mutexOrder); 
                    auto it = m_mapOrder.find(strOrderId);
                    if(it != m_mapOrder.end())
                    {
                        it->second.OrderStatus = LF_CHAR_Canceled;
                        //撤单回报延时返回
                        m_mapCanceledOrder.insert(std::make_pair(strOrderId,getMSTime()));
                        //on_rtn_order(&(it->second));

                        auto it_id = localOrderRefRemoteOrderId.find(it->second.OrderRef);
                        if(it_id != localOrderRefRemoteOrderId.end())
                        {
                            localOrderRefRemoteOrderId.erase(it_id);
                        }
                        //m_mapOrder.erase(it);
                    }
                    //
                    auto it2 = m_mapInputOrder.find(strOrderId);
                    if(it2 != m_mapInputOrder.end())
                    {
                        m_mapInputOrder.erase(it2);
                    }
                    auto it3 = m_mapOrderAction.find(strOrderId);
                    if(it3 != m_mapOrderAction.end())
                    {    
                        m_mapOrderAction.erase(it3);
                    }
                }
                else if(strAction == "filled")
                {
                    std::string strStatus = data["status"].GetString();
                    std::string strOrderId = data["order_id"].GetString();
                    std::string strSize = data["size"].GetString();
                    std::string strSizeFilled = data["size_filled"].GetString();
                    std::string strSizeFilledDelta = data["size_filled_delta"].GetString();
                    std::string strFillPrice = data["fill_price"].GetString();
                    std::lock_guard<std::mutex> lck(*m_mutexOrder); 
                    auto it = m_mapOrder.find(strOrderId);
                    if(it != m_mapOrder.end())
                    {
                        it->second.OrderStatus = LF_CHAR_AllTraded;
                        if(strStatus == "accepted")
                        {
                            it->second.OrderStatus = LF_CHAR_PartTradedQueueing;
                        }
                        it->second.VolumeTraded = std::round(std::stod(strSizeFilled)*scale_offset);
                        it->second.VolumeTotal = it->second.VolumeTotalOriginal - it->second.VolumeTraded;
                        on_rtn_order(&(it->second));
                        raw_writer->write_frame(&(it->second), sizeof(LFRtnOrderField),
                                                source_id, MSG_TYPE_LF_RTN_ORDER_EMX, 1, -1);

                        LFRtnTradeField rtn_trade;
                        memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
                        strcpy(rtn_trade.ExchangeID,it->second.ExchangeID);
                        strncpy(rtn_trade.UserID, it->second.UserID,sizeof(rtn_trade.UserID));
                        strncpy(rtn_trade.InstrumentID, it->second.InstrumentID, sizeof(rtn_trade.InstrumentID));
                        strncpy(rtn_trade.OrderRef, it->second.OrderRef, sizeof(rtn_trade.OrderRef));
                        rtn_trade.Direction = it->second.Direction;
                        strncpy(rtn_trade.OrderSysID,strOrderId.c_str(),sizeof(rtn_trade.OrderSysID));
                        rtn_trade.Volume = std::round(std::stod(strSizeFilledDelta)*scale_offset);
                        rtn_trade.Price = std::round(std::stod(strFillPrice)*scale_offset);
                        on_rtn_trade(&rtn_trade);
                        raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
                                                source_id, MSG_TYPE_LF_RTN_TRADE_EMX, 1, -1);

                        if(it->second.OrderStatus == LF_CHAR_AllTraded)
                        {
                            auto it_id = localOrderRefRemoteOrderId.find(it->second.OrderRef);
                            if(it_id != localOrderRefRemoteOrderId.end())
                            {
                                localOrderRefRemoteOrderId.erase(it_id);
                            }
                            m_mapOrder.erase(it);
                            //
                            auto it2 = m_mapInputOrder.find(strOrderId);
                            if(it2 != m_mapInputOrder.end())
                            {
                                m_mapInputOrder.erase(it2);
                            }
                            auto it3 = m_mapOrderAction.find(strOrderId);
                            if(it3 != m_mapOrderAction.end())
                            {    
                                m_mapOrderAction.erase(it3);
                            }
                        }
                    }
                    
                }

            }
 }

void TDEngineEmx::on_lws_data(struct lws* conn, const char* data, size_t len)
{
    //std::string strData = dealDataSprit(data);
	KF_LOG_INFO(logger, "TDEngineEmx::on_lws_data: " << data);
    Document json;
	json.Parse(data);

    if(!json.HasParseError() && json.IsObject())
	{
		if(json.HasMember("channel") && json["channel"].IsString() && strcmp(json["channel"].GetString(), "orders") == 0)
		{
           onOrderChange(json);
		}
        else if(json.HasMember("type") && json["type"].IsString())	
        {
            std::string type = json["type"].GetString();
            if(type == "subscriptions")
            {
                m_isSubOK = true;
            }
            else if(type == "error")
            {

            }
        }
	} else 
    {
		KF_LOG_ERROR(logger, "MDEngineEmx::on_lws_data . parse json error");
	}
	
}

/*
{
    "type": "subscribe",
    "contract_codes": [],
    "channels": ["orders"],
    "key": "...",
    "sig": "...",
    "timestamp": "..."
}
*/
std::string TDEngineEmx::makeSubscribeChannelString(AccountUnitEmx& unit)
{
    std::string strTime = getTimestampStr();
    StringBuffer sbUpdate;
	Writer<StringBuffer> writer(sbUpdate);
	writer.StartObject();
    writer.Key("type");
	writer.String("subscribe");
    writer.Key("contract_codes");
    writer.StartArray();
    writer.EndArray();
	writer.Key("channels");
    writer.StartArray();
    writer.String("orders");
    writer.String("trading");
    writer.EndArray();
	writer.Key("key");
	writer.String(unit.api_key.c_str());
    std::string strSignatrue = sign(unit,"GET",strTime,"/v1/user/verify");
    writer.Key("sig");
	writer.String(strSignatrue.c_str());
    writer.Key("timestamp");
	writer.String(strTime.c_str());
	writer.EndObject();
    std::string strUpdate = sbUpdate.GetString();

    return strUpdate;
}
std::string TDEngineEmx::sign(const AccountUnitEmx& unit,const std::string& method,const std::string& timestamp,const std::string& endpoint)
 {
    std::string to_sign = timestamp + method + endpoint;
    std::string decode_secret = base64_decode(unit.secret_key);
    unsigned char * strHmac = hmac_sha256_byte(decode_secret.c_str(),to_sign.c_str());
    std::string strSignatrue = base64_encode(strHmac,32);
    return strSignatrue;
 }
int TDEngineEmx::lws_write_msg(struct lws* conn)
{
	//KF_LOG_INFO(logger, "TDEngineEmx::lws_write_msg:" );
    
    int ret = 0;
    std::string strMsg = "";
    if (!m_isSub)
    {
        strMsg = makeSubscribeChannelString(account_units[0]);
        m_isSub = true;
    }
    else if(m_isSubOK)
    {
        std::lock_guard<std::mutex> lck(mutex_msg_queue);
        if(m_vstMsg.size() == 0)
            return 0;
        else
        {
            strMsg = m_vstMsg.front();
            m_vstMsg.pop();
        }
    }
    else
    {
        return 0;
    }
    
    unsigned char msg[1024];
    memset(&msg[LWS_PRE], 0, 1024-LWS_PRE);
    int length = strMsg.length();
    KF_LOG_INFO(logger, "TDEngineEmx::lws_write_msg: " << strMsg.c_str() << " ,len = " << length);
    strncpy((char *)msg+LWS_PRE, strMsg.c_str(), length);
    ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);
    lws_callback_on_writable(conn);  
    return ret;
}

void TDEngineEmx::on_lws_connection_error(struct lws* conn)
{
    KF_LOG_ERROR(logger, "TDEngineEmx::on_lws_connection_error. login again.");
	//no use it
    long timeout_nsec = 0;

    login(timeout_nsec);
}

static struct lws_protocols protocols[] =
	{
			{
					"md-protocol",
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

void TDEngineEmx::genUniqueKey()
{
    struct tm cur_time = getCurLocalTime();
    //SSMMHHDDN
    char key[11]{0};
    snprintf((char*)key, 11, "%02d%02d%02d%02d%02d", cur_time.tm_sec, cur_time.tm_min, cur_time.tm_hour, cur_time.tm_mday, m_CurrentTDIndex);
    m_uniqueKey = key;
}

//clientid =  m_uniqueKey+orderRef
std::string TDEngineEmx::genClinetid(const std::string &orderRef)
{
    static int nIndex = 0;
    return m_uniqueKey + orderRef + std::to_string(nIndex++);
}

void TDEngineEmx::writeErrorLog(std::string strError)
{
    KF_LOG_ERROR(logger, strError);
}



int64_t TDEngineEmx::getMSTime()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return  timestamp;
}




void TDEngineEmx::init()
{
    genUniqueKey();
    ITDEngine::init();
    JournalPair tdRawPair = getTdRawJournalPair(source_id);
    raw_writer = yijinjing::JournalSafeWriter::create(tdRawPair.first, tdRawPair.second, "RAW_" + name());
    KF_LOG_INFO(logger, "[init]");
}

void TDEngineEmx::pre_load(const json& j_config)
{
    KF_LOG_INFO(logger, "[pre_load]");
}

void TDEngineEmx::resize_accounts(int account_num)
{
    account_units.resize(account_num);
    KF_LOG_INFO(logger, "[resize_accounts]");
}

TradeAccount TDEngineEmx::load_account(int idx, const json& j_config)
{
    KF_LOG_INFO(logger, "[load_account]");
    // internal load
    string api_key = j_config["APIKey"].get<string>();
    string secret_key = j_config["SecretKey"].get<string>();
    string baseUrl = j_config["baseUrl"].get<string>();
    string wsUrl = j_config["wsUrl"].get<string>();
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
    if(j_config.find("current_td_index") != j_config.end()) {
        m_CurrentTDIndex = j_config["current_td_index"].get<int>();
    }
    KF_LOG_INFO(logger, "[load_account] (retry_interval_milliseconds)" << retry_interval_milliseconds);

    AccountUnitEmx& unit = account_units[idx];
    unit.api_key = api_key;
    unit.secret_key = secret_key;
    unit.baseUrl = baseUrl;
    unit.wsUrl = wsUrl;
    KF_LOG_INFO(logger, "[load_account] (api_key)" << api_key << " (baseUrl)" << unit.baseUrl);


    unit.coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
    unit.coinPairWhiteList.Debug_print();

    unit.positionWhiteList.ReadWhiteLists(j_config, "positionWhiteLists");
    unit.positionWhiteList.Debug_print();

    //display usage:
    if(unit.coinPairWhiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "TDEngineEmx::load_account: please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"btcusdt\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"etceth\"");
        KF_LOG_ERROR(logger, "},");
    }

    //test
    Document json;
    get_account(unit, json);
    
    getPriceIncrement(unit);
    // set up
    TradeAccount account = {};
    //partly copy this fields
    strncpy(account.UserID, api_key.c_str(), 16);
    strncpy(account.Password, secret_key.c_str(), 21);
    return account;
}

void TDEngineEmx::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[connect]");
    for (size_t idx = 0; idx < account_units.size(); idx++)
    {
        AccountUnitEmx& unit = account_units[idx];
        unit.logged_in = true;
        KF_LOG_INFO(logger, "[connect] (api_key)" << unit.api_key);
        login(timeout_nsec);
    }
	
    cancel_all_orders();
}

   void TDEngineEmx::getPriceIncrement(AccountUnitEmx& unit)
   { 
        KF_LOG_INFO(logger, "[getPriceIncrement]");
        std::string requestPath = "/v1/contracts/active";
        string url = unit.baseUrl + requestPath ;
        std::string strTimestamp = getTimestampStr();

        std::string strSignatrue = sign(unit,"GET",strTimestamp,requestPath);
        cpr::Header mapHeader = cpr::Header{{"EMX-ACCESS-SIG",strSignatrue},
                                            {"EMX-ACCESS-TIMESTAMP",strTimestamp},
                                            {"EMX-ACCESS-KEY",unit.api_key}};
        KF_LOG_INFO(logger, "EMX-ACCESS-SIG = " << strSignatrue 
                            << ", EMX-ACCESS-TIMESTAMP = " << strTimestamp 
                            << ", EMX-API-KEY = " << unit.api_key);


        std::unique_lock<std::mutex> lock(g_httpMutex);
        const auto response = cpr::Get(Url{url}, Header{mapHeader}, Timeout{10000} );
        lock.unlock();
        KF_LOG_INFO(logger, "[get] (url) " << url << " (response.status_code) " << response.status_code <<
                                                " (response.error.message) " << response.error.message <<
                                               " (response.text) " << response.text.c_str());
        Document json;
        json.Parse(response.text.c_str());

        if(!json.HasParseError() && json.HasMember("contracts"))
        {
            auto& jisonData = json["contracts"];
            size_t len = jisonData.Size();
            KF_LOG_INFO(logger, "[getPriceIncrement] (accounts.length)" << len);
            for(size_t i = 0; i < len; i++)
            {
                std::string symbol = jisonData.GetArray()[i]["contract_code"].GetString();
                std::string ticker = unit.coinPairWhiteList.GetKeyByValue(symbol);
                KF_LOG_INFO(logger, "[getPriceIncrement] (symbol) " << symbol << " (ticker) " << ticker);
                if(ticker.length() > 0) { 
                    std::string size = jisonData.GetArray()[i]["minimum_price_increment"].GetString(); 
                    PriceIncrement increment;
                    increment.nPriceIncrement = std::round(std::stod(size)*scale_offset);
                    unit.mapPriceIncrement.insert(std::make_pair(ticker,increment));           
                    KF_LOG_INFO(logger, "[getPriceIncrement] (symbol) " << symbol << " (position) " << increment.nPriceIncrement);
                }
            }
        }
        
   }

void TDEngineEmx::login(long timeout_nsec)
{
    KF_LOG_INFO(logger, "TDEngineEmx::login:");

    global_md = this;

    
    m_isSub = false;
    m_isSubOK = false;
	global_md = this;
	int inputPort = 8443;
	int logs = LLL_ERR | LLL_DEBUG | LLL_WARN;

	struct lws_context_creation_info ctxCreationInfo;
	struct lws_client_connect_info clientConnectInfo;
	struct lws *wsi = NULL;
	struct lws_protocols protocol;

	memset(&ctxCreationInfo, 0, sizeof(ctxCreationInfo));
	memset(&clientConnectInfo, 0, sizeof(clientConnectInfo));

	ctxCreationInfo.port = CONTEXT_PORT_NO_LISTEN;
	ctxCreationInfo.iface = NULL;
	ctxCreationInfo.protocols = protocols;
	ctxCreationInfo.ssl_cert_filepath = NULL;
	ctxCreationInfo.ssl_private_key_filepath = NULL;
	ctxCreationInfo.extensions = NULL;
	ctxCreationInfo.gid = -1;
	ctxCreationInfo.uid = -1;
	ctxCreationInfo.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
	ctxCreationInfo.fd_limit_per_thread = 1024;
	ctxCreationInfo.max_http_header_pool = 1024;
	ctxCreationInfo.ws_ping_pong_interval=1;
	ctxCreationInfo.ka_time = 10;
	ctxCreationInfo.ka_probes = 10;
	ctxCreationInfo.ka_interval = 10;

	protocol.name  = protocols[PROTOCOL_TEST].name;
	protocol.callback = &ws_service_cb;
	protocol.per_session_data_size = sizeof(struct session_data);
	protocol.rx_buffer_size = 0;
	protocol.id = 0;
	protocol.user = NULL;

	context = lws_create_context(&ctxCreationInfo);
	KF_LOG_INFO(logger, "TDEngineEmx::login: context created.");


	if (context == NULL) {
		KF_LOG_ERROR(logger, "TDEngineEmx::login: context is NULL. return");
		return;
	}

	// Set up the client creation info
	std::string strAddress = account_units[0].wsUrl;
    clientConnectInfo.address = strAddress.c_str();
    clientConnectInfo.path = "/"; // Set the info's path to the fixed up url path
	clientConnectInfo.context = context;
	clientConnectInfo.port = 443;
	clientConnectInfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
	clientConnectInfo.host =strAddress.c_str();
	clientConnectInfo.origin = strAddress.c_str();
	clientConnectInfo.ietf_version_or_minus_one = -1;
	clientConnectInfo.protocol = protocols[PROTOCOL_TEST].name;
	clientConnectInfo.pwsi = &wsi;

    KF_LOG_INFO(logger, "TDEngineEmx::login: address = " << clientConnectInfo.address << ",path = " << clientConnectInfo.path);

	wsi = lws_client_connect_via_info(&clientConnectInfo);
	if (wsi == NULL) {
		KF_LOG_ERROR(logger, "TDEngineEmx::login: wsi create error.");
		return;
	}
	KF_LOG_INFO(logger, "TDEngineEmx::login: wsi create success.");
    m_conn = wsi;
    //connect(timeout_nsec);
}

void TDEngineEmx::logout()
{
    KF_LOG_INFO(logger, "[logout]");
}

void TDEngineEmx::release_api()
{
    KF_LOG_INFO(logger, "[release_api]");
}

bool TDEngineEmx::is_logged_in() const
{
    KF_LOG_INFO(logger, "[is_logged_in]");
    for (auto& unit: account_units)
    {
        if (!unit.logged_in)
            return false;
    }
    return true;
}

bool TDEngineEmx::is_connected() const
{
    KF_LOG_INFO(logger, "[is_connected]");
    return is_logged_in();
}


std::string TDEngineEmx::GetSide(const LfDirectionType& input) {
    if (LF_CHAR_Buy == input) {
        return "buy";
    } else if (LF_CHAR_Sell == input) {
        return "sell";
    } else {
        return "";
    }
}

LfDirectionType TDEngineEmx::GetDirection(std::string input) {
    if ("buy" == input) {
        return LF_CHAR_Buy;
    } else if ("sell" == input) {
        return LF_CHAR_Sell;
    } else {
        return LF_CHAR_Buy;
    }
}

std::string TDEngineEmx::GetType(const LfOrderPriceTypeType& input) {
    if (LF_CHAR_LimitPrice == input) {
        return "limit";
    } else if (LF_CHAR_AnyPrice == input) {
        return "market";
    } else {
        return "";
    }
}

LfOrderPriceTypeType TDEngineEmx::GetPriceType(std::string input) 
{
    if ("limit" == input) {
        return LF_CHAR_LimitPrice;
    } else if ("market" == input) {
        return LF_CHAR_AnyPrice;
    } else {
        return '0';
    }
}

/**
 * req functions
 */
void TDEngineEmx::req_investor_position(const LFQryPositionField* data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId);

    AccountUnitEmx& unit = account_units[account_index];
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
    KF_LOG_INFO(logger, "[req_investor_position] (get_account)" );
    if(d.IsObject() && d.HasMember("code") && d.HasMember("message"))
    {
        errorId =  d["code"].GetInt();
        errorMsg = d["message"].GetString();
        KF_LOG_ERROR(logger, "[req_investor_position] failed!" << " (rid)" << requestId << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        raw_writer->write_error_frame(&pos, sizeof(LFRspPositionField), source_id, MSG_TYPE_LF_RSP_POS_KUCOIN, 1, requestId, errorId, errorMsg.c_str());
    }
    send_writer->write_frame(data, sizeof(LFQryPositionField), source_id, MSG_TYPE_LF_QRY_POS_EMX, 1, requestId);

    std::map<std::string,LFRspPositionField> tmp_map;
    if(!d.HasParseError() && d.HasMember("positions"))
    {
        auto& jisonData = d["positions"];
        size_t len = jisonData.Size();
        KF_LOG_INFO(logger, "[req_investor_position] (accounts.length)" << len);
        for(size_t i = 0; i < len; i++)
        {
            std::string symbol = jisonData.GetArray()[i]["contract_code"].GetString();
             KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId << " (symbol) " << symbol);
            std::string ticker = unit.coinPairWhiteList.GetKeyByValue(symbol);
             KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId << " (ticker) " << ticker);
            if(ticker.length() > 0) {            
                uint64_t nPosition = std::round(std::stod(jisonData.GetArray()[i]["quantity"].GetString()) * scale_offset);   
                auto it = tmp_map.find(ticker);
                if(it == tmp_map.end())
                {
                     it = tmp_map.insert(std::make_pair(ticker,pos)).first;
                     strncpy(it->second.InstrumentID, ticker.c_str(), 31);      
                }
                it->second.Position += nPosition;
                KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId << " (symbol) " << symbol << " (position) " << it->second.Position);
            }
        }
    }

    //send the filtered position
    int position_count = tmp_map.size();
    if(position_count > 0) {
        for (auto it =  tmp_map.begin() ; it != tmp_map.end() ;  ++it) {
            --position_count;
            on_rsp_position(&it->second, position_count == 0, requestId, 0, errorMsg.c_str());
        }
    }
    else
    {
        KF_LOG_INFO(logger, "[req_investor_position] (!findSymbolInResult) (requestId)" << requestId);
        on_rsp_position(&pos, 1, requestId, errorId, errorMsg.c_str());
    }
}

void TDEngineEmx::req_qry_account(const LFQryAccountField *data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_qry_account]");
}

void TDEngineEmx::dealPriceVolume(AccountUnitEmx& unit,const std::string& symbol,int64_t nPrice,int64_t nVolume,double& dDealPrice,double& dDealVolume)
{
        KF_LOG_DEBUG(logger, "[dealPriceVolume] (symbol)" << symbol);
        auto it = unit.mapPriceIncrement.find(symbol);
        if(it == unit.mapPriceIncrement.end())
        {
            KF_LOG_INFO(logger, "[dealPriceVolume] symbol not find :" << symbol);
            dDealVolume = nVolume * 1.0 / scale_offset;
            dDealPrice = nPrice * 1.0 / scale_offset;
        }
        else
        {
            int64_t nDealVolume =  it->second.nQuoteIncrement  > 0 ? nVolume / it->second.nQuoteIncrement * it->second.nQuoteIncrement : nVolume;
            int64_t nDealPrice = it->second.nPriceIncrement > 0 ? nPrice / it->second.nPriceIncrement * it->second.nPriceIncrement : nPrice;
            dDealVolume = nDealVolume * 1.0 / scale_offset;
            dDealPrice = nDealPrice * 1.0 / scale_offset;
            
        }
        char strVolume[64];
        char strPrice[64];
        sprintf(strVolume,"%.4lf",dDealVolume + 0.00001);
        sprintf(strPrice,"%.2lf",dDealPrice + 0.001);
        dDealVolume = std::stod(strVolume);
        dDealPrice = std::stod(strPrice);
        KF_LOG_INFO(logger, "[dealPriceVolume]  (symbol)" << symbol << " (Volume)" << nVolume << " (Price)" << nPrice << " (FixedVolume)" << strVolume << " (FixedPrice)" << strPrice);
}

void TDEngineEmx::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitEmx& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_insert]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Tid)" << data->InstrumentID
                                              << " (Volume)" << data->Volume
                                              << " (LimitPrice)" << data->LimitPrice
                                              << " (OrderRef)" << data->OrderRef);
    send_writer->write_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_EMX, 1/*ISLAST*/, requestId);
    int errorId = 0;
    std::string errorMsg = "";
    on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_insert]: not in WhiteList, ignore it  (rid)" << requestId <<
                                                                                      " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_EMX, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_insert] (exchange_ticker)" << ticker);

    double fixedPrice = 0;
    double fixedVolume = 0;
    dealPriceVolume(unit,data->InstrumentID,data->LimitPrice,data->Volume,fixedPrice,fixedVolume);
    
    if(fixedVolume == 0)
    {
        KF_LOG_DEBUG(logger, "[req_order_insert] fixed Volume error" << ticker);
        errorId = 200;
        errorMsg = data->InstrumentID;
        errorMsg += " : quote less than baseMinSize";
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_EMX, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    std::string strClientId = genClinetid(data->OrderRef);
    {
        std::lock_guard<std::mutex> lck(*m_mutexOrder);
        m_mapInputOrder.insert(std::make_pair(strClientId,*data));
        LFRtnOrderField order;
        memset(&order, 0, sizeof(LFRtnOrderField));
        order.OrderStatus = LF_CHAR_Unknown;
        order.VolumeTotalOriginal = std::round(fixedVolume*scale_offset);
        order.VolumeTotal = order.VolumeTotalOriginal;
        strncpy(order.OrderRef, data->OrderRef, 21);
        strncpy(order.InstrumentID, data->InstrumentID, 31);
        order.RequestID = requestId;
        strcpy(order.ExchangeID, "Emx");
        strncpy(order.UserID, unit.api_key.c_str(), 16);
        order.LimitPrice = std::round(fixedPrice*scale_offset);
        order.TimeCondition = data->TimeCondition;
        order.Direction = data->Direction;
        order.OrderPriceType = data->OrderPriceType;
        m_mapNewOrder.insert(std::make_pair(strClientId,order));
    }

    send_order(ticker.c_str(),strClientId.c_str(), GetSide(data->Direction).c_str(),GetType(data->OrderPriceType).c_str(), fixedVolume, fixedPrice);

   
}

void TDEngineEmx::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitEmx& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_action]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Iid)" << data->InvestorID
                                              << " (OrderRef)" << data->OrderRef
                                              << " (KfOrderID)" << data->KfOrderID);

    send_writer->write_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_EMX, 1, requestId);

    int errorId = 0;
    std::string errorMsg = "";
    on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_action]: not in WhiteList , ignore it: (rid)" << requestId << " (errorId)" <<
                                                                                       errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_EMX, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_action] (exchange_ticker)" << ticker);
    std::lock_guard<std::mutex> lck(*m_mutexOrder);
    std::map<std::string, std::string>::iterator itr = localOrderRefRemoteOrderId.find(data->OrderRef);
    std::string remoteOrderId;
    if(itr == localOrderRefRemoteOrderId.end()) {
        errorId = 1;
        std::stringstream ss;
        ss << "[req_order_action] not found in localOrderRefRemoteOrderId map (orderRef) " << data->OrderRef;
        errorMsg = ss.str();
        KF_LOG_ERROR(logger, "[req_order_action] not found in localOrderRefRemoteOrderId map. "
                << " (rid)" << requestId << " (orderRef)" << data->OrderRef << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_EMX, 1, requestId, errorId, errorMsg.c_str());
        return;
    } else {
        remoteOrderId = itr->second;
        KF_LOG_DEBUG(logger, "[req_order_action] found in localOrderRefRemoteOrderId map (orderRef) "
                << data->OrderRef << " (remoteOrderId) " << remoteOrderId);
        {
            //std::lock_guard<std::mutex> lck(*m_mutexOrder);
            m_mapOrderAction.insert(std::make_pair(remoteOrderId,*data));
        }
        cancel_order(remoteOrderId);
    }
    
}

//对于每个撤单指令发出后30秒（可配置）内，如果没有收到回报，就给策略报错（撤单被拒绝，pls retry)
void TDEngineEmx::addRemoteOrderIdOrderActionSentTime(const LFOrderActionField* data, int requestId, const std::string& remoteOrderId)
{
    std::lock_guard<std::mutex> guard_mutex_order_action(*mutex_orderaction_waiting_response);

    OrderActionSentTime newOrderActionSent;
    newOrderActionSent.requestId = requestId;
    newOrderActionSent.sentNameTime = getTimestamp();
    memcpy(&newOrderActionSent.data, data, sizeof(LFOrderActionField));
    remoteOrderIdOrderActionSentTime[remoteOrderId] = newOrderActionSent;
}


void TDEngineEmx::set_reader_thread()
{
    ITDEngine::set_reader_thread();

    KF_LOG_INFO(logger, "[set_reader_thread] rest_thread start on TDEngineEmx::loop");
    rest_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineEmx::loopwebsocket, this)));

    //KF_LOG_INFO(logger, "[set_reader_thread] orderaction_timeout_thread start on TDEngineEmx::loopOrderActionNoResponseTimeOut");
    //orderaction_timeout_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineEmx::loopOrderActionNoResponseTimeOut, this)));
}

void TDEngineEmx::loopwebsocket()
{
		while(isRunning)
		{
            //KF_LOG_INFO(logger, "TDEngineEmx::loop:lws_service");
			lws_service( context, rest_get_interval_ms );
            //延时返回撤单回报
            std::lock_guard<std::mutex> lck(*m_mutexOrder); 
            for(auto canceled_order = m_mapCanceledOrder.begin();canceled_order != m_mapCanceledOrder.end();++canceled_order)
            {
                if(getMSTime() - canceled_order->second >= 1000)
                {// 撤单成功超过1秒时，回报205
                    auto it = m_mapOrder.find(canceled_order->first);
                    if(it != m_mapOrder.end())
                    {
                        on_rtn_order(&(it->second));
                        m_mapOrder.erase(it);
                    }
                    canceled_order = m_mapCanceledOrder.erase(canceled_order);
                    if(canceled_order == m_mapCanceledOrder.end())
                    {
                        break;
                    }
                }
            }
		}
}



void TDEngineEmx::loopOrderActionNoResponseTimeOut()
{
    KF_LOG_INFO(logger, "[loopOrderActionNoResponseTimeOut] (isRunning) " << isRunning);
    while(isRunning)
    {
        orderActionNoResponseTimeOut();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void TDEngineEmx::orderActionNoResponseTimeOut()
{
//    KF_LOG_DEBUG(logger, "[orderActionNoResponseTimeOut]");
    int errorId = 100;
    std::string errorMsg = "OrderAction has none response for a long time(" + std::to_string(orderaction_max_waiting_seconds) + " s), please send OrderAction again";

    std::lock_guard<std::mutex> guard_mutex_order_action(*mutex_orderaction_waiting_response);

    int64_t currentNano = getTimestamp();
    int64_t timeBeforeNano = currentNano - orderaction_max_waiting_seconds * 1000;
//    KF_LOG_DEBUG(logger, "[orderActionNoResponseTimeOut] (currentNano)" << currentNano << " (timeBeforeNano)" << timeBeforeNano);
    std::map<std::string, OrderActionSentTime>::iterator itr;
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

void TDEngineEmx::printResponse(const Document& d)
{
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);
    KF_LOG_INFO(logger, "[printResponse] ok (text) " << buffer.GetString());
}


void TDEngineEmx::get_account(AccountUnitEmx& unit, Document& json)
{
    KF_LOG_INFO(logger, "[get_account]");

    std::string requestPath = "/v1/positions";

    string url = unit.baseUrl + requestPath ;

    std::string strTimestamp = getTimestampStr();

    std::string strSignatrue = sign(unit,"GET",strTimestamp,requestPath);
    cpr::Header mapHeader = cpr::Header{{"EMX-ACCESS-SIG",strSignatrue},
                                        {"EMX-ACCESS-TIMESTAMP",strTimestamp},
                                        {"EMX-ACCESS-KEY",unit.api_key}};
     KF_LOG_INFO(logger, "EMX-ACCESS-SIG = " << strSignatrue 
                        << ", EMX-ACCESS-TIMESTAMP = " << strTimestamp 
                        << ", EMX-API-KEY = " << unit.api_key);


    std::unique_lock<std::mutex> lock(g_httpMutex);
    const auto response = cpr::Get(Url{url}, 
                             Header{mapHeader}, Timeout{10000} );
    lock.unlock();
    KF_LOG_INFO(logger, "[get] (url) " << url << " (response.status_code) " << response.status_code <<
                                               " (response.error.message) " << response.error.message <<
                                               " (response.text) " << response.text.c_str());
    
    json.Parse(response.text.c_str());
    return ;
}

/*
 {
  channel: "trading",
  type: "request",
  action: "create-order",
  data: {
    contract_code: "BTCU18",
    client_id: "My Order #10",
    type: "limit",
    side: "buy",
    size: "10.0000",
    price: "6500.00"
  }
}
 */
std::string TDEngineEmx::createInsertOrderString(const char *code,const char* strClientId,const char *side, const char *type, double& size, double& price)
{
    KF_LOG_INFO(logger, "[TDEngineEmx::createInsertOrdertring]:(price)"<<price << "(volume)" << size);
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();
    writer.Key("channel");
    writer.String("trading");
    writer.Key("type");
    writer.String("request");
    writer.Key("action");
    writer.String("create-order");
    writer.Key("data");
    writer.StartObject();
    writer.Key("client_id");
    writer.String(strClientId);
    writer.Key("side");
    writer.String(side);
    writer.Key("contract_code");
    writer.String(code);
    writer.Key("type");
    writer.String(type);
    if(strcmp("market",type) != 0)
    {
        std::stringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(2);
        ss << price;
        std::string strPrice = ss.str();
        writer.Key("price");
        writer.String(strPrice.c_str());
    }
    std::stringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(4);
    ss << size;
    std::string strSize = ss.str();
    writer.Key("size");
    writer.String(strSize.c_str());
    writer.EndObject();
    writer.EndObject();
    std::string strOrder = s.GetString();
    KF_LOG_INFO(logger, "[TDEngineEmx::createInsertOrdertring]:" << strOrder);
    return strOrder;
}

void TDEngineEmx::send_order(const char *code,const char* strClientId,const char *side, const char *type, double& size, double& price)
{
    KF_LOG_INFO(logger, "[send_order]");
    {
        std::string new_order = createInsertOrderString(code, strClientId,side, type, size, price);
        std::lock_guard<std::mutex> lck(mutex_msg_queue);
        m_vstMsg.push(new_order);
        lws_callback_on_writable(m_conn);
    }
}
    


void TDEngineEmx::cancel_all_orders()
{
    KF_LOG_INFO(logger, "[cancel_all_orders]");

    std::string cancel_order = createCancelOrderString(nullptr);
    std::lock_guard<std::mutex> lck(mutex_msg_queue);
    m_vstMsg.push(cancel_order);
    //lws_callback_on_writable(m_conn);
}
/*
{
  channel: "trading",
  type: "request",
  action: "cancel-order",
  data: {
    order_id: "58f5435e-02b8-4875-81d4-e3976c5ed68b"
  }
}
*/
std::string TDEngineEmx::createCancelOrderString(const char* strOrderId)
{
    KF_LOG_INFO(logger, "[TDEngineEmx::createCancelOrderString]");
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();
    writer.Key("channel");
    writer.String("trading");
    writer.Key("type");
    writer.String("request");
    writer.Key("action");
    if(nullptr != strOrderId)
    {
        writer.String("cancel-order");
    }
    else
    {
        writer.String("cancel-all-orders");
    }
    writer.Key("data");
    writer.StartObject();
    if(nullptr != strOrderId)
    {
        writer.Key("order_id");
        writer.String(strOrderId);
    }
    writer.EndObject();
    writer.EndObject();
    std::string strOrder = s.GetString();
    KF_LOG_INFO(logger, "[TDEngineEmx::createCancelOrderString]:" << strOrder);
    return strOrder;
}

void TDEngineEmx::cancel_order(std::string orderId)
{
    KF_LOG_INFO(logger, "[cancel_order]");
    std::string cancel_order = createCancelOrderString(orderId.c_str());
    std::lock_guard<std::mutex> lck(mutex_msg_queue);
    m_vstMsg.push(cancel_order);
    lws_callback_on_writable(m_conn);
}



std::string TDEngineEmx::parseJsonToString(Document &d)
{
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);

    return buffer.GetString();
}


inline int64_t TDEngineEmx::getTimestamp()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp;
}



#define GBK2UTF8(msg) kungfu::yijinjing::gbk2utf8(string(msg))

BOOST_PYTHON_MODULE(libemxtd)
{
    using namespace boost::python;
    class_<TDEngineEmx, boost::shared_ptr<TDEngineEmx> >("Engine")
            .def(init<>())
            .def("init", &TDEngineEmx::initialize)
            .def("start", &TDEngineEmx::start)
            .def("stop", &TDEngineEmx::stop)
            .def("logout", &TDEngineEmx::logout)
            .def("wait_for_stop", &TDEngineEmx::wait_for_stop);
}
