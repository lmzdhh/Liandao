#include "MDEngineBitfinex.h"
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

static MDEngineBitfinex* global_md = nullptr;

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


enum protocolList {
    PROTOCOL_TEST,

    PROTOCOL_LIST_COUNT
};

struct session_data {
    int fd;
};

MDEngineBitfinex::MDEngineBitfinex(): IMDEngine(SOURCE_BITFINEX)
{
    logger = yijinjing::KfLog::getLogger("MdEngine.Bitfinex");
}

void MDEngineBitfinex::load(const json& j_config)
{
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();
    KF_LOG_INFO(logger, "MDEngineBitfinex:: rest_get_interval_ms: " << rest_get_interval_ms);


    whiteList.ReadWhiteLists(j_config);
    whiteList.Debug_print();

    makeWebsocketSubscribeJsonString();
    debug_print(websocketSubscribeJsonString);

    //display usage:
    if(whiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "MDEngineBitfinex::lws_write_subscribe: subscribeCoinBaseQuote is empty. please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"tBTCUSDT\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"tETCETH\"");
        KF_LOG_ERROR(logger, "},");
    }
}

void MDEngineBitfinex::makeWebsocketSubscribeJsonString()
{
    std::unordered_map<std::string, std::string>::iterator map_itr;
    map_itr = whiteList.GetKeyIsStrategyCoinpairWhiteList().begin();
    while(map_itr != whiteList.GetKeyIsStrategyCoinpairWhiteList().end()) {
        KF_LOG_DEBUG(logger, "[makeWebsocketSubscribeJsonString] keyIsExchangeSideWhiteList (strategy_coinpair) " << map_itr->first << " (exchange_coinpair) "<< map_itr->second);

        std::string jsonBookString = createBookJsonString(map_itr->second);
        websocketSubscribeJsonString.push_back(jsonBookString);

        std::string jsonTradeString = createTradeJsonString(map_itr->second);
        websocketSubscribeJsonString.push_back(jsonTradeString);

        map_itr++;
    }
}

void MDEngineBitfinex::debug_print(std::vector<std::string> &subJsonString)
{
    size_t count = subJsonString.size();
    KF_LOG_INFO(logger, "[debug_print] websocketSubscribeJsonString (count) " << count);

    for (size_t i = 0; i < count; i++)
    {
        KF_LOG_INFO(logger, "[debug_print] websocketSubscribeJsonString (subJsonString) " << subJsonString[i]);
    }
}

void MDEngineBitfinex::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "MDEngineBitfinex::connect:");
    connected = true;
}
void MDEngineBitfinex::login(long timeout_nsec) {
    KF_LOG_INFO(logger, "MDEngineBitfinex::login:");
    global_md = this;

    if (context == NULL) {
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
        KF_LOG_INFO(logger, "MDEngineBitfinex::login: context created.");
    }

    if (context == NULL) {
        KF_LOG_ERROR(logger, "MDEngineBitfinex::login: context is NULL. return");
        return;
    }

    int logs = LLL_ERR | LLL_DEBUG | LLL_WARN;
    lws_set_log_level(logs, NULL);

    struct lws_client_connect_info ccinfo = {0};

    static std::string host  = "api.bitfinex.com";
    static std::string path = "/ws/2";
    static int port = 443;

    ccinfo.context 	= context;
    ccinfo.address 	= host.c_str();
    ccinfo.port 	= port;
    ccinfo.path 	= path.c_str();
    ccinfo.host 	= host.c_str();
    ccinfo.origin 	= host.c_str();
    ccinfo.ietf_version_or_minus_one = -1;
    ccinfo.protocol = protocols[0].name;
    ccinfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;

    struct lws* wsi = lws_client_connect_via_info(&ccinfo);
    KF_LOG_INFO(logger, "MDEngineBitfinex::login: Connecting to " <<  ccinfo.host << ":" << ccinfo.port << ":" << ccinfo.path);

    if (wsi == NULL) {
        KF_LOG_ERROR(logger, "MDEngineBitfinex::login: wsi create error.");
        return;
    }
    KF_LOG_INFO(logger, "MDEngineBitfinex::login: wsi create success.");

    logged_in = true;
}

