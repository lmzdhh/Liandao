#include "MDEngineBitmex.h"
#include "TypeConvert.hpp"
#include "Timer.h"
#include "longfist/LFUtils.h"
#include "longfist/LFDataStruct.h"

#include <document.h>
#include <stdio.h>
#include <stringbuffer.h>
#include <writer.h>

#include <chrono>
#include <iostream>
#include <string>

using rapidjson::Document;

USING_WC_NAMESPACE

static MDEngineBitmex* md_instance = nullptr;

// pick the callback function to process data received from server
static int eventCallback(struct lws* conn, enum lws_callback_reasons reason, void* user, void* data, size_t len)
{
    switch(reason)
    {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
        {
            std::cout << "received signal LWS_CALLBACK_CLIENT_ESTABLISHED" << std::endl;
            lws_callback_on_writable(conn);
            break;
        }
        case LWS_CALLBACK_PROTOCOL_INIT:
        {
            std::cout << "received signal LWS_CALLBACK_PROTOCOL_INIT" << std::endl;
            break;
        }
        case LWS_CALLBACK_CLIENT_RECEIVE:
        {
            std::cout << "received signal LWS_CALLBACK_CLIENT_RECEIVE" << std::endl;
	    if(md_instance)
	    {
	        md_instance->processData(conn, (const char*)data, len);
	    }
            break;
        }
        case LWS_CALLBACK_CLIENT_WRITEABLE:
        {
            std::cout << "received signal LWS_CALLBACK_CLIENT_WRITEABLE" << std::endl;
	    if(md_instance)
	    {
	        md_instance->subscribeChannel(conn);
	    }
	    break;
	}
        case LWS_CALLBACK_CLOSED:
        {
	    std::cout << "received signal LWS_CALLBACK_CLOSED" << std::endl;
            break;
	}
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
	{
	    std::cout << "received signal LWS_CALLBACK_CLIENT_CONNECTION_ERROR" << std::endl;
	    if(md_instance)
	    {
	        md_instance->handleConnectionError(conn);
	    }
	    break;
	}
        default:
        {
            break;
        }
    }

    return 0;
}

static struct lws_protocols protocols[] =
{
    {
        "example_protocol",
        eventCallback,
        0,
	65536,
    },
    {NULL, NULL, 0, 0} /* terminator */
};

struct session_data {
    int fd;
};

MDEngineBitmex::MDEngineBitmex(): IMDEngine(SOURCE_BITMEX)
{
    logger = yijinjing::KfLog::getLogger("MDEngine.BitMEX");

    KF_LOG_INFO(logger, "initiating MDEngineBitmex");
}

void MDEngineBitmex::load(const json& config)
{
    KF_LOG_INFO(logger, "loading config from json");

    rest_get_interval_ms = config["rest_get_interval_ms"].get<int>();

    whiteList.ReadWhiteLists(config, "whiteLists");
    whiteList.Debug_print();

    createSubscribeJsonStrings();
    debugPrint(subscribeJsonStrings);

    if(whiteList.Size() == 0)
    {
        KF_LOG_ERROR(logger, "subscribeCoinBaseQuote is empty please add whiteLists in kungfu.json like this");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"btcusdt\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"etceth\"");
        KF_LOG_ERROR(logger, "},");
    }
}

void MDEngineBitmex::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "connecting to BitMEX");
    connected = true;
}

