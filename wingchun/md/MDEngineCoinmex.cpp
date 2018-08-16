#include "MDEngineCoinmex.h"
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

static MDEngineCoinmex* global_md = nullptr;

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

MDEngineCoinmex::MDEngineCoinmex(): IMDEngine(SOURCE_COINMEX)
{
    logger = yijinjing::KfLog::getLogger("MdEngine.Coinmex");
}

void MDEngineCoinmex::load(const json& j_config)
{
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();
    KF_LOG_INFO(logger, "MDEngineCoinmex:: rest_get_interval_ms: " << rest_get_interval_ms);


    whiteList.ReadWhiteLists(j_config);
    whiteList.Debug_print();

    makeWebsocketSubscribeJsonString();
    debug_print(websocketSubscribeJsonString);

    //display usage:
    if(whiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "MDEngineCoinmex::lws_write_subscribe: subscribeCoinBaseQuote is empty. please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"btcusdt\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"etceth\"");
        KF_LOG_ERROR(logger, "},");
    }
}

void MDEngineCoinmex::makeWebsocketSubscribeJsonString()
{
    int count = whiteList.GetCoinBaseQuotes().size();
    for (int i = 0; i < count; i++)
    {
        //get ready websocket subscrube json strings
        std::string jsonDepthString = createDepthJsonString(whiteList.GetCoinBaseQuotes()[i].base, whiteList.GetCoinBaseQuotes()[i].quote);
        websocketSubscribeJsonString.push_back(jsonDepthString);
        std::string jsonFillsString = createFillsJsonString(whiteList.GetCoinBaseQuotes()[i].base, whiteList.GetCoinBaseQuotes()[i].quote);
        websocketSubscribeJsonString.push_back(jsonFillsString);
    }
}

void MDEngineCoinmex::debug_print(std::vector<std::string> &subJsonString)
{
    size_t count = subJsonString.size();
    KF_LOG_INFO(logger, "[debug_print] websocketSubscribeJsonString (count) " << count);

    for (size_t i = 0; i < count; i++)
    {
        KF_LOG_INFO(logger, "[debug_print] websocketSubscribeJsonString (subJsonString) " << subJsonString[i]);
    }
}

void MDEngineCoinmex::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "MDEngineCoinmex::connect:");
    connected = true;
}
void MDEngineCoinmex::login(long timeout_nsec) {
    KF_LOG_INFO(logger, "MDEngineCoinmex::login:");
    global_md = this;

    char inputURL[300] = "wss://websocket.coinmex.com";

    const char *urlProtocol, *urlTempPath;
    char urlPath[300];
    //int logs = LLL_ERR | LLL_DEBUG | LLL_WARN;
    struct lws_client_connect_info clientConnectInfo;
    memset(&clientConnectInfo, 0, sizeof(clientConnectInfo));
    clientConnectInfo.port = 8443;
    if (lws_parse_uri(inputURL, &urlProtocol, &clientConnectInfo.address, &clientConnectInfo.port, &urlTempPath)) {
        KF_LOG_ERROR(logger,
                     "MDEngineCoinmex::connect: Couldn't parse URL. Please check the URL and retry: " << inputURL);
        return;
    }

    // Fix up the urlPath by adding a / at the beginning, copy the temp path, and add a \0     at the end
    urlPath[0] = '/';
    strncpy(urlPath + 1, urlTempPath, sizeof(urlPath) - 2);
    urlPath[sizeof(urlPath) - 1] = '\0';
    clientConnectInfo.path = urlPath; // Set the info's path to the fixed up url path

    KF_LOG_INFO(logger, "MDEngineCoinmex::login:" << "urlProtocol=" << urlProtocol <<
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
        KF_LOG_INFO(logger, "MDEngineCoinmex::login: context created.");
    }

    if (context == NULL) {
        KF_LOG_ERROR(logger, "MDEngineCoinmex::login: context is NULL. return");
        return;
    }

    struct lws *wsi = NULL;
    // Set up the client creation info
    clientConnectInfo.context = context;
    clientConnectInfo.port = 8443;
    clientConnectInfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
    clientConnectInfo.host = clientConnectInfo.address;
    clientConnectInfo.origin = clientConnectInfo.address;
    clientConnectInfo.ietf_version_or_minus_one = -1;
    clientConnectInfo.protocol = protocols[PROTOCOL_TEST].name;

    KF_LOG_INFO(logger, "MDEngineCoinmex::login:" << "Connecting to " << urlProtocol << ":" <<
                                                  clientConnectInfo.host << ":" <<
                                                  clientConnectInfo.port << ":" << urlPath);

    wsi = lws_client_connect_via_info(&clientConnectInfo);
    if (wsi == NULL) {
        KF_LOG_ERROR(logger, "MDEngineCoinmex::login: wsi create error.");
        return;
    }
    KF_LOG_INFO(logger, "MDEngineCoinmex::login: wsi create success.");
    logged_in = true;
}

