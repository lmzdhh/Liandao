//
// Created by xiaoning on 12/5/18.
//
#include "MDEngineDaybit.h"
#include <stringbuffer.h>
#include <writer.h>
#include <document.h>
#include <libwebsockets.h>
#include <algorithm>
#include <stdio.h>
#include "../../utils/common/Utils.h"
using rapidjson::Document;
using namespace rapidjson;
using namespace kungfu;
using namespace std;

#define SCALE_OFFSET 1e8
#define	SUBS_ORDERBOOK "/subscription:order_books;"
#define SUBS_TRADE	"/subscription:trades;"
#define SUBS_MARKETS	"/subscription:markets"
#define TOPIC_API	"/api"
#define TOPIC_PHOENIX	"phoenix"
#define WSS_TIMEOUT	-1

WC_NAMESPACE_START

std::mutex m_mutex;

//lws event function
static int lwsEventCallback( struct lws *conn, enum lws_callback_reasons reason, void *user, void* data, size_t len );
static  struct lws_protocols  lwsProtocols [] {{"md-protocol", lwsEventCallback, 0, 65536,}, { NULL, NULL, 0, 0 }};

MDEngineDaybit* MDEngineDaybit::m_instance = nullptr;

MDEngineDaybit::MDEngineDaybit(): IMDEngine(SOURCE_DAYBIT)
{
	
    logger = yijinjing::KfLog::getLogger("MdEngine.Daybit");
    KF_LOG_DEBUG(logger, "MDEngineDaybit construct");
}

MDEngineDaybit::~MDEngineDaybit()
{
    if (m_thread)
    {
        if(m_thread->joinable())
        {
            m_thread->join();
        }
    }

	if (m_heartBeatThread)
    {
        if(m_heartBeatThread->joinable())
        {
            m_heartBeatThread->join();
        }
    }
    KF_LOG_DEBUG(logger, "MDEngineDaybit deconstruct");
}

void MDEngineDaybit::set_reader_thread()
{
    IMDEngine::set_reader_thread();
    m_thread = ThreadPtr(new std::thread(boost::bind(&MDEngineDaybit::lwsEventLoop, this)));
	m_heartBeatThread = ThreadPtr(new std::thread(boost::bind(&MDEngineDaybit::heartBeatLoop, this)));
}

void MDEngineDaybit::reset()
{
	KF_LOG_DEBUG(logger, "MDEngineDaybit reset");
	m_joinRef = 1;
	m_ref = 1;
    m_logged_in = true;

	//1. create serverTime json
	int64_t joinRef = 0;
	this->phxClear();
	string subscription = genServerTimeJoin(joinRef);
	KF_LOG_INFO(logger, "getServerTime join:" << subscription);
	this->phxPush(subscription);

	subscription = genServerTimeReq(joinRef);
	KF_LOG_INFO(logger, "getServerTime request:" << subscription);
	this->phxPush(subscription);

	//2. create marketData json
	subscription = genMarketDataJoin(joinRef);
	KF_LOG_INFO(logger, "genMarketDataJoin join:" << subscription);
	this->phxPush(subscription);

	subscription = genMarketDataReq(joinRef);
	KF_LOG_INFO(logger, "genMarketDataReq request:" << subscription);
	this->phxPush(subscription);
}

void MDEngineDaybit::load(const json& config)
{
    KF_LOG_INFO(logger, "load config start");
    try
    {
        m_priceBookNum = config["book_depth_count"].get<int>();
		m_tradeNum = config["trade_count"].get<int>();
		m_url = config["baseUrl"].get<string>();
		m_path = config["path"].get<string>();
		
        m_whiteList.ReadWhiteLists(config, "whiteLists");
        m_whiteList.Debug_print();
    }
    catch (const std::exception& e)
    {
        KF_LOG_INFO(logger, "load config exception,"<<e.what());
    }
	
    KF_LOG_INFO(logger, "load config end");
	return;
}

