#include "MDEngineKuCoin.h"
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
#include <string>
#include <cpr/cpr.h>
#include <chrono>


using cpr::Get;
using cpr::Url;
using cpr::Parameters;
using cpr::Payload;
using cpr::Post;

using rapidjson::Document;
using rapidjson::SizeType;
using rapidjson::Value;
using rapidjson::Writer;
using rapidjson::StringBuffer;
using std::string;
using std::to_string;
using std::stod;
using std::stoi;


USING_WC_NAMESPACE

static MDEngineKuCoin* global_md = nullptr;

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
            global_md->writeErrorLog(ss.str());
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

MDEngineKuCoin::MDEngineKuCoin(): IMDEngine(SOURCE_KUCOIN)
{
    logger = yijinjing::KfLog::getLogger("MdEngine.KuCoin");
    m_mutexPriceBookData = new std::mutex;
}

MDEngineKuCoin::~MDEngineKuCoin()
{
   if(m_mutexPriceBookData)
   {
       delete m_mutexPriceBookData;
   }
}

void MDEngineKuCoin::writeErrorLog(std::string strError)
{
    KF_LOG_ERROR(logger, strError);
}

void MDEngineKuCoin::load(const json& j_config)
{
     KF_LOG_ERROR(logger, "MDEngineKuCoin::load:");
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();
    KF_LOG_INFO(logger, "MDEngineOceanEx:: rest_get_interval_ms: " << rest_get_interval_ms);
    book_depth_count = j_config["book_depth_count"].get<int>();
    KF_LOG_INFO(logger, "MDEngineKuCoin:: book_depth_count: " << book_depth_count);
    rest_try_count = j_config["rest_try_count"].get<int>();
    KF_LOG_INFO(logger, "MDEngineKuCoin:: rest_try_count: " << rest_try_count);
    readWhiteLists(j_config);

    debug_print(keyIsStrategyCoinpairWhiteList);
    //display usage:
    if(keyIsStrategyCoinpairWhiteList.size() == 0) {
        KF_LOG_ERROR(logger, "MDEngineKuCoin::lws_write_subscribe: subscribeCoinBaseQuote is empty. please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"btcusdt\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"etceth\"");
        KF_LOG_ERROR(logger, "},");
    }
}

void MDEngineKuCoin::readWhiteLists(const json& j_config)
{
	KF_LOG_INFO(logger, "[readWhiteLists]");

	if(j_config.find("whiteLists") != j_config.end()) {
		KF_LOG_INFO(logger, "[readWhiteLists] found whiteLists");
		//has whiteLists
		json whiteLists = j_config["whiteLists"].get<json>();
		if(whiteLists.is_object())
		{
			for (json::iterator it = whiteLists.begin(); it != whiteLists.end(); ++it) {
                    std::string strategy_coinpair = it.key();
				    std::string exchange_coinpair = it.value();
				    KF_LOG_INFO(logger, "[readWhiteLists] (strategy_coinpair) " << strategy_coinpair << " (exchange_coinpair) " << exchange_coinpair);
				    keyIsStrategyCoinpairWhiteList.insert(std::pair<std::string, std::string>(strategy_coinpair, exchange_coinpair));

                    m_vstrSubscribeJsonString.push_back(makeSubscribeL2Update(exchange_coinpair));
                    m_vstrSubscribeJsonString.push_back(makeSubscribeMatch(exchange_coinpair));
			}
		}
	}
}

std::string MDEngineKuCoin::getWhiteListCoinpairFrom(std::string md_coinpair)
{
    std::string& ticker = md_coinpair;
    //std::transform(ticker.begin(), ticker.end(), ticker.begin(), ::toupper);

    KF_LOG_INFO(logger, "[getWhiteListCoinpairFrom] find md_coinpair (md_coinpair) " << md_coinpair << " (toupper(ticker)) " << ticker);
    std::map<std::string, std::string>::iterator map_itr;
    map_itr = keyIsStrategyCoinpairWhiteList.begin();
    while(map_itr != keyIsStrategyCoinpairWhiteList.end()) {
		if(ticker == map_itr->second)
		{
            KF_LOG_INFO(logger, "[getWhiteListCoinpairFrom] found md_coinpair (strategy_coinpair) " << map_itr->first << " (exchange_coinpair) " << map_itr->second);
            return map_itr->first;
		}
        map_itr++;
    }
    KF_LOG_INFO(logger, "[getWhiteListCoinpairFrom] not found md_coinpair (md_coinpair) " << md_coinpair);
    return "";
}

