#include "TDEngineKuCoin.h"
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
using utils::crypto::hmac_sha256;
using utils::crypto::hmac_sha256_byte;
using utils::crypto::base64_encode;
USING_WC_NAMESPACE

TDEngineKuCoin::TDEngineKuCoin(): ITDEngine(SOURCE_KUCOIN)
{
    logger = yijinjing::KfLog::getLogger("TradeEngine.KuCoin");
    KF_LOG_INFO(logger, "[TDEngineKuCoin]");

    m_mutexOrder = new std::mutex();
    mutex_order_and_trade = new std::mutex();
    mutex_response_order_status = new std::mutex();
    mutex_orderaction_waiting_response = new std::mutex();
}

TDEngineKuCoin::~TDEngineKuCoin()
{
    if(m_mutexOrder != nullptr) delete m_mutexOrder;
    if(mutex_order_and_trade != nullptr) delete mutex_order_and_trade;
    if(mutex_response_order_status != nullptr) delete mutex_response_order_status;
    if(mutex_orderaction_waiting_response != nullptr) delete mutex_orderaction_waiting_response;
}

static TDEngineKuCoin* global_md = nullptr;

static int ws_service_cb( struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len )
{
    std::stringstream ss;
    ss << "lws_callback,reason=" << reason << ",";
	switch( reason )
	{
		case LWS_CALLBACK_CLIENT_ESTABLISHED:
		{
            ss << "LWS_CALLBACK_CLIENT_ESTABLISHED.";
            global_md->writeErrorLog(ss.str());
			//lws_callback_on_writable( wsi );
			break;
		}
		case LWS_CALLBACK_PROTOCOL_INIT:
        {
			 ss << "LWS_CALLBACK_PROTOCOL_INIT.";
            global_md->writeErrorLog(ss.str());
			break;
		}
		case LWS_CALLBACK_CLIENT_RECEIVE:
		{
		     ss << "LWS_CALLBACK_CLIENT_RECEIVE.";
           		//global_md->writeErrorLog(ss.str());
			if(global_md)
			{
				global_md->on_lws_data(wsi, (const char*)in, len);
			}
			break;
		}
		case LWS_CALLBACK_CLIENT_WRITEABLE:
		{
		    ss << "LWS_CALLBACK_CLIENT_WRITEABLE.";
            global_md->writeErrorLog(ss.str());
            int ret = 0;
			if(global_md)
			{
				ret = global_md->lws_write_subscribe(wsi);
			}
			break;
		}
		case LWS_CALLBACK_CLOSED:
        {
           // ss << "LWS_CALLBACK_CLOSED.";
           // global_md->writeErrorLog(ss.str());
           // break;
        }
        case LWS_CALLBACK_WSI_DESTROY:
		case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
		{
           // ss << "LWS_CALLBACK_CLIENT_CONNECTION_ERROR.";
            global_md->writeErrorLog(ss.str());
 			if(global_md)
			{
				global_md->on_lws_connection_error(wsi);
			}
			break;
		}
		default:
              global_md->writeErrorLog(ss.str());
			break;
	}

	return 0;
}

std::string TDEngineKuCoin::getId()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return  std::to_string(timestamp);
}

 void TDEngineKuCoin::onPong(struct lws* conn)
 {
     Ping(conn);
 }

 void TDEngineKuCoin::Ping(struct lws* conn)
 {
     m_shouldPing = false;
    StringBuffer sbPing;
	Writer<StringBuffer> writer(sbPing);
	writer.StartObject();
	writer.Key("id");
	writer.String(getId().c_str());
	writer.Key("type");
	writer.String("ping");
	writer.EndObject();
    std::string strPing = sbPing.GetString();
    unsigned char msg[512];
    memset(&msg[LWS_PRE], 0, 512-LWS_PRE);
     int length = strPing.length();
    KF_LOG_INFO(logger, "TDEngineKuCoin::lws_write_ping: " << strPing.c_str() << " ,len = " << length);
    strncpy((char *)msg+LWS_PRE, strPing.c_str(), length);
    int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);
 }

 void TDEngineKuCoin::onOrder(const PendingOrderStatus& stPendingOrderStatus)
 {
            LFRtnOrderField rtn_order;
            memset(&rtn_order, 0, sizeof(LFRtnOrderField));
            rtn_order.RequestID = stPendingOrderStatus.nRequestID;
            rtn_order.OrderStatus = stPendingOrderStatus.OrderStatus;
            rtn_order.VolumeTraded = stPendingOrderStatus.VolumeTraded;

            //first send onRtnOrder about the status change or VolumeTraded change
            strcpy(rtn_order.ExchangeID, "kucoin");
            strncpy(rtn_order.UserID, stPendingOrderStatus.strUserID.c_str(), sizeof(rtn_order.UserID));
            strncpy(rtn_order.InstrumentID, stPendingOrderStatus.InstrumentID, sizeof(rtn_order.InstrumentID));
            rtn_order.Direction = stPendingOrderStatus.Direction;
            //No this setting on KuCoin
            rtn_order.TimeCondition = LF_CHAR_GTC;
            rtn_order.OrderPriceType = stPendingOrderStatus.OrderPriceType;
            strncpy(rtn_order.OrderRef, stPendingOrderStatus.OrderRef, sizeof(rtn_order.OrderRef));
            rtn_order.VolumeTotal = stPendingOrderStatus.nVolume - rtn_order.VolumeTraded;
            rtn_order.LimitPrice = stPendingOrderStatus.nPrice;
            rtn_order.VolumeTotalOriginal = stPendingOrderStatus.nVolume;
            strncpy(rtn_order.BusinessUnit,stPendingOrderStatus.remoteOrderId.c_str(),sizeof(rtn_order.BusinessUnit));

            on_rtn_order(&rtn_order);

            raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                    source_id, MSG_TYPE_LF_RTN_ORDER_KUCOIN,
                                    1, (rtn_order.RequestID > 0) ? rtn_order.RequestID : -1);
            
            KF_LOG_INFO(logger, "[on_rtn_order] (InstrumentID)" << rtn_order.InstrumentID << "(OrderStatus)" <<  rtn_order.OrderStatus
                        << "(Volume)" << rtn_order.VolumeTotalOriginal << "(VolumeTraded)" << rtn_order.VolumeTraded);


 }

void TDEngineKuCoin::onTrade(const PendingOrderStatus& stPendingOrderStatus,int64_t nSize,int64_t nPrice,std::string& strTradeId,std::string& strTime)
{
            LFRtnTradeField rtn_trade;
            memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
            strcpy(rtn_trade.ExchangeID, "kucoin");
            strncpy(rtn_trade.UserID, stPendingOrderStatus.strUserID.c_str(), sizeof(rtn_trade.UserID));
            strncpy(rtn_trade.InstrumentID, stPendingOrderStatus.InstrumentID, sizeof(rtn_trade.InstrumentID));
            strncpy(rtn_trade.OrderRef, stPendingOrderStatus.OrderRef, sizeof(rtn_trade.OrderRef));
            rtn_trade.Direction = stPendingOrderStatus.Direction;
            //calculate the volumn and price (it is average too)
            rtn_trade.Volume = nSize;
            rtn_trade.Price = nPrice;
            strncpy(rtn_trade.OrderSysID,stPendingOrderStatus.remoteOrderId.c_str(),sizeof(rtn_trade.OrderSysID));
            strncpy(rtn_trade.TradeID, strTradeId.c_str(), sizeof(rtn_trade.TradeID));
            strncpy(rtn_trade.TradeTime, strTime.c_str(), sizeof(rtn_trade.TradeTime));
            strncpy(rtn_trade.ClientID, stPendingOrderStatus.strClientId.c_str(), sizeof(rtn_trade.ClientID));
            on_rtn_trade(&rtn_trade);
            raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
                                    source_id, MSG_TYPE_LF_RTN_TRADE_KUCOIN, 1, -1);

             KF_LOG_INFO(logger, "[on_rtn_trade 1] (InstrumentID)" << rtn_trade.InstrumentID << "(Direction)" << rtn_trade.Direction 
                        << "(Volume)" << rtn_trade.Volume << "(Price)" <<  rtn_trade.Price);
}

 void TDEngineKuCoin::onOrderChange(Document& d)
 {
        if(d.HasMember("data"))
        {
            auto& data = d["data"];
            if(data.HasMember("type"))
            {
                std::string strType = data["type"].GetString();
                if(strType == "done" && data["reason"].GetString() == std::string("canceled"))
                {
                    std::string strOrderId = data["orderId"].GetString();
                    std::lock_guard<std::mutex> lck(*m_mutexOrder); 
                    auto it = m_mapOrder.find(strOrderId);
                    if(it != m_mapOrder.end())
                    {
                        it->second.OrderStatus = LF_CHAR_Canceled;
                        onOrder( it->second);
                        m_mapOrder.erase(it);
                    }
                }
                if(strType == "match")
                {
                    std::string strOrderId = data["takerOrderId"].GetString();
                    std::lock_guard<std::mutex> lck(*m_mutexOrder); 
                    auto it = m_mapOrder.find(strOrderId);
                    if(it != m_mapOrder.end())
                    {
                        int64_t nSize = std::round(std::stod(data["size"].GetString()) * scale_offset);
                        int64_t nPrice = std::round(std::stod(data["price"].GetString()) * scale_offset);
                        std::string strTradeId = data["tradeId"].GetString();
                        std::string strTime = data["time"].GetString();
                        it->second.VolumeTraded += nSize;
                        it->second.OrderStatus =  it->second.VolumeTraded ==  it->second.nVolume ? LF_CHAR_AllTraded : LF_CHAR_PartTradedQueueing;
                        onOrder( it->second);
                        onTrade(it->second,nSize,nPrice,strTradeId,strTime);
                       if( it->second.OrderStatus == LF_CHAR_AllTraded)
                       {
                            m_mapOrder.erase(it);
                       }
                    }

                    strOrderId = data["makerOrderId"].GetString();
                    it = m_mapOrder.find(strOrderId);
                    if(it != m_mapOrder.end())
                    {
                        int64_t nSize = std::round(std::stod(data["size"].GetString()) * scale_offset);
                        int64_t nPrice = std::round(std::stod(data["price"].GetString()) * scale_offset);
                        std::string strTradeId = data["tradeId"].GetString();
                        std::string strTime = data["time"].GetString();
                        it->second.VolumeTraded += nSize;
                        it->second.OrderStatus =  it->second.VolumeTraded ==  it->second.nVolume ? LF_CHAR_AllTraded : LF_CHAR_PartTradedQueueing;
                        onOrder( it->second);
                       onTrade(it->second,nSize,nPrice,strTradeId,strTime);
                       if( it->second.OrderStatus == LF_CHAR_AllTraded)
                       {
                            m_mapOrder.erase(it);
                       }
                    }

                }
            }
        }
 }

