#include "MDEngineProbit.h"
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

static MDEngineProbit* global_md = nullptr;

static int ws_service_cb( struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len )
{

    switch( reason )
    {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
        {
            std::cout << "3.1415926 LWS_CALLBACK_CLIENT_ESTABLISHED callback client established, reason = " << reason << std::endl;
            lws_callback_on_writable( wsi );
            break;
        }
        case LWS_CALLBACK_PROTOCOL_INIT:
        {
            std::cout << "3.1415926 LWS_CALLBACK_PROTOCOL_INIT init, reason = " << reason << std::endl;
            break;
        }
        case LWS_CALLBACK_CLIENT_RECEIVE:
        {
            std::cout << "3.1415926 LWS_CALLBACK_CLIENT_RECEIVE on data, reason = " << reason << std::endl;
            if(global_md)
            {
                global_md->on_lws_data(wsi, (const char*)in, len);
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_CLOSED:
        {
            std::cout << "3.1415926 LWS_CALLBACK_CLIENT_CLOSED, reason = " << reason << std::endl;
            if(global_md) {
                std::cout << "3.1415926 LWS_CALLBACK_CLIENT_CLOSED 2,  (call on_lws_connection_error)  reason = " << reason << std::endl;
                global_md->on_lws_connection_error(wsi);
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_RECEIVE_PONG:
        {
            std::cout << "3.1415926 LWS_CALLBACK_CLIENT_RECEIVE_PONG, reason = " << reason << std::endl;
            break;
        }
        case LWS_CALLBACK_CLIENT_WRITEABLE:
        {
            std::cout << "3.1415926 LWS_CALLBACK_CLIENT_WRITEABLE writeable, reason = " << reason << std::endl;
            if(global_md)
            {
                global_md->lws_write_subscribe(wsi);
            }
            break;
        }
	    case LWS_CALLBACK_TIMER:
        {
            std::cout << "3.1415926 LWS_CALLBACK_TIMER, reason = " << reason << std::endl;
            break;
	    }
        case LWS_CALLBACK_CLOSED:
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        {
            std::cout << "3.1415926 LWS_CALLBACK_CLOSED/LWS_CALLBACK_CLIENT_CONNECTION_ERROR writeable, reason = " << reason << std::endl;
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


enum protocolList 
{
    PROTOCOL_TEST,

    PROTOCOL_LIST_COUNT
};

struct session_data 
{
    int fd;
};

MDEngineProbit::MDEngineProbit(): IMDEngine(SOURCE_PROBIT)
{
    logger = yijinjing::KfLog::getLogger("MdEngine.Probit");
}

void MDEngineProbit::load(const json& j_config)
{
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();
    KF_LOG_INFO(logger, "MDEngineProbit:: rest_get_interval_ms: " << rest_get_interval_ms);

    coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
    coinPairWhiteList.Debug_print();

    getBaseQuoteFromWhiteListStrategyCoinPair();
    
    makeWebsocketSubscribeJsonString();
    debug_print(websocketSubscribeJsonString);

    //display usage:
    if(coinPairWhiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "MDEngineProbit::lws_write_subscribe: subscribeCoinBaseQuote is empty. please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"btcusdt\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"etceth\"");
        KF_LOG_ERROR(logger, "},");
    }
}


void MDEngineProbit::getBaseQuoteFromWhiteListStrategyCoinPair()
{
    std::unordered_map<std::string, std::string>::iterator map_itr;
    map_itr = coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().begin();
    while(map_itr != coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().end())
    {
        std::cout << "[getBaseQuoteFromWhiteListStrategyCoinPair] keyIsExchangeSideWhiteList (strategy_coinpair) "
                  << map_itr->first << " (exchange_coinpair) "<< map_itr->second << std::endl;

        // strategy_coinpair 转换成大写字母
        std::string coinpair = map_itr->first;
        std::transform(coinpair.begin(), coinpair.end(), coinpair.begin(), ::toupper);

        CoinBaseQuote baseQuote;
        split(coinpair, "_", baseQuote);
        std::cout << "[readWhiteLists] getBaseQuoteFromWhiteListStrategyCoinPair (base) " << baseQuote.base << " (quote) " << baseQuote.quote << std::endl;

        if(baseQuote.base.length() > 0)
        {
            //get correct base_quote config
            coinBaseQuotes.push_back(baseQuote);
        }
        map_itr++;
    }
}


//example: btc_usdt
void MDEngineProbit::split(std::string str, std::string token, CoinBaseQuote& sub)
{
    if (str.size() > 0)
    {
        size_t index = str.find(token);
        if (index != std::string::npos)
        {
            sub.base = str.substr(0, index);
            sub.quote = str.substr(index + token.size());
        }
        else {
            //not found, do nothing
        }
    }
}

void MDEngineProbit::makeWebsocketSubscribeJsonString()
{
    int count = coinBaseQuotes.size();
    for (int i = 0; i < count; i++)
    {
        //get ready websocket subscrube json strings
        std::string jsonDepthString = createMarketDataJsonString();
        websocketSubscribeJsonString.push_back(jsonDepthString);
        //std::string jsonFillsString = createFillsJsonString(coinBaseQuotes[i].base, coinBaseQuotes[i].quote);
        //websocketSubscribeJsonString.push_back(jsonFillsString);
    }
}

void MDEngineProbit::debug_print(std::vector<std::string> &subJsonString)
{
    size_t count = subJsonString.size();
    KF_LOG_INFO(logger, "[debug_print] websocketSubscribeJsonString (count) " << count);

    for (size_t i = 0; i < count; i++)
    {
        KF_LOG_INFO(logger, "[debug_print] websocketSubscribeJsonString (subJsonString) " << subJsonString[i]);
    }
}

void MDEngineProbit::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "MDEngineProbit::connect:");
    connected = true;
}
void MDEngineProbit::login(long timeout_nsec) {
    KF_LOG_INFO(logger, "MDEngineProbit::login:");
    global_md = this;

    char inputURL[300] = "wss://demo-api.probit.com/api/exchange/v1/ws";

    const char *urlProtocol, *urlTempPath;
    char urlPath[300];
    //int logs = LLL_ERR | LLL_DEBUG | LLL_WARN;
    struct lws_client_connect_info clientConnectInfo;
    memset(&clientConnectInfo, 0, sizeof(clientConnectInfo));
    clientConnectInfo.port = 443;
    if (lws_parse_uri(inputURL, &urlProtocol, &clientConnectInfo.address, &clientConnectInfo.port, &urlTempPath)) {
        KF_LOG_ERROR(logger,
                     "MDEngineProbit::connect: Couldn't parse URL. Please check the URL and retry: " << inputURL);
        return;
    }

    // Fix up the urlPath by adding a / at the beginning, copy the temp path, and add a \0     at the end
    urlPath[0] = '/';
    strncpy(urlPath + 1, urlTempPath, sizeof(urlPath) - 2);
    urlPath[sizeof(urlPath) - 1] = '\0';
    clientConnectInfo.path = urlPath; // Set the info's path to the fixed up url path

    KF_LOG_INFO(logger, "MDEngineProbit::login:" << "urlProtocol=" << urlProtocol <<
                                                  "address=" << clientConnectInfo.address <<
                                                  "urlTempPath=" << urlTempPath <<
                                                  "urlPath=" << urlPath);
    if (context == NULL) {
        struct lws_protocols protocol;
        protocol.name = protocols[PROTOCOL_TEST].name;
        protocol.callback = &ws_service_cb;
        protocol.per_session_data_size = sizeof(struct session_data);
        protocol.rx_buffer_size = 0;
        protocol.id = 0;
        protocol.user = NULL;

        struct lws_context_creation_info ctxCreationInfo;
        memset(&ctxCreationInfo, 0, sizeof(ctxCreationInfo));
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
        ctxCreationInfo.ws_ping_pong_interval = 1;
        ctxCreationInfo.ka_time = 10;
        ctxCreationInfo.ka_probes = 10;
        ctxCreationInfo.ka_interval = 10;

        context = lws_create_context(&ctxCreationInfo);
        KF_LOG_INFO(logger, "MDEngineProbit::login: context created.");
    }

    if (context == NULL) {
        KF_LOG_ERROR(logger, "MDEngineProbit::login: context is NULL. return");
        return;
    }

    struct lws *wsi = NULL;
    // Set up the client creation info
    clientConnectInfo.context = context;
    clientConnectInfo.port = 443;
    clientConnectInfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
    clientConnectInfo.host = clientConnectInfo.address;
    clientConnectInfo.origin = clientConnectInfo.address;
    clientConnectInfo.ietf_version_or_minus_one = -1;
    clientConnectInfo.protocol = protocols[PROTOCOL_TEST].name;

    KF_LOG_INFO(logger, "MDEngineProbit::login:" << "Connecting to " << urlProtocol << ":" <<
                                                  clientConnectInfo.host << ":" <<
                                                  clientConnectInfo.port << ":" << urlPath);

    wsi = lws_client_connect_via_info(&clientConnectInfo);
    if (wsi == NULL) {
        KF_LOG_ERROR(logger, "MDEngineProbit::login: wsi create error.");
        return;
    }
    KF_LOG_INFO(logger, "MDEngineProbit::login: wsi create success.");
    logged_in = true;
}

void MDEngineProbit::set_reader_thread()
{
    IMDEngine::set_reader_thread();

    rest_thread = ThreadPtr(new std::thread(boost::bind(&MDEngineProbit::loop, this)));
}

void MDEngineProbit::logout()
{
    KF_LOG_INFO(logger, "MDEngineProbit::logout:");
}

void MDEngineProbit::release_api()
{
    KF_LOG_INFO(logger, "MDEngineProbit::release_api:");
}

void MDEngineProbit::subscribeMarketData(const vector<string>& instruments, const vector<string>& markets)
{
    KF_LOG_INFO(logger, "MDEngineProbit::subscribeMarketData:");
}

int MDEngineProbit::lws_write_subscribe(struct lws* conn)
{
    KF_LOG_INFO(logger, "MDEngineProbit::lws_write_subscribe: (subscribe_index)" << subscribe_index);

    if(websocketSubscribeJsonString.size() == 0) return 0;
    //sub depth
    if(subscribe_index >= websocketSubscribeJsonString.size())
    {
        //subscribe_index = 0;
        KF_LOG_INFO(logger, "MDEngineProbit::lws_write_subscribe: (none reset subscribe_index = 0, just return 0)");
	    return 0;
    }

    unsigned char msg[512];
    memset(&msg[LWS_PRE], 0, 512-LWS_PRE);

    std::string jsonString = websocketSubscribeJsonString[subscribe_index++];

    KF_LOG_INFO(logger, "MDEngineProbit::lws_write_subscribe: " << jsonString.c_str() << " ,after ++, (subscribe_index)" << subscribe_index);
    int length = jsonString.length();

    strncpy((char *)msg+LWS_PRE, jsonString.c_str(), length);
    int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);

    if(subscribe_index < websocketSubscribeJsonString.size())
    {
        lws_callback_on_writable( conn );
        KF_LOG_INFO(logger, "MDEngineProbit::lws_write_subscribe: (subscribe_index < websocketSubscribeJsonString) call lws_callback_on_writable");
    }

    return ret;
}

void MDEngineProbit::on_lws_data(struct lws* conn, const char* data, size_t len)
{
    KF_LOG_INFO(logger, "MDEngineProbit::on_lws_data: " << data);
    Document json;
    json.Parse(data);

    if(!json.HasParseError() && json.IsObject() && json.HasMember("channel") && json["channel"].IsString())
    {
        if(strcmp(json["channel"].GetString(), "marketdata") == 0)
        {
            KF_LOG_INFO(logger, "MDEngineProbit::on_lws_data: is marketdata");
            onMarketData(json);
        }
    } else {
        KF_LOG_ERROR(logger, "MDEngineProbit::on_lws_data . parse json error: " << data);
    }
}

void MDEngineProbit::on_lws_connection_error(struct lws* conn)
{
    KF_LOG_ERROR(logger, "MDEngineProbit::on_lws_connection_error.");
    //market logged_in false;
    logged_in = false;
    KF_LOG_ERROR(logger, "MDEngineProbit::on_lws_connection_error. login again.");
    //clear the price book, the new websocket will give 200 depth on the first connect, it will make a new price book
    priceBook20Assembler.clearPriceBook();
    //no use it
    long timeout_nsec = 0;
    //reset sub
    subscribe_index = 0;

    login(timeout_nsec);
}

/*
{
  "channel":"marketdata",
  "market_id":"XRP-BTC",
  "status":"ok",
  "lag":0,
  "ticker":{
   "time":"2018-08-17T03:00:43.000Z",
   "last":"0.00004221",
   "low":"0.00003953",
   "high":"0.00004233",
   "change":"0.00000195",
   "base_volume":"119304953.57728445",
   "quote_volume":"4914.391934022046355"
  },
  "recent_trades":[
	{"price":"0.00004221","quantity":"555","time":"2018-08-17T02:56:17.249Z","side":"buy","tick_direction":"zeroup"},
	.
	.
	.
	],
	"order_books":[
	{"side":"buy","price":"0.00003218","quantity":"167912.55816365"},
	.
	.
	.
	],
"reset":true,
}
**/
void MDEngineProbit::onMarketData(Document& json)
{
    std::string channel="";
    if(json.HasMember("channel") && json["channel"].IsString()) 
    {
        channel = json["channel"].GetString();
    }
    std::string market_id="";
    if(json.HasMember("market_id") && json["market_id"].IsString()) 
    {
        market_id = json["market_id"].GetString();
    }
	std::string status="";
    if(json.HasMember("status") && json["status"].IsString()) 
    {
        status = json["status"].GetString();
    }
	std::string lag="";
    if(json.HasMember("lag") && json["lag"].IsString()) 
    {
        status = json["lag"].GetInt();
    }
    std::string ticker = coinPairWhiteList.GetKeyByValue(market_id);
    if(ticker.length() == 0) 
    {
        KF_LOG_INFO(logger, "MDEngineCoinmex::onMarketData: market_id not in WhiteList , ignore it:" << "market_id : " << market_id);
        return;
    }
    KF_LOG_INFO(logger, "MDEngineProbit::onMarketData:" << "channel : " << channel << "  market_id: " << market_id << "  status: " << status << "  lag: " << lag << "  ticker: " << ticker);

    if(json.HasMember("ticker") && json["ticker"].IsObject()) 
    {
        std::string ticker_time="";
        std::string ticker_last="";
        std::string ticker_low="";
        std::string ticker_high="";
        std::string ticker_change="";
        std::string ticker_base_volume="";
        std::string ticker_quote_volume="";
        if(json["ticker"].HasMember("time") && json["ticker"]["time"].IsString()) 
        {
            ticker_time = json["ticker"]["time"].GetString();
        }
		if(json["ticker"].HasMember("last") && json["ticker"]["last"].IsString()) 
        {
            ticker_last = json["ticker"]["last"].GetString();
        }
		if(json["ticker"].HasMember("low") && json["ticker"]["low"].IsString()) 
        {
            ticker_low = json["ticker"]["low"].GetString();
        }
		if(json["ticker"].HasMember("high") && json["ticker"]["high"].IsString()) 
        {
            ticker_high = json["ticker"]["high"].GetString();
        }
		if(json["ticker"].HasMember("change") && json["ticker"]["change"].IsString()) 
        {
            ticker_change = json["ticker"]["change"].GetString();
        }
		if(json["ticker"].HasMember("base_volume") && json["ticker"]["base_volume"].IsString()) 
        {
            ticker_base_volume = json["ticker"]["base_volume"].GetString();
        }
		if(json["ticker"].HasMember("quote_volume") && json["ticker"]["quote_volume"].IsString()) 
        {
            ticker_quote_volume = json["ticker"]["quote_volume"].GetString();
        }
        KF_LOG_INFO(logger, "MDEngineProbit::onMarketData:" << "ticker_time : " << ticker_time << "  ticker_last : " << ticker_last << "  ticker_low : " << ticker_low << "  ticker_high : " << ticker_high << "  ticker_change : " << ticker_change << "  ticker_base_volume : " << ticker_base_volume << "  ticker_quote_volume : " << ticker_quote_volume);
    }
	if(json.HasMember("recent_trades") && json["recent_trades"].IsArray()) 
	{
        int len = json["recent_trades"].Size();
        for(int i = 0 ; i < len; i++)
        {
            std::string rt_side="";
            std::string rt_price="";
            std::string rt_quantity="";
            std::string rt_tick_direction="";
            std::string rt_time="";
            auto& trade_data = json["recent_trades"].GetArray()[i];
            if(trade_data.IsObject())
            {
                if(trade_data.HasMember("side") && trade_data["side"].IsString())
                {
                    rt_side = trade_data["side"].GetString();
                }
                if(trade_data.HasMember("price") && trade_data["price"].IsString())
                {
                    rt_price = trade_data["price"].GetString();
                }
                if(trade_data.HasMember("quantity") && trade_data["quantity"].IsString())
                {
                    rt_quantity = trade_data["quantity"].GetString();
                }
                if(trade_data.HasMember("tick_direction") && trade_data["tick_direction"].IsString())
                {
                    rt_tick_direction = trade_data["tick_direction"].GetString();
                }
                if(trade_data.HasMember("time") && trade_data["time"].IsString())
                {
                    rt_time = trade_data["time"].GetString();
                }
            }
            KF_LOG_INFO(logger, "MDEngineProbit::onMarketData:" << "rt_side : " << rt_side << "  rt_price: " << rt_price << "  rt_quantity: " << rt_quantity << "  rt_tick_direction: " << rt_tick_direction << "  rt_time: " << rt_time);
        }
    }
	if(json.HasMember("order_books") && json["order_books"].IsArray()) 
	{
        int len = json["order_books"].Size();
        for(int i = 0 ; i < len; i++)
        {
            std::string ob_side="";
            std::string ob_price="";
            std::string ob_quantity="";
            auto& order_book = json["order_books"].GetArray()[i];
            if(order_book.IsObject())
            {
                if(order_book.HasMember("side") && order_book["side"].IsString())
                {
                    ob_side = order_book["side"].GetString();
                }
                if(order_book.HasMember("price") && order_book["price"].IsString())
                {
                    ob_price = order_book["price"].GetString();
                }
                if(order_book.HasMember("quantity") && order_book["quantity"].IsString())
                {
                    ob_quantity = order_book["quantity"].GetString();
                }

                int64_t price = std::round(stod(ob_price) * scale_offset);
                uint64_t volume = std::round(stod(ob_quantity) * scale_offset);
                if(strcmp(ob_side.c_str(), "buy") == 0)
                {
                    if (volume == 0)
                    {
                        priceBook20Assembler.EraseAskPrice(ticker, price);
                        KF_LOG_INFO(logger, "MDEngineCoinmex::onMarketData: ##########################################asksPriceAndVolume volume == 0############################# price:" << price<<  "  volume:"<< volume);
                    }
                    else
                    {
                        priceBook20Assembler.UpdateAskPrice(ticker, price, volume);    
                    }                    
                }
                else if(strcmp(ob_side.c_str(), "sell") == 0)
                {
                    if (volume == 0)
                    {
                        priceBook20Assembler.EraseBidPrice(ticker, price);
                        KF_LOG_INFO(logger, "MDEngineCoinmex::onMarketData: ##########################################bidsPriceAndVolume volume == 0############################# price:" << price<<  "  volume:"<< volume);
                    }
                    else
                    {
                        priceBook20Assembler.UpdateBidPrice(ticker, price, volume);
                    }
                }
            }
            KF_LOG_INFO(logger, "MDEngineProbit::onMarketData:" << "ob_side : " << ob_side << "  ob_price: " << ob_price << "  ob_quantity: " << ob_quantity);
        }
    }
    bool reset = false;
	if(json.HasMember("reset") && json["reset"].IsBool()) 
	{
        reset = json["reset"].GetBool();
        KF_LOG_INFO(logger, "MDEngineProbit::onMarketData:" << "reset : " << reset);
    }

    // has any update
    LFPriceBook20Field md;
    memset(&md, 0, sizeof(md));
    if(priceBook20Assembler.Assembler(ticker, md)) {
        strcpy(md.ExchangeID, "probit");

        KF_LOG_INFO(logger, "MDEngineProbit::onMarketData: on_price_book_update");
        on_price_book_update(&md);
    }
}

std::string MDEngineProbit::parseJsonToString(const char* in)
{
    Document d;
    d.Parse(reinterpret_cast<const char*>(in));

    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);

    return buffer.GetString();
}

/*
Name    	Type    Required    Description
type   		String    true    	must be 'subscribe'
channel     String    true    	Must be 'marketdata'
market_id   String    true    	Market ID that you want subscribe
interval    String    true    	Data sending interval(ms). It can be one of this values:100, 500
								If it is set to 0, it enables real-time marketdata streaming.
filter   	String    true    	Data that what you need.It can be one of this values:
								ticker,
								recent_trades,
								order_books,
								order_books_l2,
								order_books_l3,
								order_books_l4
								
Example
	{"type":"subscribe","channel":"marketdata","market_id":"ETH-BTC","interval":100,"filter":["ticker","order_books"]}
	{"type":"subscribe","channel":"exchange_rate","filter":["BTC"]}
	{"type":"unsubscribe","channel":"marketdata","market_id":"ETH-BTC"}
	{"type":"unsubscribe","channel":"exchange_rate"}
 * */
std::string MDEngineProbit::createMarketDataJsonString()
{
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();
    writer.Key("type");
    writer.String("subscribe");
	writer.Key("channel");
    writer.String("marketdata");
	writer.Key("market_id");
    writer.String("BTC-USDT");
	writer.Key("interval");
    writer.Int(100);
    writer.Key("filter");
    writer.StartArray();
    writer.String("ticker");
	writer.String("recent_trades");
    writer.String("order_books");
    writer.EndArray();
    writer.EndObject();
    return s.GetString();
}

void MDEngineProbit::loop()
{
    while(isRunning)
    {
            int n = lws_service( context, rest_get_interval_ms );
            std::cout << " 3.1415 loop() lws_service (n)" << n << std::endl;
    }
}

BOOST_PYTHON_MODULE(libprobitmd)
{
    using namespace boost::python;
    class_<MDEngineProbit, boost::shared_ptr<MDEngineProbit> >("Engine")
            .def(init<>())
            .def("init", &MDEngineProbit::initialize)
            .def("start", &MDEngineProbit::start)
            .def("stop", &MDEngineProbit::stop)
            .def("logout", &MDEngineProbit::logout)
            .def("wait_for_stop", &MDEngineProbit::wait_for_stop);
}