void MDEngineKuCoin::debug_print(std::map<std::string, std::string> &keyIsStrategyCoinpairWhiteList)
{
	std::map<std::string, std::string>::iterator map_itr;
	map_itr = keyIsStrategyCoinpairWhiteList.begin();
	while(map_itr != keyIsStrategyCoinpairWhiteList.end()) {
		KF_LOG_INFO(logger, "[debug_print] keyIsExchangeSideWhiteList (strategy_coinpair) " << map_itr->first << " (md_coinpair) "<< map_itr->second);
		map_itr++;
	}
}

void MDEngineKuCoin::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "MDEngineKuCoin::connect:");
    connected = true;
}

bool MDEngineKuCoin::getToken(Document& d) 
{
    int nTryCount = 0;
    cpr::Response response;
    do{
        std::string url = "https://api.kucoin.com/api/v1/bullet-public";
       response = Post(Url{url.c_str()}, Parameters{}); 
       
    }while(++nTryCount < rest_try_count && response.status_code != 200);

    if(response.status_code != 200)
    {
        KF_LOG_ERROR(logger, "MDEngineKuCoin::login::getToken Error");
        return false;
    }

    KF_LOG_INFO(logger, "MDEngineKuCoin::getToken: " << response.text.c_str());

    d.Parse(response.text.c_str());
    return true;
}


bool MDEngineKuCoin::getServers(Document& d)
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
        KF_LOG_ERROR(logger, "MDEngineKuCoin::login::getServers Error");
        return false;
    }
    return true;
}

std::string MDEngineKuCoin::getId()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return  std::to_string(timestamp);
}

int64_t MDEngineKuCoin::getMSTime()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return  timestamp;
}

void MDEngineKuCoin::login(long timeout_nsec)
{
	KF_LOG_INFO(logger, "MDEngineKuCoin::login:");

    Document d;
    if(!getToken(d))
    {
        return;
    }
    if(!getServers(d))
   {
       return;
   }
    m_nSubscribePos = 0;
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
	KF_LOG_INFO(logger, "MDEngineKuCoin::login: context created.");


	if (context == NULL) {
		KF_LOG_ERROR(logger, "MDEngineKuCoin::login: context is NULL. return");
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

    KF_LOG_INFO(logger, "MDEngineKuCoin::login: address = " << clientConnectInfo.address << ",path = " << clientConnectInfo.path);

	wsi = lws_client_connect_via_info(&clientConnectInfo);
	if (wsi == NULL) {
		KF_LOG_ERROR(logger, "MDEngineKuCoin::login: wsi create error.");
		return;
	}
	KF_LOG_INFO(logger, "MDEngineKuCoin::login: wsi create success.");
	logged_in = true;
}

void MDEngineKuCoin::logout()
{
   KF_LOG_INFO(logger, "MDEngineKuCoin::logout:");
}

void MDEngineKuCoin::release_api()
{
   KF_LOG_INFO(logger, "MDEngineKuCoin::release_api:");
}

void MDEngineKuCoin::set_reader_thread()
{
	IMDEngine::set_reader_thread();

	rest_thread = ThreadPtr(new std::thread(boost::bind(&MDEngineKuCoin::loop, this)));
}

void MDEngineKuCoin::subscribeMarketData(const vector<string>& instruments, const vector<string>& markets)
{
   KF_LOG_INFO(logger, "MDEngineKuCoin::subscribeMarketData:");
}

std::string MDEngineKuCoin::makeSubscribeL2Update(std::string& strSymbol)
{
    StringBuffer sbL2Update;
	Writer<StringBuffer> writer(sbL2Update);
	writer.StartObject();
	writer.Key("id");
	writer.String(getId().c_str());
	writer.Key("type");
	writer.String("subscribe");
	writer.Key("topic");
    std::string strTopic = "/market/level2:";
    strTopic += strSymbol;
	writer.String(strTopic.c_str());
	writer.Key("response");
	writer.String("true");
	writer.EndObject();
    std::string strL2Update = sbL2Update.GetString();

    return strL2Update;
}

std::string MDEngineKuCoin::makeSubscribeMatch(std::string& strSymbol)
{
     StringBuffer sbMacth;
	Writer<StringBuffer> writer1(sbMacth);
	writer1.StartObject();
	writer1.Key("id");
	writer1.String(getId().c_str());
	writer1.Key("type");
	writer1.String("subscribe");
	writer1.Key("topic");
    std::string strTopic1 = "/market/match:";
    strTopic1 += strSymbol;
	writer1.String(strTopic1.c_str());
    writer1.Key("privateChannel");
	writer1.String("false");
	writer1.Key("response");
	writer1.String("true");
	writer1.EndObject();
    std::string strLMatch = sbMacth.GetString();

    return strLMatch;
}

int MDEngineKuCoin::lws_write_subscribe(struct lws* conn)
{
	//KF_LOG_INFO(logger, "MDEngineKuCoin::lws_write_subscribe: (subscribe_index)" << subscribe_index);

    if(keyIsStrategyCoinpairWhiteList.size() == 0) return 0;
    int ret = 0;
    if(m_nSubscribePos < m_vstrSubscribeJsonString.size())
    {
        std::string& strSubscribe = m_vstrSubscribeJsonString[m_nSubscribePos];
        unsigned char msg[512];
        memset(&msg[LWS_PRE], 0, 512-LWS_PRE);
        int length = strSubscribe.length();
        KF_LOG_INFO(logger, "MDEngineKuCoin::lws_write_subscribe: " << strSubscribe.c_str() << " ,len = " << length);
        strncpy((char *)msg+LWS_PRE, strSubscribe.c_str(), length);
        int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);
        m_nSubscribePos++;
        lws_callback_on_writable(conn);
    }
    else
    {
        if(shouldPing)
        {
            isPong = false;
            Ping(conn);
        }
    }
    
    return ret;
}