void TDEngineKuCoin::on_lws_data(struct lws* conn, const char* data, size_t len)
{
    //std::string strData = dealDataSprit(data);
//	KF_LOG_INFO(logger, "TDEngineKuCoin::on_lws_data: " << data);
    Document json;
	json.Parse(data);

    if(!json.HasParseError() && json.IsObject() && json.HasMember("type") && json["type"].IsString())
	{
        if(strcmp(json["type"].GetString(), "welcome") == 0)
        {
            KF_LOG_INFO(logger, "MDEngineKuCoin::on_lws_data: welcome");
            lws_callback_on_writable(conn);
        }
        if(strcmp(json["type"].GetString(), "pong") == 0)
		{
			KF_LOG_INFO(logger, "MDEngineKuCoin::on_lws_data: pong");
           m_isPong = true;
           m_conn = conn;
		}
		if(strcmp(json["type"].GetString(), "message") == 0)
		{
           onOrderChange(json);
		}	
	} else 
    {
		KF_LOG_ERROR(logger, "MDEngineKuCoin::on_lws_data . parse json error: " << data);
	}
	
}

std::string TDEngineKuCoin::makeSubscribeL3Update(const std::map<std::string,int>& mapAllSymbols)
{
    StringBuffer sbUpdate;
	Writer<StringBuffer> writer(sbUpdate);
	writer.StartObject();
	writer.Key("id");
	writer.String(getId().c_str());
	writer.Key("type");
	writer.String("subscribe");
	writer.Key("topic");
    std::string strTopic = "/market/level3:";
    for(const auto&  pair : mapAllSymbols)
    {
        strTopic += pair.first + ",";
    }
    strTopic.pop_back();
	writer.String(strTopic.c_str());
	writer.Key("privateChannel");
	writer.String("false");
    writer.Key("response");
	writer.String("true");
	writer.EndObject();
    std::string strUpdate = sbUpdate.GetString();

    return strUpdate;
}

int TDEngineKuCoin::lws_write_subscribe(struct lws* conn)
{
	KF_LOG_INFO(logger, "TDEngineKuCoin::lws_write_subscribe:" );
    
    int ret = 0;

    if(!m_isSubL3)
    {
         m_isSubL3 = true;

        std::map<std::string,int> mapAllSymbols;
        for(auto& unit : account_units)
        {
            for(auto& pair :  unit.coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList())
            {
                mapAllSymbols[pair.second] = 0;
            }
        }

        std::string strSubscribe = makeSubscribeL3Update(mapAllSymbols);
        unsigned char msg[1024];
        memset(&msg[LWS_PRE], 0, 1024-LWS_PRE);
        int length = strSubscribe.length();
        KF_LOG_INFO(logger, "TDEngineKuCoin::lws_write_subscribe: " << strSubscribe.c_str() << " ,len = " << length);
        strncpy((char *)msg+LWS_PRE, strSubscribe.c_str(), length);
        ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);
        lws_callback_on_writable(conn);  
    }
    else
    {
        if(m_shouldPing)
        {
            m_isPong = false;
            Ping(conn);
        }
    }
    
    return ret;
}

void TDEngineKuCoin::on_lws_connection_error(struct lws* conn)
{
    KF_LOG_ERROR(logger, "TDEngineKuCoin::on_lws_connection_error. login again.");
    //clear the price book, the new websocket will give 200 depth on the first connect, it will make a new price book
    m_isPong = false;
    m_shouldPing = true;
	//no use it
    long timeout_nsec = 0;
    //reset sub
    m_isSubL3 = false;

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
int lws_write_subscribe(struct lws* conn);
            void on_lws_connection_error(struct lws* conn);

enum protocolList {
	PROTOCOL_TEST,

	PROTOCOL_LIST_COUNT
};

struct session_data {
    int fd;
};

void TDEngineKuCoin::writeErrorLog(std::string strError)
{
    KF_LOG_ERROR(logger, strError);
}

bool TDEngineKuCoin::getToken(Document& d) 
{
    int nTryCount = 0;
    cpr::Response response;
    do{
        std::string url = "https://api.kucoin.com/api/v1/bullet-public";
       response = cpr::Post(Url{url.c_str()}, Parameters{}); 
       
    }while(++nTryCount < max_rest_retry_times && response.status_code != 200);

    if(response.status_code != 200)
    {
        KF_LOG_ERROR(logger, "TDEngineKuCoin::login::getToken Error");
        return false;
    }

    KF_LOG_INFO(logger, "TDEngineKuCoin::getToken: " << response.text.c_str());

    d.Parse(response.text.c_str());
    return true;
}


bool TDEngineKuCoin::getServers(Document& d)
{
    m_vstServerInfos.clear();
    m_strToken = "";
     if(d.HasMember("data"))
     {
         auto& data = d["data"];
         if(data.HasMember("token"))
         {
             m_strToken = data["token"].GetString();
             if(data.HasMember("instanceServers"))
             {
                 int nSize = data["instanceServers"].Size();
                for(int nPos = 0;nPos<nSize;++nPos)
                {
                    ServerInfo stServerInfo;
                    auto& server = data["instanceServers"].GetArray()[nPos];
                    if(server.HasMember("pingInterval"))
                    {
                        stServerInfo.nPingInterval = server["pingInterval"].GetInt();
                    }
                    if(server.HasMember("pingTimeOut"))
                    {
                        stServerInfo.nPingTimeOut = server["pingTimeOut"].GetInt();
                    }
                    if(server.HasMember("endpoint"))
                    {
                        stServerInfo.strEndpoint = server["endpoint"].GetString();
                    }
                    if(server.HasMember("protocol"))
                    {
                        stServerInfo.strProtocol = server["protocol"].GetString();
                    }
                    if(server.HasMember("encrypt"))
                    {
                        stServerInfo.bEncrypt = server["encrypt"].GetBool();
                    }
                    m_vstServerInfos.push_back(stServerInfo);
                }
             }
         }
     }
    if(m_strToken == "" || m_vstServerInfos.empty())
    {
        KF_LOG_ERROR(logger, "TDEngineKuCoin::login::getServers Error");
        return false;
    }
    return true;
}

int64_t TDEngineKuCoin::getMSTime()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return  timestamp;
}


