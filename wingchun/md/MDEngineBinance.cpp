#include "MDEngineBinance.h"
#include "TypeConvert.hpp"
#include "Timer.h"
#include "longfist/LFUtils.h"
#include "longfist/LFDataStruct.h"

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
using std::string;
using std::to_string;
using std::stod;
using std::stoi;

USING_WC_NAMESPACE


static MDEngineBinance* g_md_binance = nullptr;

static int lws_event_cb( struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len )
{

    switch( reason )
    {   
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
        {
            lws_callback_on_writable( wsi );
            break;      
        }
        case LWS_CALLBACK_CLIENT_RECEIVE:
        {
			if(g_md_binance)
			{
				g_md_binance->on_lws_data(wsi, (const char*)in, len);
			}
            break;      
        }
        case LWS_CALLBACK_CLIENT_WRITEABLE:
        {       
            break;      
        }       
        case LWS_CALLBACK_CLOSED:
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        {
            if(g_md_binance)
			{
				g_md_binance->on_lws_connection_error(wsi);
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
		"example-protocol",
		lws_event_cb,
		0,
		65536,
	},
	{ NULL, NULL, 0, 0 } /* terminator */
};


MDEngineBinance::MDEngineBinance(): IMDEngine(SOURCE_BINANCE)
{
    logger = yijinjing::KfLog::getLogger("MdEngine.Binance");
}

void MDEngineBinance::load(const json& j_config)
{
    book_depth_count = j_config["book_depth_count"].get<int>();
    trade_count = j_config["trade_count"].get<int>();
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();

    coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
    coinPairWhiteList.Debug_print();

    //display usage:
    if(coinPairWhiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "MDEngineBinance::lws_write_subscribe: subscribeCoinBaseQuote is empty. please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"BTCUSDT\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"ETCETH\"");
        KF_LOG_ERROR(logger, "},");
    }

    KF_LOG_INFO(logger, "MDEngineBinance::load:  book_depth_count: "
		<< book_depth_count << " trade_count: " << trade_count << " rest_get_interval_ms: " << rest_get_interval_ms); 	
}


void MDEngineBinance::connect(long timeout_nsec)
{
   KF_LOG_INFO(logger, "MDEngineBinance::connect:"); 	
	
   connected = true;
}

void MDEngineBinance::login(long timeout_nsec)
{
	g_md_binance = this;

	struct lws_context_creation_info info;
	memset( &info, 0, sizeof(info) );

	info.port = CONTEXT_PORT_NO_LISTEN;
	info.protocols = protocols;
	info.gid = -1;
	info.uid = -1;
	info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
	info.max_http_header_pool = 1024;
	info.fd_limit_per_thread = 1024;

	context = lws_create_context( &info );

    std::unordered_map<std::string, std::string>::iterator map_itr;
    map_itr = coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().begin();
    while(map_itr != coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().end()) {
        KF_LOG_INFO(logger, "[debug_print] keyIsExchangeSideWhiteList (strategy_coinpair) " << map_itr->first << " (exchange_coinpair) "<< map_itr->second);
        connect_lws(map_itr->second, lws_event::trade);
        //connect_lws(map_itr->second, lws_event::depth5);
        connect_lws(map_itr->second, lws_event::depth20);

        map_itr++;
    }

   	KF_LOG_INFO(logger, "MDEngineBinance::login:"); 	

   	logged_in = true;
}

void MDEngineBinance::connect_lws(std::string symbol, lws_event e)
{
		std::string t = symbol;
		std::transform(t.begin(), t.end(), t.begin(), ::tolower);
		std::string path("/ws/");
		switch(e)
		{
				case trade:
						path += t + "@trade";
						break;
				case depth5:
						path += t + "@depth5";
						break;
				case depth20:
						path += t + "@depth20";
						break;
				default:
						KF_LOG_ERROR(logger, "invalid lws event");
						return;
		}

		struct lws_client_connect_info ccinfo = {0};
		ccinfo.context 	= context;
		ccinfo.address 	= "stream.binance.com";
		ccinfo.port 	= 9443;
		ccinfo.path 	= path.c_str();
		ccinfo.host 	= lws_canonical_hostname( context );
		ccinfo.origin 	= "origin";
		ccinfo.protocol = protocols[0].name;
		ccinfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;

		struct lws* conn = lws_client_connect_via_info(&ccinfo);
		KF_LOG_INFO_FMT(logger, "create a lws connection for %s %d at %lu", 
						t.c_str(), static_cast<int>(e), reinterpret_cast<uint64_t>(conn));
		lws_handle_map[conn] = std::make_pair(symbol, e);
}

void MDEngineBinance::on_lws_data(struct lws* conn, const char* data, size_t len)
{
	auto iter = lws_handle_map.find(conn);
	if(iter == lws_handle_map.end())
	{
		KF_LOG_ERROR_FMT(logger, "failed to find ticker and event from %lu", reinterpret_cast<uint64_t>(conn));
		return;
	}

	//KF_LOG_INFO_FMT(logger, "%s, %d, %s", iter->second.first.c_str(), static_cast<int>(iter->second.second), data);
	
	if(iter->second.second == lws_event::trade)
	{
		on_lws_market_trade(data, len);
	}
	else if(iter->second.second == lws_event::depth5 || iter->second.second == lws_event::depth20)
	{
		on_lws_book_update(data, len, iter->second.first);
	}
}

void MDEngineBinance::on_lws_connection_error(struct lws* conn)
{
	auto iter = lws_handle_map.find(conn);
	if(iter != lws_handle_map.end())
	{
		KF_LOG_ERROR_FMT(logger, "lws connection broken for %s %d %lu, ", 
					iter->second.first.c_str(), static_cast<int>(iter->second.second), reinterpret_cast<uint64_t>(conn));
		
		connect_lws(iter->second.first, iter->second.second);
		lws_handle_map.erase(iter);
	}
}

void MDEngineBinance::on_lws_market_trade(const char* data, size_t len)
{
	//{"e":"trade","E":1529713551873,"s":"BTCUSDT","t":52690105,"p":"6040.80000000","q":"0.10006000","b":122540296,"a":122539764,"T":1529713551870,"m":false,"M":true}
	
    Document d;
    d.Parse(data);

	LFL2TradeField trade;
	memset(&trade, 0, sizeof(trade));
	
	if(!d.HasMember("s") || !d.HasMember("p") || !d.HasMember("q") || !d.HasMember("m"))
	{
		KF_LOG_ERROR(logger, "invalid market trade message");
		return;	
	}

	std::string symbol = d["s"].GetString();
	std::string ticker = coinPairWhiteList.GetKeyByValue(symbol);
    if(ticker.length() == 0) {
        KF_LOG_INFO(logger, "MDEngineBinance::on_lws_market_trade: not in WhiteList , ignore it:" << symbol);
        return;
    }

	strcpy(trade.InstrumentID, ticker.c_str());
	strcpy(trade.ExchangeID, "binance");

	trade.Price = std::round(std::stod(d["p"].GetString()) * scale_offset);
	trade.Volume = std::round(std::stod(d["q"].GetString()) * scale_offset);
	//"m": true,        // Is the buyer the market maker?
	trade.OrderBSFlag[0] = d["m"].GetBool() ? 'B' : 'S';
	on_trade(&trade);
}

void MDEngineBinance::on_lws_book_update(const char* data, size_t len, const std::string& ticker)
{
	//{"lastUpdateId":86947555,"bids":[["0.00000702","17966.00000000",[]],["0.00000701","111276.00000000",[]],["0.00000700","11730816.00000000",[]],["0.00000699","304119.00000000",[]],["0.00000698","337397.00000000",[]]],"asks":[["0.00000703","65956.00000000",[]],["0.00000704","213919.00000000",[]],["0.00000705","463226.00000000",[]],["0.00000706", "709268.00000000",[]],["0.00000707","78529.00000000",[]]]}

    Document d;
    d.Parse(data);

	LFPriceBook20Field md;
	memset(&md, 0, sizeof(md));

    bool has_update = false;	    	
	if(d.HasMember("bids"))
	{
		auto& bids = d["bids"];

		if(bids.IsArray() && bids.Size() >0)
		{
			auto size = std::min((int)bids.Size(), 20);
		
			for(int i = 0; i < size; ++i)
			{
				md.BidLevels[i].price = stod(bids.GetArray()[i][0].GetString()) * scale_offset;
				md.BidLevels[i].volume = stod(bids.GetArray()[i][1].GetString()) * scale_offset;
			}
			md.BidLevelCount = size;

			has_update = true;
		}
	}

	if(d.HasMember("asks"))
	{
		auto& asks = d["asks"];

		if(asks.IsArray() && asks.Size() >0)
		{
			auto size = std::min((int)asks.Size(), 20);
		
			for(int i = 0; i < size; ++i)
			{
				md.AskLevels[i].price = stod(asks.GetArray()[i][0].GetString()) * scale_offset;
				md.AskLevels[i].volume = stod(asks.GetArray()[i][1].GetString()) * scale_offset;
			}
			md.AskLevelCount = size;

			has_update = true;
		}
	}	
    
    if(has_update)
    {
        std::string strategy_ticker = coinPairWhiteList.GetKeyByValue(ticker);
        if(strategy_ticker.length() == 0) {
            KF_LOG_INFO(logger, "MDEngineBinance::on_lws_market_trade: not in WhiteList , ignore it:" << strategy_ticker);
            return;
        }

        strcpy(md.InstrumentID, strategy_ticker.c_str());
	    strcpy(md.ExchangeID, "binance");

	    on_price_book_update(&md);
	} 
}

void MDEngineBinance::set_reader_thread()
{
	IMDEngine::set_reader_thread();

   	rest_thread = ThreadPtr(new std::thread(boost::bind(&MDEngineBinance::loop, this)));
}

void MDEngineBinance::logout()
{
	lws_context_destroy( context );
   	KF_LOG_INFO(logger, "MDEngineBinance::logout:"); 	
}

void MDEngineBinance::release_api()
{
   KF_LOG_INFO(logger, "MDEngineBinance::release_api:"); 	
}

void MDEngineBinance::subscribeMarketData(const vector<string>& instruments, const vector<string>& markets)
{
	/* Connect if we are not connected to the server. */
   	KF_LOG_INFO(logger, "MDEngineBinance::subscribeMarketData:"); 	
}

void MDEngineBinance::GetAndHandleDepthResponse(const std::string& symbol, int limit) 
{
    const auto static url = "https://api.binance.com/api/v1/depth";
    const auto response = Get(Url{url}, Parameters{{"symbol", symbol},
                                                        {"limit",  to_string(limit)}});
    Document d;
    d.Parse(response.text.c_str());

	LFMarketDataField md;
	memset(&md, 0, sizeof(md));

    bool has_update = false;	    	
	if(d.HasMember("bids") && d["bids"].IsArray() && d["bids"].Size() >= limit)
	{
		md.BidPrice1 = stod(d["bids"].GetArray()[0][0].GetString()) * scale_offset;
		md.BidVolume1 = stod(d["bids"].GetArray()[0][1].GetString()) * scale_offset;
		md.BidPrice2 = stod(d["bids"].GetArray()[1][0].GetString()) * scale_offset;
		md.BidVolume2 = stod(d["bids"].GetArray()[1][1].GetString()) * scale_offset;
		md.BidPrice3 = stod(d["bids"].GetArray()[2][0].GetString()) * scale_offset;
		md.BidVolume3 = stod(d["bids"].GetArray()[2][1].GetString()) * scale_offset;
		md.BidPrice4 = stod(d["bids"].GetArray()[3][0].GetString()) * scale_offset;
		md.BidVolume4 = stod(d["bids"].GetArray()[3][1].GetString()) * scale_offset;
		md.BidPrice5 = stod(d["bids"].GetArray()[4][0].GetString()) * scale_offset;
		md.BidVolume5 = stod(d["bids"].GetArray()[4][1].GetString()) * scale_offset;
		
		has_update = true;
	}

	if(d.HasMember("asks") && d["asks"].IsArray() && d["asks"].Size() >= limit)
	{
		md.AskPrice1 = stod(d["asks"].GetArray()[0][0].GetString()) * scale_offset;
		md.AskVolume1 = stod(d["asks"].GetArray()[0][1].GetString()) * scale_offset;
		md.AskPrice2 = stod(d["asks"].GetArray()[1][0].GetString()) * scale_offset;
		md.AskVolume2 = stod(d["asks"].GetArray()[1][1].GetString()) * scale_offset;
		md.AskPrice3 = stod(d["asks"].GetArray()[2][0].GetString()) * scale_offset;
		md.AskVolume3 = stod(d["asks"].GetArray()[2][1].GetString()) * scale_offset;
		md.AskPrice4 = stod(d["asks"].GetArray()[3][0].GetString()) * scale_offset;
		md.AskVolume4 = stod(d["asks"].GetArray()[3][1].GetString()) * scale_offset;
		md.AskPrice5 = stod(d["asks"].GetArray()[4][0].GetString()) * scale_offset;
		md.AskVolume5 = stod(d["asks"].GetArray()[4][1].GetString()) * scale_offset;
		
		has_update = true;
	}
    
    if(has_update)
    {
        strcpy(md.InstrumentID, symbol.c_str());
	    strcpy(md.ExchangeID, "binance");
	    md.UpdateMillisec = last_rest_get_ts;

	    on_market_data(&md);
	} 
}

void MDEngineBinance::GetAndHandleTradeResponse(const std::string& symbol, int limit)
{
    const auto static url = "https://api.binance.com/api/v1/trades";
    const auto response = Get(Url{url}, Parameters{{"symbol", symbol},
		    {"limit",  to_string(limit)}});
    Document d;
    d.Parse(response.text.c_str());
    if(d.IsArray())
    {
	    LFL2TradeField trade;
	    memset(&trade, 0, sizeof(trade));
	    std::string symbols = "TRXBTC";
	    strcpy(trade.InstrumentID, symbols.c_str());
	    strcpy(trade.ExchangeID, "binance");

	    for(int i = 0; i < d.Size(); ++i)
	    {
		    const auto& ele = d[i];
		    if(!ele.HasMember("id"))
		    {
			continue;
		    }
		    
  		    const auto trade_id = ele["id"].GetUint64();
		    if(trade_id <= last_trade_id)
		    {
		    	continue;
		    }
		    
		    last_trade_id = trade_id;
		    if(ele.HasMember("price") && ele.HasMember("qty") && ele.HasMember("isBuyerMaker") && ele.HasMember("isBestMatch"))
		    {
			    trade.Price = std::stod(ele["price"].GetString()) * scale_offset;
			    trade.Volume = std::stod(ele["qty"].GetString()) * scale_offset;
			    trade.OrderKind[0] = ele["isBestMatch"].GetBool() ? 'B' : 'N';
			    trade.OrderBSFlag[0] = ele["isBuyerMaker"].GetBool() ? 'B' : 'S';
			    on_trade(&trade);
		    }
	    }
    }   
}


void MDEngineBinance::loop()
{
		while(isRunning)
		{
				/*
				using namespace std::chrono;
				auto current_ms = duration_cast< milliseconds>(system_clock::now().time_since_epoch()).count();
				if(last_rest_get_ts != 0 && (current_ms - last_rest_get_ts) < rest_get_interval_ms)
				{	
						continue;	
				}

				last_rest_get_ts = current_ms;

				for(auto& symbol : symbols)
				{
						GetAndHandleDepthResponse(symbol, book_depth_count);

						GetAndHandleTradeResponse(symbol, trade_count);
				}	
			    */
	
				lws_service( context, rest_get_interval_ms );
		}
}

BOOST_PYTHON_MODULE(libbinancemd)
{
    using namespace boost::python;
    class_<MDEngineBinance, boost::shared_ptr<MDEngineBinance> >("Engine")
    .def(init<>())
    .def("init", &MDEngineBinance::initialize)
    .def("start", &MDEngineBinance::start)
    .def("stop", &MDEngineBinance::stop)
    .def("logout", &MDEngineBinance::logout)
    .def("wait_for_stop", &MDEngineBinance::wait_for_stop);
}