void MDEngineBitfinex::set_reader_thread()
{
    IMDEngine::set_reader_thread();

    rest_thread = ThreadPtr(new std::thread(boost::bind(&MDEngineBitfinex::loop, this)));
}

void MDEngineBitfinex::logout()
{
    KF_LOG_INFO(logger, "MDEngineBitfinex::logout:");
}

void MDEngineBitfinex::release_api()
{
    KF_LOG_INFO(logger, "MDEngineBitfinex::release_api:");
}

void MDEngineBitfinex::subscribeMarketData(const vector<string>& instruments, const vector<string>& markets)
{
    KF_LOG_INFO(logger, "MDEngineBitfinex::subscribeMarketData:");
}

int MDEngineBitfinex::lws_write_subscribe(struct lws* conn)
{
    KF_LOG_INFO(logger, "MDEngineBitfinex::lws_write_subscribe: (subscribe_index)" << subscribe_index);

    //有待发送的数据，先把待发送的发完，在继续订阅逻辑。  ping?
    if(websocketPendingSendMsg.size() > 0) {
        unsigned char msg[512];
        memset(&msg[LWS_PRE], 0, 512-LWS_PRE);

        std::string jsonString = websocketPendingSendMsg[websocketPendingSendMsg.size() - 1];

        KF_LOG_INFO(logger, "MDEngineBitfinex::lws_write_subscribe: websocketPendingSendMsg" << jsonString.c_str());
        int length = jsonString.length();

        strncpy((char *)msg+LWS_PRE, jsonString.c_str(), length);
        int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);

        if(websocketPendingSendMsg.size() > 0)
        {    //still has pending send data, emit a lws_callback_on_writable()
            lws_callback_on_writable( conn );
            KF_LOG_INFO(logger, "MDEngineBitfinex::lws_write_subscribe: (websocketPendingSendMsg,size)" << websocketPendingSendMsg.size());
        }
        return ret;
    }

    if(websocketSubscribeJsonString.size() == 0) return 0;
    //sub depth
    if(subscribe_index >= websocketSubscribeJsonString.size())
    {
        //subscribe_index = 0;
        KF_LOG_INFO(logger, "MDEngineBitfinex::lws_write_subscribe: (none reset subscribe_index = 0, just return 0)");
	    return 0;
    }

    unsigned char msg[512];
    memset(&msg[LWS_PRE], 0, 512-LWS_PRE);

    std::string jsonString = websocketSubscribeJsonString[subscribe_index++];

    KF_LOG_INFO(logger, "MDEngineBitfinex::lws_write_subscribe: " << jsonString.c_str() << " ,after ++, (subscribe_index)" << subscribe_index);
    int length = jsonString.length();

    strncpy((char *)msg+LWS_PRE, jsonString.c_str(), length);
    int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);

    if(subscribe_index < websocketSubscribeJsonString.size())
    {
        lws_callback_on_writable( conn );
        KF_LOG_INFO(logger, "MDEngineBitfinex::lws_write_subscribe: (subscribe_index < websocketSubscribeJsonString) call lws_callback_on_writable");
    }

    return ret;
}

void MDEngineBitfinex::on_lws_data(struct lws* conn, const char* data, size_t len)
{
    KF_LOG_INFO(logger, "MDEngineBitfinex::on_lws_data: " << data);
    Document json;
    json.Parse(data);

    if(json.HasParseError()) {
        KF_LOG_ERROR(logger, "MDEngineBitfinex::on_lws_data. parse json error: " << data);
        return;
    }

    if(json.IsObject() && json.HasMember("event")) {
        if (strcmp(json["event"].GetString(), "info") == 0) {
            KF_LOG_INFO(logger, "MDEngineBitfinex::on_lws_data: is info");
            onInfo(json);
        }

        if (strcmp(json["event"].GetString(), "ping") == 0) {
            KF_LOG_INFO(logger, "MDEngineBitfinex::on_lws_data: is ping");
            onPing(conn, json);
        }

        if (strcmp(json["event"].GetString(), "subscribed") == 0) {
            KF_LOG_INFO(logger, "MDEngineBitfinex::on_lws_data: is subscribed");
            onSubscribed(json);
        }
    }

    //data
    if(json.IsArray()) {
        int chanId = json.GetArray()[0].GetInt();
        KF_LOG_INFO(logger, "MDEngineBitfinex::on_lws_data: (chanId)" << chanId);
        //find book or trade by chanId
//TODO
//        {
//            KF_LOG_INFO(logger, "MDEngineBitfinex::on_lws_data: is book");
//            onBook(json);
//        }
//
//        {
//            KF_LOG_INFO(logger, "MDEngineBitfinex::on_lws_data: is trade");
//            onTrade(json);
//        }
    }
}


