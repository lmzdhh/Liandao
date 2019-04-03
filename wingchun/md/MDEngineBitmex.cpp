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
            lws_callback_on_writable(conn);
            break;
        }
        case LWS_CALLBACK_PROTOCOL_INIT:
        {
            break;
        }
        case LWS_CALLBACK_CLIENT_RECEIVE:
        {
            if(md_instance)
            {
                md_instance->processData(conn, (const char*)data, len);
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_WRITEABLE:
        {
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
        return;
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
    std::string str = "orderBookL2_25:" + symbol;
    writer.String(str.c_str());
    writer.EndObject();
    return buffer.GetString();
}

std::string MDEngineBitmex::createQuoteBinsJsonString(std::string symbol)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("op");
    writer.String("subscribe");
    writer.Key("args");
    std::string str = "quoteBin1m:" + symbol;
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

std::string MDEngineBitmex::createTradeBinsJsonString(std::string symbol)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("op");
    writer.String("subscribe");
    writer.Key("args");
    std::string str = "tradeBin1m:" + symbol;
    writer.String(str.c_str());
    writer.EndObject();
    return buffer.GetString();
}

std::string MDEngineBitmex::createFundingJsonString()
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("op");
    writer.String("subscribe");
    writer.Key("args");
    std::string str = "funding";
    writer.String(str.c_str());
    writer.EndObject();
    return buffer.GetString();
}

void MDEngineBitmex::createSubscribeJsonStrings()
{
    subscribeJsonStrings.push_back(createFundingJsonString());
    std::unordered_map<std::string, std::string>::iterator iter = whiteList.GetKeyIsStrategyCoinpairWhiteList().begin();
    for( ; iter != whiteList.GetKeyIsStrategyCoinpairWhiteList().end(); iter++)
    {
        KF_LOG_DEBUG(logger, "creating subscribe json string for strategy symbol " << iter->first << ", market symbol " << iter->second);
        subscribeJsonStrings.push_back(createOrderbookJsonString(iter->second));
        subscribeJsonStrings.push_back(createTradeJsonString(iter->second));
        subscribeJsonStrings.push_back(createTradeBinsJsonString(iter->second));
        
    }
}

void MDEngineBitmex::debugPrint(std::vector<std::string> &jsons)
{
    KF_LOG_INFO(logger, "printing out all subscribe json strings");

    for(size_t count = 0; count < jsons.size(); count++)
    {
        KF_LOG_INFO(logger, "json string: " << jsons[count]);
    }
}