void MDEngineBitmex::login(long timeout_nsec)
{
    KF_LOG_INFO(logger, "initiating BitMEX websocket");

    md_instance = this;

    int logs = LLL_ERR | LLL_DEBUG | LLL_WARN;
    lws_set_log_level(logs, NULL);
    
    KF_LOG_INFO(logger, "creating lws context");
    
    struct lws_context_creation_info creation_info;
    memset(&creation_info, 0, sizeof(creation_info));
    
    creation_info.port                     = CONTEXT_PORT_NO_LISTEN;
    creation_info.protocols                = protocols;
    creation_info.iface                    = NULL;
    creation_info.ssl_cert_filepath        = NULL;
    creation_info.ssl_private_key_filepath = NULL;
    creation_info.extensions               = NULL;
    creation_info.gid                      = -1;
    creation_info.uid                      = -1;
    creation_info.options                 |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    creation_info.fd_limit_per_thread      = 1024;
    creation_info.max_http_header_pool     = 1024;
    creation_info.ws_ping_pong_interval    = 10;
    creation_info.ka_time                  = 10;
    creation_info.ka_probes                = 10;
    creation_info.ka_interval              = 10;

    context = lws_create_context(&creation_info);

    KF_LOG_INFO(logger, "lws context created");
    
    KF_LOG_INFO(logger, "creating initial lws connection");

    struct lws_client_connect_info connect_info = {0};
    std::string host = "www.bitmex.com";
    std::string path = "/realtime";
    int port = 443;

    connect_info.context        = context;
    connect_info.address        = host.c_str();
    connect_info.port           = port;
    connect_info.path           = path.c_str();
    connect_info.host           = host.c_str();
    connect_info.origin         = host.c_str();
    connect_info.protocol       = protocols[0].name;
    connect_info.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
    
    struct lws* conn = lws_client_connect_via_info(&connect_info);
    
    KF_LOG_INFO(logger, "connecting to " << connect_info.host << ":" << connect_info.port << ":" << connect_info.path);

    if(!conn)
    {
	KF_LOG_INFO(logger, "error creating initial lws connection");
    }

    KF_LOG_INFO(logger, "done initiating and creating initial lws connection");

    logged_in = true;
}

void MDEngineBitmex::logout()
{
    KF_LOG_INFO(logger, "logging out");
}

void MDEngineBitmex::release_api()
{
    KF_LOG_INFO(logger, "releasing API");
}

void MDEngineBitmex::subscribeMarketData(const vector<string>& instruments, const vector<string>& markets)
{
    KF_LOG_INFO(logger, "MDEngineBitmex::subscribeMarketData:");
}

void MDEngineBitmex::set_reader_thread()
{
    KF_LOG_INFO(logger, "setting reader thread");

    IMDEngine::set_reader_thread();
    read_thread = ThreadPtr(new std::thread(boost::bind(&MDEngineBitmex::enterEventLoop, this)));
}

std::string MDEngineBitmex::createOrderbookJsonString(std::string symbol)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("op");
    writer.String("subscribe");
    writer.Key("args");
    std::string str = "orderBook10:" + symbol;
    writer.String(str.c_str());
    writer.EndObject();
    return buffer.GetString();
}

std::string MDEngineBitmex::createTradeJsonString(std::string symbol)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("op");
    writer.String("subscribe");
    writer.Key("args");
    std::string str = "trade:" + symbol;
    writer.String(str.c_str());
    writer.EndObject();
    return buffer.GetString();
}

void MDEngineBitmex::createSubscribeJsonStrings()
{
    std::unordered_map<std::string, std::string>::iterator iter = whiteList.GetKeyIsStrategyCoinpairWhiteList().begin();
    for( ; iter != whiteList.GetKeyIsStrategyCoinpairWhiteList().end(); iter++)
    {
        KF_LOG_DEBUG(logger, "creating subscribe json string for strategy symbol " << iter->first << ", market symbol " << iter->second);
        subscribeJsonStrings.push_back(createOrderbookJsonString(iter->second));
        subscribeJsonStrings.push_back(createTradeJsonString(iter->second));
    }
}

void MDEngineBitmex::debugPrint(std::vector<std::string> &jsons)
{
    KF_LOG_INFO(logger, "printing out all subscribe json strings");

    for (size_t count = 0; count < jsons.size(); count++)
    {
        KF_LOG_INFO(logger, "json string: " << jsons[count]);
    }
}

void MDEngineBitmex::subscribeChannel(struct lws* conn)
{
    KF_LOG_INFO(logger, "subscribe to channel #" << num_subscribed);

    if(subscribeJsonStrings.size() == 0)
    {
	KF_LOG_INFO(logger, "there is no channel to subscribe to");
	return;
    }
    else if(num_subscribed >= subscribeJsonStrings.size())
    {
        KF_LOG_INFO(logger, "no more channel to subscribe to");
	return;
    }

    unsigned char message[512];
    memset(&message[LWS_PRE], 0, 512 - LWS_PRE);

    std::string json = subscribeJsonStrings[num_subscribed++];
    int length = json.length();
    strncpy((char *)message + LWS_PRE, json.c_str(), length);

    KF_LOG_INFO(logger, "subscribing to " << json);
    
    lws_write(conn, &message[LWS_PRE], length, LWS_WRITE_TEXT);

    KF_LOG_INFO(logger, "subscribed to " << json);

    if(num_subscribed < subscribeJsonStrings.size())
    {
        lws_callback_on_writable(conn);
        KF_LOG_INFO(logger, "there are more channels to subscribe to");
    }
}

