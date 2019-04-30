#include "MDEngineHitBTC.h"
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

static MDEngineHitBTC* global_md = nullptr;

static int ws_service_cb( struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len )
{

	switch( reason )
	{
		case LWS_CALLBACK_CLIENT_ESTABLISHED:
		{
			std::cout << "callback client established, reason = " << reason << std::endl;
			lws_callback_on_writable( wsi );
			break;
		}
		case LWS_CALLBACK_PROTOCOL_INIT:{
			std::cout << "init, reason = " << reason << std::endl;
			break;
		}
		case LWS_CALLBACK_CLIENT_RECEIVE:
		{
			std::cout << "on data, reason = " << reason << std::endl;
			if(global_md)
			{
				global_md->on_lws_data(wsi, (const char*)in, len);
			}
			break;
		}
		case LWS_CALLBACK_CLIENT_WRITEABLE:
		{
			std::cout << "writeable, reason = " << reason << std::endl;
			int ret = 0;
			if(global_md)
			{
				ret = global_md->lws_write_subscribe(wsi);
			}
			std::cout << "send depth result: " << ret << std::endl;
			break;
		}
		case LWS_CALLBACK_CLOSED:
		case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
		{
			if(global_md)
			{
				global_md->on_lws_connection_error(wsi);
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

MDEngineHitBTC::MDEngineHitBTC(): IMDEngine(SOURCE_HITBTC)
{
    logger = yijinjing::KfLog::getLogger("MdEngine.HitBTC");
}

void MDEngineHitBTC::load(const json& j_config)
{
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();
    KF_LOG_INFO(logger, "MDEngineHitBTC:: rest_get_interval_ms: " << rest_get_interval_ms);

    readWhiteLists(j_config);

    debug_print(subscribeCoinBaseQuote);
    debug_print(keyIsStrategyCoinpairWhiteList);
    debug_print(websocketSubscribeJsonString);
    //display usage:
    if(keyIsStrategyCoinpairWhiteList.size() == 0) {
        KF_LOG_ERROR(logger, "MDEngineHitBTC::lws_write_subscribe: subscribeCoinBaseQuote is empty. please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"btcusdt\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"etceth\"");
        KF_LOG_ERROR(logger, "},");
    }
}

void MDEngineHitBTC::readWhiteLists(const json& j_config)
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
				//make subscribeCoinBaseQuote

				//coinmex MD must has base and quote, please see this->createDepthJsonString.
                SubscribeCoinBaseQuote baseQuote;
				split(it.key(), "_", baseQuote);
                KF_LOG_INFO(logger, "[readWhiteLists] SubscribeCoinBaseQuote (base) " << baseQuote.base << " (quote) " << baseQuote.quote);

				if(baseQuote.base.length() > 0)
				{
					//get correct base_quote config
                    subscribeCoinBaseQuote.push_back(baseQuote);
                    //get ready websocket subscrube json strings
                    std::string jsonDepthString = createDepthJsonString(baseQuote.base, baseQuote.quote);
                    websocketSubscribeJsonString.push_back(jsonDepthString);
                    std::string jsonFillsString = createFillsJsonString(baseQuote.base, baseQuote.quote);
                    websocketSubscribeJsonString.push_back(jsonFillsString);

				}
			}
		}
	}
}

//example: btc_usdt
void MDEngineHitBTC::split(std::string str, std::string token, SubscribeCoinBaseQuote& sub)
{
	if (str.size() > 0) {
		size_t index = str.find(token);
		if (index != std::string::npos) {
			sub.base = str.substr(0, index);
			sub.quote = str.substr(index + token.size());
		}
		else {
			//not found, do nothing
		}
	}
}