std::string MDEngineKuCoin::dealDataSprit(const char* src)
{
     std::string strData = src;
     auto nPos = strData.find("\\");
     while(nPos != std::string::npos)
     {
        strData.replace(nPos,1,"");
        nPos = strData.find("\\");
     }

     return strData;
}

 void MDEngineKuCoin::onPong(struct lws* conn)
 {
     Ping(conn);
 }

 void MDEngineKuCoin::Ping(struct lws* conn)
 {
     shouldPing = false;
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
    KF_LOG_INFO(logger, "MDEngineKuCoin::lws_write_ping: " << strPing.c_str() << " ,len = " << length);
    strncpy((char *)msg+LWS_PRE, strPing.c_str(), length);
    int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);
 }

void MDEngineKuCoin::on_lws_data(struct lws* conn, const char* data, size_t len)
{
    //std::string strData = dealDataSprit(data);
	KF_LOG_INFO(logger, "MDEngineKuCoin::on_lws_data: " << data);
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
           isPong = true;
           m_conn = conn;
		}
		if(strcmp(json["type"].GetString(), "message") == 0)
		{
            if(strcmp(json["subject"].GetString(), "trade.l2update") == 0)
		    {
			    KF_LOG_INFO(logger, "MDEngineKuCoin::on_lws_data: is trade.l2update");
                onDepth(json);
            }
            if(strcmp(json["subject"].GetString(), "trade.l3match") == 0)
            {
                KF_LOG_INFO(logger, "MDEngineKuCoin::on_lws_data: is trade.l3match");
                onFills(json);
            }      
		}	
	} else 
    {
		KF_LOG_ERROR(logger, "MDEngineKuCoin::on_lws_data . parse json error: " << data);
	}
}


void MDEngineKuCoin::on_lws_connection_error(struct lws* conn)
{
	KF_LOG_ERROR(logger, "MDEngineKuCoin::on_lws_connection_error.");
	//market logged_in false;
    logged_in = false;
    KF_LOG_ERROR(logger, "MDEngineKuCoin::on_lws_connection_error. login again.");
    //clear the price book, the new websocket will give 200 depth on the first connect, it will make a new price book
	clearPriceBook();
    isPong = false;
    shouldPing = true;
	//no use it
    long timeout_nsec = 0;
    //reset sub
    subscribe_index = 0;

    login(timeout_nsec);
}

void MDEngineKuCoin::clearPriceBook()
{
     std::lock_guard<std::mutex> lck(*m_mutexPriceBookData);
    m_mapPriceBookData.clear();
    mapLastData.clear();
}