void MDEngineBitfinex::on_lws_connection_error(struct lws* conn)
{
    KF_LOG_ERROR(logger, "MDEngineBitfinex::on_lws_connection_error.");
    //market logged_in false;
    logged_in = false;
    KF_LOG_ERROR(logger, "MDEngineBitfinex::on_lws_connection_error. login again.");
    //clear the price book, the new websocket will give 200 depth on the first connect, it will make a new price book
    clearPriceBook();
    //no use it
    long timeout_nsec = 0;
    //reset sub
    subscribe_index = 0;

    login(timeout_nsec);
}


int64_t MDEngineBitfinex::getTimestamp()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp;
}

/*
 * // request
{
   "event":"ping",
   "cid": 1234
}

// response
{
   "event":"pong",
   "ts": 1511545528111,
   "cid": 1234
}
 * */
void MDEngineBitfinex::onPing(struct lws* conn, Document& json)
{
    KF_LOG_INFO(logger, "MDEngineBitfinex::onPing: " << parseJsonToString(json));
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();
    writer.Key("event");
    writer.String("pong");

    writer.Key("ts");
    writer.Int64(getTimestamp());

    writer.Key("cid");
    writer.Int(json["cid"].GetInt());

    writer.EndObject();

    std::string result = s.GetString();
    KF_LOG_INFO(logger, "MDEngineBitfinex::onPing: (Pong)" << result);
    websocketPendingSendMsg.push_back(result);

    //emit a callback
    lws_callback_on_writable( conn );
}

/*
 * #1
 * {
   "event":"info",
   "code": CODE,
   "msg": MSG
}
#2
 {
   "event": "info",
   "version":  VERSION,
   "platform": {
      "status": 1
   }
}
 * */
void MDEngineBitfinex::onInfo(Document& json)
{
    KF_LOG_INFO(logger, "MDEngineBitfinex::onInfo: " << parseJsonToString(json));
}


//{\event\:\subscribed\,\channel\:\book\,\chanId\:56,\symbol\:\tETCBTC\,\prec\:\P0\,\freq\:\F0\,\len\:\25\,\pair\:\ETCBTC\}
//{\event\:\subscribed\,\channel\:\trades\,\chanId\:2337,\symbol\:\tETHBTC\,\pair\:\ETHBTC\}

//{\event\:\subscribed\,\channel\:\trades\,\chanId\:1,\symbol\:\tBTCUSD\,\pair\:\BTCUSD\}
void MDEngineBitfinex::onSubscribed(Document& json)
{
    KF_LOG_INFO(logger, "MDEngineBitfinex::onSubscribed: " << parseJsonToString(json));

    if(json.HasMember("chanId") && json.HasMember("symbol") && json.HasMember("channel")) {
        int chanId = json["chanId"].GetInt();
        std::string symbol = json["symbol"].GetString();
        if(strcmp(json["channel"].GetString(), "trades") == 0) {
//TODO
        }

        if(strcmp(json["channel"].GetString(), "book") == 0) {
//TODO
        }
    }
}