std::string MDEngineHitBTC::getWhiteListCoinpairFrom(std::string md_coinpair)
{
    std::string ticker = md_coinpair;
    std::transform(ticker.begin(), ticker.end(), ticker.begin(), ::toupper);

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

void MDEngineHitBTC::debug_print(std::vector<SubscribeCoinBaseQuote> &sub)
{
    int count = sub.size();
    KF_LOG_INFO(logger, "[debug_print] SubscribeCoinBaseQuote (count) " << count);

	for (int i = 0; i < count;i++)
	{
		KF_LOG_INFO(logger, "[debug_print] SubscribeCoinBaseQuote (base) " << sub[i].base <<  " (quote) " << sub[i].quote);
	}
}

void MDEngineHitBTC::debug_print(std::map<std::string, std::string> &keyIsStrategyCoinpairWhiteList)
{
	std::map<std::string, std::string>::iterator map_itr;
	map_itr = keyIsStrategyCoinpairWhiteList.begin();
	while(map_itr != keyIsStrategyCoinpairWhiteList.end()) {
		KF_LOG_INFO(logger, "[debug_print] keyIsExchangeSideWhiteList (strategy_coinpair) " << map_itr->first << " (md_coinpair) "<< map_itr->second);
		map_itr++;
	}
}


void MDEngineHitBTC::debug_print(std::vector<std::string> &subJsonString)
{
    int count = subJsonString.size();
    KF_LOG_INFO(logger, "[debug_print] websocketSubscribeJsonString (count) " << count);

    for (int i = 0; i < count;i++)
    {
        KF_LOG_INFO(logger, "[debug_print] websocketSubscribeJsonString (subJsonString) " << subJsonString[i]);
    }
}

void MDEngineHitBTC::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "MDEngineHitBTC::connect:");
    connected = true;
}

void MDEngineHitBTC::login(long timeout_nsec)
{
	KF_LOG_INFO(logger, "MDEngineHitBTC::login:");
	global_md = this;

	char inputURL[300] = "wss://api.hitbtc.com/api/2/ws";
	int inputPort = 8443;
	const char *urlProtocol, *urlTempPath;
	char urlPath[300];
	int logs = LLL_ERR | LLL_DEBUG | LLL_WARN;


	struct lws_context_creation_info ctxCreationInfo;
	struct lws_client_connect_info clientConnectInfo;
	struct lws *wsi = NULL;
	struct lws_protocols protocol;

	memset(&ctxCreationInfo, 0, sizeof(ctxCreationInfo));
	memset(&clientConnectInfo, 0, sizeof(clientConnectInfo));

	clientConnectInfo.port = 8443;

	if (lws_parse_uri(inputURL, &urlProtocol, &clientConnectInfo.address, &clientConnectInfo.port, &urlTempPath))
	{
		KF_LOG_ERROR(logger, "MDEngineHitBTC::connect: Couldn't parse URL. Please check the URL and retry: " << inputURL);
		return;
	}

	// Fix up the urlPath by adding a / at the beginning, copy the temp path, and add a \0     at the end
	urlPath[0] = '/';
	strncpy(urlPath + 1, urlTempPath, sizeof(urlPath) - 2);
	urlPath[sizeof(urlPath) - 1] = '\0';
	clientConnectInfo.path = urlPath; // Set the info's path to the fixed up url path

	KF_LOG_INFO(logger, "MDEngineHitBTC::login:" << "urlProtocol=" << urlProtocol <<
												  "address=" << clientConnectInfo.address <<
												  "urlTempPath=" << urlTempPath <<
												  "urlPath=" << urlPath);

	ctxCreationInfo.port = CONTEXT_PORT_NO_LISTEN;
	ctxCreationInfo.iface = NULL;
	ctxCreationInfo.protocols = &protocol;
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
	KF_LOG_INFO(logger, "MDEngineHitBTC::login: context created.");


	if (context == NULL) {
		KF_LOG_ERROR(logger, "MDEngineHitBTC::login: context is NULL. return");
		return;
	}

	// Set up the client creation info
	clientConnectInfo.context = context;
	clientConnectInfo.port = 8443;
	clientConnectInfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
	clientConnectInfo.host = clientConnectInfo.address;
	clientConnectInfo.origin = clientConnectInfo.address;
	clientConnectInfo.ietf_version_or_minus_one = -1;
	clientConnectInfo.protocol = protocols[PROTOCOL_TEST].name;
	clientConnectInfo.pwsi = &wsi;

	KF_LOG_INFO(logger, "MDEngineHitBTC::login:" << "Connecting to " << urlProtocol << ":" <<
												  clientConnectInfo.host << ":" <<
												  clientConnectInfo.port << ":" << urlPath);

	wsi = lws_client_connect_via_info(&clientConnectInfo);
	if (wsi == NULL) {
		KF_LOG_ERROR(logger, "MDEngineHitBTC::login: wsi create error.");
		return;
	}
	KF_LOG_INFO(logger, "MDEngineHitBTC::login: wsi create success.");
	logged_in = true;
}

void MDEngineHitBTC::set_reader_thread()
{
	IMDEngine::set_reader_thread();

	rest_thread = ThreadPtr(new std::thread(boost::bind(&MDEngineHitBTC::loop, this)));
}