void MDEngineDaybit::genSubscribeJson()
{
	int64_t joinRef = 0;
	string subscription;
	
	this->phxClear();
    auto& symbol_map = m_whiteList.GetKeyIsStrategyCoinpairWhiteList();
    for(const auto& var : symbol_map) {
		//joinRef = this->makeJoinRef();
		subscription = this->genOrderbookJoin(var.second, joinRef);
		KF_LOG_DEBUG(logger, "genOrderbookJoin:" << subscription);
		if (subscription.empty()) continue;

		this->phxPush(subscription);

		subscription = this->genOrderbookReq(var.second, joinRef);
		KF_LOG_DEBUG(logger, "genOrderbookReq:" << subscription);
		this->phxPush(subscription);

		//joinRef = this->makeJoinRef();
		subscription = this->genTradeJoin(var.second, joinRef);
		this->phxPush(subscription);
		KF_LOG_DEBUG(logger, "genTradeJoin:" << subscription);

		subscription = this->genTradeReq(var.second, joinRef);
		this->phxPush(subscription);
		KF_LOG_DEBUG(logger, "genTradeReq:" << subscription);
	}

	return;
}

inline int64_t MDEngineDaybit::getTimestamp()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
							std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp;
}

/**
format:
  {"join_ref": "1", "ref": "1", "topic": "/subscription:order_books;USDT;BTC;0.20000000", 
  "event": "phx_join", "payload": {}, "timeout": 3000}
**/
std::string MDEngineDaybit::genOrderbookJoin(const std::string& symbol, int64_t& nJoinRef)
{	 
	nJoinRef = this->makeRef();
	
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	writer.StartObject();
	writer.Key("join_ref");
	writer.String(std::to_string(nJoinRef).c_str());

	writer.Key("ref");
	writer.String(std::to_string(nJoinRef).c_str());

	writer.Key("topic");
	string topic = SUBS_ORDERBOOK;
	string tickPrice = this->getTickPrice(symbol);
	if (tickPrice.empty()) {
		KF_LOG_ERROR(logger, "find tick_price fail, symbol:" << symbol);
		return "";
	}
	topic.append(symbol).append(";").append(tickPrice);
	writer.String(topic.c_str());

	writer.Key("event");
	writer.String("phx_join");
	
	writer.Key("payload");
	writer.StartObject();
	writer.Key("timeout");
	writer.Int(WSS_TIMEOUT);
	writer.EndObject();
	
	writer.Key("timeout");
	writer.Int(WSS_TIMEOUT);
	writer.EndObject();

	return buffer.GetString();
}

/**
format:
	{"join_ref": "1", "ref": "2", "topic": "/subscription:order_books;USDT;BTC;0.20000000", 
	"event": "request", "payload": {"timestamp": 1544018438331}, "timeout": 3000}
**/
std::string MDEngineDaybit::genOrderbookReq(const std::string& symbol, int64_t nJoinRef)
{
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	writer.StartObject();
	writer.Key("join_ref");
	writer.String(std::to_string(nJoinRef).c_str());
	
	writer.Key("ref");
	writer.String(std::to_string(this->makeRef()).c_str());
	
	writer.Key("topic");
	string topic = SUBS_ORDERBOOK;
	string tickPrice = this->getTickPrice(symbol);
	if (tickPrice.empty()) {
		KF_LOG_ERROR(logger, "find tick_price fail, symbol:" << symbol);
		return "";
	}
	topic.append(symbol).append(";").append(tickPrice);
	writer.String(topic.c_str());
	
	writer.Key("event");
	writer.String("request");
	
	writer.Key("payload");
	writer.StartObject();
	writer.Key("timestamp");
	writer.Int64(this->getTimestamp() + m_timeDiffWithServer);
	writer.Key("timeout");
	writer.Int(WSS_TIMEOUT);
	writer.EndObject();
	
	writer.Key("timeout");
	writer.Int(WSS_TIMEOUT);
	writer.EndObject();
	
	return buffer.GetString();

}