cpr::Header TDEngineKuCoin::construct_request_header(AccountUnitKuCoin& unit,const std::string& strSign,const std::string& strContentType)
{
    unsigned char * strHmac = hmac_sha256_byte(unit.secret_key.c_str(),strSign.c_str());
    std::string strSignatrue = base64_encode(strHmac,32);
    
    if(strContentType.empty())
    {
           return cpr::Header{{"KC-API-SIGN",strSignatrue},
                                        {"KC-API-TIMESTAMP",std::to_string(getTimestamp())},
                                        {"KC-API-KEY",unit.api_key},
                                        {"KC-API-PASSPHRASE",unit.passphrase}};
    }
    else
    {
        return cpr::Header{{"KC-API-SIGN",strSignatrue},
                                    {"KC-API-TIMESTAMP",std::to_string(getTimestamp())},
                                    {"KC-API-KEY",unit.api_key},
                                    {"KC-API-PASSPHRASE",unit.passphrase},
                                    {"Contenr-Type",strContentType}};
    }
    
}

std::mutex g_httpMutex;
cpr::Response TDEngineKuCoin::Get(const std::string& method_url,const std::string& body, AccountUnitKuCoin& unit)
{
    string url = unit.baseUrl + method_url;
    std::string strTimestamp = std::to_string(getTimestamp());
    std::string strSign = strTimestamp + "GET" + method_url;
    KF_LOG_INFO(logger, "strSign = " << strSign );
    unsigned char* strHmac = hmac_sha256_byte(unit.secret_key.c_str(),strSign.c_str());
    KF_LOG_INFO(logger, "strHmac = " << strHmac );
    std::string strSignatrue = base64_encode(strHmac,32);
    cpr::Header mapHeader = cpr::Header{{"KC-API-SIGN",strSignatrue},
                                        {"KC-API-TIMESTAMP",strTimestamp},
                                        {"KC-API-KEY",unit.api_key},
                                        {"KC-API-PASSPHRASE",unit.passphrase}};
     KF_LOG_INFO(logger, "KC-API-SIGN = " << strSignatrue 
                        << ", KC-API-TIMESTAMP = " << strTimestamp 
                        << ", KC-API-KEY = " << unit.api_key 
                        << ", KC-API-PASSPHRASE = " << unit.passphrase);


    std::unique_lock<std::mutex> lock(g_httpMutex);
    const auto response = cpr::Get(Url{url}, 
                             Header{mapHeader}, Timeout{10000} );
    lock.unlock();
    KF_LOG_INFO(logger, "[get] (url) " << url << " (response.status_code) " << response.status_code <<
                                               " (response.error.message) " << response.error.message <<
                                               " (response.text) " << response.text.c_str());
    return response;
}

cpr::Response TDEngineKuCoin::Delete(const std::string& method_url,const std::string& body, AccountUnitKuCoin& unit)
{
    string url = unit.baseUrl + method_url + body;
    std::string strTimestamp = std::to_string(getTimestamp());
    std::string strSign =  strTimestamp + "DELETE" + method_url + body;
    KF_LOG_INFO(logger, "strSign = " << strSign );
    unsigned char* strHmac = hmac_sha256_byte(unit.secret_key.c_str(),strSign.c_str());
    KF_LOG_INFO(logger, "strHmac = " << strHmac );
    std::string strSignatrue = base64_encode(strHmac,32);
    cpr::Header mapHeader = cpr::Header{{"KC-API-SIGN",strSignatrue},
                                        {"KC-API-TIMESTAMP",strTimestamp},
                                        {"KC-API-KEY",unit.api_key},
                                        {"KC-API-PASSPHRASE",unit.passphrase}};
     KF_LOG_INFO(logger, "KC-API-SIGN = " << strSignatrue 
                        << ", KC-API-TIMESTAMP = " << strTimestamp 
                        << ", KC-API-KEY = " << unit.api_key 
                        << ", KC-API-PASSPHRASE = " << unit.passphrase);

    std::unique_lock<std::mutex> lock(g_httpMutex);
    const auto response = cpr::Delete(Url{url},Header{mapHeader}, Timeout{10000} );
    lock.unlock();
    KF_LOG_INFO(logger, "[delete] (url) " << url << " (response.status_code) " << response.status_code <<
                                               " (response.error.message) " << response.error.message <<
                                               " (response.text) " << response.text.c_str());
    return response;
}

cpr::Response TDEngineKuCoin::Post(const std::string& method_url,const std::string& body, AccountUnitKuCoin& unit)
{
    std::string strTimestamp = std::to_string(getTimestamp());
    std::string strSign =  strTimestamp + "POST" + method_url + body;
     KF_LOG_INFO(logger, "strSign = " << strSign );
    unsigned char* strHmac = hmac_sha256_byte(unit.secret_key.c_str(),strSign.c_str());
    std::string strSignatrue = base64_encode(strHmac,32);
    cpr::Header mapHeader = cpr::Header{{"KC-API-SIGN",strSignatrue},
                                        {"KC-API-TIMESTAMP",strTimestamp},
                                        {"KC-API-KEY",unit.api_key},
                                        {"KC-API-PASSPHRASE",unit.passphrase},
                                        {"Content-Type", "application/json"}};

    
    string url = unit.baseUrl + method_url;
    std::unique_lock<std::mutex> lock(g_httpMutex);
    auto response = cpr::Post(Url{url}, Header{mapHeader},
                    Body{body},Timeout{30000});
    lock.unlock();
    KF_LOG_INFO(logger, "[post] (url) " << url <<"(body) "<< body<< " (response.status_code) " << response.status_code <<
                                       " (response.error.message) " << response.error.message <<
                                       " (response.text) " << response.text.c_str());
    return response;
}

void TDEngineKuCoin::init()
{
    genUniqueKey();
    ITDEngine::init();
    JournalPair tdRawPair = getTdRawJournalPair(source_id);
    raw_writer = yijinjing::JournalSafeWriter::create(tdRawPair.first, tdRawPair.second, "RAW_" + name());
    KF_LOG_INFO(logger, "[init]");
}

void TDEngineKuCoin::pre_load(const json& j_config)
{
    KF_LOG_INFO(logger, "[pre_load]");
}

void TDEngineKuCoin::resize_accounts(int account_num)
{
    account_units.resize(account_num);
    KF_LOG_INFO(logger, "[resize_accounts]");
}

TradeAccount TDEngineKuCoin::load_account(int idx, const json& j_config)
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

    AccountUnitKuCoin& unit = account_units[idx];
    unit.api_key = api_key;
    unit.secret_key = secret_key;
    unit.passphrase = passphrase;
    unit.baseUrl = baseUrl;

    KF_LOG_INFO(logger, "[load_account] (api_key)" << api_key << " (baseUrl)" << unit.baseUrl);

//test rs256
  //  std::string data ="{}";
  //  std::string signature =utils::crypto::rsa256_private_sign(data, g_private_key);
   // std::string sign = base64_encode((unsigned char*)signature.c_str(), signature.size());
    //std::cout  << "[TDEngineKuCoin] (test rs256-base64-sign)" << sign << std::endl;

    //std::string decodeStr = utils::crypto::rsa256_pub_verify(data,signature, g_public_key);
    //std::cout  << "[TDEngineKuCoin] (test rs256-verify)" << (decodeStr.empty()?"yes":"no") << std::endl;

    unit.coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
    unit.coinPairWhiteList.Debug_print();

    unit.positionWhiteList.ReadWhiteLists(j_config, "positionWhiteLists");
    unit.positionWhiteList.Debug_print();

    //display usage:
    if(unit.coinPairWhiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "TDEngineKuCoin::load_account: please add whiteLists in kungfu.json like this :");
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
    getPriceIncrement(unit);
    // set up
    TradeAccount account = {};
    //partly copy this fields
    strncpy(account.UserID, api_key.c_str(), 16);
    strncpy(account.Password, secret_key.c_str(), 21);
    return account;
}