void MDEngineHitBTC::logout()
{
   KF_LOG_INFO(logger, "MDEngineHitBTC::logout:");
}

void MDEngineHitBTC::release_api()
{
   KF_LOG_INFO(logger, "MDEngineHitBTC::release_api:");
}

void MDEngineHitBTC::subscribeMarketData(const vector<string>& instruments, const vector<string>& markets)
{
   KF_LOG_INFO(logger, "MDEngineHitBTC::subscribeMarketData:");
}

int MDEngineHitBTC::lws_write_subscribe(struct lws* conn)
{
	KF_LOG_INFO(logger, "MDEngineHitBTC::lws_write_subscribe: (subscribe_index)" << subscribe_index);

    if(websocketSubscribeJsonString.size() == 0) return 0;
    //sub depth
    if(subscribe_index >= websocketSubscribeJsonString.size())
    {
        subscribe_index = 0;
    }

    unsigned char msg[512];
    memset(&msg[LWS_PRE], 0, 512-LWS_PRE);

    std::string jsonString = websocketSubscribeJsonString[subscribe_index++];

    KF_LOG_INFO(logger, "MDEngineHitBTC::lws_write_subscribe: " << jsonString.c_str());
    int length = jsonString.length();

    strncpy((char *)msg+LWS_PRE, jsonString.c_str(), length);
    int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);

    if(subscribe_index < websocketSubscribeJsonString.size())
    {
        lws_callback_on_writable( conn );
    }

    return ret;
}

void MDEngineHitBTC::on_lws_data(struct lws* conn, const char* data, size_t len)
{
	KF_LOG_INFO(logger, "MDEngineHitBTC::on_lws_data: " << data);
    Document json;
	json.Parse(data);

	if(!json.HasParseError() && json.IsObject() && json.HasMember("method"))
	{

		if(strcmp(json["method"].GetString(), "updateOrderbook") == 0)
		{
			KF_LOG_INFO(logger, "MDEngineHitBTC::on_lws_data: is book update");
            onDepthHit(json);
		}

		else if(strcmp(json["method"].GetString(), "updateTrades") == 0)
		{
			KF_LOG_INFO(logger, "MDEngineHitBTC::on_lws_data: is trade");
            onFillsHit(json);
		}
        else if(strcmp(json["method"].GetString(), "ticker") == 0)
        {
            KF_LOG_INFO(logger, "MDEngineHitBTC::on_lws_data: is tickers");
            onTickers(json);
        }
	} else {
		KF_LOG_ERROR(logger, "MDEngineHitBTC::on_lws_data . parse json error: " << data);
	}
}


void MDEngineHitBTC::on_lws_connection_error(struct lws* conn)
{
	KF_LOG_ERROR(logger, "MDEngineHitBTC::on_lws_connection_error.");
	//market logged_in false;
    logged_in = false;
    KF_LOG_ERROR(logger, "MDEngineHitBTC::on_lws_connection_error. login again.");
    //clear the price book, the new websocket will give 200 depth on the first connect, it will make a new price book
	clearPriceBook();
	//no use it
    long timeout_nsec = 0;
    //reset sub
    subscribe_index = 0;

    login(timeout_nsec);
}

void MDEngineHitBTC::clearPriceBook()
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

void MDEngineHitBTC::onTickers(Document& d)
{
/*
{
    "type": "tickers",
    "biz":"spot",
    "data": [
      [
     "1520318917765", #创建时间
     "0.02",#当日最高成交价
     "0.01",#当日最低成交价
     "0.01",#成交单价
     "200",#基准货币成交量
     "300",#报价货币成交量
     "10",#变化量
     "30",#涨幅百分比
     "btc_usdt",#币对
      9,   #币对ID
     ]
    ],
    "zip":false
}
 * */


}


