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
			lws_callback_on_writable( wsi );
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
			/*
            int ret = 0;
			if(global_md)
			{
				ret = global_md->lws_write_subscribe(wsi);
			}
            */
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

int MDEngineKuCoin::lws_write_subscribe(struct lws* conn,Document& json)
{
	//KF_LOG_INFO(logger, "MDEngineKuCoin::lws_write_subscribe: (subscribe_index)" << subscribe_index);

    if(keyIsStrategyCoinpairWhiteList.size() == 0) return 0;
    
    std::string strSymbol;
    for(auto& pair:keyIsStrategyCoinpairWhiteList)
    {
        strSymbol += pair.second;
        strSymbol += ",";
    }
    strSymbol.pop_back();

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
	writer.Bool(true);
	writer.EndObject();
    std::string strL2Update = sbL2Update.GetString();
    unsigned char msg[2046];
    memset(&msg[LWS_PRE], 0, 2046-LWS_PRE);
    KF_LOG_INFO(logger, "MDEngineKuCoin::lws_write_subscribe: " << strL2Update.c_str());
    int length = strL2Update.length();
    strncpy((char *)msg+LWS_PRE, strL2Update.c_str(), length);
    int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);

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
	writer1.Bool(false);
	writer1.Key("response");
	writer1.Bool(true);
	writer1.EndObject();
    std::string strLMatch = sbMacth.GetString();
    memset(&msg[LWS_PRE], 0, 2046-LWS_PRE);
    KF_LOG_INFO(logger, "MDEngineKuCoin::lws_write_subscribe: " << strLMatch.c_str());
    length = strL2Update.length();
    strncpy((char *)msg+LWS_PRE, strLMatch.c_str(), length);
    ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);

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

 void MDEngineKuCoin::onPong(struct lws* conn,Document& d)
 {
     Ping(conn,d);
 }

 void MDEngineKuCoin::Ping(struct lws* conn,Document& d)
 {
    StringBuffer sbPing;
	Writer<StringBuffer> writer(sbPing);
	writer.StartObject();
	writer.Key("id");
	writer.String(getId().c_str());
	writer.Key("type");
	writer.String("ping");
	writer.EndObject();
    std::string strPing = sbPing.GetString();
    unsigned char msg[2046];
    memset(&msg[LWS_PRE], 0, 2046-LWS_PRE);
    KF_LOG_INFO(logger, "MDEngineKuCoin::lws_write_ping: ");
    int length = strPing.length();
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
            if(global_md)
            {
                Ping(conn,json);
                global_md->lws_write_subscribe(conn,json);
            }
        }
        if(strcmp(json["type"].GetString(), "pong") == 0)
		{
			KF_LOG_INFO(logger, "MDEngineKuCoin::on_lws_data: pong");
            onPong(conn,json);
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
	//no use it
    long timeout_nsec = 0;
    //reset sub
    subscribe_index = 0;

    login(timeout_nsec);
}

void MDEngineKuCoin::clearPriceBook()
{
    //clear price and volumes of tickers
    std::map<std::string, std::map<int64_t, uint64_t>*> ::iterator map_itr;

    map_itr = tickerAskPriceMap.begin();
    while(map_itr != tickerAskPriceMap.end()){
        map_itr->second->clear();
        map_itr++;
    }

    map_itr = tickerBidPriceMap.begin();
    while(map_itr != tickerBidPriceMap.end()){
        map_itr->second->clear();
        map_itr++;
    }
}