void TDEngineKuCoin::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[connect]");
    for (size_t idx = 0; idx < account_units.size(); idx++)
    {
        AccountUnitKuCoin& unit = account_units[idx];
        unit.logged_in = true;
        KF_LOG_INFO(logger, "[connect] (api_key)" << unit.api_key);
       // Document doc;
        //
        //std::string requestPath = "/key";
       // const auto response = Get(requestPath,"{}",unit);

      //  getResponse(response.status_code, response.text, response.error.message, doc);

       // if ( !unit.logged_in && doc.HasMember("code"))
        //{
         //   int code = doc["code"].GetInt();
         //   unit.logged_in = (code == 0);
        //}
    }
	login(timeout_nsec);
}

   void TDEngineKuCoin::getPriceIncrement(AccountUnitKuCoin& unit)
   {
        auto& coinPairWhiteList = unit.coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList();
        for(auto& pair : coinPairWhiteList)
        {
            Document json;
             const auto response = Get("/api/v1/symbols/" + pair.second,"",unit);
            json.Parse(response.text.c_str());
            const static std::string strSuccesse = "200000";
            if(json.HasMember("code") && json["code"].GetString() == strSuccesse)
            {
                auto& data = json["data"];
                PriceIncrement stPriceIncrement;
                stPriceIncrement.nBaseMinSize = std::round(std::stod(data["baseMinSize"].GetString())* scale_offset);
                stPriceIncrement.nPriceIncrement = std::round(std::stod(data["priceIncrement"].GetString()) * scale_offset);
                stPriceIncrement.nQuoteIncrement = std::round(std::stod(data["quoteIncrement"].GetString()) * scale_offset);
                unit.mapPriceIncrement.insert(std::make_pair(pair.first,stPriceIncrement));

                 KF_LOG_INFO(logger, "[getPriceIncrement] (BaseMinSize )" << stPriceIncrement.nBaseMinSize << "(PriceIncrement)" << stPriceIncrement.nPriceIncrement
                                    << "(QuoteIncrement)" << stPriceIncrement.nQuoteIncrement);
            }
        }
   }

void TDEngineKuCoin::login(long timeout_nsec)
{
    KF_LOG_INFO(logger, "TDEngineKuCoin::login:");

    global_md = this;

    Document d;
    if(!getToken(d))
    {
        return;
    }
    if(!getServers(d))
   {
       return;
   }
    m_isSubL3 = false;
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
	KF_LOG_INFO(logger, "TDEngineKuCoin::login: context created.");


	if (context == NULL) {
		KF_LOG_ERROR(logger, "TDEngineKuCoin::login: context is NULL. return");
		return;
	}

	// Set up the client creation info
    auto& stServerInfo = m_vstServerInfos.front();
	std::string strAddress = stServerInfo.strEndpoint;
    size_t nAddressEndPos = strAddress.find_last_of('/');
    std::string strPath = strAddress.substr(nAddressEndPos);
    strPath += "?token=";
    strPath += m_strToken;
    strPath += "&[connectId=" +  getId() +"]";
    strAddress = strAddress.substr(0,nAddressEndPos);
    strAddress = strAddress.substr(strAddress.find_last_of('/') + 1);
    clientConnectInfo.address = strAddress.c_str();
    clientConnectInfo.path = strPath.c_str(); // Set the info's path to the fixed up url path
	clientConnectInfo.context = context;
	clientConnectInfo.port = 443;
	clientConnectInfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
	clientConnectInfo.host =strAddress.c_str();
	clientConnectInfo.origin = strAddress.c_str();
	clientConnectInfo.ietf_version_or_minus_one = -1;
	clientConnectInfo.protocol = protocols[PROTOCOL_TEST].name;
	clientConnectInfo.pwsi = &wsi;

    KF_LOG_INFO(logger, "TDEngineKuCoin::login: address = " << clientConnectInfo.address << ",path = " << clientConnectInfo.path);

	wsi = lws_client_connect_via_info(&clientConnectInfo);
	if (wsi == NULL) {
		KF_LOG_ERROR(logger, "TDEngineKuCoin::login: wsi create error.");
		return;
	}
	KF_LOG_INFO(logger, "TDEngineKuCoin::login: wsi create success.");

    //connect(timeout_nsec);
}

void TDEngineKuCoin::logout()
{
    KF_LOG_INFO(logger, "[logout]");
}

void TDEngineKuCoin::release_api()
{
    KF_LOG_INFO(logger, "[release_api]");
}

bool TDEngineKuCoin::is_logged_in() const
{
    KF_LOG_INFO(logger, "[is_logged_in]");
    for (auto& unit: account_units)
    {
        if (!unit.logged_in)
            return false;
    }
    return true;
}

bool TDEngineKuCoin::is_connected() const
{
    KF_LOG_INFO(logger, "[is_connected]");
    return is_logged_in();
}


std::string TDEngineKuCoin::GetSide(const LfDirectionType& input) {
    if (LF_CHAR_Buy == input) {
        return "buy";
    } else if (LF_CHAR_Sell == input) {
        return "sell";
    } else {
        return "";
    }
}

LfDirectionType TDEngineKuCoin::GetDirection(std::string input) {
    if ("buy" == input) {
        return LF_CHAR_Buy;
    } else if ("sell" == input) {
        return LF_CHAR_Sell;
    } else {
        return LF_CHAR_Buy;
    }
}

std::string TDEngineKuCoin::GetType(const LfOrderPriceTypeType& input) {
    if (LF_CHAR_LimitPrice == input) {
        return "limit";
    } else if (LF_CHAR_AnyPrice == input) {
        return "market";
    } else {
        return "";
    }
}

LfOrderPriceTypeType TDEngineKuCoin::GetPriceType(std::string input) {
    if ("limit" == input) {
        return LF_CHAR_LimitPrice;
    } else if ("market" == input) {
        return LF_CHAR_AnyPrice;
    } else {
        return '0';
    }
}
//订单状态，﻿open（未成交）、filled（已完成）、canceled（已撤销）、cancel（撤销中）、partially-filled（部分成交）
LfOrderStatusType TDEngineKuCoin::GetOrderStatus(bool isCancel,int64_t nSize,int64_t nDealSize) {
    
    if(isCancel)
    {
          return LF_CHAR_Canceled; 
    }
    if(nDealSize == 0)
    {
        return LF_CHAR_NotTouched;
    }
    if(nSize > nDealSize)
   {
        return  LF_CHAR_PartTradedQueueing;
   }
    return LF_CHAR_AllTraded;
}

/**
 * req functions
 */
void TDEngineKuCoin::req_investor_position(const LFQryPositionField* data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId);

    AccountUnitKuCoin& unit = account_units[account_index];
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
    if(d.IsObject() && d.HasMember("code"))
    {
         KF_LOG_INFO(logger, "[req_investor_position] (getcode)" );
        errorId =  std::round(std::stod(d["code"].GetString()));
         KF_LOG_INFO(logger, "[req_investor_position] (errorId)" << errorId);
        if(errorId != 200000) {
            if (d.HasMember("msg") && d["msg"].IsString()) {
                errorMsg = d["msg"].GetString();
            }
            KF_LOG_ERROR(logger, "[req_investor_position] failed!" << " (rid)" << requestId << " (errorId)" << errorId
                                                                   << " (errorMsg) " << errorMsg);
            raw_writer->write_error_frame(&pos, sizeof(LFRspPositionField), source_id, MSG_TYPE_LF_RSP_POS_KUCOIN, 1, requestId, errorId, errorMsg.c_str());
        }
    }
    send_writer->write_frame(data, sizeof(LFQryPositionField), source_id, MSG_TYPE_LF_QRY_POS_KUCOIN, 1, requestId);

    std::map<std::string,LFRspPositionField> tmp_map;
    if(!d.HasParseError() && d.HasMember("data"))
    {
        auto& jisonData = d["data"];
        size_t len = jisonData.Size();
        KF_LOG_INFO(logger, "[req_investor_position] (accounts.length)" << len);
        for(size_t i = 0; i < len; i++)
        {
            std::string symbol = jisonData.GetArray()[i]["currency"].GetString();
             KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId << " (symbol) " << symbol);
            std::string ticker = unit.positionWhiteList.GetKeyByValue(symbol);
             KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId << " (ticker) " << ticker);
            if(ticker.length() > 0) {            
                uint64_t nPosition = std::round(std::stod(jisonData.GetArray()[i]["balance"].GetString()) * scale_offset);   
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

void TDEngineKuCoin::req_qry_account(const LFQryAccountField *data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_qry_account]");
}