//{"jsonrpc":"2.0","method":"updateTrades","params":{"data":
// [{"id":400312606,"price":"0.031321","quantity":"0.004","side":"buy","timestamp":"2018-11-18T01:36:32.174Z"},
// {"id":400312607,"price":"0.031324","quantity":"0.014","side":"buy","timestamp":"2018-11-18T01:36:32.174Z"}],"symbol":"ETHBTC"}}
void MDEngineHitBTC::onFillsHit(Document& json)
{
    if(!json.HasMember("data") || !json["data"].IsArray())
    {
        KF_LOG_ERROR(logger, "MDEngineHitBTC::[onFills] invalid market trade message");
        return;
    }

    std::string symbol="";
    if(json.HasMember("symbol") && json["symbol"].IsString()) {
        symbol = json["symbol"].GetString();
    }

    KF_LOG_INFO(logger, "MDEngineHitBTC::onFills:" << "Symbol: "<< symbol);

    std::string ticker = getWhiteListCoinpairFrom(symbol);
    if(ticker.length() == 0) {
        return;
    }

    int len = json["data"].Size();

    for(int i = 0 ; i < len; i++) {
        LFL2TradeField trade;
        memset(&trade, 0, sizeof(trade));
        strcpy(trade.InstrumentID, ticker.c_str());
        strcpy(trade.ExchangeID, "HitBTC");

        auto &tradeData = json["data"].GetArray()[i];

        std::string priceStr = tradeData["price"].GetString();
        std::string qtyStr = tradeData["quantity"].GetString();

        trade.Price = std::round(std::stod(priceStr) * scale_offset);
        trade.Volume = std::round(std::stod(qtyStr) * scale_offset);
        trade.OrderBSFlag[0] = "buy" == tradeData["side"].GetString() ? 'B' : 'S';

        KF_LOG_INFO(logger, "MDEngineHitBTC::[onFills] (ticker)" << symbol <<
                                                                 " (Price)" << trade.Price <<
                                                                 " (trade.Volume)" << trade.Volume);
        on_trade(&trade);
    }


}

void MDEngineHitBTC::onFills(Document& json)
{
    /*
     {
  "base": "cel",
  "zip": false,
  "data": [[
    "0.02"# 价格
    "200"# 数量
    "buy"#交易方向（buy:买|sell:卖）
    1520318917765 #创建时间
  ]],
  "biz": "spot",
  "quote": "btc",
  "type": "fills"
}

     * */

    if(!json.HasMember("data") || !json["data"].IsArray())
    {
        KF_LOG_ERROR(logger, "MDEngineHitBTC::[onFills] invalid market trade message");
        return;
    }

    std::string base="";
    if(json.HasMember("base") && json["base"].IsString()) {
        base = json["base"].GetString();
    }
    std::string quote="";
    if(json.HasMember("quote") && json["quote"].IsString()) {
        quote = json["quote"].GetString();
    }

    KF_LOG_INFO(logger, "MDEngineHitBTC::onFills:" << "base : " << base << "  quote: " << quote);

    std::string ticker = getWhiteListCoinpairFrom(base + "_" +  quote);
    if(ticker.length() == 0) {
        return;
    }

    int len = json["data"].Size();

    for(int i = 0 ; i < len; i++) {
        LFL2TradeField trade;
        memset(&trade, 0, sizeof(trade));
        strcpy(trade.InstrumentID, ticker.c_str());
        strcpy(trade.ExchangeID, "mock");

        trade.Price = std::round(std::stod(json["data"].GetArray()[i].GetArray()[0].GetString()) * scale_offset);
        trade.Volume = std::round(std::stod(json["data"].GetArray()[i].GetArray()[1].GetString()) * scale_offset);
        trade.OrderBSFlag[0] = "buy" == json["data"].GetArray()[i].GetArray()[2].GetString() ? 'B' : 'S';

        KF_LOG_INFO(logger, "MDEngineHitBTC::[onFills] (ticker)" << ticker <<
                                                                    " (Price)" << trade.Price <<
                                                                    " (trade.Volume)" << trade.Volume);
        on_trade(&trade);
    }
}