/**
{"join_ref": "1", "ref": "1", "topic": "/subscription:trades;USDT;BTC", "event": "phx_join", "payload": {}, 
	"timeout": 3000}
**/
std::string MDEngineDaybit::genTradeJoin(const std::string& symbol, int64_t& nJoinRef)
{
	nJoinRef = this->makeRef();

	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	writer.StartObject();
	writer.Key("join_ref");
	writer.String(std::to_string(nJoinRef).c_str());
	
	writer.Key("ref");
	writer.String(std::to_string(nJoinRef).c_str());
	
	writer.Key("topic");
	string topic = SUBS_TRADE;
	topic.append(symbol);
	writer.String(topic.c_str());
	
	writer.Key("event");
	writer.String("phx_join");
	
	writer.Key("payload");
	writer.StartObject();
	writer.Key("timeout");
	writer.Int(WSS_TIMEOUT);
	writer.EndObject();
	
	writer.Key("timeout");
	writer.Int(WSS_TIMEOUT);
	writer.EndObject();
	
	return buffer.GetString();
}


/**
format: 
	{"join_ref": "1", "ref": "2", "topic": "/subscription:trades;USDT;BTC", "event": "request",
	"payload": {"size": 10, "timestamp": 1544019236283}, "timeout": 3000}
**/
std::string MDEngineDaybit::genTradeReq(const std::string& symbol, int64_t nJoinRef)
{ 	
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	writer.StartObject();
	writer.Key("join_ref");
	writer.String(std::to_string(nJoinRef).c_str());

	writer.Key("ref");
	writer.String(std::to_string(this->makeRef()).c_str());

	writer.Key("topic");
	string topic = SUBS_TRADE;
	topic.append(symbol);
	writer.String(topic.c_str());

	writer.Key("event");
	writer.String("request");

	writer.Key("payload");
	writer.StartObject();
	writer.Key("size");
	writer.Int(m_tradeNum);
	writer.Key("timestamp");
	writer.Int64(this->getTimestamp() + m_timeDiffWithServer);
	writer.Key("timeout");
	writer.Int(WSS_TIMEOUT);
	writer.EndObject();

	writer.Key("timeout");
	writer.Int(WSS_TIMEOUT);
	writer.EndObject();

	return buffer.GetString();
}

void MDEngineDaybit::connect(long)
{
    KF_LOG_INFO(logger, "connect");
    m_connected = true;
}

void MDEngineDaybit::login(long)
{
    KF_LOG_DEBUG(logger, "create context start");
    m_instance = this;
	reset();
    struct lws_context_creation_info creation_info;
    memset(&creation_info, 0x00, sizeof(creation_info));
    creation_info.port = CONTEXT_PORT_NO_LISTEN;
    creation_info.protocols = lwsProtocols;
    creation_info.gid = -1;
    creation_info.uid = -1;
    creation_info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    creation_info.max_http_header_pool = 1024;
    creation_info.fd_limit_per_thread = 1024;
    m_lwsContext = lws_create_context( &creation_info );
    if (!m_lwsContext)
    {
        KF_LOG_ERROR(logger, "create context error");
        return;
    }
    KF_LOG_INFO(logger, "create context success");

	//connect to Daybit platform
    createConnection();
}

void MDEngineDaybit::createConnection()
{
    KF_LOG_DEBUG(logger, "create connect start");
    struct lws_client_connect_info conn_info = { 0 };
    //parse uri
    conn_info.context 	= m_lwsContext;
    conn_info.address = m_url.c_str();
    conn_info.path 	= m_path.c_str();
    conn_info.port = 443;
    conn_info.protocol = lwsProtocols[0].name;
    conn_info.host 	= conn_info.address;
    conn_info.origin = conn_info.address;
    conn_info.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
    m_lwsConnection = lws_client_connect_via_info(&conn_info);
    if(!m_lwsConnection)
    {
        KF_LOG_INFO(logger, "create connect error");
        return ;
    }
    KF_LOG_INFO(logger, "connect to "<< conn_info.protocol<< conn_info.address<< ":"<< conn_info.port
    			<< conn_info.path <<" success");				
}

void MDEngineDaybit::logout()
{
    lws_context_destroy(m_lwsContext);
    m_logged_in = false;
    KF_LOG_INFO(logger, "logout");
}

void MDEngineDaybit::lwsEventLoop()
{
    while(isRunning)
    {
        lws_service(m_lwsContext, 500);
    }
}