void TDEngineKuCoin::dealPriceVolume(AccountUnitKuCoin& unit,const std::string& symbol,int64_t nPrice,int64_t nVolume,double& dDealPrice,double& dDealVolume)
{
        KF_LOG_DEBUG(logger, "[dealPriceVolume] (symbol)" << symbol);
        auto it = unit.mapPriceIncrement.find(symbol);
        if(it == unit.mapPriceIncrement.end())
        {
                  KF_LOG_INFO(logger, "[dealPriceVolume] symbol not find :" << symbol);
                  dDealVolume = 0;
                  return ;
        }
        else
        {
            if(it->second.nBaseMinSize > nVolume)
            {
                KF_LOG_INFO(logger, "[dealPriceVolume] (Volume) "  << nVolume  << " <  (BaseMinSize)  "  << it->second.nBaseMinSize << " (symbol)" << symbol);
                dDealVolume = 0;
                return ;
            }
            int64_t nDealVolume =  it->second.nQuoteIncrement  > 0 ? nVolume / it->second.nQuoteIncrement * it->second.nQuoteIncrement : nVolume;
            int64_t nDealPrice = it->second.nPriceIncrement > 0 ? nPrice / it->second.nPriceIncrement * it->second.nPriceIncrement : nPrice;
            dDealVolume = nDealVolume * 1.0 / scale_offset;
            dDealPrice = nDealPrice * 1.0 / scale_offset;
            char strVolume[64];
            char strPrice[64];
            sprintf(strVolume,"%.8lf",dDealVolume + 0.0000000001);
            sprintf(strPrice,"%.8lf",dDealPrice + 0.0000000001);
            dDealVolume = std::stod(strVolume);
            dDealPrice = std::stod(strPrice);
        }
         KF_LOG_INFO(logger, "[dealPriceVolume]  (symbol)" << symbol << " (Volume)" << nVolume << " (Price)" << nPrice 
                << " (FixedVolume)" << dDealVolume << " (FixedPrice)" << dDealPrice);
}

void TDEngineKuCoin::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitKuCoin& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_insert]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Tid)" << data->InstrumentID
                                              << " (Volume)" << data->Volume
                                              << " (LimitPrice)" << data->LimitPrice
                                              << " (OrderRef)" << data->OrderRef);
    send_writer->write_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_KUCOIN, 1/*ISLAST*/, requestId);
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
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_KUCOIN, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_insert] (exchange_ticker)" << ticker);

    double funds = 0;
    Document d;

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
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_KUCOIN, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    std::string strClientId = genClinetid(data->OrderRef);
    std::lock_guard<std::mutex> lck(*m_mutexOrder);
    send_order(unit, ticker.c_str(),strClientId, GetSide(data->Direction).c_str(),
               GetType(data->OrderPriceType).c_str(), fixedVolume, fixedPrice, funds, data->OrderRef,is_post_only(data),d);
    //d.Parse("{\"orderId\":19319936159776,\"result\":true}");
    //not expected response
    if(!d.IsObject())
    {
        errorId = 100;
        errorMsg = "send_order http response has parse error or is not json. please check the log";
        KF_LOG_ERROR(logger, "[req_order_insert] send_order error!  (rid)" << requestId << " (errorId)" <<
                                                                           errorId << " (errorMsg) " << errorMsg);
    } else  if(d.HasMember("code"))
    {
        int code =std::round(std::stod(d["code"].GetString()));
        if(code == 200000) {
            //if send successful and the exchange has received ok, then add to  pending query order list
            std::string remoteOrderId = d["data"]["orderId"].GetString();
            //fix defect of use the old value
            localOrderRefRemoteOrderId[std::string(data->OrderRef)] = remoteOrderId;
            KF_LOG_INFO(logger, "[req_order_insert] after send  (rid)" << requestId << " (OrderRef) " <<
                                                                       data->OrderRef << " (remoteOrderId) "
                                                                       << remoteOrderId);

            PendingOrderStatus stPendingOrderStatus;
            stPendingOrderStatus.nVolume = data->Volume;
            stPendingOrderStatus.nPrice = data->LimitPrice;;
            strncpy(stPendingOrderStatus.InstrumentID, data->InstrumentID, sizeof(stPendingOrderStatus.InstrumentID));
            strncpy(stPendingOrderStatus.OrderRef, data->OrderRef, sizeof(stPendingOrderStatus.OrderRef));
            stPendingOrderStatus.strUserID = unit.api_key;
            stPendingOrderStatus.OrderStatus = LF_CHAR_NotTouched;
            stPendingOrderStatus.VolumeTraded = 0;
            stPendingOrderStatus.Direction = data->Direction;
            stPendingOrderStatus.OrderPriceType = data->OrderPriceType;
            stPendingOrderStatus.remoteOrderId = remoteOrderId;
            stPendingOrderStatus.nRequestID = requestId;
            stPendingOrderStatus.strClientId = strClientId;
            onOrder(stPendingOrderStatus);
         
            KF_LOG_DEBUG(logger, "[req_order_insert] (addNewQueryOrdersAndTrades)" );
            char noneStatus = LF_CHAR_NotTouched;
            //addNewQueryOrdersAndTrades(unit, data->InstrumentID, data->OrderRef, noneStatus, 0, remoteOrderId);
            m_mapOrder[remoteOrderId] = stPendingOrderStatus;
            //success, only record raw data
            raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_KUCOIN, 1,
                                          requestId, errorId, errorMsg.c_str());
            KF_LOG_DEBUG(logger, "[req_order_insert] success" );
            return;

        }else {
            errorId = code;
            if(d.HasMember("msg") && d["msg"].IsString())
            {
                errorMsg = d["msg"].GetString();
            }
            KF_LOG_ERROR(logger, "[req_order_insert] send_order error!  (rid)" << requestId << " (errorId)" <<
                                                                               errorId << " (errorMsg) " << errorMsg);
        }
    }
    if(errorId != 0)
    {
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_KUCOIN, 1, requestId, errorId, errorMsg.c_str());
    }
}

void TDEngineKuCoin::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitKuCoin& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_action]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Iid)" << data->InvestorID
                                              << " (OrderRef)" << data->OrderRef
                                              << " (KfOrderID)" << data->KfOrderID);

    send_writer->write_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_KUCOIN, 1, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_action]: not in WhiteList , ignore it: (rid)" << requestId << " (errorId)" <<
                                                                                       errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_KUCOIN, 1, requestId, errorId, errorMsg.c_str());
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
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_KUCOIN, 1, requestId, errorId, errorMsg.c_str());
        return;
    } else {
        remoteOrderId = itr->second;
        KF_LOG_DEBUG(logger, "[req_order_action] found in localOrderRefRemoteOrderId map (orderRef) "
                << data->OrderRef << " (remoteOrderId) " << remoteOrderId);
    }

    Document d;
    cancel_order(unit, ticker, remoteOrderId, d);

    std::string strSuccessCode =  "200000";
    if(!d.HasParseError() && d.HasMember("code") && strSuccessCode != d["code"].GetString()) {
        errorId = std::stoi(d["code"].GetString());
        if(d.HasMember("msg") && d["msg"].IsString())
        {
            errorMsg = d["msg"].GetString();
        }
        KF_LOG_ERROR(logger, "[req_order_action] cancel_order failed!" << " (rid)" << requestId
                                                                       << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }

    if(errorId != 0)
    {
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
	raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_KUCOIN, 1, requestId, errorId, errorMsg.c_str());

    } else {
        //addRemoteOrderIdOrderActionSentTime( data, requestId, remoteOrderId);

       // addRemoteOrderIdOrderActionSentTime( data, requestId, remoteOrderId);

        //TODO:   onRtn order/on rtn trade

    }
    }

//对于每个撤单指令发出后30秒（可配置）内，如果没有收到回报，就给策略报错（撤单被拒绝，pls retry)
void TDEngineKuCoin::addRemoteOrderIdOrderActionSentTime(const LFOrderActionField* data, int requestId, const std::string& remoteOrderId)
{
    std::lock_guard<std::mutex> guard_mutex_order_action(*mutex_orderaction_waiting_response);

    OrderActionSentTime newOrderActionSent;
    newOrderActionSent.requestId = requestId;
    newOrderActionSent.sentNameTime = getTimestamp();
    memcpy(&newOrderActionSent.data, data, sizeof(LFOrderActionField));
    remoteOrderIdOrderActionSentTime[remoteOrderId] = newOrderActionSent;
}

void TDEngineKuCoin::GetAndHandleOrderTradeResponse()
{
    // KF_LOG_INFO(logger, "[GetAndHandleOrderTradeResponse]" );
    //every account
    for (size_t idx = 0; idx < account_units.size(); idx++)
    {
        AccountUnitKuCoin& unit = account_units[idx];
        if (!unit.logged_in)
        {
            continue;
        }
        moveNewOrderStatusToPending(unit);
        retrieveOrderStatus(unit);
    }//end every account
}