//sample HitBTC mesage
/*{"jsonrpc":"2.0","method":"updateOrderbook","params":{"ask":[{"price":"0.033772","size":"2.344"},
 * {"price":"0.033774","size":"0.000"},{"price":"0.033813","size":"10.145"},{"price":"0.033820","size":"0.000"},
 * {"price":"0.034017","size":"10.089"},{"price":"0.034031","size":"0.000"},{"price":"0.034051","size":"15.933"},
 * {"price":"0.034062","size":"16.765"},{"price":"0.034077","size":"23.386"},{"price":"0.034105","size":"0.000"},
 * {"price":"0.034149","size":"0.000"},{"price":"0.034160","size":"0.000"},{"price":"0.034165","size":"26.365"},
 * {"price":"0.034176","size":"20.446"},{"price":"0.034207","size":"0.000"},{"price":"0.034218","size":"28.204"},
 * {"price":"0.034257","size":"0.000"},{"price":"0.034278","size":"29.869"},{"price":"0.034285","size":"0.000"},
 * {"price":"0.034288","size":"22.409"},{"price":"0.034374","size":"0.000"},{"price":"0.034410","size":"29.976"},
 * {"price":"0.034438","size":"0.030"},{"price":"0.034451","size":"0.000"},{"price":"0.034512","size":"29.508"},
 *{"price":"0.034516","size":"30.699"},{"price":"0.034549","size":"0.200"},{"price":"0.034648","size":"34.985"},
 * {"price":"0.034740","size":"0.000"},{"price":"0.034845","size":"32.562"},{"price":"0.034847","size":"0.000"},
 * {"price":"0.034951","size":"41.828"},{"price":"0.034972","size":"0.000"},{"price":"0.035067","size":"0.000"}],
 * "bid":[{"price":"0.033705","size":"0.000"},{"price":"0.033703","size":"0.500"},
 * {"price":"0.033682","size":"0.000"},{"price":"0.033657","size":"8.953"},{"price":"0.033640","size":"12.389"},
 * {"price":"0.033635","size":"0.000"},{"price":"0.033623","size":"16.819"},{"price":"0.033615","size":"0.000"},
 * {"price":"0.030149","size":"0.000"},{"price":"0.029836","size":"121.917"}],"symbol":"ETHBTC","sequence":12701142}}*/