void MDEngineCoinmex::set_reader_thread()
{
    IMDEngine::set_reader_thread();

    rest_thread = ThreadPtr(new std::thread(boost::bind(&MDEngineCoinmex::loop, this)));
}

void MDEngineCoinmex::logout()
{
    KF_LOG_INFO(logger, "MDEngineCoinmex::logout:");
}

void MDEngineCoinmex::release_api()
{
    KF_LOG_INFO(logger, "MDEngineCoinmex::release_api:");
}

void MDEngineCoinmex::subscribeMarketData(const vector<string>& instruments, const vector<string>& markets)
{
    KF_LOG_INFO(logger, "MDEngineCoinmex::subscribeMarketData:");
}

int MDEngineCoinmex::lws_write_subscribe(struct lws* conn)
{
    KF_LOG_INFO(logger, "MDEngineCoinmex::lws_write_subscribe: (subscribe_index)" << subscribe_index);

    if(websocketSubscribeJsonString.size() == 0) return 0;
    //sub depth
    if(subscribe_index >= websocketSubscribeJsonString.size())
    {
        //subscribe_index = 0;
        KF_LOG_INFO(logger, "MDEngineCoinmex::lws_write_subscribe: (none reset subscribe_index = 0, just return 0)");
	    return 0;
    }

    unsigned char msg[512];
    memset(&msg[LWS_PRE], 0, 512-LWS_PRE);

    std::string jsonString = websocketSubscribeJsonString[subscribe_index++];

    KF_LOG_INFO(logger, "MDEngineCoinmex::lws_write_subscribe: " << jsonString.c_str() << " ,after ++, (subscribe_index)" << subscribe_index);
    int length = jsonString.length();

    strncpy((char *)msg+LWS_PRE, jsonString.c_str(), length);
    int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);

    if(subscribe_index < websocketSubscribeJsonString.size())
    {
        lws_callback_on_writable( conn );
        KF_LOG_INFO(logger, "MDEngineCoinmex::lws_write_subscribe: (subscribe_index < websocketSubscribeJsonString) call lws_callback_on_writable");
    }

    return ret;
}

void MDEngineCoinmex::on_lws_data(struct lws* conn, const char* data, size_t len)
{
    KF_LOG_INFO(logger, "MDEngineCoinmex::on_lws_data: " << data);
    Document json;
    json.Parse(data);

    if(!json.HasParseError() && json.IsObject() && json.HasMember("type") && json["type"].IsString())
    {

        if(strcmp(json["type"].GetString(), "depth") == 0)
        {
            KF_LOG_INFO(logger, "MDEngineCoinmex::on_lws_data: is depth");
            onDepth(json);
        }

        if(strcmp(json["type"].GetString(), "fills") == 0)
        {
            KF_LOG_INFO(logger, "MDEngineCoinmex::on_lws_data: is fills");
            onFills(json);
        }
        if(strcmp(json["type"].GetString(), "tickers") == 0)
        {
            KF_LOG_INFO(logger, "MDEngineCoinmex::on_lws_data: is tickers");
            onTickers(json);
        }
    } else {
        KF_LOG_ERROR(logger, "MDEngineCoinmex::on_lws_data . parse json error: " << data);
    }
}