void MDEngineBitmex::enterEventLoop()
{
    KF_LOG_INFO(logger, "enterint event loop");
    
    while(1)
    {
        lws_service(context, 500);
    }
}

void MDEngineBitmex::handleConnectionError(struct lws* conn)
{
    KF_LOG_ERROR(logger, "having connection error");
    KF_LOG_ERROR(logger, "trying to login again");

    logged_in = false;
    num_subscribed = 0;
    
    priceBook.clearPriceBook();

    long timeout_nsec = 0;
    login(timeout_nsec);
}

void MDEngineBitmex::processData(struct lws* conn, const char* data, size_t len)
{
    KF_LOG_INFO(logger, "received data from BitMEX: " << data);

    Document json;
    json.Parse(data);

    if(!json.HasParseError() && json.IsObject() && json.HasMember("table") && json["table"].IsString())
    {
        if(strcmp(json["table"].GetString(), "orderBook10") == 0)
	{
            KF_LOG_INFO(logger, "received data is orderBook");
	    processOrderbookData(json);
	}
	else if(strcmp(json["table"].GetString(), "trade") == 0)
	{
            KF_LOG_INFO(logger, "received data is trade");
	    processTradingData(json);
	}
	else
	{
            KF_LOG_INFO(logger, "received data is unknown");
	}
    }
}

void MDEngineBitmex::processOrderbookData(Document& json)
{
    KF_LOG_INFO(logger, "processing orderbook data");

    std::string symbol;
    std::string ticker;

    if(json.HasMember("action"))
    {
        std::string action = json["action"].GetString();

	// upon subscription, an image of the existing data will be received through
	// a partial action, so you can get started and apply deltas after that
        if(action == "partial")
	{
	    received_partial = true;
	    KF_LOG_INFO(logger, "received orderbook is a partial action");
	       
	    if(json.HasMember("data"))
	    {
	        auto& data = json["data"];
		if(data.IsArray() && data.Size() > 0)
		{
		    for(int count = 0; count < data.Size(); count++)
		    {
		        auto& update = data.GetArray()[count];
			// each partial action table row contains symbol, id, side, size, and price field
			symbol = update["symbol"].GetString();
			ticker = whiteList.GetKeyByValue(symbol);
			if(ticker.empty())
			{
			    KF_LOG_INFO(logger, "received orderbook symbol not in white list");
			    continue;
			}
			KF_LOG_INFO(logger, "symbol is " << symbol << " and ticker is " << ticker);

			auto& bids = update["bids"];
			if(bids.IsArray() && bids.Size() > 0)
			{
			    for(int bid = 0; bid < bids.Size(); bid++)
			    {
				auto& curr = bids.GetArray()[bid];
				int64_t price = std::round(curr.GetArray()[0].GetDouble() * scale_offset);
				uint64_t amount = std::round(curr.GetArray()[1].GetDouble() * scale_offset);
				KF_LOG_INFO(logger, "bid update: price " << price << " and amount " << amount);
				priceBook.UpdateBidPrice(ticker, price, amount);
			    }
			}

			auto& asks = update["asks"];
			if(asks.IsArray() && asks.Size() > 0)
			{
			    for(int ask = 0; ask < asks.Size(); ask++)
			    {
				auto& curr = asks.GetArray()[ask];
				int64_t price = std::round(curr.GetArray()[0].GetDouble() * scale_offset);
				uint64_t amount = std::round(curr.GetArray()[1].GetDouble() * scale_offset);
				KF_LOG_INFO(logger, "ask update: price " << price << " and amount " << amount);
				priceBook.UpdateAskPrice(ticker, price, amount);
			    }
			}
		    }
		}
	    }	    
	}
	// other messages may be received before the partial action comes through
	// in that case drop any messages received until partial action has been received
	else if(!received_partial)
	{
	    KF_LOG_INFO(logger, "have not received first partial action for orderbook");
	    KF_LOG_INFO(logger, "drop any messages received until partial action has been received");
	}
	else if(action == "update" || action == "insert" || action == "delete")
	{
	    KF_LOG_INFO(logger, "received orderbook is a " << action << " action");

	    if(json.HasMember("data"))
	    {
	        auto& data = json["data"];
		if(data.IsArray() && data.Size() > 0)
		{   
		    for(int count = 0; count < data.Size(); count++)
		    {
		        auto& update = data.GetArray()[count];
			
			symbol = update["symbol"].GetString();
			ticker = whiteList.GetKeyByValue(symbol);
			if(ticker.empty())
			{
			    KF_LOG_INFO(logger, "received orderbook symbol not in white list");
			    continue;
			}
			KF_LOG_INFO(logger, "symbol is " << symbol << " and ticker is " << ticker);

			auto& bids = update["bids"];
			if(bids.IsArray() && bids.Size() > 0)
			{
			    for(int bid = 0; bid < bids.Size(); bid++)
			    {
				auto& curr = bids.GetArray()[bid];
				int64_t price = std::round(curr.GetArray()[0].GetDouble() * scale_offset);
				uint64_t amount = std::round(curr.GetArray()[1].GetDouble() * scale_offset);
				KF_LOG_INFO(logger, "bid update: price " << price << " and amount " << amount);
				priceBook.UpdateBidPrice(ticker, price, amount);
			    }
			}

			auto& asks = update["asks"];
			if(asks.IsArray() && asks.Size() > 0)
			{
			    for(int ask = 0; ask < asks.Size(); ask++)
			    {
				auto& curr = asks.GetArray()[ask];
				int64_t price = std::round(curr.GetArray()[0].GetDouble() * scale_offset);
				uint64_t amount = std::round(curr.GetArray()[1].GetDouble() * scale_offset);
				KF_LOG_INFO(logger, "ask update: price " << price << " and amount " << amount);
				priceBook.UpdateAskPrice(ticker, price, amount);
			    }
			}
		    }
		}
	    }
	}
	else
	{
	    KF_LOG_INFO(logger, "received orderbook has invalid action");
	}
    }
    else
    {
	KF_LOG_INFO(logger, "received orderbook does not have action");
    }

    LFPriceBook20Field update;
    memset(&update, 0, sizeof(update));
    if(priceBook.Assembler(ticker, update))
    {
        strcpy(update.ExchangeID, "bitmex");
	KF_LOG_INFO(logger, "sending out orderbook");
	on_price_book_update(&update);
    }
}