void MDEngineHitBTC::onDepthHit(Document& json)
{
    bool asks_update = false;
    bool bids_update = false;


    std::string ticker = "";
    if (json.HasMember("symbol")) {
        ticker = json["symbol"].GetString();
    }
    KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth:" << "(ticker) " << ticker);
    std::map<int64_t, uint64_t> *asksPriceAndVolume;
    std::map<int64_t, uint64_t> *bidsPriceAndVolume;

    auto iter = tickerAskPriceMap.find(ticker);
    if (iter != tickerAskPriceMap.end()) {
        asksPriceAndVolume = iter->second;
//        KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth:" << "ticker : " << ticker << "  get from map (asksPriceAndVolume.size) " << asksPriceAndVolume->size());
    } else {
        asksPriceAndVolume = new std::map<int64_t, uint64_t>();
        tickerAskPriceMap.insert(std::pair<std::string, std::map<int64_t, uint64_t> *>(ticker, asksPriceAndVolume));
//        KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth:" << "ticker : " << ticker << "  insert into map (asksPriceAndVolume.size) " << asksPriceAndVolume->size());
    }

    iter = tickerBidPriceMap.find(ticker);
    if (iter != tickerBidPriceMap.end()) {
        bidsPriceAndVolume = iter->second;
//        KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth:" << "ticker : " << ticker << "  get from map (bidsPriceAndVolume.size) " << bidsPriceAndVolume->size());
    } else {
        bidsPriceAndVolume = new std::map<int64_t, uint64_t>();
        tickerBidPriceMap.insert(std::pair<std::string, std::map<int64_t, uint64_t> *>(ticker, bidsPriceAndVolume));
//        KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth:" << "ticker : " << ticker << "  insert into map (bidsPriceAndVolume.size) " << bidsPriceAndVolume->size());
    }

    //make depth map
    if (json.HasMember("params") && json["params"].IsObject()) {
        if (json["params"].HasMember("ask") && json["params"]["ask"].IsArray()) {
            int len = json["params"]["ask"].Size();
            auto &asks = json["params"]["ask"];
            for (int i = 0; i < len; i++) {
                int64_t price = std::round(stod(asks.GetArray()[i]["price"].GetString()) * scale_offset);
                uint64_t volume = std::round(stod(asks.GetArray()[i]["size"].GetString()) * scale_offset);
                //if volume is 0, remove it
                if (volume == 0) {
                    asksPriceAndVolume->erase(price);
                    KF_LOG_INFO(logger,
                                "MDEngineHitBTC::onDepth: ##########################################asksPriceAndVolume volume == 0############################# price:"
                                        << price << "  volume:" << volume);
                } else {
                    asksPriceAndVolume->erase(price);
                    asksPriceAndVolume->insert(std::pair<int64_t, uint64_t>(price, volume));
                }
                KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth: asks price:" << price << "  volume:" << volume);
                asks_update = true;
            }
        }

        if (json["params"].HasMember("bid") && json["params"]["bid"].IsArray()) {
            int len = json["params"]["bid"].Size();
            auto &bids = json["params"]["bid"];
            for (int i = 0; i < len; i++) {
                int64_t price = std::round(stod(bids.GetArray()[i]["price"].GetString()) * scale_offset);
                uint64_t volume = std::round(stod(bids.GetArray()[i]["size"].GetString()) * scale_offset);
                //if volume is 0, remove it
                if (volume == 0) {
                    bidsPriceAndVolume->erase(price);
                    KF_LOG_INFO(logger,
                                "MDEngineHitBTC::onDepth: ##########################################bidsPriceAndVolume volume == 0############################# price:"
                                        << price << "  volume:" << volume);
                } else {
                    bidsPriceAndVolume->erase(price);
                    bidsPriceAndVolume->insert(std::pair<int64_t, uint64_t>(price, volume));
                }
                KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth: bids price:" << price << "  volume:" << volume);
                bids_update = true;
            }
        }
    }
    
    // has any update
    if(asks_update || bids_update)
    {
        //create book update
        std::vector<PriceAndVolume> sort_result;
        LFPriceBook20Field md;
        memset(&md, 0, sizeof(md));

        sortMapByKey(*asksPriceAndVolume, sort_result, sort_price_desc);
        std::cout<<"asksPriceAndVolume sorted desc:"<< std::endl;
        for(int i=0; i<sort_result.size(); i++)
        {
            std::cout << i << "    " << sort_result[i].price << "," << sort_result[i].volume << std::endl;
        }
        //asks 	卖方深度 from big to little
        int askTotalSize = (int)sort_result.size();
        auto size = std::min(askTotalSize, 20);

        for(int i = 0; i < size; ++i)
        {
            md.AskLevels[i].price = sort_result[askTotalSize - i - 1].price;
            md.AskLevels[i].volume = sort_result[askTotalSize - i - 1].volume;
            KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth:  LFPriceBook20Field AskLevels: (i)" << i << "(price)" << md.AskLevels[i].price<<  "  (volume)"<< md.AskLevels[i].volume);
        }
        md.AskLevelCount = size;


        sort_result.clear();
        sortMapByKey(*bidsPriceAndVolume, sort_result, sort_price_asc);
        std::cout<<"bidsPriceAndVolume sorted asc:"<< std::endl;
        for(int i=0; i<sort_result.size(); i++)
        {
            std::cout << i << "    " << sort_result[i].price << "," << sort_result[i].volume << std::endl;
        }
        //bids 	买方深度 from big to little
        int bidTotalSize = (int)sort_result.size();
        size = std::min(bidTotalSize, 20);

        for(int i = 0; i < size; ++i)
        {
            md.BidLevels[i].price = sort_result[bidTotalSize - i - 1].price;
            md.BidLevels[i].volume = sort_result[bidTotalSize - i - 1].volume;
            KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth:  LFPriceBook20Field BidLevels: (i) " << i << "(price)" << md.BidLevels[i].price<<  "  (volume)"<< md.BidLevels[i].volume);
        }
        md.BidLevelCount = size;
        sort_result.clear();


        strcpy(md.InstrumentID, ticker.c_str());
        strcpy(md.ExchangeID, "mock");

        KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth: on_price_book_update");
        on_price_book_update(&md);
    }

}