void TDEngineKuCoin::retrieveOrderStatus(AccountUnitKuCoin& unit)
{
    //KF_LOG_INFO(logger, "[retrieveOrderStatus] order_size:"<< unit.pendingOrderStatus.size());
    std::lock_guard<std::mutex> guard_mutex(*mutex_response_order_status);
    std::lock_guard<std::mutex> guard_mutex_order_action(*mutex_orderaction_waiting_response);

    std::vector<PendingOrderStatus>::iterator orderStatusIterator;

    for(orderStatusIterator = unit.pendingOrderStatus.begin(); orderStatusIterator != unit.pendingOrderStatus.end();)
    {

        std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(orderStatusIterator->InstrumentID));
        if(ticker.length() == 0) {
            KF_LOG_INFO(logger, "[retrieveOrderStatus]: not in WhiteList , ignore it:" << orderStatusIterator->InstrumentID);
            continue;
        }
        KF_LOG_INFO(logger, "[retrieveOrderStatus] get_order " << "( account.api_key) " << unit.api_key
                                                               << "  (account.pendingOrderStatus.InstrumentID) " << orderStatusIterator->InstrumentID
                                                               << "  (account.pendingOrderStatus.OrderRef) " << orderStatusIterator->OrderRef
                                                               << "  (account.pendingOrderStatus.remoteOrderId) " << orderStatusIterator->remoteOrderId
                                                               << "  (account.pendingOrderStatus.OrderStatus) " << orderStatusIterator->OrderStatus
                                                               << "  (exchange_ticker)" << ticker
        );

        Document d;
        query_order(unit, ticker,orderStatusIterator->remoteOrderId, d);

        //parse order status
        //订单状态，﻿open（未成交）、filled（已完成）、canceled（已撤销）、cancel（撤销中）、partially-filled（部分成交）
        if(d.HasParseError()) {
            //HasParseError, skip
            KF_LOG_ERROR(logger, "[retrieveOrderStatus] get_order response HasParseError " << " (symbol)" << orderStatusIterator->InstrumentID
                                                                                           << " (orderRef)" << orderStatusIterator->OrderRef
                                                                                           << " (remoteOrderId) " << orderStatusIterator->remoteOrderId);
            continue;
        }
        const std::string strSuccessCode = "200000";
         KF_LOG_INFO(logger, "[retrieveOrderStatus] query_order:");
        if(d.HasMember("code") && strSuccessCode ==  d["code"].GetString())
        {
            KF_LOG_INFO(logger, "[retrieveOrderStatus] (query success)");
            rapidjson::Value &data = d["data"];
            ResponsedOrderStatus responsedOrderStatus;
            responsedOrderStatus.ticker = ticker;
            double dDealFunds = std::stod(data["dealFunds"].GetString());
            double dDealSize = std::stod(data["dealSize"].GetString());
            responsedOrderStatus.averagePrice = dDealSize > 0 ? std::round(dDealFunds / dDealSize * scale_offset): 0;
            responsedOrderStatus.orderId = orderStatusIterator->remoteOrderId;
            //报单价格条件
            responsedOrderStatus.OrderPriceType = GetPriceType(data["type"].GetString());
            //买卖方向
            responsedOrderStatus.Direction = GetDirection(data["side"].GetString());
            //报单状态
            int64_t nDealSize = std::round(dDealSize * scale_offset);
            int64_t nSize = std::round(std::stod(data["size"].GetString()) * scale_offset);
            responsedOrderStatus.OrderStatus = GetOrderStatus(data["cancelExist"].GetBool(),nSize,nDealSize);
            responsedOrderStatus.price = std::round(std::stod(data["price"].GetString()) * scale_offset);
            responsedOrderStatus.volume = nSize;
            //今成交数量
            responsedOrderStatus.VolumeTraded = nDealSize;
            responsedOrderStatus.openVolume =  nSize - nDealSize;

            handlerResponseOrderStatus(unit, orderStatusIterator, responsedOrderStatus);

            //OrderAction发出以后，有状态回来，就清空这次OrderAction的发送状态，不必制造超时提醒信息
            remoteOrderIdOrderActionSentTime.erase(orderStatusIterator->remoteOrderId);
        } else {
            KF_LOG_INFO(logger, "[retrieveOrderStatus] (query failed)");
            std::string errorMsg;
            std::string errorId = d["code"].GetString();
            if(d.HasMember("msg") && d["msg"].IsString())
            {
                errorMsg = d["msg"].GetString();
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

void TDEngineKuCoin::addNewQueryOrdersAndTrades(AccountUnitKuCoin& unit, const char_31 InstrumentID,
                                                 const char_21 OrderRef, const LfOrderStatusType OrderStatus,
                                                 const uint64_t VolumeTraded, const std::string& remoteOrderId)
{
      KF_LOG_DEBUG(logger, "[addNewQueryOrdersAndTrades]" );
    //add new orderId for GetAndHandleOrderTradeResponse
    std::lock_guard<std::mutex> guard_mutex(*mutex_order_and_trade);

    PendingOrderStatus status;
    strncpy(status.InstrumentID, InstrumentID, 31);
    strncpy(status.OrderRef, OrderRef, 21);
    status.OrderStatus = OrderStatus;
    status.VolumeTraded = VolumeTraded;
    //status.averagePrice = 0.0;
    status.remoteOrderId = remoteOrderId;
    unit.newOrderStatus.push_back(status);
    KF_LOG_INFO(logger, "[addNewQueryOrdersAndTrades] (InstrumentID) " << status.InstrumentID
                                                                       << " (OrderRef) " << status.OrderRef
                                                                       << " (remoteOrderId) " << status.remoteOrderId
                                                                       << "(VolumeTraded)" << VolumeTraded);
}


void TDEngineKuCoin::moveNewOrderStatusToPending(AccountUnitKuCoin& unit)
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

void TDEngineKuCoin::set_reader_thread()
{
    ITDEngine::set_reader_thread();

    KF_LOG_INFO(logger, "[set_reader_thread] rest_thread start on TDEngineKuCoin::loop");
    rest_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineKuCoin::loopwebsocket, this)));

    KF_LOG_INFO(logger, "[set_reader_thread] orderaction_timeout_thread start on TDEngineKuCoin::loopOrderActionNoResponseTimeOut");
    orderaction_timeout_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineKuCoin::loopOrderActionNoResponseTimeOut, this)));
}

void TDEngineKuCoin::loopwebsocket()
{
        time_t nLastTime = time(0);

		while(isRunning)
		{
             time_t nNowTime = time(0);
            if(m_isPong && (nNowTime - nLastTime>= 30))
            {
                m_isPong = false;
                 nLastTime = nNowTime;
                 KF_LOG_INFO(logger, "TDEngineKuCoin::loop: last time = " <<  nLastTime << ",now time = " << nNowTime << ",m_isPong = " << m_isPong);
                m_shouldPing = true;
                lws_callback_on_writable(m_conn);  
            }
            //KF_LOG_INFO(logger, "TDEngineKuCoin::loop:lws_service");
			lws_service( context, rest_get_interval_ms );
		}
}

void TDEngineKuCoin::loop()
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


void TDEngineKuCoin::loopOrderActionNoResponseTimeOut()
{
    KF_LOG_INFO(logger, "[loopOrderActionNoResponseTimeOut] (isRunning) " << isRunning);
    while(isRunning)
    {
        orderActionNoResponseTimeOut();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void TDEngineKuCoin::orderActionNoResponseTimeOut()
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

void TDEngineKuCoin::printResponse(const Document& d)
{
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);
    KF_LOG_INFO(logger, "[printResponse] ok (text) " << buffer.GetString());
}

void TDEngineKuCoin::getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& json)
{
    if(http_status_code >= HTTP_RESPONSE_OK && http_status_code <= 299)
    {
        json.Parse(responseText.c_str());
    } else if(http_status_code == 0)
    {
        json.SetObject();
        Document::AllocatorType& allocator = json.GetAllocator();
        //int errorId = 1;
        json.AddMember("code", Document::StringRefType("1"), allocator);
        KF_LOG_INFO(logger, "[getResponse] (errorMsg)" << errorMsg);
        rapidjson::Value val;
        val.SetString(errorMsg.c_str(), errorMsg.length(), allocator);
        json.AddMember("msg", val, allocator);
    } else
    {
        /*
        {"code":"503000","msg":"Service unavailable"}
        */
        json.Parse(responseText.c_str());
        /*
        Document d;
        d.Parse(responseText.c_str());
        KF_LOG_INFO(logger, "[getResponse] (err) (responseText)" << responseText.c_str());
        json.SetObject();
        Document::AllocatorType& allocator = json.GetAllocator();
        json.AddMember("code", http_status_code, allocator);

        rapidjson::Value val;
        if(errorMsg.size() > 0)
        {
            val.SetString(errorMsg.c_str(), errorMsg.length(), allocator);
        }
        else if(responseText.size() > 0)
        {
            val.SetString(errorMsg.c_str(), errorMsg.length(), allocator);
        }
        json.AddMember("msg", val, allocator);
        */
    }
}