void MDEngineBitmex::processTradingData(Document& json)
{
    KF_LOG_INFO(logger, "processing trade data");

    if(json.HasMember("data"))
    {
        auto& data = json["data"];
	if(data.IsArray() && data.Size() > 0)
	{
	    for(int count = 0; count < data.Size(); count++)
	    {
	        auto& update = data.GetArray()[count];
		std::string symbol = update["symbol"].GetString();
		std::string ticker = whiteList.GetKeyByValue(symbol);
		if(ticker.empty())
		{
		    KF_LOG_INFO(logger, "received trade symbol not in white list");
		    continue;
		}
		KF_LOG_INFO(logger, "symbol is " << symbol << " and ticker is " << ticker);

		LFL2TradeField trade;
		memset(&trade, 0, sizeof(trade));
		strcpy(trade.InstrumentID, ticker.c_str());
		strcpy(trade.ExchangeID, "bitmex");

		int64_t price = std::round(update["price"].GetDouble() * scale_offset);
		uint64_t amount = std::round(update["size"].GetDouble() * scale_offset);
		std::string side = update["side"].GetString();

		trade.Price = price;
	        trade.Volume = amount;
		trade.OrderBSFlag[0] = side == "Buy" ? 'B' : 'S';

		KF_LOG_INFO(logger, "ticker " << ticker << " traded at price " << trade.Price << " with volume " << trade.Volume << " as a " << side);

		on_trade(&trade);				 
	    }
	}
    }
}

BOOST_PYTHON_MODULE(libbitmexmd)
{
    using namespace boost::python;
    class_<MDEngineBitmex, boost::shared_ptr<MDEngineBitmex> >("Engine")
    .def(init<>())
    .def("init", &MDEngineBitmex::initialize)
    .def("start", &MDEngineBitmex::start)
    .def("stop", &MDEngineBitmex::stop)
    .def("logout", &MDEngineBitmex::logout)
    .def("wait_for_stop", &MDEngineBitmex::wait_for_stop);
}