// {"base":"btc","biz":"spot","data":{"asks":[["6628.6245","0"],["6624.3958","0"]],
// "bids":[["6600.7846","0"],["6580.8484","0"]]},"quote":"usdt","type":"depth","zip":false}
void MDEngineHitBTC::onDepth(Document& json)
{
    bool asks_update = false;
    bool bids_update = false;

    std::string base="";
    if(json.HasMember("base") && json["base"].IsString()) {
        base = json["base"].GetString();
    }
    std::string quote="";
    if(json.HasMember("quote") && json["quote"].IsString()) {
        quote = json["quote"].GetString();
    }

    KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth:" << "base : " << base << "  quote: " << quote);

    std::string ticker = getWhiteListCoinpairFrom(base + "_" +  quote);
    if(ticker.length() == 0) {
		KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth: not in WhiteList , ignore it:" << "base : " << base << "  quote: " << quote);
		return;
    }

    KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth:" << "(ticker) " << ticker);
	std::map<int64_t, uint64_t>*  asksPriceAndVolume;
	std::map<int64_t, uint64_t>*  bidsPriceAndVolume;

	auto iter = tickerAskPriceMap.find(ticker);
	if(iter != tickerAskPriceMap.end()) {
		asksPriceAndVolume = iter->second;
//        KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth:" << "ticker : " << ticker << "  get from map (asksPriceAndVolume.size) " << asksPriceAndVolume->size());
	} else {
        asksPriceAndVolume = new std::map<int64_t, uint64_t>();
		tickerAskPriceMap.insert(std::pair<std::string, std::map<int64_t, uint64_t>*>(ticker, asksPriceAndVolume));
//        KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth:" << "ticker : " << ticker << "  insert into map (asksPriceAndVolume.size) " << asksPriceAndVolume->size());
	}

	iter = tickerBidPriceMap.find(ticker);
	if(iter != tickerBidPriceMap.end()) {
		bidsPriceAndVolume = iter->second;
//        KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth:" << "ticker : " << ticker << "  get from map (bidsPriceAndVolume.size) " << bidsPriceAndVolume->size());
	} else {
        bidsPriceAndVolume = new std::map<int64_t, uint64_t>();
		tickerBidPriceMap.insert(std::pair<std::string, std::map<int64_t, uint64_t>*>(ticker, bidsPriceAndVolume));
//        KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth:" << "ticker : " << ticker << "  insert into map (bidsPriceAndVolume.size) " << bidsPriceAndVolume->size());
	}

    //make depth map
    if(json.HasMember("data") && json["data"].IsObject()) {
        if(json["data"].HasMember("asks") && json["data"]["asks"].IsArray()) {
            int len = json["data"]["asks"].Size();
            auto& asks = json["data"]["asks"];
            for(int i = 0 ; i < len; i++)
            {
                int64_t price = std::round(stod(asks.GetArray()[i][0].GetString()) * scale_offset);
                uint64_t volume = std::round(stod(asks.GetArray()[i][1].GetString()) * scale_offset);
                //if volume is 0, remove it
                if(volume == 0) {
                    asksPriceAndVolume->erase(price);
                    KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth: ##########################################asksPriceAndVolume volume == 0############################# price:" << price<<  "  volume:"<< volume);
                } else {
					asksPriceAndVolume->erase(price);
                    asksPriceAndVolume->insert(std::pair<int64_t, uint64_t>(price, volume));
                }
//                KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth: asks price:" << price<<  "  volume:"<< volume);
                asks_update = true;
            }
        }

        if(json["data"].HasMember("bids") && json["data"]["bids"].IsArray()) {
            int len = json["data"]["bids"].Size();
            auto& bids = json["data"]["bids"];
            for(int i = 0 ; i < len; i++)
            {
                int64_t price = std::round(stod(bids.GetArray()[i][0].GetString()) * scale_offset);
                uint64_t volume = std::round(stod(bids.GetArray()[i][1].GetString()) * scale_offset);
                if(volume == 0) {
                    bidsPriceAndVolume->erase(price);
                    KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth: ##########################################bidsPriceAndVolume volume == 0############################# price:" << price<<  "  volume:"<< volume);

                } else {
					bidsPriceAndVolume->erase(price);
                    bidsPriceAndVolume->insert(std::pair<int64_t, uint64_t>(price, volume));
                }
//                KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth: bids price:" << price<<  "  volume:"<< volume);
                bids_update = true;
            }
        }
    }
    // has any update
    if(asks_update || bids_update)
    {
        //create book update
        std::vector<PriceAndVolume> sort_result;
        LFPriceBook20Field md;
        memset(&md, 0, sizeof(md));

        sortMapByKey(*asksPriceAndVolume, sort_result, sort_price_desc);
        std::cout<<"asksPriceAndVolume sorted desc:"<< std::endl;
        for(int i=0; i<sort_result.size(); i++)
        {
            std::cout << i << "    " << sort_result[i].price << "," << sort_result[i].volume << std::endl;
        }
        //asks 	卖方深度 from big to little
        int askTotalSize = (int)sort_result.size();
        auto size = std::min(askTotalSize, 20);

        for(int i = 0; i < size; ++i)
        {
            md.AskLevels[i].price = sort_result[askTotalSize - i - 1].price;
            md.AskLevels[i].volume = sort_result[askTotalSize - i - 1].volume;
            KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth:  LFPriceBook20Field AskLevels: (i)" << i << "(price)" << md.AskLevels[i].price<<  "  (volume)"<< md.AskLevels[i].volume);
        }
        md.AskLevelCount = size;


        sort_result.clear();
        sortMapByKey(*bidsPriceAndVolume, sort_result, sort_price_asc);
        std::cout<<"bidsPriceAndVolume sorted asc:"<< std::endl;
        for(int i=0; i<sort_result.size(); i++)
        {
            std::cout << i << "    " << sort_result[i].price << "," << sort_result[i].volume << std::endl;
        }
        //bids 	买方深度 from big to little
        int bidTotalSize = (int)sort_result.size();
        size = std::min(bidTotalSize, 20);

        for(int i = 0; i < size; ++i)
        {
            md.BidLevels[i].price = sort_result[bidTotalSize - i - 1].price;
            md.BidLevels[i].volume = sort_result[bidTotalSize - i - 1].volume;
            KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth:  LFPriceBook20Field BidLevels: (i) " << i << "(price)" << md.BidLevels[i].price<<  "  (volume)"<< md.BidLevels[i].volume);
        }
        md.BidLevelCount = size;
        sort_result.clear();


        strcpy(md.InstrumentID, ticker.c_str());
        strcpy(md.ExchangeID, "mock");

        KF_LOG_INFO(logger, "MDEngineHitBTC::onDepth: on_price_book_update");
        on_price_book_update(&md);
    }
}