void MDEngineBitmex::subscribeChannel(struct lws* conn)
{
    if(num_subscribed >= subscribeJsonStrings.size())
    {
        return;
    }

    unsigned char message[512];
    memset(&message[LWS_PRE], 0, 512 - LWS_PRE);

    std::string json = subscribeJsonStrings[num_subscribed++];
    int length = json.length();
    strncpy((char *)message + LWS_PRE, json.c_str(), length);

    lws_write(conn, &message[LWS_PRE], length, LWS_WRITE_TEXT);

    KF_LOG_INFO(logger, "subscribed to " << json);

    if(num_subscribed < subscribeJsonStrings.size())
    {
        lws_callback_on_writable(conn);
        KF_LOG_INFO(logger, "there are more channels to subscribe to");
    }
    else
    {
        KF_LOG_INFO(logger, "there are no more channels to subscribe to");
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
    id_to_price.clear();
    received_partial.clear();

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
        if(strcmp(json["table"].GetString(), "orderBookL2_25") == 0)
        {
            KF_LOG_INFO(logger, "received data is orderBook");
            processOrderbookData(json);
        }
        else if(strcmp(json["table"].GetString(), "quoteBin1m") == 0)
        {
            KF_LOG_INFO(logger, "received data is 1-minute quote bins");
        }
        else if(strcmp(json["table"].GetString(), "trade") == 0)
        {
            KF_LOG_INFO(logger, "received data is live trade");
            processTradingData(json);
        }
        else if(strcmp(json["table"].GetString(), "tradeBin1m") == 0)
        {
            KF_LOG_INFO(logger, "received data is 1-minute trade bins");
            processTradeBinsData(json);
        }
        else if(strcmp(json["table"].GetString(), "funding") == 0)
        {
            KF_LOG_INFO(logger, "received data is funding data:" << data);
            processFundingData(json);
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

    if(!json.HasMember("data") || !json["data"].IsArray() || json["data"].Size() == 0)
    {
        KF_LOG_INFO(logger, "received orderbook does not have valid data");
        return;
    }

    std::string symbol = json["data"].GetArray()[0]["symbol"].GetString();
    std::string ticker = whiteList.GetKeyByValue(symbol);
    if(ticker.empty())
    {
        KF_LOG_INFO(logger, "received orderbook symbol not in white list");
        return;
    }
    KF_LOG_INFO(logger, "received orderbook symbol is " << symbol << " and ticker is " << ticker);

    std::string action = json["action"].GetString();
    KF_LOG_INFO(logger, "received orderbook action is a(n) " << action);

    // upon subscription, an image of the existing data will be received through
    // a partial action, so you can get started and apply deltas after that
    if(action == "partial")
    {
        received_partial.insert(symbol);
        auto& data = json["data"];
        for(int count = 0; count < data.Size(); count++)
        {
            auto& update = data.GetArray()[count];
            // each partial table data row contains symbol, id, side, size, and price field
            uint64_t id = update["id"].GetUint64();
            std::string side = update["side"].GetString();
            uint64_t size = std::round(update["size"].GetUint64() * scale_offset);
            int64_t price = std::round(update["price"].GetFloat() * scale_offset);
            // save id/price pair for future update/delete lookup
            id_to_price[id] = price;

            if(side == "Buy")
            {
                KF_LOG_INFO(logger, "new bid: price " << price << " and amount " << size);
                priceBook.UpdateBidPrice(ticker, price, size);
            }
            else
            {
                KF_LOG_INFO(logger, "new ask: price " << price << " and amount " << size);
                priceBook.UpdateAskPrice(ticker, price, size);
            }
        }
    }
    // other messages may be received before the partial action comes through
    // in that case drop any messages received until partial action has been received
    else if(received_partial.find(symbol) == received_partial.end())
    {
        KF_LOG_INFO(logger, "have not received first partial action for symbol " << symbol);
        KF_LOG_INFO(logger, "drop any messages received until partial action has been received");
    }
    else if(action == "update")
    {
        auto& data = json["data"];
        for(int count = 0; count < data.Size(); count++)
        {
            auto& update = data.GetArray()[count];
            // each update table data row contains symbol, id, side, and size field
            // price is looked up using id
            uint64_t id = update["id"].GetUint64();
            std::string side = update["side"].GetString();
            uint64_t size = std::round(update["size"].GetUint64() * scale_offset);
            int64_t price = id_to_price[id];

            if(side == "Buy")
            {
                KF_LOG_INFO(logger, "updated bid: price " << price << " and amount " << size);
                priceBook.UpdateBidPrice(ticker, price, size);
            }
            else
            {
                KF_LOG_INFO(logger, "updated ask: price " << price << " and amount " << size);
                priceBook.UpdateAskPrice(ticker, price, size);
            }
        }
    }
    else if(action == "insert")
    {
        auto& data = json["data"];
        for(int count = 0; count < data.Size(); count++)
        {
            auto& update = data.GetArray()[count];
            // each insert table data row contains symbol, id, side, size, and price field
            uint64_t id = update["id"].GetUint64();
            std::string side = update["side"].GetString();
            uint64_t size = std::round(update["size"].GetUint64() * scale_offset);
            int64_t price = std::round(update["price"].GetFloat() * scale_offset);
            // save id/price pair for future update/delete lookup
            id_to_price[id] = price;

            if(side == "Buy")
            {
                KF_LOG_INFO(logger, "new bid: price " << price << " and amount " << size);
                priceBook.UpdateBidPrice(ticker, price, size);
            }
            else
            {
                KF_LOG_INFO(logger, "new ask: price " << price << " and amount " << size);
                priceBook.UpdateAskPrice(ticker, price, size);
            }
        }
    }
    else if(action == "delete")
    {
        auto& data = json["data"];
        for(int count = 0; count < data.Size(); count++)
        {
            auto& update = data.GetArray()[count];
            // each delete table data row contains symbol, id, and side field
            // price is looked up using id
            uint64_t id = update["id"].GetUint64();
            std::string side = update["side"].GetString();
            int64_t price = id_to_price[id];
            id_to_price.erase(id);

            if(side == "Buy")
            {
                KF_LOG_INFO(logger, "deleted bid: price " << price);
                priceBook.EraseBidPrice(ticker, price);
            }
            else
            {
                KF_LOG_INFO(logger, "deleted ask: price " << price);
                priceBook.EraseAskPrice(ticker, price);
            }
        }
    }

    LFPriceBook20Field update;
    memset(&update, 0, sizeof(update));
    if(priceBook.Assembler(ticker, update))
    {
        strcpy(update.ExchangeID, "bitmex");
        KF_LOG_INFO(logger, "sending out orderbook");
        on_price_book_update(&update);
        //
        LFFundingField fundingdata;
        strcpy(fundingdata.InstrumentID, "test");
        on_funding_update(&fundingdata);
    }
}

void MDEngineBitmex::processTradingData(Document& json)
{
    KF_LOG_INFO(logger, "processing trade data");

    if(!json.HasMember("data") || !json["data"].IsArray() || json["data"].Size() == 0)
    {
        KF_LOG_INFO(logger, "received trade does not have valid data");
        return;
    }

    auto& data = json["data"];
    std::string symbol = data.GetArray()[0]["symbol"].GetString();
    std::string ticker = whiteList.GetKeyByValue(symbol);
    if(ticker.empty())
    {
        KF_LOG_INFO(logger, "received trade symbol not in white list");
        return;
    }
    KF_LOG_INFO(logger, "received trade symbol is " << symbol << " and ticker is " << ticker);

    for(int count = 0; count < data.Size(); count++)
    {
        auto& update = data.GetArray()[count];

        LFL2TradeField trade;
        memset(&trade, 0, sizeof(trade));
        strcpy(trade.InstrumentID, ticker.c_str());
        strcpy(trade.ExchangeID, "bitmex");

        int64_t price = std::round(update["price"].GetFloat() * scale_offset);
        uint64_t amount = std::round(update["size"].GetUint64() * scale_offset);
        std::string side = update["side"].GetString();

        trade.Price = price;
        trade.Volume = amount;
        trade.OrderBSFlag[0] = side == "Buy" ? 'B' : 'S';

        KF_LOG_INFO(logger, "ticker " << ticker << " traded at price " << trade.Price << " with volume " << trade.Volume << " as a " << side);

        on_trade(&trade);
    }
}

void MDEngineBitmex::processTradeBinsData(Document& json)
{
    KF_LOG_INFO(logger, "processing 1-min trade bins data");

    if(!json.HasMember("data") || !json["data"].IsArray() || json["data"].Size() == 0)
    {
        KF_LOG_INFO(logger, "received 1-min trade bin does not have valid data");
        return;
    }

    auto& data = json["data"];
    std::string symbol = data.GetArray()[0]["symbol"].GetString();
    std::string ticker = whiteList.GetKeyByValue(symbol);
    if(ticker.empty())
    {
        KF_LOG_INFO(logger, "received 1-min trade bin symbol not in white list");
        return;
    }
    KF_LOG_INFO(logger, "received 1-min trade bin symbol is " << symbol << " and ticker is " << ticker);

    for(int count = 0; count < data.Size(); count++)
    {
        auto& update = data.GetArray()[count];
        

        LFBarMarketDataField market;
        memset(&market, 0, sizeof(market));
        strcpy(market.InstrumentID, ticker.c_str());
        strcpy(market.ExchangeID, "bitmex");

        std::string timestamp = update["timestamp"].GetString();//'2019-01-06T03:32:00.000Z'
        if(timestamp.size() == strlen("2019-01-06T03:32:00.000Z"))
        {
            //sprintf(market.TradingDay, "%s%s%s", timestamp.substr(0,4).c_str(),timestamp.substr(5,7).c_str(),timestamp.substr(8,10).c_str());
            struct tm time;
            time.tm_year = std::stoi(timestamp.substr(0,4))-1900;
            time.tm_mon = std::stoi(timestamp.substr(5,7))-1;
            time.tm_mday = std::stoi(timestamp.substr(8,10));
            time.tm_hour = std::stoi(timestamp.substr(11,13));
            time.tm_min = std::stoi(timestamp.substr(14,16));
            time.tm_sec = std::stoi(timestamp.substr(17,19));
            
            time_t gm_time = timegm(&time);
            gm_time-=1;
            time = *gmtime(&gm_time);
            sprintf(market.EndUpdateTime,"%2d:%02d:%02d.999", time.tm_hour,time.tm_min,time.tm_sec);
            market.EndUpdateMillisec = gm_time *1000 + 999;
            gm_time-=59;
            time = *gmtime(&gm_time);
            sprintf(market.StartUpdateTime,"%2d:%02d:%02d.000", time.tm_hour,time.tm_min,time.tm_sec);
            market.StartUpdateMillisec =gm_time *1000;

            strftime(market.TradingDay, 9, "%Y%m%d", &time);
        }
        /*
        struct tm cur_tm, start_tm, end_tm;
        time_t now = time(0);
        cur_tm = *localtime(&now);
        strftime(market.TradingDay, 9, "%Y%m%d", &cur_tm);
	
        start_tm = cur_tm;
        start_tm.tm_min -= 1;
        market.StartUpdateMillisec = kungfu::yijinjing::parseTm(start_tm) / 1000000;
        strftime(market.StartUpdateTime, 13, "%H:%M:%S", &start_tm);

        end_tm = cur_tm;
        market.EndUpdateMillisec = kungfu::yijinjing::parseTm(end_tm) / 1000000;
        strftime(market.EndUpdateTime, 13, "%H:%M:%S", &end_tm);
        */
        market.PeriodMillisec = 60000;
        market.Open = std::round(update["open"].GetFloat() * scale_offset);;
        market.Close = std::round(update["close"].GetFloat() * scale_offset);;
        market.Low = std::round(update["low"].GetFloat() * scale_offset);;
        market.High = std::round(update["high"].GetFloat() * scale_offset);;
        market.BestBidPrice = priceBook.GetBestBidPrice(ticker);
        market.BestAskPrice = priceBook.GetBestAskPrice(ticker);
        market.Volume = std::round(update["volume"].GetUint64() * scale_offset);;

        on_market_bar_data(&market);
    }
}

int64_t getTimestampFromStr(std::string timestamp)
{
    std::string year = timestamp.substr(0,4);
    std::string month = timestamp.substr(5,2);
    std::string day = timestamp.substr(8,2);
    std::string hour = timestamp.substr(11,2);
    std::string min = timestamp.substr(14,2);
    std::string sec = timestamp.substr(17,2);
    std::string ms = timestamp.substr(20,3);
    struct tm localTM;
    localTM.tm_year = std::stoi(year)-1900;
    localTM.tm_mon = std::stoi(month)-1;
    localTM.tm_mday = std::stoi(day);
    localTM.tm_hour = std::stoi(hour);
    localTM.tm_min = std::stoi(min);
    localTM.tm_sec = std::stoi(sec);
    time_t time = mktime(&localTM);
    return time*1000+std::stoi(ms);
}

void MDEngineBitmex::processFundingData(Document& json)
{
    KF_LOG_INFO(logger, "processing funding data");

    if(!json.HasMember("data") || !json["data"].IsArray() || json["data"].Size() == 0)
    {
        KF_LOG_INFO(logger, "received funding does not have valid data");
        return;
    }

    auto& data = json["data"];
    

    for(int count = 0; count < data.Size(); count++)
    {
        auto& update = data.GetArray()[count];
        std::string symbol = update["symbol"].GetString();
        std::string ticker = whiteList.GetKeyByValue(symbol);
        if(ticker.empty())
        {
            KF_LOG_INFO(logger, "received funding symbol not in white list");
            continue;
        }
        KF_LOG_INFO(logger, "received funding symbol is " << symbol << " and ticker is " << ticker);
        std::string timestamp = update["timestamp"].GetString();//"2019-03-19T07:52:44.318Z"
        

        LFFundingField fundingdata;
        memset(&fundingdata, 0, sizeof(fundingdata));
        strcpy(fundingdata.InstrumentID, ticker.c_str());
        strcpy(fundingdata.ExchangeID, "bitmex");

        //struct tm cur_tm;
        //time_t now = time(0);
        //cur_tm = *localtime(&now);
        fundingdata.TimeStamp = getTimestampFromStr(timestamp);//kungfu::yijinjing::parseTm(cur_tm) / 1000000;
        fundingdata.Rate = update["fundingRate"].GetDouble();
        fundingdata.RateDaily = update["fundingRateDaily"].GetDouble();
        on_funding_update(&fundingdata);
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