std::string TDEngineKuCoin::construct_request_body(const AccountUnitKuCoin& unit,const  std::string& data,bool isget)
{
    std::string pay_load = R"({"uid":")" + unit.api_key + R"(","data":)" + data + R"(})";
    std::string request_body = utils::crypto::jwt_create(pay_load,unit.secret_key);
    //std::cout  << "[construct_request_body] (request_body)" << request_body << std::endl;
    return  isget ? "user_jwt="+request_body:R"({"user_jwt":")"+request_body+"\"}";
}


void TDEngineKuCoin::get_account(AccountUnitKuCoin& unit, Document& json)
{
    KF_LOG_INFO(logger, "[get_account]");

    std::string requestPath = "/api/v1/accounts";
    //std::string queryString= construct_request_body(unit,"{}");
    //RkTgU1lne1aWSBnC171j0eJe__fILSclRpUJ7SWDDulWd4QvLa0-WVRTeyloJOsjyUtduuF0K0SdkYqXR-ibuULqXEDGCGSHSed8WaNtHpvf-AyCI-JKucLH7bgQxT1yPtrJC6W31W5dQ2Spp3IEpXFS49pMD3FRFeHF4HAImo9VlPUM_bP-1kZt0l9RbzWjxVtaYbx3L8msXXyr_wqacNnIV6X9m8eie_DqZHYzGrN_25PfAFgKmghfpL-jmu53kgSyTw5v-rfZRP9VMAuryRIMvOf9LBuMaxcuFn7PjVJx8F7fcEPBCd0roMTLKhHjFidi6QxZNUO1WKSkoSbRxA
            ;//construct_request_body(unit, "{}");

    //string url = unit.baseUrl + requestPath + queryString;

    const auto response = Get(requestPath,"{}",unit);
    
    json.Parse(response.text.c_str());
    return ;
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
std::string TDEngineKuCoin::createInsertOrdertring(const char *code,const std::string& strClientId,
                                                    const char *side, const char *type, double& size, double& price,const string& strOrderRef,bool isPostOnly)
{
    KF_LOG_INFO(logger, "[TDEngineKuCoin::createInsertOrdertring]:(price)"<<price << "(volume)" << size);
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();
    writer.Key("clientOid");
    writer.String(strClientId.c_str());

    writer.Key("side");
    writer.String(side);
    writer.Key("symbol");
    writer.String(code);
    writer.Key("type");
    writer.String(type);
    writer.Key("stp");
    writer.String("CO");
   
     //writer.Key("price");
     // writer.Double(price);
    //writer.Key("size");
    //writer.Double(size);
    if(isPostOnly)
    {
        writer.Key("postOnly");
        writer.Bool(isPostOnly);
    }
    writer.EndObject();
    std::stringstream ss;
    std::string str = s.GetString();
    str.pop_back();
    ss << str;
     if(strcmp("market",type) != 0)
    {
        ss << ",\"price\":" << price;
    }
    ss << ",\"size\":" << size << "}";
    str = ss.str();
    KF_LOG_INFO(logger, "[TDEngineKuCoin::createInsertOrdertring]:" << str);
    return str;
}

void TDEngineKuCoin::send_order(AccountUnitKuCoin& unit, const char *code,const std::string& strClientId,
                                 const char *side, const char *type, double& size, double& price, double funds, const std::string& strOrderRef, bool isPostOnly,Document& json)
{
    KF_LOG_INFO(logger, "[send_order]");

    int retry_times = 0;
    cpr::Response response;
    bool should_retry = false;
    do {
        should_retry = false;

        std::string requestPath = "/api/v1/orders";
        response = Post(requestPath,createInsertOrdertring(code, strClientId,side, type, size, price,strOrderRef,isPostOnly),unit);

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

bool TDEngineKuCoin::shouldRetry(Document& doc)
{
    bool ret = false;
    std::string strCode ;
    if(doc.HasMember("code"))
    {
       strCode = doc["code"].GetString();
    }
    bool isObJect = doc.IsObject();
    if(!isObJect || strCode != "200000")
    {
        ret = true;
    }

     KF_LOG_INFO(logger, "[shouldRetry] isObJect = " << isObJect << ",strCode = " << strCode);

    return ret;
}

void TDEngineKuCoin::cancel_all_orders(AccountUnitKuCoin& unit, std::string code, Document& json)
{
    KF_LOG_INFO(logger, "[cancel_all_orders]");

    std::string requestPath = "/api/v1/orders";
    //std::string queryString= "?user_jwt=RkTgU1lne1aWSBnC171j0eJe__fILSclRpUJ7SWDDulWd4QvLa0-WVRTeyloJOsjyUtduuF0K0SdkYqXR-ibuULqXEDGCGSHSed8WaNtHpvf-AyCI-JKucLH7bgQxT1yPtrJC6W31W5dQ2Spp3IEpXFS49pMD3FRFeHF4HAImo9VlPUM_bP-1kZt0l9RbzWjxVtaYbx3L8msXXyr_wqacNnIV6X9m8eie_DqZHYzGrN_25PfAFgKmghfpL-jmu53kgSyTw5v-rfZRP9VMAuryRIMvOf9LBuMaxcuFn7PjVJx8F7fcEPBCd0roMTLKhHjFidi6QxZNUO1WKSkoSbRxA";//construct_request_body(unit, "{}");

    auto response = Delete(requestPath,"",unit);

    getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineKuCoin::cancel_order(AccountUnitKuCoin& unit, std::string code, std::string orderId, Document& json)
{
    KF_LOG_INFO(logger, "[cancel_order]");

    int retry_times = 0;
    cpr::Response response;
    bool should_retry = false;
    do {
        should_retry = false;

        std::string requestPath = "/api/v1/orders/" + orderId;
        //std::string queryString= construct_request_body(unit, "{\"id\":" + orderId + "}");
        response = Delete(requestPath,"",unit);

        //json.Clear();
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

    //getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineKuCoin::query_order(AccountUnitKuCoin& unit, std::string code, std::string orderId, Document& json)
{
    KF_LOG_INFO(logger, "[query_order]");
    std::string requestPath = "/api/v1/orders/" + orderId;
    auto response = Get(requestPath,"",unit);

    json.Parse(response.text.c_str());
    //getResponse(response.status_code, response.text, response.error.message, json);
}



void TDEngineKuCoin::handlerResponseOrderStatus(AccountUnitKuCoin& unit, std::vector<PendingOrderStatus>::iterator orderStatusIterator, ResponsedOrderStatus& responsedOrderStatus)
{
     KF_LOG_INFO(logger, "[handlerResponseOrderStatus]");

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

            std::string strOrderID = orderStatusIterator->remoteOrderId;
            strncpy(rtn_order.BusinessUnit,strOrderID.c_str(),21);

            rtn_order.OrderStatus = LF_CHAR_PartTradedQueueing;
            rtn_order.VolumeTraded = responsedOrderStatus.VolumeTraded;
            //first send onRtnOrder about the status change or VolumeTraded change
            strcpy(rtn_order.ExchangeID, "kucoin");
            strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
            strncpy(rtn_order.InstrumentID, orderStatusIterator->InstrumentID, 31);
            rtn_order.Direction = responsedOrderStatus.Direction;
            //No this setting on KuCoin
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
                                    source_id, MSG_TYPE_LF_RTN_ORDER_KUCOIN,
                                    1, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);


            //send OnRtnTrade
            LFRtnTradeField rtn_trade;
            memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
            strcpy(rtn_trade.ExchangeID, "kucoin");
            strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
            strncpy(rtn_trade.InstrumentID, orderStatusIterator->InstrumentID, 31);
            strncpy(rtn_trade.OrderRef, orderStatusIterator->OrderRef, 13);
            rtn_trade.Direction = rtn_order.Direction;
            //double oldAmount = (double)orderStatusIterator->VolumeTraded/scale_offset * orderStatusIterator->averagePrice/scale_offset*1.0;
            //double newAmount = (double)rtn_order.VolumeTraded/scale_offset * newAveragePrice/scale_offset*1.0;

            //calculate the volumn and price (it is average too)
            rtn_trade.Volume = rtn_order.VolumeTraded - orderStatusIterator->VolumeTraded;
           // double price = (newAmount - oldAmount)/((double)rtn_trade.Volume/scale_offset);
           // rtn_trade.Price =(price + 0.000000001)*scale_offset;//(newAmount - oldAmount)/(rtn_trade.Volume);
            strncpy(rtn_trade.OrderSysID,strOrderID.c_str(),31);
            on_rtn_trade(&rtn_trade);
            raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
                                    source_id, MSG_TYPE_LF_RTN_TRADE_KUCOIN, 1, -1);

             KF_LOG_INFO(logger, "[on_rtn_trade 1] (InstrumentID)" << rtn_trade.InstrumentID << "(Direction)" << rtn_trade.Direction 
                        << "(Volume)" << rtn_trade.Volume << "(Price)" <<  rtn_trade.Price);


        }

        //emit the LF_CHAR_Canceled status
        LFRtnOrderField rtn_order;
        memset(&rtn_order, 0, sizeof(LFRtnOrderField));

        std::string strOrderID = orderStatusIterator->remoteOrderId;
        strncpy(rtn_order.BusinessUnit,strOrderID.c_str(),21);

        rtn_order.OrderStatus = LF_CHAR_Canceled;
        rtn_order.VolumeTraded = responsedOrderStatus.VolumeTraded;

        //first send onRtnOrder about the status change or VolumeTraded change
        strcpy(rtn_order.ExchangeID, "kucoin");
        strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
        strncpy(rtn_order.InstrumentID, orderStatusIterator->InstrumentID, 31);
        rtn_order.Direction = responsedOrderStatus.Direction;
        //KuCoin has no this setting
        rtn_order.TimeCondition = LF_CHAR_GTC;
        rtn_order.OrderPriceType = responsedOrderStatus.OrderPriceType;
        strncpy(rtn_order.OrderRef, orderStatusIterator->OrderRef, 13);
        rtn_order.VolumeTotalOriginal = responsedOrderStatus.volume;
        rtn_order.LimitPrice = responsedOrderStatus.price;
        //剩余数量
        rtn_order.VolumeTotal = responsedOrderStatus.openVolume;

        on_rtn_order(&rtn_order);
        raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                source_id, MSG_TYPE_LF_RTN_ORDER_KUCOIN,
                                1, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);
        
         KF_LOG_INFO(logger, "[on_rtn_order] (InstrumentID)" << rtn_order.InstrumentID << "(OrderStatus)" <<  rtn_order.OrderStatus
                        << "(Volume)" << rtn_order.VolumeTotalOriginal << "(VolumeTraded)" << rtn_order.VolumeTraded);


        //third, update last status for next query_order
        orderStatusIterator->OrderStatus = rtn_order.OrderStatus;
        orderStatusIterator->VolumeTraded = rtn_order.VolumeTraded;
       // orderStatusIterator->averagePrice = newAveragePrice;

    }
    else
    {
        //if status changed or LF_CHAR_PartTradedQueueing but traded valume changes, emit onRtnOrder
        LFRtnOrderField rtn_order;
        memset(&rtn_order, 0, sizeof(LFRtnOrderField));

        std::string strOrderID = orderStatusIterator->remoteOrderId;
        strncpy(rtn_order.BusinessUnit,strOrderID.c_str(),21);

        KF_LOG_INFO(logger, "[handlerResponseOrderStatus] VolumeTraded Change  LastOrderPsp:" << orderStatusIterator->VolumeTraded << ", NewOrderRsp: " << responsedOrderStatus.VolumeTraded  <<
                                                        " NewOrderRsp.Status " << responsedOrderStatus.OrderStatus);
        if(responsedOrderStatus.OrderStatus == LF_CHAR_NotTouched && responsedOrderStatus.VolumeTraded != orderStatusIterator->VolumeTraded) {
            rtn_order.OrderStatus = LF_CHAR_PartTradedQueueing;
        } else{
            rtn_order.OrderStatus = responsedOrderStatus.OrderStatus;
        }
        rtn_order.VolumeTraded = responsedOrderStatus.VolumeTraded;

        //first send onRtnOrder about the status change or VolumeTraded change
        strcpy(rtn_order.ExchangeID, "kucoin");
        strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
        strncpy(rtn_order.InstrumentID, orderStatusIterator->InstrumentID, 31);
        rtn_order.Direction = responsedOrderStatus.Direction;
        //No this setting on KuCoin
        rtn_order.TimeCondition = LF_CHAR_GTC;
        rtn_order.OrderPriceType = responsedOrderStatus.OrderPriceType;
        strncpy(rtn_order.OrderRef, orderStatusIterator->OrderRef, 13);
        rtn_order.VolumeTotalOriginal = responsedOrderStatus.volume;
        rtn_order.LimitPrice = responsedOrderStatus.price;
        rtn_order.VolumeTotal = responsedOrderStatus.openVolume;

        on_rtn_order(&rtn_order);
        raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                source_id, MSG_TYPE_LF_RTN_ORDER_KUCOIN,
                                1, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);

         KF_LOG_INFO(logger, "[on_rtn_order] (InstrumentID)" << rtn_order.InstrumentID << "(OrderStatus)" <<  rtn_order.OrderStatus
                        << "(Volume)" << rtn_order.VolumeTotalOriginal << "(VolumeTraded)" << rtn_order.VolumeTraded);

        int64_t newAveragePrice = responsedOrderStatus.averagePrice;
        //second, if the status is PartTraded/AllTraded, send OnRtnTrade
        if(rtn_order.OrderStatus == LF_CHAR_AllTraded ||
           (LF_CHAR_PartTradedQueueing == rtn_order.OrderStatus
            && rtn_order.VolumeTraded != orderStatusIterator->VolumeTraded))
        {
            LFRtnTradeField rtn_trade;
            memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
            strcpy(rtn_trade.ExchangeID, "kucoin");
            strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
            strncpy(rtn_trade.InstrumentID, orderStatusIterator->InstrumentID, 31);
            strncpy(rtn_trade.OrderRef, orderStatusIterator->OrderRef, 13);
            rtn_trade.Direction = rtn_order.Direction;
            //double oldAmount = (double)orderStatusIterator->VolumeTraded/scale_offset * orderStatusIterator->averagePrice/scale_offset*1.0;
           // double newAmount = (double)rtn_order.VolumeTraded/scale_offset * newAveragePrice/scale_offset*1.0;

            //calculate the volumn and price (it is average too)
            rtn_trade.Volume = rtn_order.VolumeTraded - orderStatusIterator->VolumeTraded;
            //double price = (newAmount - oldAmount)/((double)rtn_trade.Volume/scale_offset);
            //rtn_trade.Price = (price + 0.000000001)*scale_offset;//(newAmount - oldAmount)/(rtn_trade.Volume);
            strncpy(rtn_trade.OrderSysID,strOrderID.c_str(),31);
            on_rtn_trade(&rtn_trade);
            raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
                                    source_id, MSG_TYPE_LF_RTN_TRADE_KUCOIN, 1, -1);

             KF_LOG_INFO(logger, "[on_rtn_trade] (InstrumentID)" << rtn_trade.InstrumentID << "(Direction)" << rtn_trade.Direction 
                        << "(Volume)" << rtn_trade.Volume << "(Price)" <<  rtn_trade.Price);

        }
        //third, update last status for next query_order
        orderStatusIterator->OrderStatus = rtn_order.OrderStatus;
        orderStatusIterator->VolumeTraded = rtn_order.VolumeTraded;
        //orderStatusIterator->averagePrice = newAveragePrice;
    }
}