void MDEngineDaybit::heartBeatLoop()
{
    while(isRunning) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
		std::string message = this->genHeartBeatJson();
		this->phxPush(message);
		lws_callback_on_writable(m_lwsConnection);
    }
}

void MDEngineDaybit::sendMessage(std::string&& msg)
{
    msg.insert(msg.begin(),  LWS_PRE, 0x00);
    lws_write(m_lwsConnection, (uint8_t*)(msg.data() + LWS_PRE), msg.size() - LWS_PRE, LWS_WRITE_TEXT);
}

void MDEngineDaybit::onMessage(struct lws* conn, char* data, size_t len)
{
	if (conn == NULL || data == NULL) {
		KF_LOG_DEBUG(logger, "data format error!");
		return;
	}
	
    KF_LOG_DEBUG(logger, "received data from Daybit start, data: " << data);
    try
    {
        if(!isRunning)
        {
            return;
        }
		
        Document json;
        json.Parse(data);
        if(json.HasParseError())
        {
            KF_LOG_ERROR(logger, "received data from Daybit failed,json parse error");
            return;
        }
		
		if (!json.HasMember("payload") || !json.HasMember("topic")) {
			KF_LOG_ERROR(logger, "unkown json response.");
            return;
		}
		
		string coinPair;
		string topic = json["topic"].GetString();
		if (topic == TOPIC_PHOENIX) {
			KF_LOG_INFO(logger, "receive heartbeat response.");
			return;
		} else if (topic == TOPIC_API) {
		 	KF_LOG_INFO(logger, "begin to handler serverTime topic.");
			serverTimeHandler(json);
			return;
    	} else if (topic == SUBS_MARKETS) {
			KF_LOG_INFO(logger, "begin to handler serverTime topic.");
			marketDataHandler(json);
			return;
		}

		std::vector<std::string> array = LDUtils::split(topic, ";");
		if (array.size() >= 3) {
			coinPair.append(array[1]);
			coinPair.append(";");
			coinPair.append(array[2]);
		} else {
			KF_LOG_ERROR(logger, "topic execption, topic: "  << topic);
            return;
		}
		
		string instrument = m_whiteList.GetKeyByValue(coinPair);
	    if(instrument.empty()) {
	         KF_LOG_DEBUG(logger, "whiteList has no this {symbol:"<<coinPair<<"}");
	         return;
	    }
		
		if (topic.find(SUBS_ORDERBOOK) != string::npos) {		
			orderbookHandler(json, instrument);
		} else if (topic.find(SUBS_TRADE) != string::npos) {
			tradeHandler(json, instrument);
		}
		
    }
    catch(const std::exception& e)
    {
        KF_LOG_ERROR(logger, "received data from Daybit exception,{error:" << e.what() << "}");
    }
    catch(...)
    {
        KF_LOG_ERROR(logger, "received data from Daybit system exception");
    }
    KF_LOG_DEBUG(logger, "received data from Daybit end");
}

void MDEngineDaybit::onClose(struct lws* conn)
{
    if(isRunning)
    {
		//reLogin
        login(0);
    }
    if(!m_logged_in)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }

	return;
}

void MDEngineDaybit::onWrite(struct lws* conn)
{
    if(!isRunning)
    {
        return;
    }
    KF_LOG_DEBUG(logger, "subscribe start");

	std::string message = this->phxPop();
	KF_LOG_DEBUG(logger, "subscribe message: " << message);
	if (!message.empty()) {
		sendMessage(std::move(message));
		lws_callback_on_writable(conn);
		return;
	}
	
    KF_LOG_DEBUG(logger, "subscribe end");
}