void MDEngineBitfinex::onTrade(Document& json)
{
    if(!json.HasMember("data") || !json["data"].IsArray())
    {
        KF_LOG_ERROR(logger, "MDEngineBitfinex::[onFills] invalid market trade message");
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

    KF_LOG_INFO(logger, "MDEngineBitfinex::onFills:" << "base : " << base << "  quote: " << quote);

    std::string ticker = whiteList.GetKeyByValue(base + "_" +  quote);
    if(ticker.length() == 0) {
        return;
    }

    int len = json["data"].Size();

    for(int i = 0 ; i < len; i++) {
        LFL2TradeField trade;
        memset(&trade, 0, sizeof(trade));
        strcpy(trade.InstrumentID, ticker.c_str());
        strcpy(trade.ExchangeID, "bitfinex");

        trade.Price = std::round(std::stod(json["data"].GetArray()[i].GetArray()[0].GetString()) * scale_offset);
        trade.Volume = std::round(std::stod(json["data"].GetArray()[i].GetArray()[1].GetString()) * scale_offset);
        trade.OrderBSFlag[0] = strcmp("buy", json["data"].GetArray()[i].GetArray()[2].GetString()) == 0 ? 'B' : 'S';

        KF_LOG_INFO(logger, "MDEngineBitfinex::[onFills] (ticker)" << ticker <<
                                                                  " (Price)" << trade.Price <<
                                                                  " (trade.Volume)" << trade.Volume);
        on_trade(&trade);
    }
}


void MDEngineBitfinex::onBook(Document& json)
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

    KF_LOG_INFO(logger, "MDEngineBitfinex::onDepth:" << "base : " << base << "  quote: " << quote);

    std::string ticker = whiteList.GetKeyByValue(base + "_" +  quote);
    if(ticker.length() == 0) {
        KF_LOG_INFO(logger, "MDEngineBitfinex::onDepth: not in WhiteList , ignore it:" << "base : " << base << "  quote: " << quote);
        return;
    }

    KF_LOG_INFO(logger, "MDEngineBitfinex::onDepth:" << "(ticker) " << ticker);
    std::map<int64_t, uint64_t>*  asksPriceAndVolume;
    std::map<int64_t, uint64_t>*  bidsPriceAndVolume;

    auto iter = tickerAskPriceMap.find(ticker);
    if(iter != tickerAskPriceMap.end()) {
        asksPriceAndVolume = iter->second;
//        KF_LOG_INFO(logger, "MDEngineBitfinex::onDepth:" << "ticker : " << ticker << "  get from map (asksPriceAndVolume.size) " << asksPriceAndVolume->size());
    } else {
        asksPriceAndVolume = new std::map<int64_t, uint64_t>();
        tickerAskPriceMap.insert(std::pair<std::string, std::map<int64_t, uint64_t>*>(ticker, asksPriceAndVolume));
//        KF_LOG_INFO(logger, "MDEngineBitfinex::onDepth:" << "ticker : " << ticker << "  insert into map (asksPriceAndVolume.size) " << asksPriceAndVolume->size());
    }

    iter = tickerBidPriceMap.find(ticker);
    if(iter != tickerBidPriceMap.end()) {
        bidsPriceAndVolume = iter->second;
//        KF_LOG_INFO(logger, "MDEngineBitfinex::onDepth:" << "ticker : " << ticker << "  get from map (bidsPriceAndVolume.size) " << bidsPriceAndVolume->size());
    } else {
        bidsPriceAndVolume = new std::map<int64_t, uint64_t>();
        tickerBidPriceMap.insert(std::pair<std::string, std::map<int64_t, uint64_t>*>(ticker, bidsPriceAndVolume));
//        KF_LOG_INFO(logger, "MDEngineBitfinex::onDepth:" << "ticker : " << ticker << "  insert into map (bidsPriceAndVolume.size) " << bidsPriceAndVolume->size());
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
                    KF_LOG_INFO(logger, "MDEngineBitfinex::onDepth: ##########################################asksPriceAndVolume volume == 0############################# price:" << price<<  "  volume:"<< volume);
                } else {
                    asksPriceAndVolume->insert(std::pair<int64_t, uint64_t>(price, volume));
                }
//                KF_LOG_INFO(logger, "MDEngineBitfinex::onDepth: asks price:" << price<<  "  volume:"<< volume);
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
                    KF_LOG_INFO(logger, "MDEngineBitfinex::onDepth: ##########################################bidsPriceAndVolume volume == 0############################# price:" << price<<  "  volume:"<< volume);

                } else {
                    bidsPriceAndVolume->insert(std::pair<int64_t, uint64_t>(price, volume));
                }