void MDEngineKuCoin::onFills(Document& json)
{
    if(!json.HasMember("data"))
    {
        KF_LOG_ERROR(logger, "MDEngineKuCoin::[onFills] invalid market trade message");
        return;
    }
    std::string ticker;
    auto& jsonData = json["data"];
   if(jsonData.HasMember("symbol"))
    {
        ticker = jsonData["symbol"].GetString();
    }
    if(ticker.length() == 0) {
		KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth: invaild data");
		return;
    }
    
    std::string strInstrumentID = getWhiteListCoinpairFrom(ticker);
    if(strInstrumentID == "")
    {
        KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth: invaild data " << ticker.c_str());
		return;
    }

    LFL2TradeField trade;
    memset(&trade, 0, sizeof(trade));
    strcpy(trade.InstrumentID, strInstrumentID.c_str());
    strcpy(trade.ExchangeID, "kucoin");

    trade.Price = std::round(std::stod(jsonData["price"].GetString()) * scale_offset);
    trade.Volume = std::round(std::stod(jsonData["size"].GetString()) * scale_offset);
    static const string strBuy = "buy" ;
    trade.OrderBSFlag[0] = (strBuy == jsonData["side"].GetString()) ? 'B' : 'S';

    KF_LOG_INFO(logger, "MDEngineKuCoin::[onFills] (ticker)" << ticker <<
                                                                " (Price)" << trade.Price <<
                                                                " (Volume)" << trade.Volume << 
                                                                "(OrderBSFlag)" << trade.OrderBSFlag);
    on_trade(&trade);
}

bool MDEngineKuCoin::shouldUpdateData(const LFPriceBook20Field& md)
{
    bool has_update = false;
    auto it = mapLastData.find (md.InstrumentID);
    if(it == mapLastData.end())
    {
        mapLastData[md.InstrumentID] = md;
         has_update = true;
    }
     else
     {
        LFPriceBook20Field& lastMD = it->second;
        if(md.BidLevelCount != lastMD.BidLevelCount)
        {
            has_update = true;
        }
        else
        {
            for(int i = 0;i < md.BidLevelCount; ++i)
            {
                if(md.BidLevels[i].price != lastMD.BidLevels[i].price || md.BidLevels[i].volume != lastMD.BidLevels[i].volume)
                {
                    has_update = true;
                    break;
                }
            }
        }
        if(!has_update && md.AskLevelCount != lastMD.AskLevelCount)
        {
            has_update = true;
        }
        else if(!has_update)
        {
            for(int i = 0;i < md.AskLevelCount ;++i)
            {
                if(md.AskLevels[i].price != lastMD.AskLevels[i].price || md.AskLevels[i].volume != lastMD.AskLevels[i].volume)
                {
                    has_update = true;
                    break;
                }
            }
        }
        if(has_update)
        {
             mapLastData[md.InstrumentID] = md;
        }
    }	

    return has_update;
}

bool MDEngineKuCoin::getInitPriceBook(const std::string& strSymbol,std::map<std::string,PriceBookData>::iterator& itPriceBookData)
{
    int nTryCount = 0;
    cpr::Response response;
    std::string url = "https://api.kucoin.com/api/v1/market/orderbook/level2_20?symbol=";
    url += strSymbol;

    do{  
       response = Get(Url{url.c_str()}, Parameters{}); 
       
    }while(++nTryCount < rest_try_count && response.status_code != 200);

    if(response.status_code != 200)
    {
        KF_LOG_ERROR(logger, "MDEngineKuCoin::login::getInitPriceBook Error, response = " <<response.text.c_str());
        return false;
    }
    KF_LOG_INFO(logger, "MDEngineKuCoin::getInitPriceBook: " << response.text.c_str());

    Document d;
    d.Parse(response.text.c_str());
    itPriceBookData = m_mapPriceBookData.insert(std::make_pair(strSymbol,PriceBookData())).first;
    if(!d.HasMember("data"))
    {
        return  true;
    }
    auto& jsonData = d["data"];
    if(jsonData.HasMember("sequence"))
    {
        itPriceBookData->second.nSequence = std::round(stod(jsonData["sequence"].GetString()));
    }
    if(jsonData.HasMember("bids"))
    {
        auto& bids =jsonData["bids"];
         if(bids .IsArray()) 
         {
                int len = bids.Size();
                for(int i = 0 ; i < len; i++)
                {
                    int64_t price = std::round(stod(bids.GetArray()[i][0].GetString()) * scale_offset);
                    uint64_t volume = std::round(stod(bids.GetArray()[i][1].GetString()) * scale_offset);
                   itPriceBookData->second.mapBidPrice[price] = volume;
                }
         }
    }
    if(jsonData.HasMember("asks"))
    {
        auto& asks =jsonData["asks"];
         if(asks .IsArray()) 
         {
                int len = asks.Size();
                for(int i = 0 ; i < len; i++)
                {
                    int64_t price = std::round(stod(asks.GetArray()[i][0].GetString()) * scale_offset);
                    uint64_t volume = std::round(stod(asks.GetArray()[i][1].GetString()) * scale_offset);
                   itPriceBookData->second.mapAskPrice[price] = volume;
                }
         }
    }

     printPriceBook(itPriceBookData->second);

    return true;
}

