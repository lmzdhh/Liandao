#include "MDEnginebitFlyer.h"
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

static MDEnginebitFlyer* global_md = nullptr;

static int ws_service_cb( struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len )
{

    switch( reason )
    {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
        {
            lws_callback_on_writable( wsi );
            break;
        }
        case LWS_CALLBACK_PROTOCOL_INIT:
        {
            break;
        }
        case LWS_CALLBACK_CLIENT_RECEIVE:
        {
            if(global_md)
            {
                global_md->on_lws_data(wsi, (const char*)in, len);
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_CLOSED:
        {
            if(global_md) {
                std::cout << "3.1415926 LWS_CALLBACK_CLIENT_CLOSED 2,  (call on_lws_connection_error)  reason = " << reason << std::endl;
                global_md->on_lws_connection_error(wsi);
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_RECEIVE_PONG:
        {
            break;
        }
        case LWS_CALLBACK_CLIENT_WRITEABLE:
        {
            if(global_md)
            {
                global_md->lws_write_subscribe(wsi);
            }
            break;
        }
	    case LWS_CALLBACK_TIMER:
        {
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

MDEnginebitFlyer::MDEnginebitFlyer(): IMDEngine(SOURCE_BITFLYER)
{
    logger = yijinjing::KfLog::getLogger("MdEngine.bitFlyer");
}

void MDEnginebitFlyer::load(const json& j_config)
{
    book_depth_count = j_config["book_depth_count"].get<int>();
    trade_count = j_config["trade_count"].get<int>();
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();
    KF_LOG_INFO(logger, "MDEnginebitFlyer:: rest_get_interval_ms: " << rest_get_interval_ms);


    coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
    coinPairWhiteList.Debug_print();

    makeWebsocketSubscribeJsonString();
    debug_print(websocketSubscribeJsonString);

    //display usage:
    if(coinPairWhiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "MDEnginebitFlyer::lws_write_subscribe: subscribeCoinBaseQuote is empty. please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"tBTCUSDT\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"tETCETH\"");
        KF_LOG_ERROR(logger, "},");
    }

    KF_LOG_INFO(logger, "MDEnginebitFlyer::load:  book_depth_count: "
            << book_depth_count << " trade_count: " << trade_count << " rest_get_interval_ms: " << rest_get_interval_ms);
}

void MDEnginebitFlyer::makeWebsocketSubscribeJsonString()
{
    std::unordered_map<std::string, std::string>::iterator map_itr;
    map_itr = coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().begin();
    while(map_itr != coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().end()) {
        KF_LOG_DEBUG(logger, "[makeWebsocketSubscribeJsonString] keyIsExchangeSideWhiteList (strategy_coinpair) " << map_itr->first << " (exchange_coinpair) "<< map_itr->second);

        std::string jsonBookString = createBookJsonString(map_itr->second);
        websocketSubscribeJsonString.push_back(jsonBookString);

        std::string jsonSnapshotString = createSnapshotJsonString(map_itr->second);
        websocketSubscribeJsonString.push_back(jsonSnapshotString);

        std::string jsonTradeString = createTradeJsonString(map_itr->second);
        websocketSubscribeJsonString.push_back(jsonTradeString);        

        map_itr++;
    }
}

void MDEnginebitFlyer::debug_print(std::vector<std::string> &subJsonString)
{
    size_t count = subJsonString.size();
    KF_LOG_INFO(logger, "[debug_print] websocketSubscribeJsonString (count) " << count);

    for (size_t i = 0; i < count; i++)
    {
        KF_LOG_INFO(logger, "[debug_print] websocketSubscribeJsonString (subJsonString) " << subJsonString[i]);
    }
}

void MDEnginebitFlyer::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "MDEnginebitFlyer::connect:");
    connected = true;
}

void MDEnginebitFlyer::login(long timeout_nsec) {
    KF_LOG_INFO(logger, "MDEnginebitFlyer::login:");
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
        KF_LOG_INFO(logger, "MDEnginebitFlyer::login: context created.");
    }

    if (context == NULL) {
        KF_LOG_ERROR(logger, "MDEnginebitFlyer::login: context is NULL. return");
        return;
    }

    int logs = LLL_ERR | LLL_DEBUG | LLL_WARN;
    lws_set_log_level(logs, NULL);

    struct lws_client_connect_info ccinfo = {0};

    static std::string host  = "ws.lightstream.bitflyer.com";
    static std::string path = "/json-rpc";
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
    KF_LOG_INFO(logger, "MDEnginebitFlyer::login: Connecting to " <<  ccinfo.host << ":" << ccinfo.port << ":" << ccinfo.path);

    if (wsi == NULL) {
        KF_LOG_ERROR(logger, "MDEnginebitFlyer::login: wsi create error.");
        return;
    }
    KF_LOG_INFO(logger, "MDEnginebitFlyer::login: wsi create success.");

    logged_in = true;
}

void MDEnginebitFlyer::set_reader_thread()
{
    IMDEngine::set_reader_thread();

    rest_thread = ThreadPtr(new std::thread(boost::bind(&MDEnginebitFlyer::loop, this)));
}

void MDEnginebitFlyer::logout()
{
    KF_LOG_INFO(logger, "MDEnginebitFlyer::logout:");
}

void MDEnginebitFlyer::release_api()
{
    KF_LOG_INFO(logger, "MDEnginebitFlyer::release_api:");
}

void MDEnginebitFlyer::subscribeMarketData(const vector<string>& instruments, const vector<string>& markets)
{
    KF_LOG_INFO(logger, "MDEnginebitFlyer::subscribeMarketData:");
}

int MDEnginebitFlyer::lws_write_subscribe(struct lws* conn)
{
    KF_LOG_INFO(logger, "MDEnginebitFlyer::lws_write_subscribe: (subscribe_index)" << subscribe_index);

    //有待发送的数据，先把待发送的发完，在继续订阅逻辑。  ping?
    if(websocketPendingSendMsg.size() > 0) {
        unsigned char msg[512];
        memset(&msg[LWS_PRE], 0, 512-LWS_PRE);

        std::string jsonString = websocketPendingSendMsg[websocketPendingSendMsg.size() - 1];
        websocketPendingSendMsg.pop_back();
        KF_LOG_INFO(logger, "MDEnginebitFlyer::lws_write_subscribe: websocketPendingSendMsg" << jsonString.c_str());
        int length = jsonString.length();

        strncpy((char *)msg+LWS_PRE, jsonString.c_str(), length);
        int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);

        if(websocketPendingSendMsg.size() > 0)
        {    //still has pending send data, emit a lws_callback_on_writable()
            lws_callback_on_writable( conn );
            KF_LOG_INFO(logger, "MDEnginebitFlyer::lws_write_subscribe: (websocketPendingSendMsg,size)" << websocketPendingSendMsg.size());
        }
        return ret;
    }

    if(websocketSubscribeJsonString.size() == 0) return 0;
    //sub depth
    if(subscribe_index >= websocketSubscribeJsonString.size())
    {
        //subscribe_index = 0;
        KF_LOG_INFO(logger, "MDEnginebitFlyer::lws_write_subscribe: (none reset subscribe_index = 0, just return 0)");
	    return 0;
    }

    unsigned char msg[512];
    memset(&msg[LWS_PRE], 0, 512-LWS_PRE);

    std::string jsonString = websocketSubscribeJsonString[subscribe_index++];

    KF_LOG_INFO(logger, "MDEnginebitFlyer::lws_write_subscribe: " << jsonString.c_str() << " ,after ++, (subscribe_index)" << subscribe_index);
    int length = jsonString.length();

    strncpy((char *)msg+LWS_PRE, jsonString.c_str(), length);
    int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);

    if(subscribe_index < websocketSubscribeJsonString.size())
    {
        lws_callback_on_writable( conn );
        KF_LOG_INFO(logger, "MDEnginebitFlyer::lws_write_subscribe: (subscribe_index < websocketSubscribeJsonString) call lws_callback_on_writable");
    }

    return ret;
}

void MDEnginebitFlyer::on_lws_data(struct lws* conn, const char* data, size_t len)
{
    KF_LOG_INFO(logger, "MDEnginebitFlyer::on_lws_data: " << data);
    Document json;
    json.Parse(data);

    if(json.HasParseError()) {
        KF_LOG_ERROR(logger, "MDEnginebitFlyer::on_lws_data. parse json error: " << data);
        return;
    }

/*    if(json.IsObject() && json.HasMember("method")) {
        if (strcmp(json["method"].GetString(), "info") == 0) {
            KF_LOG_INFO(logger, "MDEnginebitFlyer::on_lws_data: is info");
            onInfo(json);
        } else if (strcmp(json["method"].GetString(), "ping") == 0) {
            KF_LOG_INFO(logger, "MDEnginebitFlyer::on_lws_data: is ping");
            onPing(conn, json);
        } else if (strcmp(json["method"].GetString(), "channelMessage") == 0) {
            KF_LOG_INFO(logger, "MDEnginebitFlyer::on_lws_data: is subscribed");
            onSubscribed(json);
        } else {
            KF_LOG_INFO(logger, "MDEnginebitFlyer::on_lws_data: unknown method: " << data);
        };
    }
*/

    //data

 /*       int chanId = json.GetArray()[0].GetInt();
        KF_LOG_INFO(logger, "MDEnginebitFlyer::on_lws_data: (chanId)" << chanId);

        SubscribeChannel channel = findByChannelID( chanId );
        if (channel.channelId == 0) {
            KF_LOG_ERROR(logger, "MDEnginebitFlyer::on_lws_data: EMPTY_CHANNEL (chanId)" << chanId);*/

        string channame = json["params"].GetObject()["channel"].GetString();;
        KF_LOG_INFO(logger, "MDEnginebitFlyer::on_lws_data: (channame)" << channame);


        //SubscribeChannel channel = findByChannelNAME( channame );
        if (channame==" ") {
            KF_LOG_ERROR(logger, "MDEnginebitFlyer::on_lws_data: EMPTY_CHANNEL (channame)" << channame);
        } else {
            if (channame=="lightning_board_snapshot_BTC_JPY"||channame=="lightning_board_snapshot_FX_BTC_JPY"||channame=="lightning_board_snapshot_ETH_BTC") {
                KF_LOG_INFO(logger, "MDEnginebitFlyer::on_lws_data: is book");
                onBook(json);
            } else if (channame=="lightning_board_BTC_JPY"||channame=="lightning_board_FX_BTC_JPY"||channame=="lightning_board_ETH_BTC") {
                KF_LOG_INFO(logger, "MDEnginebitFlyer::on_lws_data: is upbook");
                onBook(json);
            } else if(channame=="lightning_executions_BTC_JPY"||channame=="lightning_executions_FX_BTC_JPY"||channame=="lightning_executions_ETH_BTC") {
                KF_LOG_INFO(logger, "MDEnginebitFlyer::on_lws_data: is trade");
                onTrade(json);               
            }

            else {
                KF_LOG_INFO(logger, "MDEnginebitFlyer::on_lws_data: unknown array data: " << data);
            }
        }
    
}


void MDEnginebitFlyer::on_lws_connection_error(struct lws* conn)
{
    KF_LOG_ERROR(logger, "MDEnginebitFlyer::on_lws_connection_error.");
    //market logged_in false;
    logged_in = false;
    KF_LOG_ERROR(logger, "MDEnginebitFlyer::on_lws_connection_error. login again.");
    //clear the price book, the new websocket will give 200 depth on the first connect, it will make a new price book
    priceBook20Assembler.clearPriceBook();
    //no use it
    long timeout_nsec = 0;
    //reset sub
    subscribe_index = 0;

    login(timeout_nsec);
}

/*
int64_t MDEnginebitFlyer::getTimestamp()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp;
}
*/

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
/*
void MDEnginebitFlyer::onPing(struct lws* conn, Document& json)
{
    KF_LOG_INFO(logger, "MDEnginebitFlyer::onPing: " << parseJsonToString(json));
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();
    writer.Key("method");
    writer.String("pong");

    writer.Key("ts");
    writer.Int64(getTimestamp());

    writer.Key("cid");
    writer.Int(json["cid"].GetInt());

    writer.EndObject();

    std::string result = s.GetString();
    KF_LOG_INFO(logger, "MDEnginebitFlyer::onPing: (Pong)" << result);
    websocketPendingSendMsg.push_back(result);

    //emit a callback
    lws_callback_on_writable( conn );
}
*/

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
/*
void MDEnginebitFlyer::onInfo(Document& json)
{
    KF_LOG_INFO(logger, "MDEnginebitFlyer::onInfo: " << parseJsonToString(json));
}
*/

//{\event\:\subscribed\,\channel\:\book\,\chanId\:56,\symbol\:\tETCBTC\,\prec\:\P0\,\freq\:\F0\,\len\:\25\,\pair\:\ETCBTC\}
//{\event\:\subscribed\,\channel\:\trades\,\chanId\:2337,\symbol\:\tETHBTC\,\pair\:\ETHBTC\}
//{\event\:\subscribed\,\channel\:\trades\,\chanId\:1,\symbol\:\tBTCUSD\,\pair\:\BTCUSD\}
/*
void MDEnginebitFlyer::onSubscribed(Document& json)
{
    KF_LOG_INFO(logger, "MDEnginebitFlyer::onSubscribed: " << parseJsonToString(json));

    if(json.HasMember("params")) {
        string channame=json["params"].GetObject()["channel"].GetString();

        if(channame=="lightning_board_snapshot_BTC_JPY"||channame=="lightning_board_snapshot_FX_BTC_JPY"||channame=="lightning_board_snapshot_ETH_BTC") {
            SubscribeChannel newChannel;
            newChannel.subType = book_channel;
            newChannel.channelname = channame;
            websocketSubscribeChannel.push_back(newChannel);
        }

        if(channame=="lightning_board_BTC_JPY"||channame=="lightning_board_FX_BTC_JPY"||channame=="lightning_board_ETH_BTC") {
            SubscribeChannel newChannel;
            newChannel.subType = book_channel;
            newChannel.channelname = channame;
            websocketSubscribeChannel.push_back(newChannel);
        }
        
        if(channame=="lightning_executions_BTC_JPY"||channame=="lightning_executions_FX_BTC_JPY"||channame=="lightning_executions_ETH_BTC") {
            SubscribeChannel newChannel;
            newChannel.subType = trade_channel;
            newChannel.channelname = channame;
            websocketSubscribeChannel.push_back(newChannel);
        }        
      
    }

    debug_print(websocketSubscribeChannel);
}
*/

void MDEnginebitFlyer::debug_print(std::vector<SubscribeChannel> &websocketSubscribeChannel)
{
    size_t count = websocketSubscribeChannel.size();
    KF_LOG_INFO(logger, "[debug_print] websocketSubscribeChannel (count) " << count);

    for (size_t i = 0; i < count; i++)
    {
        KF_LOG_INFO(logger, "[debug_print] websocketSubscribeChannel (subType) "
                            << websocketSubscribeChannel[i].subType <<
                            " (channelname)" << websocketSubscribeChannel[i].channelname);
    }
}

/*SubscribeChannel MDEnginebitFlyer::findByChannelID(int channelId)
{
    size_t count = websocketSubscribeChannel.size();

    for (size_t i = 0; i < count; i++)
    {
        if(channelId == websocketSubscribeChannel[i].channelId) {
            return websocketSubscribeChannel[i];
        }
    }
    return EMPTY_CHANNEL;
}*/
/*
SubscribeChannel MDEnginebitFlyer::findByChannelNAME(string channelname)
{
    size_t count = websocketSubscribeChannel.size();

    for (size_t i = 0; i < count; i++)
    {
        if(channelname == websocketSubscribeChannel[i].channelname) {
            return websocketSubscribeChannel[i];
        }
    }
    return EMPTY_CHANNEL;
}
*/

//[1,[[279619183,1534151022575,0.05404775,6485.1],[279619171,1534151022010,-1.04,6485],[279619170,1534151021847,-0.02211732,6485],......]
//[1,"te",[279619192,1534151024181,-0.05678467,6485]]
void MDEnginebitFlyer::onTrade(Document& json)
{
/*    KF_LOG_INFO(logger, "MDEnginebitFlyer::onTrade: (symbol) " << channel.exchange_coinpair);

    std::string ticker = coinPairWhiteList.GetKeyByValue(channel.exchange_coinpair);
    if(ticker.length() == 0) {
        return;
    }*/

    std::string ticker;
    Value &node1=json["params"];
    std::string pair=node1["channel"].GetString();
    std::string TICKER=pair.erase(0,21);
    ticker = coinPairWhiteList.GetKeyByValue(TICKER);

    Value &node=node1["message"];
    if(node.IsArray()) {
        int len = node.GetArray().Size();
        if(len == 0) {
            return;
        }
        for(int i = 0; i < len; i++) {
            LFL2TradeField trade;
            memset(&trade, 0, sizeof(trade));
            strcpy(trade.InstrumentID, ticker.c_str());
            strcpy(trade.ExchangeID, "bitflyer");

            trade.Price = std::round(node.GetArray()[i]["price"].GetDouble() * scale_offset);
            trade.Volume = std::round(node.GetArray()[i]["size"].GetDouble() * scale_offset);
            std::string side = node.GetArray()[i]["side"].GetString();
            trade.OrderBSFlag[0] = side == "BUY" ? 'B' : 'S';    

            KF_LOG_INFO(logger, "MDEnginebitFlyer::[onTrade] (ticker)" << ticker <<
                                                                           " (Price)" << trade.Price <<
                                                                           " (trade.Volume)" << trade.Volume);
                                                                       
            on_trade(&trade);
        }
    }
    
}


void MDEnginebitFlyer::onBook(Document& json)
{
/*    KF_LOG_INFO(logger, "MDEnginebitFlyer::onBook: (symbol) " << channel.exchange_coinpair);

    std::string ticker = coinPairWhiteList.GetKeyByValue(channel.exchange_coinpair);
    if(ticker.length() == 0) {
        return;
    }*/

//    KF_LOG_INFO(logger, "MDEnginebitFlyer::onBook: (ticker) " << ticker);

/*    if(json.IsArray()){
        int len=json.Size();
        for (int i = 0; i < len; i++) {
            int64_t price = std::round(stod(json.GetArray()[i]["prize"].GetString()) * scale_offset);
            uint64_t volume = std::round(stod(json.GetArray()[i]["size"].GetString()) * scale_offset);
            if(volume == 0) {
                priceBook20Assembler.EraseBidPrice(ticker, price);
            } else {
                priceBook20Assembler.UpdateBidPrice(ticker, price, volume);
            }
        }*/

    std::string ticker;
    Value &node1=json["params"];
    std::string pair=node1["channel"].GetString();
 /*   std::string TICKER=pair.erase(0,16);
    ticker = coinPairWhiteList.GetKeyByValue(TICKER);*/   

    if(pair=="lightning_board_BTC_JPY"||pair=="lightning_board_snapshot_BTC_JPY"){
        ticker="btc_jpy";
    }
    if(pair=="lightning_board_FX_BTC_JPY"||pair=="lightning_board_snapshot_FX_BTC_JPY"){
        ticker="fx_btc_jpy";
    }
    if(pair=="lightning_board_ETH_BTC"||pair=="lightning_board_snapshot_ETH_BTC"){
        ticker="eth_btc";
    }
    KF_LOG_INFO(logger, "MDEnginebitFlyer::onBook: (ticker) " << ticker);

    if(pair=="lightning_board_snapshot_BTC_JPY"||pair=="lightning_board_snapshot_FX_BTC_JPY"||pair=="lightning_board_snapshot_ETH_BTC") {
    Value &node=node1["message"];    
    int len = node["bids"].GetArray().Size();
    if(len==0){
        return;
    }    
        for (int i = 0; i < len; i++) {
            int64_t price = std::round(node["bids"].GetArray()[i]["price"].GetDouble() * scale_offset);
            uint64_t volume = std::round(node["bids"].GetArray()[i]["size"].GetDouble() * scale_offset);
            if(volume == 0) {
                priceBook20Assembler.EraseBidPrice(ticker, price);
            } else {
                priceBook20Assembler.UpdateBidPrice(ticker, price, volume);
            }
        }

    int len1 = node["asks"].GetArray().Size();
    if(len1==0){
        return;
    }    
        for (int i = 0; i < len1; i++) {
            int64_t price = std::round(node["asks"].GetArray()[i]["price"].GetDouble() * scale_offset);
            uint64_t volume = std::round(node["asks"].GetArray()[i]["size"].GetDouble() * scale_offset);
            if(volume == 0) {
                priceBook20Assembler.EraseAskPrice(ticker, price);
            } else {
                priceBook20Assembler.UpdateAskPrice(ticker, price, volume);
            }
        }
    }
    else{
    Value &node=node1["message"];    
    int len = node["bids"].GetArray().Size();
    if(len==0){
        return;
    }    
        for (int i = 0; i < len; i++) {
            int64_t price = std::round(node["bids"].GetArray()[i]["price"].GetDouble() * scale_offset);
            uint64_t volume = std::round(node["bids"].GetArray()[i]["size"].GetDouble() * scale_offset);
            if(volume == 0) {
                priceBook20Assembler.EraseBidPrice(ticker, price);
            } else {
                priceBook20Assembler.UpdateBidPrice(ticker, price, volume);
            }
        }

    int len1 = node["asks"].GetArray().Size();
    if(len1==0){
        return;
    }    
        for (int i = 0; i < len1; i++) {
            int64_t price = std::round(node["asks"].GetArray()[i]["price"].GetDouble() * scale_offset);
            uint64_t volume = std::round(node["asks"].GetArray()[i]["size"].GetDouble() * scale_offset);
            if(volume == 0) {
                priceBook20Assembler.EraseAskPrice(ticker, price);
            } else {
                priceBook20Assembler.UpdateAskPrice(ticker, price, volume);
            }
        }            
    }
    
    // has any update
    LFPriceBook20Field md;
    memset(&md, 0, sizeof(md));
    if(priceBook20Assembler.Assembler(ticker, md)) {
        strcpy(md.ExchangeID, "bitflyer");

        KF_LOG_INFO(logger, "MDEnginebitFlyer::onDepth: on_price_book_update");
        on_price_book_update(&md);
    }
}

/*
void MDEnginebitFlyer::kline(Document& json)
{
    KF_LOG_INFO(logger, "MDEnginebitFlyer::onBook: (symbol) " << channel.exchange_coinpair);

    std::string ticker = coinPairWhiteList.GetKeyByValue(channel.exchange_coinpair);
    if(ticker.length() == 0) {
        return;
    }

 //   KF_LOG_INFO(logger, "MDEnginebitFlyer::onBook: (ticker) " << ticker);

    std::string ticker;
    std::string pair=json["params"].GetObject()["channel"].GetString();
    if(pair=="lightning_executions_BTC_JPY"){
        ticker="btc_jpy";
    }
    if(pair=="lightning_executions_FX_BTC_JPY"){
        ticker="fx_btc_jpy";
    }
    if(pair=="lightning_executions_ETH_BTC"){
        ticker="eth_btc";
    }

    LFMarketDataField md;
    memset(&md, 0, sizeof(md));

    bool has_update = false;            
    if(json.HasMember("bids") && json["bids"].IsArray())
    {
        md.BidPrice1 = stod(json["bids"].GetArray()[0]["prize"].GetString()) * scale_offset;
        md.BidVolume1 = stod(json["bids"].GetArray()[0]["size"].GetString()) * scale_offset;
        md.BidPrice2 = stod(json["bids"].GetArray()[1]["prize"].GetString()) * scale_offset;
        md.BidVolume2 = stod(json["bids"].GetArray()[1]["size"].GetString()) * scale_offset;
        md.BidPrice3 = stod(json["bids"].GetArray()[2]["prize"].GetString()) * scale_offset;
        md.BidVolume3 = stod(json["bids"].GetArray()[2]["size"].GetString()) * scale_offset;
        md.BidPrice4 = stod(json["bids"].GetArray()[3]["prize"].GetString()) * scale_offset;
        md.BidVolume4 = stod(json["bids"].GetArray()[3]["size"].GetString()) * scale_offset;
        md.BidPrice5 = stod(json["bids"].GetArray()[4]["prize"].GetString()) * scale_offset;
        md.BidVolume5 = stod(json["bids"].GetArray()[4]["size"].GetString()) * scale_offset;
        
        has_update = true;
    }

    if(json.HasMember("asks") && json["asks"].IsArray())
    {
        md.AskPrice1 = stod(json["asks"].GetArray()[0]["prize"].GetString()) * scale_offset;
        md.AskVolume1 = stod(json["asks"].GetArray()[0]["size"].GetString()) * scale_offset;
        md.AskPrice2 = stod(json["asks"].GetArray()[1]["prize"].GetString()) * scale_offset;
        md.AskVolume2 = stod(json["asks"].GetArray()[1]["size"].GetString()) * scale_offset;
        md.AskPrice3 = stod(json["asks"].GetArray()[2]["prize"].GetString()) * scale_offset;
        md.AskVolume3 = stod(json["asks"].GetArray()[2]["size"].GetString()) * scale_offset;
        md.AskPrice4 = stod(json["asks"].GetArray()[3]["prize"].GetString()) * scale_offset;
        md.AskVolume4 = stod(json["asks"].GetArray()[3]["size"].GetString()) * scale_offset;
        md.AskPrice5 = stod(json["asks"].GetArray()[4]["prize"].GetString()) * scale_offset;
        md.AskVolume5 = stod(json["asks"].GetArray()[4]["size"].GetString()) * scale_offset;
        
        has_update = true;
    }
    
    if(has_update)
    {
        strcpy(md.InstrumentID, ticker.c_str());
        strcpy(md.ExchangeID, "bitflyer");
        md.UpdateMillisec = last_rest_get_ts;

        on_market_data(&md);
    } 
*/


std::string MDEnginebitFlyer::parseJsonToString(Document &d)
{
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);

    return buffer.GetString();
}