void MDEngineKuCoin::onFills(Document& json)
{
    if(!json.HasMember("data"))
    {
        KF_LOG_ERROR(logger, "MDEngineKuCoin::[onFills] invalid market trade message");
        return;
    }
     std::string ticker;
   if(json.HasMember("channel"))
    {
        ticker = json["channel"].GetString();
    }
    if(ticker.length() == 0) {
		KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth: invaild data");
		return;
    }
    
     auto strData =  json["data"].GetString();
     Document jsonData;
	jsonData.Parse(strData);
    if(jsonData.HasMember("trades"))
    {
        int len = jsonData["trades"].Size();
        auto& arrayTrades = jsonData["trades"];
        std::string strInstrumentID = ticker.substr(ticker.find_first_of('-')+1);
        strInstrumentID = strInstrumentID.substr(0,strInstrumentID.find_first_of('-'));
        strInstrumentID = getWhiteListCoinpairFrom(strInstrumentID);
        for(int i = 0 ; i < len; i++) {
            LFL2TradeField trade;
            memset(&trade, 0, sizeof(trade));
            strcpy(trade.InstrumentID, strInstrumentID.c_str());
            strcpy(trade.ExchangeID, "kucoin");

            trade.Price = std::round(std::stod(arrayTrades.GetArray()[i]["price"].GetString()) * scale_offset);
            trade.Volume = std::round(std::stod(arrayTrades.GetArray()[i]["amount"].GetString()) * scale_offset);
            static const string strBuy = "buy" ;
            trade.OrderBSFlag[0] = (strBuy == arrayTrades[i]["type"].GetString()) ? 'B' : 'S';

            KF_LOG_INFO(logger, "MDEngineKuCoin::[onFills] (ticker)" << ticker <<
                                                                        " (Price)" << trade.Price <<
                                                                        " (Volume)" << trade.Volume << 
                                                                        "(OrderBSFlag)" << trade.OrderBSFlag);
            on_trade(&trade);
        }
    }
    else {   KF_LOG_INFO(logger, "iMDEngineKuCoin::[onFills] : nvaild data"); }
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

void MDEngineKuCoin::onDepth(Document& json)
{
    bool asks_update = false;
    bool bids_update = false;

    std::string ticker;
    if(json.HasMember("channel"))
    {
        ticker = json["channel"].GetString();
    }
    if(ticker.length() == 0) {
		KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth: invaild data");
		return;
    }
 
    KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth:" << "(ticker) " << ticker);
    LFPriceBook20Field md;
    memset(&md, 0, sizeof(md));
    if(json.HasMember("data"))
    {
        auto strData =  json["data"].GetString();
        Document jsonData;
        //KF_LOG_INFO(logger, "strData:" << strData);
	    jsonData.Parse(strData);
        if(jsonData.IsObject() && jsonData.HasMember("asks")) 
        {      
            auto& asks = jsonData["asks"];
            if(asks .IsArray()) {
                int len = asks.Size();
                len = len <= book_depth_count ? len : book_depth_count;
                for(int i = 0 ; i < len; i++)
                {
                    int64_t price = std::round(stod(asks.GetArray()[i][0].GetString()) * scale_offset);
                    uint64_t volume = std::round(stod(asks.GetArray()[i][1].GetString()) * scale_offset);
                    md.AskLevels[i].price = price;
                    md.AskLevels[i].volume = volume;
                }
                 md.AskLevelCount = len;  
            }
        }
        else { KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth:  asks not found");}
        if(jsonData.IsObject() && jsonData.HasMember("bids"))
       {
            auto& bids = jsonData["bids"];
            if(bids.IsArray()) {
                int len = bids.Size();
                len = len <= book_depth_count ? len : book_depth_count;
                for(int i = 0 ; i < len; i++)
                {
                    int64_t price = std::round(stod(bids.GetArray()[i][0].GetString()) * scale_offset);
                    uint64_t volume = std::round(stod(bids.GetArray()[i][1].GetString()) * scale_offset);
                    md.BidLevels[i].price = price;
                    md.BidLevels[i].volume = volume;
                }
                md.BidLevelCount = len;
            }
       } else { KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth:  asks not found");}
    }
    else
    {
          KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth:  data not found");
    }
    
    std::string strInstrumentID = ticker.substr(ticker.find_first_of('-')+1);
    strInstrumentID = strInstrumentID.substr(0,strInstrumentID.find_first_of('-'));
    strInstrumentID = getWhiteListCoinpairFrom(strInstrumentID);
    strcpy(md.InstrumentID, strInstrumentID.c_str());
    strcpy(md.ExchangeID, "kucoin");

    if(shouldUpdateData(md))
    {
        KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth: on_price_book_update," << strInstrumentID << ",kucoin");
        on_price_book_update(&md);
    }else { KF_LOG_INFO(logger, "MDEngineKuCoin::onDepth: same data not update:" << json["data"].GetString());}
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
		while(isRunning)
		{
            KF_LOG_INFO(logger, "MDEngineKuCoin::loop:lws_service");
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
