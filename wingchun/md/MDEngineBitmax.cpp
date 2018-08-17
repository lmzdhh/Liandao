#include "MDEngineBitmax.h"
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
#include <algorithm>

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


static MDEngineBitmax* global_md = nullptr;

static int lws_event_cb( struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len )
{

	switch( reason )
	{
		case LWS_CALLBACK_CLIENT_ESTABLISHED:
		{
            std::cout << "3.1415926 LWS_CALLBACK_CLIENT_ESTABLISHED callback client established, reason = " << reason << std::endl;
			lws_callback_on_writable( wsi );
			break;
		}
        case LWS_CALLBACK_PROTOCOL_INIT:{
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
            if(global_md)
            {
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
        case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE:
        {
            std::cout << "3.1415926 LWS_CALLBACK_CLIENT_HTTP_WRITEABLE writeable, reason = " << reason << std::endl;
            /**< when doing an HTTP type client connection, you can call
             * lws_client_http_body_pending(wsi, 1) from
             * LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER to get these callbacks
             * sending the HTTP headers.
             *
             * From this callback, when you have sent everything, you should let
             * lws know by calling lws_client_http_body_pending(wsi, 0)
             */
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
            std::cout << "3.1415926 LWS_CALLBACK_CLOSED,LWS_CALLBACK_CLIENT_CONNECTION_ERROR reason = " << reason << std::endl;
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
						lws_event_cb,
							  0,
								 65536,
				},
				{ NULL, NULL, 0, 0 } /* terminator */
		};


MDEngineBitmax::MDEngineBitmax(): IMDEngine(SOURCE_BITMAX)
{
	logger = yijinjing::KfLog::getLogger("MdEngine.Bitmax");
}

void MDEngineBitmax::load(const json& j_config)
{
	book_depth_count = j_config["book_depth_count"].get<int>();
	trade_count = j_config["trade_count"].get<int>();
	rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();

    api_key = j_config["APIKey"].get<string>();
    secret_key = j_config["SecretKey"].get<string>();
    KF_LOG_INFO(logger, "MDEngineBitmax::load:  api_key: " << api_key);

	whiteList.ReadWhiteLists(j_config);

	whiteList.Debug_print();
	//display usage:
	if(whiteList.Size() == 0) {
		KF_LOG_ERROR(logger, "MDEngineBitmax::lws_write_subscribe: subscribeCoinBaseQuote is empty. please add whiteLists in kungfu.json like this :");
		KF_LOG_ERROR(logger, "\"whiteLists\":{");
		KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
		KF_LOG_ERROR(logger, "    \"btc_usdt\": \"BTC/USDT\",");
		KF_LOG_ERROR(logger, "     \"etc_eth\": \"ETC/ETH\"");
		KF_LOG_ERROR(logger, "},");
	}

	KF_LOG_INFO(logger, "MDEngineBitmax::load:  book_depth_count: "
			<< book_depth_count << " trade_count: " << trade_count << " rest_get_interval_ms: " << rest_get_interval_ms);
}


void MDEngineBitmax::connect(long timeout_nsec)
{
	KF_LOG_INFO(logger, "MDEngineBitmax::connect:");

	connected = true;
}

void MDEngineBitmax::login(long timeout_nsec)
{
	global_md = this;

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

	std::unordered_map<std::string, std::string>::iterator map_itr;
	map_itr = whiteList.GetKeyIsStrategyCoinpairWhiteList().begin();
	while(map_itr != whiteList.GetKeyIsStrategyCoinpairWhiteList().end()) {
        KF_LOG_DEBUG(logger, "[debug_print] keyIsExchangeSideWhiteList (strategy_coinpair) " << map_itr->first << " (exchange_coinpair) "<< map_itr->second);
		connect_lws(map_itr->second, lws_event::depth20);

		map_itr++;
	}

    KF_LOG_INFO(logger, "MDEngineBitmax::login:");

	logged_in = true;
}

//bitmax订阅的币值对是需要ETH-BTC格式的，而md返回的都是 ETH/BTC格式的。在白名单只能定义一种，选了 ETH/BTC格式，所以在订阅的时候， 需要这个转换函数
//从第一个-位置替换一个/
std::string MDEngineBitmax::bitmaxSubscribeSymbol(std::string coinpair)
{
	return coinpair.replace(coinpair.find("/"), 1, "-");
}


void MDEngineBitmax::connect_lws(std::string symbol, lws_event e)
{
    KF_LOG_INFO(logger, "MDEngineBitmax::connect_lws (symbol)" << symbol);
	//已经是大写， 不需要转换

	std::string path("/api/tradeview/");
	switch(e)
	{
		case depth20:
			path += bitmaxSubscribeSymbol(symbol);
			break;
		default:
			KF_LOG_ERROR(logger, "MDEngineBitmax::connect_lws:invalid lws event");
			return;
	}

    KF_LOG_DEBUG(logger, "MDEngineBitmax::connect_lws (path)" << path);

    int logs = LLL_ERR | LLL_DEBUG | LLL_WARN;
    lws_set_log_level(logs, NULL);

    struct lws_client_connect_info ccinfo = {0};

	ccinfo.context 	= context;
	ccinfo.address 	= "bitmax.io";
	ccinfo.port 	= 443;
	ccinfo.path 	= path.c_str();
	ccinfo.host 	= "bitmax.io";
	ccinfo.origin 	= "bitmax.io";
    ccinfo.ietf_version_or_minus_one = -1;
	ccinfo.protocol = protocols[0].name;
	ccinfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;

    struct lws* conn = lws_client_connect_via_info(&ccinfo);
	KF_LOG_INFO_FMT(logger, "create a lws connection for %s %d at %lu",
                    symbol.c_str(), static_cast<int>(e), reinterpret_cast<uint64_t>(conn));
	lws_handle_map[conn] = std::make_pair(symbol, e);
}

int MDEngineBitmax::lws_write_subscribe(struct lws* conn)
{
	KF_LOG_INFO(logger, "MDEngineBitmax::lws_write_subscribe");

	auto iter = lws_handle_map.find(conn);
	if(iter == lws_handle_map.end())
	{
		KF_LOG_ERROR_FMT(logger, "MDEngineBitmax::lws_write_subscribe failed to find ticker and event from %lu", reinterpret_cast<uint64_t>(conn));
		return 0;
	}

    KF_LOG_DEBUG(logger, "MDEngineBitmax::lws_write_subscribe: (symbol)" << iter->second.first);

	unsigned char msg[512];
	memset(&msg[LWS_PRE], 0, 512-LWS_PRE);

	//{"messageType":"subscribe", "marketDepthLevel":20, "recentTradeMaxCount": 20}
	std::string jsonString = "{\"messageType\":\"subscribe\", \"marketDepthLevel\":20, \"recentTradeMaxCount\": 20}";
    KF_LOG_DEBUG(logger, "MDEngineBitmax::lws_write_subscribe: " << jsonString.c_str());
	int length = jsonString.length();

	strncpy((char *)msg+LWS_PRE, jsonString.c_str(), length);
	int ret = lws_write(conn, &msg[LWS_PRE], length, LWS_WRITE_TEXT);

	return ret;
}

void MDEngineBitmax::on_lws_data(struct lws* conn, const char* data, size_t len)
{
	KF_LOG_INFO(logger, "MDEngineBitmax::on_lws_data: " << data);
	Document json;
	json.Parse(data);

	if(!json.HasParseError() && json.IsObject() && json.HasMember("m") && json["m"].IsString())
	{
		if(strcmp(json["m"].GetString(), "depth") == 0)
		{
            KF_LOG_DEBUG(logger, "MDEngineBitmax::on_lws_data: is depth");
			onDepth(json);
		}

		if(strcmp(json["m"].GetString(), "marketTrades") == 0)
		{
            KF_LOG_DEBUG(logger, "MDEngineBitmax::on_lws_data: is marketTrades");
			onMarketTrades(json);
		}
		if(strcmp(json["m"].GetString(), "order") == 0)
		{
            KF_LOG_DEBUG(logger, "MDEngineBitmax::on_lws_data: is orders " << parseJsonToString(data));
		}
	} else {
		KF_LOG_ERROR(logger, "MDEngineBitmax::on_lws_data . parse json error: " << data);
	}
}

void MDEngineBitmax::onMarketTrades(Document& json)
{
	/*
		{
		  "m": "marketTrades",      // message type
		  "s": "ETH/BTC",           // symbol
		  "trades": [
			{
			  "p":  "13.75",         // price
			  "q":  "6.68",          // quantity
			  "t":  1528988084944,   // timestamp
			  "bm": False            // if true, the buyer is the market maker
			},
			{
			  "p":  "13.75",         // price
			  "q":  "6.68",          // quantity
			  "t":  1528988084944,   // timestamp
			  "bm": False            // if true, the buyer is the market maker
			},
			...
		  ]
		}
	 * */

	std::string symbol = json["s"].GetString();
	KF_LOG_DEBUG(logger, "MDEngineBitmax::onMarketTrades: (symbol)" << symbol);
	std::string ticker = whiteList.GetKeyByValue(symbol);
	if(ticker.length() == 0) {
		return;
	}
    KF_LOG_DEBUG(logger, "MDEngineBitmax::onMarketTrades: (ticker)" << ticker);
	int len = json["trades"].Size();

	for(int i = 0 ; i < len; i++) {
		LFL2TradeField trade;
		memset(&trade, 0, sizeof(trade));
		strcpy(trade.InstrumentID, ticker.c_str());
		strcpy(trade.ExchangeID, "bitmax");

		trade.Price = std::round(std::stod(json["trades"].GetArray()[i]["p"].GetString()) * scale_offset);
		trade.Volume = std::round(std::stod(json["trades"].GetArray()[i]["q"].GetString()) * scale_offset);
		trade.OrderBSFlag[0] = json["trades"].GetArray()[i]["bm"].GetBool() ? 'B' : 'S';

        KF_LOG_DEBUG(logger, "MDEngineBitmax::[onMarketTrades] (ticker)" << ticker <<
																  " (Price)" << trade.Price <<
																		 " (OrderBSFlag)" << trade.OrderBSFlag[0] <<
																		 " (trade.Volume)" << trade.Volume);
		on_trade(&trade);
	}
}

void MDEngineBitmax::onDepth(Document& json)
{
    /*
    {
       "m": "depth",             // message type
       "s": "ETH/BTC",           // symbol
       "asks": [                 // ask levels, could be empty
           ["13.45", "59.16"],   // price, quantity
           ["13.37", "95.04"],
           ...
       ],
       "bids": [                 // bid levels, could be empty
           ["13.21", "60.17"],
           ["13,10", "13.39"],
           ...
       ]
    }
     * */
    std::string symbol = json["s"].GetString();
    KF_LOG_DEBUG(logger, "MDEngineBitmax::onDepth: (symbol)" << symbol);

	bool asks_update = false;
	bool bids_update = false;

	std::string ticker = whiteList.GetKeyByValue(symbol);
	if(ticker.length() == 0) {
        KF_LOG_DEBUG(logger, "MDEngineBitmax::onDepth: not in WhiteList , ignore it: (symbol) " << symbol);
		return;
	}

    KF_LOG_DEBUG(logger, "MDEngineBitmax::onDepth:" << "(ticker) " << ticker);
	//make depth map

    if(json.HasMember("asks") && json["asks"].IsArray()) {
        int len = json["asks"].Size();
        auto& asks = json["asks"];
        for(int i = 0 ; i < len; i++)
        {
            int64_t price = std::round(stod(asks.GetArray()[i][0].GetString()) * scale_offset);
            uint64_t volume = std::round(stod(asks.GetArray()[i][1].GetString()) * scale_offset);
            //if volume is 0, remove it
            if(volume == 0) {
				priceBook20Assembler.EraseAskPrice(ticker, price);
                KF_LOG_DEBUG(logger, "MDEngineBitmax::onDepth: ##########################################asksPriceAndVolume volume == 0############################# price:" << price<<  "  volume:"<< volume);
            } else {
				priceBook20Assembler.UpdateAskPrice(ticker, price, volume);
            }
//                KF_LOG_DEBUG(logger, "MDEngineCoinmex::onDepth: asks price:" << price<<  "  volume:"<< volume);
            asks_update = true;
        }
    }

    if(json.HasMember("bids") && json["bids"].IsArray()) {
        int len = json["bids"].Size();
        auto& bids = json["bids"];
        for(int i = 0 ; i < len; i++)
        {
            int64_t price = std::round(stod(bids.GetArray()[i][0].GetString()) * scale_offset);
            uint64_t volume = std::round(stod(bids.GetArray()[i][1].GetString()) * scale_offset);
            if(volume == 0) {
				priceBook20Assembler.EraseBidPrice(ticker, price);
                KF_LOG_DEBUG(logger, "MDEngineBitmax::onDepth: ##########################################bidsPriceAndVolume volume == 0############################# price:" << price<<  "  volume:"<< volume);

            } else {
				priceBook20Assembler.UpdateBidPrice(ticker, price, volume);
            }
//                KF_LOG_DEBUG(logger, "MDEngineCoinmex::onDepth: bids price:" << price<<  "  volume:"<< volume);
            bids_update = true;
        }
    }

	// has any update
	if(asks_update || bids_update)
	{
		LFPriceBook20Field md;
		memset(&md, 0, sizeof(md));
		priceBook20Assembler.Assembler(ticker, md);
		strcpy(md.ExchangeID, "bitmax");

		KF_LOG_INFO(logger, "MDEngineBitmax::onDepth: on_price_book_update");
		on_price_book_update(&md);
	}
}

std::string MDEngineBitmax::parseJsonToString(const char* in)
{
	Document d;
	d.Parse(reinterpret_cast<const char*>(in));

	StringBuffer buffer;
	Writer<StringBuffer> writer(buffer);
	d.Accept(writer);

	return buffer.GetString();
}

void MDEngineBitmax::on_lws_connection_error(struct lws* conn)
{
    KF_LOG_INFO(logger, "MDEngineBitmax::on_lws_connection_error:");
	auto iter = lws_handle_map.find(conn);
	if(iter != lws_handle_map.end())
	{
		KF_LOG_ERROR_FMT(logger, "lws connection broken for %s %d %lu, ",
						 iter->second.first.c_str(), static_cast<int>(iter->second.second), reinterpret_cast<uint64_t>(conn));

		priceBook20Assembler.clearPriceBook(iter->second.first);
		connect_lws(iter->second.first, iter->second.second);
		lws_handle_map.erase(iter);
	}
}


void MDEngineBitmax::set_reader_thread()
{
	IMDEngine::set_reader_thread();

	rest_thread = ThreadPtr(new std::thread(boost::bind(&MDEngineBitmax::loop, this)));
}

void MDEngineBitmax::logout()
{
	lws_context_destroy( context );
	KF_LOG_INFO(logger, "MDEngineBitmax::logout:");
}

void MDEngineBitmax::release_api()
{
	KF_LOG_INFO(logger, "MDEngineBitmax::release_api:");
}

void MDEngineBitmax::subscribeMarketData(const vector<string>& instruments, const vector<string>& markets)
{
	/* Connect if we are not connected to the server. */
	KF_LOG_INFO(logger, "MDEngineBitmax::subscribeMarketData:");
}

void MDEngineBitmax::loop()
{
	while(isRunning)
	{
		int n = lws_service( context, rest_get_interval_ms );
		std::cout << " 3.1415 loop() lws_service (n)" << n << std::endl;
	}
}

BOOST_PYTHON_MODULE(libbitmaxmd)
{
	using namespace boost::python;
	class_<MDEngineBitmax, boost::shared_ptr<MDEngineBitmax> >("Engine")
			.def(init<>())
			.def("init", &MDEngineBitmax::initialize)
			.def("start", &MDEngineBitmax::start)
			.def("stop", &MDEngineBitmax::stop)
			.def("logout", &MDEngineBitmax::logout)
			.def("wait_for_stop", &MDEngineBitmax::wait_for_stop);
}