void MDEngineDaybit::orderbookInsertNotify(const rapidjson::Value& data, const std::string& instrument)
{	
	double sellVol, buyVol, minPrice, maxPrice;
	int64_t price, volumn;
	for (size_t i = 0; i < data.Size(); ++i) {
		auto& val = data[i];
		sellVol = std::stod(val["sell_vol"].GetString());
		buyVol = std::stod(val["buy_vol"].GetString());
		minPrice = std::stod(val["min_price"].GetString());
		maxPrice = std::stod(val["max_price"].GetString());
		
		if (sellVol > 0) {
			price = std::round(maxPrice * SCALE_OFFSET);
			volumn = std::round(sellVol * SCALE_OFFSET);
			priceBook20Assembler.UpdateBidPrice(instrument, price, volumn);
		}
		
		if (buyVol > 0) {
			price = std::round(minPrice * SCALE_OFFSET);
			volumn = std::round(buyVol * SCALE_OFFSET);
			priceBook20Assembler.UpdateAskPrice(instrument, price, volumn);
		}
	}

    // has any update
    LFPriceBook20Field md;
    memset(&md, 0, sizeof(md));
    if(priceBook20Assembler.Assembler(instrument, md)) {
		strncpy(md.ExchangeID, "daybit", std::min<size_t>(sizeof(md.ExchangeID)-1, 6));

        KF_LOG_INFO(logger, "MDEngineDaybit::orderbookInsertNotify: on_price_book_update");
        on_price_book_update(&md);
    }

	return;
}

void MDEngineDaybit::orderbookInitNotify(const rapidjson::Value& data, const std::string& instrument)
{
	double sellVol, buyVol, minPrice, maxPrice;
    LFPriceBook20Field priceBook {0};
	KF_LOG_DEBUG(logger, "orderbook size: " << data.Size() << " priceBookNum: " << m_priceBookNum);
		
	strncpy(priceBook.ExchangeID, "daybit", std::min<size_t>(sizeof(priceBook.ExchangeID)-1, 6));
	strncpy(priceBook.InstrumentID, instrument.c_str(), std::min(sizeof(priceBook.InstrumentID)-1, instrument.size()));
	for (size_t i = 0; i < std::min((int)data.Size(), m_priceBookNum); ++i) {
		auto& val = data[i];
		sellVol = std::stod(val["sell_vol"].GetString());
		buyVol = std::stod(val["buy_vol"].GetString());
		minPrice = std::stod(val["min_price"].GetString());
		maxPrice = std::stod(val["max_price"].GetString());

		KF_LOG_DEBUG(logger, "orderbook buyVol : " << buyVol << ", sellVol :" << sellVol);
		if (sellVol > 0) {
			KF_LOG_DEBUG(logger, "orderbook sellVal : " << sellVol);
			priceBook.BidLevels[i].price = std::round(maxPrice * SCALE_OFFSET);
			priceBook.BidLevels[i].volume = std::round(sellVol * SCALE_OFFSET);		
		}
		
		if (buyVol > 0) {
			KF_LOG_DEBUG(logger, "orderbook buyVol : " << buyVol);
			priceBook.AskLevels[i].price = std::round(minPrice * SCALE_OFFSET);
			priceBook.AskLevels[i].volume = std::round(buyVol * SCALE_OFFSET);
		}
	}
	
	on_price_book_update(&priceBook);
	return;
}