void MDEngineKuCoin::printPriceBook(const PriceBookData& stPriceBookData)
{
    std::stringstream ss;
    ss << "Bids[";
    for(auto it = stPriceBookData.mapBidPrice.rbegin(); it != stPriceBookData.mapBidPrice.rend();++it)
    {
        ss <<  "[" << it->first << "," << it->second << "],";
    }
    ss << "],Ask[";
     for(auto& pair : stPriceBookData.mapAskPrice)
    {
        ss <<  "[" << pair.first << "," << pair.second << "],";
    }
    ss << "].";

    KF_LOG_INFO(logger, "MDEngineKuCoin::printPriceBook: " << ss.str());
}

void MDEngineKuCoin::clearVaildData(PriceBookData& stPriceBookData)
{
     KF_LOG_INFO(logger, "MDEngineKuCoin::clearVaildData: ");
    for(auto it = stPriceBookData.mapAskPrice.begin();it !=stPriceBookData.mapAskPrice.end();)
    {
        if(it->first == 0 || it->second == 0)
        {
            it = stPriceBookData.mapAskPrice.erase(it);
        }
        else
        {
            ++it;
        }        
    }

     for(auto it = stPriceBookData.mapBidPrice.begin();it !=stPriceBookData.mapBidPrice.end();)
    {
        if(it->first == 0 || it->second == 0)
        {
            it = stPriceBookData.mapBidPrice.erase(it);
        }
        else
        {
            ++it;
        }   
    }
    printPriceBook(stPriceBookData);
}