//                KF_LOG_INFO(logger, "MDEngineBitfinex::onDepth: bids price:" << price<<  "  volume:"<< volume);
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
//        std::cout<<"asksPriceAndVolume sorted desc:"<< std::endl;
//        for(int i=0; i<sort_result.size(); i++)
//        {
//            std::cout << i << "    " << sort_result[i].price << "," << sort_result[i].volume << std::endl;
//        }
        //asks 	卖方深度 from big to little
        int askTotalSize = (int)sort_result.size();
        auto size = std::min(askTotalSize, 20);

        for(int i = 0; i < size; ++i)
        {
            md.AskLevels[i].price = sort_result[askTotalSize - i - 1].price;
            md.AskLevels[i].volume = sort_result[askTotalSize - i - 1].volume;
//            KF_LOG_INFO(logger, "MDEngineBitfinex::onDepth:  LFPriceBook20Field AskLevels: (i)" << i << "(price)" << md.AskLevels[i].price<<  "  (volume)"<< md.AskLevels[i].volume);
        }
        md.AskLevelCount = size;


        sort_result.clear();
        sortMapByKey(*bidsPriceAndVolume, sort_result, sort_price_asc);
//        std::cout<<"bidsPriceAndVolume sorted asc:"<< std::endl;
//        for(int i=0; i<sort_result.size(); i++)
//        {
//            std::cout << i << "    " << sort_result[i].price << "," << sort_result[i].volume << std::endl;
//        }
        //bids 	买方深度 from big to little
        int bidTotalSize = (int)sort_result.size();
        size = std::min(bidTotalSize, 20);

        for(int i = 0; i < size; ++i)
        {
            md.BidLevels[i].price = sort_result[bidTotalSize - i - 1].price;
            md.BidLevels[i].volume = sort_result[bidTotalSize - i - 1].volume;
//            KF_LOG_INFO(logger, "MDEngineBitfinex::onDepth:  LFPriceBook20Field BidLevels: (i) " << i << "(price)" << md.BidLevels[i].price<<  "  (volume)"<< md.BidLevels[i].volume);
        }
        md.BidLevelCount = size;
        sort_result.clear();


        strcpy(md.InstrumentID, ticker.c_str());
        strcpy(md.ExchangeID, "bitfinex");

        KF_LOG_INFO(logger, "MDEngineBitfinex::onDepth: on_price_book_update");
        on_price_book_update(&md);
    }
}

void MDEngineBitfinex::clearPriceBook()
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

std::string MDEngineBitfinex::parseJsonToString(Document &d)
{
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);

    return buffer.GetString();
}


//{ "event": "subscribe", "channel": "book",  "symbol": "tBTCUSD" }
std::string MDEngineBitfinex::createBookJsonString(std::string exchange_coinpair)
{
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();
    writer.Key("event");
    writer.String("subscribe");

    writer.Key("channel");
    writer.String("book");

    writer.Key("symbol");
    writer.String(exchange_coinpair.c_str());

    writer.EndObject();
    return s.GetString();
}

//{ "event": "subscribe", "channel": "trades",  "symbol": "tETHBTC" }
std::string MDEngineBitfinex::createTradeJsonString(std::string exchange_coinpair)
{
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();
    writer.Key("event");
    writer.String("subscribe");

    writer.Key("channel");
    writer.String("trades");

    writer.Key("symbol");
    writer.String(exchange_coinpair.c_str());

    writer.EndObject();
    return s.GetString();
}

void MDEngineBitfinex::loop()
{
    while(isRunning)
    {
            int n = lws_service( context, rest_get_interval_ms );
            std::cout << " 3.1415 loop() lws_service (n)" << n << std::endl;
    }

}

BOOST_PYTHON_MODULE(libbitfinexmd)
{
    using namespace boost::python;
    class_<MDEngineBitfinex, boost::shared_ptr<MDEngineBitfinex> >("Engine")
            .def(init<>())
            .def("init", &MDEngineBitfinex::initialize)
            .def("start", &MDEngineBitfinex::start)
            .def("stop", &MDEngineBitfinex::stop)
            .def("logout", &MDEngineBitfinex::logout)
            .def("wait_for_stop", &MDEngineBitfinex::wait_for_stop);
}