void MDEngineCoinmex::on_lws_connection_error(struct lws* conn)
{
    KF_LOG_ERROR(logger, "MDEngineCoinmex::on_lws_connection_error.");
    //market logged_in false;
    logged_in = false;
    KF_LOG_ERROR(logger, "MDEngineCoinmex::on_lws_connection_error. login again.");
    //clear the price book, the new websocket will give 200 depth on the first connect, it will make a new price book
    clearPriceBook();
    //no use it
    long timeout_nsec = 0;
    //reset sub
    subscribe_index = 0;

    login(timeout_nsec);
}

void MDEngineCoinmex::clearPriceBook()
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

void MDEngineCoinmex::onTickers(Document& d)
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

void MDEngineCoinmex::onFills(Document& json)
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
        KF_LOG_ERROR(logger, "MDEngineCoinmex::[onFills] invalid market trade message");
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

    KF_LOG_INFO(logger, "MDEngineCoinmex::onFills:" << "base : " << base << "  quote: " << quote);

    std::string ticker = whiteList.GetKeyByValue(base + "_" +  quote);
    if(ticker.length() == 0) {
        return;
    }

    int len = json["data"].Size();

    for(int i = 0 ; i < len; i++) {
        LFL2TradeField trade;
        memset(&trade, 0, sizeof(trade));
        strcpy(trade.InstrumentID, ticker.c_str());
        strcpy(trade.ExchangeID, "coinmex");

        trade.Price = std::round(std::stod(json["data"].GetArray()[i].GetArray()[0].GetString()) * scale_offset);
        trade.Volume = std::round(std::stod(json["data"].GetArray()[i].GetArray()[1].GetString()) * scale_offset);
        trade.OrderBSFlag[0] = strcmp("buy", json["data"].GetArray()[i].GetArray()[2].GetString()) == 0 ? 'B' : 'S';

        KF_LOG_INFO(logger, "MDEngineCoinmex::[onFills] (ticker)" << ticker <<
                                                                  " (Price)" << trade.Price <<
                                                                  " (trade.Volume)" << trade.Volume);
        on_trade(&trade);
    }
}

// {"base":"btc","biz":"spot","data":{"asks":[["6628.6245","0"],["6624.3958","0"]],"bids":[["6600.7846","0"],["6580.8484","0"]]},"quote":"usdt","type":"depth","zip":false}
void MDEngineCoinmex::onDepth(Document& json)
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

    KF_LOG_INFO(logger, "MDEngineCoinmex::onDepth:" << "base : " << base << "  quote: " << quote);

    std::string ticker = whiteList.GetKeyByValue(base + "_" +  quote);
    if(ticker.length() == 0) {
        KF_LOG_INFO(logger, "MDEngineCoinmex::onDepth: not in WhiteList , ignore it:" << "base : " << base << "  quote: " << quote);
        return;
    }

    KF_LOG_INFO(logger, "MDEngineCoinmex::onDepth:" << "(ticker) " << ticker);
    std::map<int64_t, uint64_t>*  asksPriceAndVolume;
    std::map<int64_t, uint64_t>*  bidsPriceAndVolume;

    auto iter = tickerAskPriceMap.find(ticker);
    if(iter != tickerAskPriceMap.end()) {
        asksPriceAndVolume = iter->second;
//        KF_LOG_INFO(logger, "MDEngineCoinmex::onDepth:" << "ticker : " << ticker << "  get from map (asksPriceAndVolume.size) " << asksPriceAndVolume->size());
    } else {
        asksPriceAndVolume = new std::map<int64_t, uint64_t>();
        tickerAskPriceMap.insert(std::pair<std::string, std::map<int64_t, uint64_t>*>(ticker, asksPriceAndVolume));
//        KF_LOG_INFO(logger, "MDEngineCoinmex::onDepth:" << "ticker : " << ticker << "  insert into map (asksPriceAndVolume.size) " << asksPriceAndVolume->size());
    }

    iter = tickerBidPriceMap.find(ticker);
    if(iter != tickerBidPriceMap.end()) {
        bidsPriceAndVolume = iter->second;
//        KF_LOG_INFO(logger, "MDEngineCoinmex::onDepth:" << "ticker : " << ticker << "  get from map (bidsPriceAndVolume.size) " << bidsPriceAndVolume->size());
    } else {
        bidsPriceAndVolume = new std::map<int64_t, uint64_t>();
        tickerBidPriceMap.insert(std::pair<std::string, std::map<int64_t, uint64_t>*>(ticker, bidsPriceAndVolume));
//        KF_LOG_INFO(logger, "MDEngineCoinmex::onDepth:" << "ticker : " << ticker << "  insert into map (bidsPriceAndVolume.size) " << bidsPriceAndVolume->size());
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
                    KF_LOG_INFO(logger, "MDEngineCoinmex::onDepth: ##########################################asksPriceAndVolume volume == 0############################# price:" << price<<  "  volume:"<< volume);
                } else {
                    asksPriceAndVolume->erase(price);
                    asksPriceAndVolume->insert(std::pair<int64_t, uint64_t>(price, volume));
                }