void MDEngineKuCoin::onDepth(Document& dJson)
{
    bool update = false;

    std::string ticker;
    if(!dJson.HasMember("data"))
    {
        return;
    }
    auto& jsonData = dJson["data"];
    if(jsonData.HasMember("symbol"))
    {
        ticker = jsonData["symbol"].GetString();
    }
    if(ticker.length() == 0) {
		KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth: invaild data");
		return;
    }
    
    KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth:" << "(ticker) " << ticker);

    std::lock_guard<std::mutex> lck(*m_mutexPriceBookData);
    auto itPriceBook = m_mapPriceBookData.find(ticker);
    if(itPriceBook == m_mapPriceBookData.end())
    {
        if(!getInitPriceBook(ticker,itPriceBook))
        {
            return;
        }
    }

    if(jsonData.HasMember("changes"))
    {
        if(jsonData["changes"].HasMember("asks")) 
        {      
            auto& asks = jsonData["changes"]["asks"];
            if(asks .IsArray()) {
                int len = asks.Size();
                for(int i = 0 ; i < len; i++)
                {
                    int64_t nSequence = std::round(stod(asks.GetArray()[i][2].GetString()));
                    if(nSequence <= itPriceBook->second.nSequence)
                    {
                         KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth:  old ask data,nSequence=" << nSequence);
                        continue;
                    }
                    int64_t price = std::round(stod(asks.GetArray()[i][0].GetString()) * scale_offset);
                    uint64_t volume = std::round(stod(asks.GetArray()[i][1].GetString()) * scale_offset);
                   itPriceBook->second.mapAskPrice[price] = volume;
                }
            }
            else {KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth:  asks not Array");}
        }
        else { KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth:  asks not found");}

        if(jsonData["changes"].HasMember("bids")) 
        {      
            auto& bids = jsonData["changes"]["bids"];
            if(bids .IsArray()) 
            {
                int len = bids.Size();
                for(int i = 0 ; i < len; i++)
                {
                    int64_t nSequence = std::round(stod(bids.GetArray()[i][2].GetString()));
                    if(nSequence <= itPriceBook->second.nSequence)
                    {
                        KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth:  old bid data,nSequence=" << nSequence);
                        continue;
                    }
                    int64_t price = std::round(stod(bids.GetArray()[i][0].GetString()) * scale_offset);
                    uint64_t volume = std::round(stod(bids.GetArray()[i][1].GetString()) * scale_offset);
                   itPriceBook->second.mapBidPrice[price] = volume;
                }
            }
            else {KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth:  bids not Array");}
       } else { KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth:  bids not found");}
    }
    else
    {
          KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth:  data not found");
    }
    
     printPriceBook(itPriceBook->second);

    if(jsonData.HasMember("sequenceEnd"))
    {
        itPriceBook->second.nSequence = std::round(jsonData["sequenceEnd"].GetInt64());
         KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth:  sequenceEnd = " << itPriceBook->second.nSequence);
    }
    clearVaildData(itPriceBook->second);

     LFPriceBook20Field md;
    memset(&md, 0, sizeof(md));
    std::string strInstrumentID = getWhiteListCoinpairFrom(ticker);
    strcpy(md.InstrumentID, strInstrumentID.c_str());
    strcpy(md.ExchangeID, "kucoin");

    std::vector<PriceAndVolume> vstAskPriceAndVolume;
    std::vector<PriceAndVolume> vstBidPriceAndVolume;
    sortMapByKey(itPriceBook->second.mapAskPrice,vstAskPriceAndVolume,sort_price_asc);
    sortMapByKey(itPriceBook->second.mapBidPrice,vstBidPriceAndVolume,sort_price_desc);
    
    size_t nAskLen = 0;
    for(size_t nPos=0;nPos < vstAskPriceAndVolume.size();++nPos)
    {
        if(nAskLen >= book_depth_count)
        {
            break;
        }
        md.AskLevels[nPos].price = vstAskPriceAndVolume[nPos].price;
        md.AskLevels[nPos].volume = vstAskPriceAndVolume[nPos].volume;
        ++nAskLen ;
    }
    md.AskLevelCount = nAskLen;  

    size_t nBidLen = 0;
    for(size_t nPos=0;nPos < vstBidPriceAndVolume.size();++nPos)
    {
        if(nBidLen >= book_depth_count)
        {
            break;
        }
        md.BidLevels[nPos].price = vstBidPriceAndVolume[nPos].price;
        md.BidLevels[nPos].volume = vstBidPriceAndVolume[nPos].volume;
        ++nBidLen ;
    }
    md.BidLevelCount = nBidLen;  

    if(shouldUpdateData(md))
    {
        KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth: on_price_book_update," << strInstrumentID << ",kucoin");
        on_price_book_update(&md);
    }else { KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth: same data not update" );}
}

std::string MDEngineKuCoin::parseJsonToString(const char* in)
{
	Document d;
	d.Parse(reinterpret_cast<const char*>(in));

	StringBuffer buffer;
	Writer<StringBuffer> writer(buffer);
	d.Accept(writer);

	return buffer.GetString();
}

void MDEngineKuCoin::loop()
{
        time_t nLastTime = time(0);

		while(isRunning)
		{
             time_t nNowTime = time(0);
            if(isPong && (nNowTime - nLastTime>= 30))
            {
                isPong = false;
                 nLastTime = nNowTime;
                 KF_LOG_INFO(logger, "MDEngineKuCoin::loop: last time = " <<  nLastTime << ",now time = " << nNowTime << ",isPong = " << isPong);
                shouldPing = true;
                lws_callback_on_writable(m_conn);  
            }
            //KF_LOG_INFO(logger, "MDEngineKuCoin::loop:lws_service");
			lws_service( context, rest_get_interval_ms );
		}
}

BOOST_PYTHON_MODULE(libkucoinmd)
{
    using namespace boost::python;
    class_<MDEngineKuCoin, boost::shared_ptr<MDEngineKuCoin> >("Engine")
    .def(init<>())
    .def("init", &MDEngineKuCoin::initialize)
    .def("start", &MDEngineKuCoin::start)
    .def("stop", &MDEngineKuCoin::stop)
    .def("logout", &MDEngineKuCoin::logout)
    .def("wait_for_stop", &MDEngineKuCoin::wait_for_stop);
}