/**
{
    "topic": "/subscription:order_books;USDT;BTC;0.20000000",
    "ref": "2",
    "payload": {
        "status": "ok",
        "response": {
            "data": [
                {
                    "data": [
                        {
                            "sell_vol": "0.02418000",
                            "quote": "USDT",
                            "min_price": "6709.80000000",
                            "max_price": "6710.00000000",
                            "intvl": "0.20000000",
                            "buy_vol": "0.00000000",
                            "base": "BTC"
                        },
                        {
                            "sell_vol": "0.00158000",
                            "quote": "USDT",
                            "min_price": "6599.80000000",
                            "max_price": "6600.00000000",
                            "intvl": "0.20000000",
                            "buy_vol": "0.00000000",
                            "base": "BTC"
                        }
                    ],
                    "action": "init"
                }
            ]
        }
    },
    "event": "phx_reply"
}

{
    "topic": "/subscription:trades;USDT;BTC",
    "ref": "2",
    "payload": {
        "status": "ok",
        "response": {
            "data": [
                {
                    "data": [
                        {
                            "taker_sold": false,
                            "quote_amount": "116.00000000",
                            "quote": "USDT",
                            "price": "4000.00000000",
                            "id": 22345304,
                            "exec_at": 1544006367236,
                            "base_amount": "0.02900000",
                            "base": "BTC"
                        },
                        {
                            "taker_sold": true,
                            "quote_amount": "116.00000000",
                            "quote": "USDT",
                            "price": "4000.00000000",
                            "id": 22345283,
                            "exec_at": 1544006349951,
                            "base_amount": "0.02900000",
                            "base": "BTC"
                        }
                    ],
                    "action": "init"
                }
            ]
        }
    },
    "event": "phx_reply"
}
**/
void MDEngineDaybit::orderbookHandler(const rapidjson::Document& json, const std::string& instrument)
{
    try
    {
        KF_LOG_DEBUG(logger, "orderbookHandler start");
        auto& payload = json["payload"];

		if (!payload.HasMember("status") || !payload.HasMember("response")) {
			return;
		}
		
		string status = payload["status"].GetString();
        if (status != "ok") {
            return;
        }
		
        auto& response = payload["response"];
        if (!response.HasMember("data")) {
            return;
        }

		auto& outer =response["data"];
		if (!outer.IsArray()) {
			return;
		}

		auto& inner = outer[0];
		if (!inner.HasMember("data")) {
            return;
        }

		string action = inner["action"].GetString();
		const rapidjson::Value& data = inner["data"];
		if (!data.IsArray()) {
			return;
		}
		
		if(action == "init") {
			orderbookInitNotify(data, instrument);
		} else if (action == "upsert") {
			orderbookInsertNotify(data, instrument);
		}
		
		KF_LOG_DEBUG(logger, "orderbookHandler end.");
    }
    catch (const std::exception& e)
    {
        KF_LOG_INFO(logger, "orderbookHandler,{error:"<< e.what()<<"}");
    }

	return;
}

void MDEngineDaybit::tradeHandler(const rapidjson::Document& json, const std::string& instrument)
{
    try
    {
    	double  price, base_amount;
		
        KF_LOG_DEBUG(logger, "tradeHandler start");
        auto& payload = json["payload"];

		if (!payload.HasMember("status") || !payload.HasMember("response")) {
			return;
		}
		
		string status = payload["status"].GetString();
        if (status != "ok") {
            return;
        }
		
        auto& response = payload["response"];
        if (!response.HasMember("data")) {
            return;
        }

		auto& outer =response["data"];
		if (!outer.IsArray()) {
			return;
		}

		auto& inner = outer[0];
		if (!inner.HasMember("data")) {
            return;
        }

		const rapidjson::Value& data = inner["data"];
		if (!data.IsArray()) {
			return;
		}
		
		LFL2TradeField trade;
		for (size_t i = 0; i < std::min((int)data.Size(), m_tradeNum); ++i) {
			memset(&trade, 0, sizeof(trade));
	        strncpy(trade.ExchangeID, "daybit", std::min((int)sizeof(trade.ExchangeID)-1, 6));
	        strncpy(trade.InstrumentID, instrument.c_str(),std::min(sizeof(trade.InstrumentID)-1, instrument.size()));
	        auto& val = data[i];
			price = std::stod(val["price"].GetString());
			base_amount = std::stod(val["base_amount"].GetString());
	        trade.Volume = std::round(base_amount * SCALE_OFFSET);
	        trade.Price = std::round(price * SCALE_OFFSET);
	        bool sell = val["taker_sold"].GetBool();
	        trade.OrderBSFlag[0] = sell ? 'S' : 'B';
	        on_trade(&trade);
		}

		KF_LOG_DEBUG(logger, "tradeHandler end");
    }
    catch (const std::exception& e)
    {
        KF_LOG_INFO(logger, "tradeHandler,{error:"<< e.what()<<"}");
    }

	return;
}

void MDEngineDaybit::serverTimeHandler(const rapidjson::Document& json)
{
	auto& payload = json["payload"];
	if (payload.HasMember("status") && payload.HasMember("response")) {
		std::string status = payload["status"].GetString();
		auto& response = payload["response"];
		if(status == "ok") {
			if (response.IsObject() && response.HasMember("data")) {
				auto& data = response["data"];
				if(data.IsObject() && data.HasMember("server_time")) {
					m_timeDiffWithServer = data["server_time"].GetInt64() - getTimestamp();
					KF_LOG_INFO(logger, "serverTime: " << data["server_time"].GetInt64() 
													 << ", timeDiff: " << m_timeDiffWithServer);
				}
			}
		}
	}

	return;
}