//                KF_LOG_INFO(logger, "MDEngineCoinmex::onDepth: asks price:" << price<<  "  volume:"<< volume);
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
                    KF_LOG_INFO(logger, "MDEngineCoinmex::onDepth: ##########################################bidsPriceAndVolume volume == 0############################# price:" << price<<  "  volume:"<< volume);

                } else {
                    bidsPriceAndVolume->erase(price);
                    bidsPriceAndVolume->insert(std::pair<int64_t, uint64_t>(price, volume));
                }
//                KF_LOG_INFO(logger, "MDEngineCoinmex::onDepth: bids price:" << price<<  "  volume:"<< volume);
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
//            KF_LOG_INFO(logger, "MDEngineCoinmex::onDepth:  LFPriceBook20Field AskLevels: (i)" << i << "(price)" << md.AskLevels[i].price<<  "  (volume)"<< md.AskLevels[i].volume);
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
//            KF_LOG_INFO(logger, "MDEngineCoinmex::onDepth:  LFPriceBook20Field BidLevels: (i) " << i << "(price)" << md.BidLevels[i].price<<  "  (volume)"<< md.BidLevels[i].volume);
        }
        md.BidLevelCount = size;
        sort_result.clear();


        strcpy(md.InstrumentID, ticker.c_str());
        strcpy(md.ExchangeID, "coinmex");

        KF_LOG_INFO(logger, "MDEngineCoinmex::onDepth: on_price_book_update");
        on_price_book_update(&md);
    }
}

std::string MDEngineCoinmex::parseJsonToString(const char* in)
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
std::string MDEngineCoinmex::createDepthJsonString(std::string base, std::string quote)
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

std::string MDEngineCoinmex::createFillsJsonString(std::string base, std::string quote)
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

std::string MDEngineCoinmex::createTickersJsonString()
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

void MDEngineCoinmex::loop()
{
    while(isRunning)
    {
            int n = lws_service( context, rest_get_interval_ms );
            std::cout << " 3.1415 loop() lws_service (n)" << n << std::endl;
    }

}

BOOST_PYTHON_MODULE(libcoinmexmd)
{
    using namespace boost::python;
    class_<MDEngineCoinmex, boost::shared_ptr<MDEngineCoinmex> >("Engine")
            .def(init<>())
            .def("init", &MDEngineCoinmex::initialize)
            .def("start", &MDEngineCoinmex::start)
            .def("stop", &MDEngineCoinmex::stop)
            .def("logout", &MDEngineCoinmex::logout)
            .def("wait_for_stop", &MDEngineCoinmex::wait_for_stop);
}