std::string MDEngineHitBTC::parseJsonToString(const char* in)
{
	Document d;
	d.Parse(reinterpret_cast<const char*>(in));

	StringBuffer buffer;
	Writer<StringBuffer> writer(buffer);
	d.Accept(writer);

	return buffer.GetString();
}

/*
Name    Type    Required    Description
event   String    true    事件类型，订阅:subscribe
biz     String    true    产品类型: spot
type    String    true    业务类型: depth
base    String    true    基准货币
quote   String    true    交易货币
zip     String    false    默认false,不压缩

 * */
std::string MDEngineHitBTC::createDepthJsonString(std::string base, std::string quote)
{
    /*
{
    "event":"subscribe",
    "params":{
        "biz":"spot",
        "type":"depth",
        "base":"btc",
        "quote":"usdt",
        "zip":false
    }
}

     * */
	StringBuffer s;
	Writer<StringBuffer> writer(s);
	writer.StartObject();
	writer.Key("event");
	writer.String("subscribe");
	writer.Key("params");
	writer.StartObject();
	writer.Key("biz");
	writer.String("spot");
	writer.Key("type");
	writer.String("depth");
	writer.Key("base");
	writer.String(base.c_str());
	writer.Key("quote");
	writer.String(quote.c_str());
	writer.Key("zip");
	writer.Bool(false);
	writer.EndObject();
	writer.EndObject();
	return s.GetString();
}

std::string MDEngineHitBTC::createFillsJsonString(std::string base, std::string quote)
{
    /*
 {
"event": "subscribe",
"params": {
    "biz": "spot",
    "type": "fills",
    "base": "cel",
    "quote": "btc",
    "zip": false
}
}

     * */
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();
    writer.Key("event");
    writer.String("subscribe");
    writer.Key("params");
    writer.StartObject();
    writer.Key("biz");
    writer.String("spot");
    writer.Key("type");
    writer.String("fills");
    writer.Key("base");
    writer.String(base.c_str());
    writer.Key("quote");
    writer.String(quote.c_str());
    writer.Key("zip");
    writer.Bool(false);
    writer.EndObject();
    writer.EndObject();
    return s.GetString();
}

std::string MDEngineHitBTC::createTickersJsonString()
{
	StringBuffer s;
	Writer<StringBuffer> writer(s);
	writer.StartObject();
	writer.Key("event");
	writer.String("subscribe");
	writer.Key("params");
	writer.StartObject();
	writer.Key("biz");
	writer.String("spot");
	writer.Key("type");
	writer.String("tickers");
	writer.Key("zip");
	writer.Bool(false);
	writer.EndObject();
	writer.EndObject();
	return s.GetString();
}

void MDEngineHitBTC::loop()
{
		while(isRunning)
		{
			lws_service( context, rest_get_interval_ms );
		}
}

BOOST_PYTHON_MODULE(libhitbtcmd)
{
    using namespace boost::python;
    class_<MDEngineHitBTC, boost::shared_ptr<MDEngineHitBTC> >("Engine")
    .def(init<>())
    .def("init", &MDEngineHitBTC::initialize)
    .def("start", &MDEngineHitBTC::start)
    .def("stop", &MDEngineHitBTC::stop)
    .def("logout", &MDEngineHitBTC::logout)
    .def("wait_for_stop", &MDEngineHitBTC::wait_for_stop);
}