void MDEngineDaybit::marketDataHandler(const rapidjson::Document& json)
{
	auto& payload = json["payload"];
	if (!payload.HasMember("status") || !payload.HasMember("response")) {
		KF_LOG_ERROR(logger, "lack parameter [status] or [response]");
		return;
	}

	std::string status = payload["status"].GetString();
	if (status != "ok") {
		KF_LOG_ERROR(logger, "status error!");
		return;
	}
	
	auto& response = payload["response"];
	if (!response.IsObject() || !response.HasMember("data")) {
		KF_LOG_ERROR(logger, "response format error!");
		return;
	}

	auto& outer =response["data"];
	if (!outer.IsArray()) {
		KF_LOG_ERROR(logger, "outer data is not array!");
		return;
	}

	auto& inner = outer[0];
	if (!inner.HasMember("data")) {
		KF_LOG_ERROR(logger, "inner data not exists!");
	    return;
	}

	const rapidjson::Value& data = inner["data"];
	if (!data.IsArray()) {
		KF_LOG_ERROR(logger, "inner data is not array!");
		return;
	}

	string base, quote, tickPrice;
	for (size_t i = 0; i < data.Size(); ++i) {
		auto& val = data[i];
		base = val["base"].GetString();
		quote = val["quote"].GetString();
		tickPrice = val["tick_price"].GetString();
		
		string coinPair;
		coinPair.append(quote).append(";").append(base);

		this->setTickPrice(coinPair, tickPrice);
	}

	//after sync coinPair, begin to subscribe
	KF_LOG_INFO(logger, "begin to generate subscribe json");
	this->genSubscribeJson();
	
	return;
}

int64_t MDEngineDaybit::makeRef()
{
	return this->m_ref++;
}

int64_t MDEngineDaybit::makeJoinRef()
{
	return this->m_joinRef++;
}

std::string MDEngineDaybit::genHeartBeatJson()
{
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	writer.StartObject();
	writer.Key("join_ref");
	writer.Null();

	writer.Key("ref");
	writer.String(std::to_string(this->makeRef()).c_str());

	writer.Key("topic");
	writer.String("phoenix");

	writer.Key("event");
	writer.String("heartbeat");
	
	writer.Key("payload");
	writer.StartObject();
	writer.EndObject();
	
	writer.Key("timeout");
	writer.Int(3000);
	writer.EndObject();

	return buffer.GetString();
}

void MDEngineDaybit::phxPush(const std::string& message)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_subscribeQueue.push(message);
	return;
}

std::string MDEngineDaybit::phxPop()
{
	std::lock_guard<std::mutex> lock(m_mutex);

	std::string message;
	if(m_subscribeQueue.size() > 0) {
		message = m_subscribeQueue.front();
		m_subscribeQueue.pop();
	}
	
	return message;
}

void MDEngineDaybit::phxClear()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	while (!m_subscribeQueue.empty()) {
		m_subscribeQueue.pop();
	}

	return;
}

std::string MDEngineDaybit::genServerTimeJoin(int64_t& nJoinRef)
{
	nJoinRef = this->makeRef();

	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	writer.StartObject();
	writer.Key("join_ref");
	writer.String(std::to_string(nJoinRef).c_str());

	writer.Key("ref");
	writer.String(std::to_string(nJoinRef).c_str());

	writer.Key("topic");
	writer.String(TOPIC_API);

	writer.Key("event");
	writer.String("phx_join");

	writer.Key("payload");
	writer.StartObject();
	writer.EndObject();

	writer.Key("timeout");
	writer.Int(3000);
	writer.EndObject();

	return buffer.GetString();
}