//{ "event": "subscribe", "channel": "book",  "symbol": "tBTCUSD" }
std::string MDEnginebitFlyer::createBookJsonString(std::string exchange_coinpair)
{
    std::string strChannel="lightning_board_";
    strChannel+=exchange_coinpair.c_str(); 
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();

    writer.Key("jsonrpc");
    writer.String("2.0");

    writer.Key("method");
    writer.String("subscribe");

    writer.Key("params");
    writer.StartObject();
    
    writer.Key("channel");
    writer.String(strChannel.c_str());

    writer.EndObject();

    writer.EndObject();
    return s.GetString();
}

//{ "event": "subscribe", "channel": "trades",  "symbol": "tETHBTC" }
std::string MDEnginebitFlyer::createSnapshotJsonString(std::string exchange_coinpair)
{
    std::string strChannel="lightning_board_snapshot_";
    strChannel+=exchange_coinpair.c_str(); 
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();

    writer.Key("jsonrpc");
    writer.String("2.0");

    writer.Key("method");
    writer.String("subscribe");

    writer.Key("params");
    writer.StartObject();
    
    writer.Key("channel");
    writer.String(strChannel.c_str());

    writer.EndObject();

    writer.EndObject();
    return s.GetString();
}

std::string MDEnginebitFlyer::createTradeJsonString(std::string exchange_coinpair)
{
    std::string strChannel="lightning_executions_";
    strChannel+=exchange_coinpair.c_str(); 
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();

    writer.Key("jsonrpc");
    writer.String("2.0");

    writer.Key("method");
    writer.String("subscribe");

    writer.Key("params");
    writer.StartObject();
    
    writer.Key("channel");
    writer.String(strChannel.c_str());

    writer.EndObject();

    writer.EndObject();
    return s.GetString();
}

void MDEnginebitFlyer::loop()
{
    while(isRunning)
    {
        int n = lws_service( context, rest_get_interval_ms );
        std::cout << " 3.1415 loop() lws_service (n)" << n << std::endl;
    }
}

BOOST_PYTHON_MODULE(libbitflyermd)
{
    using namespace boost::python;
    class_<MDEnginebitFlyer, boost::shared_ptr<MDEnginebitFlyer> >("Engine")
            .def(init<>())
            .def("init", &MDEnginebitFlyer::initialize)
            .def("start", &MDEnginebitFlyer::start)
            .def("stop", &MDEnginebitFlyer::stop)
            .def("logout", &MDEnginebitFlyer::logout)
            .def("wait_for_stop", &MDEnginebitFlyer::wait_for_stop);
}