std::string TDEngineKuCoin::parseJsonToString(Document &d)
{
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);

    return buffer.GetString();
}


inline int64_t TDEngineKuCoin::getTimestamp()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp;
}

void TDEngineKuCoin::genUniqueKey()
{
    struct tm cur_time = getCurLocalTime();
    //SSMMHHDDN
    char key[11]{0};
    snprintf((char*)key, 11, "%02d%02d%02d%02d%1s", cur_time.tm_sec, cur_time.tm_min, cur_time.tm_hour, cur_time.tm_mday, m_engineIndex.c_str());
    m_uniqueKey = key;
}

//clientid =  m_uniqueKey+orderRef
std::string TDEngineKuCoin::genClinetid(const std::string &orderRef)
{
    static int nIndex = 0;
    return m_uniqueKey + orderRef + std::to_string(nIndex++);
}


#define GBK2UTF8(msg) kungfu::yijinjing::gbk2utf8(string(msg))

BOOST_PYTHON_MODULE(libkucointd)
{
    using namespace boost::python;
    class_<TDEngineKuCoin, boost::shared_ptr<TDEngineKuCoin> >("Engine")
            .def(init<>())
            .def("init", &TDEngineKuCoin::initialize)
            .def("start", &TDEngineKuCoin::start)
            .def("stop", &TDEngineKuCoin::stop)
            .def("logout", &TDEngineKuCoin::logout)
            .def("wait_for_stop", &TDEngineKuCoin::wait_for_stop);
}