std::string MDEngineDaybit::genServerTimeReq(int64_t& nJoinRef)
{
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	writer.StartObject();
	writer.Key("join_ref");
	writer.String(std::to_string(nJoinRef).c_str());

	writer.Key("ref");
	writer.String(std::to_string(this->makeRef()).c_str());

	writer.Key("topic");
	writer.String(TOPIC_API);

	writer.Key("event");
	writer.String("get_server_time");

	writer.Key("payload");
	writer.StartObject();
	writer.Key("timestamp");
	writer.Int64(this->getTimestamp() + m_timeDiffWithServer);
	writer.EndObject();

	writer.Key("timeout");
	writer.Int(3000);
	writer.EndObject();

	return buffer.GetString();
}

std::string MDEngineDaybit::getTickPrice(const std::string& coinPair)
{
	std::string tickPrice;
	if (m_tickPriceList.end() != m_tickPriceList.find(coinPair)) {
		tickPrice = m_tickPriceList[coinPair];
	}
	
	return tickPrice;
}

void MDEngineDaybit::setTickPrice(const std::string& coinPair, const std::string& tickPrice)
{
	KF_LOG_INFO(logger, "coinPair[" << coinPair <<"], tickPrice[" << tickPrice << "].");
	m_tickPriceList[coinPair] = tickPrice;
	return;
}

std::string MDEngineDaybit::genMarketDataJoin(int64_t& nJoinRef)
{
	nJoinRef = this->makeRef();

	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	writer.StartObject();
	writer.Key("join_ref");
	writer.String(std::to_string(nJoinRef).c_str());

	writer.Key("ref");
	writer.String(std::to_string(nJoinRef).c_str());

	writer.Key("topic");
	writer.String(SUBS_MARKETS);

	writer.Key("event");
	writer.String("phx_join");

	writer.Key("payload");
	writer.StartObject();
	writer.EndObject();

	writer.Key("timeout");
	writer.Int(3000);
	writer.EndObject();

	return buffer.GetString();
}

std::string MDEngineDaybit::genMarketDataReq(int64_t& nJoinRef)
{
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	writer.StartObject();
	writer.Key("join_ref");
	writer.String(std::to_string(nJoinRef).c_str());

	writer.Key("ref");
	writer.String(std::to_string(this->makeRef()).c_str());

	writer.Key("topic");
	writer.String(SUBS_MARKETS);

	writer.Key("event");
	writer.String("request");

	writer.Key("payload");
	writer.StartObject();
	writer.Key("timestamp");
	writer.Int64(this->getTimestamp() + m_timeDiffWithServer);
	writer.EndObject();

	writer.Key("timeout");
	writer.Int(3000);
	writer.EndObject();

	return buffer.GetString();
}
 
int lwsEventCallback( struct lws *conn, enum lws_callback_reasons reason, void *, void *data , size_t len )
{
    switch( reason )
    {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
        {
            lws_callback_on_writable( conn );
            break;
        }
        case LWS_CALLBACK_CLIENT_RECEIVE:
        {
            if(MDEngineDaybit::m_instance)
            {
                MDEngineDaybit::m_instance->onMessage(conn, (char*)data, len);				
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_WRITEABLE:
        {
            if(MDEngineDaybit::m_instance)
            {
                MDEngineDaybit::m_instance->onWrite(conn);
            }
            break;
        }
        case LWS_CALLBACK_WSI_DESTROY:
        case LWS_CALLBACK_CLOSED:
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        {
            if(MDEngineDaybit::m_instance)
            {
            	std::cout<< "connection broken, reason :"<<(int)reason<<std::endl;
                MDEngineDaybit::m_instance->onClose(conn);
            }
            break;
        }
        default:
            break;
    }
    return 0;
}

BOOST_PYTHON_MODULE(libdaybitmd)
{
    using namespace boost::python;
    class_<MDEngineDaybit, boost::shared_ptr<MDEngineDaybit> >("Engine")
            .def(init<>())
            .def("init", &MDEngineDaybit::initialize)
            .def("start", &MDEngineDaybit::start)
            .def("stop", &MDEngineDaybit::stop)
            .def("logout", &MDEngineDaybit::logout)
            .def("wait_for_stop", &MDEngineDaybit::wait_for_stop);
}

WC_NAMESPACE_END
