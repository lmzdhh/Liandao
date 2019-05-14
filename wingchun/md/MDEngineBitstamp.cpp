#include "MDEngineBitstamp.h"
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

static MDEngineBitstamp* global_md = nullptr;

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

MDEngineBitstamp::MDEngineBitstamp(): IMDEngine(SOURCE_BITSTAMP)
{
    logger = yijinjing::KfLog::getLogger("MdEngine.Bitstamp");
}

void MDEngineBitstamp::load(const json& j_config) 
{
    book_depth_count = j_config["book_depth_count"].get<int>();
    trade_count = j_config["trade_count"].get<int>();
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();
    KF_LOG_INFO(logger, "MDEngineBitstamp:: rest_get_interval_ms: " << rest_get_interval_ms);


    coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
    coinPairWhiteList.Debug_print();

    makeWebsocketSubscribeJsonString();
    debug_print(websocketSubscribeJsonString);

    //display usage:
    if(coinPairWhiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "MDEngineBitstamp::lws_write_subscribe: subscribeCoinBaseQuote is empty. please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"tBTCUSDT\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"tETCETH\"");
        KF_LOG_ERROR(logger, "},");
    }

    KF_LOG_INFO(logger, "MDEngineBitstamp::load:  book_depth_count: "
            << book_depth_count << " trade_count: " << trade_count << " rest_get_interval_ms: " << rest_get_interval_ms);
}


// {
//     "event": "bts:subscribe",
//     "data": {
//         "channel": "live_orders_btcusd"
//     }
// }
std::string MDEngineBitstamp::createOrderJsonString(std::string exchange_coinpair)
{
    std::string data = "order_book_"+exchange_coinpair;
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();
    writer.Key("event");
    writer.String("bts:subscribe");
    writer.Key("data");
    writer.StartObject();
    writer.Key("channel");
    writer.String(data.c_str());
    writer.EndObject();
    
    writer.EndObject();
    return s.GetString();
}

// {
//     "event": "bts:subscribe",
//     "data": {
//         "channel": "live_trades_btcusd"
//     }
// }
std::string MDEngineBitstamp::createTradeJsonString(std::string exchange_coinpair)
{
    std::string data = "live_trades_"+exchange_coinpair;
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();
    writer.Key("event");
    writer.String("bts:subscribe");
    writer.Key("data");
    writer.StartObject();
    writer.Key("channel");
    writer.String(data.c_str());
    writer.EndObject();
    
    writer.EndObject();
    return s.GetString();
}

void MDEngineBitstamp::makeWebsocketSubscribeJsonString()//创建请求
{
    std::unordered_map<std::string, std::string>::iterator map_itr;
    map_itr = coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().begin();
    while(map_itr != coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().end()) {
        KF_LOG_DEBUG(logger, "[makeWebsocketSubscribeJsonString] keyIsExchangeSideWhiteList (strategy_coinpair) " << map_itr->first << " (exchange_coinpair) "<< map_itr->second);

        std::string jsonBookString = createOrderJsonString(map_itr->second);
        websocketSubscribeJsonString.push_back(jsonBookString);

        std::string jsonTradeString = createTradeJsonString(map_itr->second);
        websocketSubscribeJsonString.push_back(jsonTradeString);

        map_itr++;
    }
}

void MDEngineBitstamp::debug_print(std::vector<std::string> &subJsonString)
{
    size_t count = subJsonString.size();
    KF_LOG_INFO(logger, "[debug_print] websocketSubscribeJsonString (count) " << count);

    for (size_t i = 0; i < count; i++)
    {
        KF_LOG_INFO(logger, "[debug_print] websocketSubscribeJsonString (subJsonString) " << subJsonString[i]);
    }
}

void MDEngineBitstamp::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "MDEngineBitstamp::connect:");
    connected = true;
}

void MDEngineBitstamp::login(long timeout_nsec) {//连接到服务器
    KF_LOG_INFO(logger, "MDEngineBitstamp::login:");
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
        KF_LOG_INFO(logger, "MDEngineBitstamp::login: context created.");
    }

    if (context == NULL) {
        KF_LOG_ERROR(logger, "MDEngineBitstamp::login: context is NULL. return");
        return;
    }

    int logs = LLL_ERR | LLL_DEBUG | LLL_WARN;
    lws_set_log_level(logs, NULL);

    struct lws_client_connect_info ccinfo = {0};

    static std::string host  = "wss://ws.bitstamp.net";
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
    KF_LOG_INFO(logger, "MDEngineBitstamp::login: Connecting to " <<  ccinfo.host << ":" << ccinfo.port << ":" << ccinfo.path);

    if (wsi == NULL) {
        KF_LOG_ERROR(logger, "MDEngineBitstamp::login: wsi create error.");
        return;
    }
    KF_LOG_INFO(logger, "MDEngineBitstamp::login: wsi create success.");

    logged_in = true;
}

void MDEngineBitstamp::set_reader_thread()
{
    IMDEngine::set_reader_thread();

    rest_thread = ThreadPtr(new std::thread(boost::bind(&MDEngineBitstamp::loop, this)));
}

void MDEngineBitstamp::logout()
{
    KF_LOG_INFO(logger, "MDEngineBitstamp::logout:");
}

void MDEngineBitstamp::release_api()
{
    KF_LOG_INFO(logger, "MDEngineBitstamp::release_api:");
}

void MDEngineBitstamp::subscribeMarketData(const vector<string>& instruments, const vector<string>& markets)
{
    KF_LOG_INFO(logger, "MDEngineBitstamp::subscribeMarketData:");
}



int MDEngineBitstamp::lws_write_subscribe(struct lws* conn)
{
    KF_LOG_INFO(logger, "MDEngineBitstamp::lws_write_subscribe: (subscribe_index)" << subscribe_index);

    // //有待发送的数据，先把待发送的发完，在继续订阅逻辑  
    // if(websocketPendingSendMsg.size() > 0) {
    //     unsigned char msg[512];
    //     memset(&msg[LWS_PRE], 0, 512-LWS_PRE);

    //     std::string jsonString = websocketPendingSendMsg[websocketPendingSendMsg.size() - 1];
    //     websocketPendingSendMsg.pop_back();
    //     KF_LOG_INFO(logger, "MDEngineBitstamp::lws_write_subscribe: websocketPendingSendMsg" << jsonString.c_str());
    //     int length = jsonString.length();

    //     strncpy((char *)msg+LWS_PRE, jsonString.c_str(), length);
    //     int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);

    //     if(websocketPendingSendMsg.size() > 0)
    //     {    //still has pending send data, emit a lws_callback_on_writable()
    //         lws_callback_on_writable( conn );
    //         KF_LOG_INFO(logger, "MDEngineBitstamp::lws_write_subscribe: (websocketPendingSendMsg,size)" << websocketPendingSendMsg.size());
    //     }
    //     return ret;
    // }

    // if(websocketSubscribeJsonString.size() == 0) return 0;//
    // //sub depth
    // if(subscribe_index >= websocketSubscribeJsonString.size())
    // {
    //     //subscribe_index = 0;
    //     KF_LOG_INFO(logger, "MDEngineBitstamp::lws_write_subscribe: (none reset subscribe_index = 0, just return 0)");
	//     return 0;
    // }

    unsigned char msg[512];
    memset(&msg[LWS_PRE], 0, 512-LWS_PRE);

    std::string jsonString = websocketSubscribeJsonString[subscribe_index++];

    KF_LOG_INFO(logger, "MDEngineBitstamp::lws_write_subscribe: " << jsonString.c_str() << " ,after ++, (subscribe_index)" << subscribe_index);
    int length = jsonString.length();

    strncpy((char *)msg+LWS_PRE, jsonString.c_str(), length);
    int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);

    if(subscribe_index < websocketSubscribeJsonString.size())
    {
        lws_callback_on_writable( conn );
        KF_LOG_INFO(logger, "MDEngineBitstamp::lws_write_subscribe: (subscribe_index < websocketSubscribeJsonString) call lws_callback_on_writable");
    }

    return ret;
}

void MDEngineBitstamp::on_lws_data(struct lws* conn, const char* data, size_t len)
{
    KF_LOG_INFO(logger, "MDEngineBitstamp::on_lws_data: " << data);
    Document json;
    json.Parse(data);

    if(json.HasParseError()) {
        KF_LOG_ERROR(logger, "MDEngineBitstamp::on_lws_data. parse json error: " << data);
        return;
    }
   
    //vector<string> v = split(json["channel"].GetString(), "_");
    if(strcmp(json["event"].GetString(),"data") == 0){
            onBook(json);
    }
    else if(strcmp(json["event"].GetString(),"trade") == 0){
            onTrade(json);
    }
    KF_LOG_INFO(logger, "MDEngineBitstamp::on_lws_data: unknown data: " << parseJsonToString(json));
}


void MDEngineBitstamp::on_lws_connection_error(struct lws* conn) //liu
{
    KF_LOG_ERROR(logger, "MDEngineBitstamp::on_lws_connection_error.");
    //market logged_in false;
    logged_in = false;
    KF_LOG_ERROR(logger, "MDEngineBitstamp::on_lws_connection_error. login again.");
    //clear the price book, the new websocket will give 200 depth on the first connect, it will make a new price book
    priceBook20Assembler.clearPriceBook();
    //no use it
    long timeout_nsec = 0;
    //reset sub
    subscribe_index = 0;

    login(timeout_nsec);
}

int64_t MDEngineBitstamp::getTimestamp()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp;
}



void MDEngineBitstamp::debug_print(std::vector<SubscribeChannel> &websocketSubscribeChannel)
{
    size_t count = websocketSubscribeChannel.size();
    KF_LOG_INFO(logger, "[debug_print] websocketSubscribeChannel (count) " << count);

    for (size_t i = 0; i < count; i++)
    {
        KF_LOG_INFO(logger, "[debug_print] websocketSubscribeChannel (subType) "
                            << websocketSubscribeChannel[i].subType <<
                            " (exchange_coinpair)" << websocketSubscribeChannel[i].exchange_coinpair <<
                            " (channelId)" << websocketSubscribeChannel[i].channelId);
    }
}

  //     {
    //     \data\: {
    //         \microtimestamp\: \1557712345770094\,
    //         \amount\: 0.013745220000000001,
    //         \buy_order_id\: 3274515232,//type = 0 ->LFDataStruct.h中LFL2TradeField的makerorderID,否则为takerOrderID
    //         \sell_order_id\: 3274511591,
    //         \amount_str\: \0.01374522\,
    //         \price_str\: \7030.80\,
    //         \timestamp\: \1557712345\,
    //         \price\: 7030.8000000000002,
    //         \type\: 0,  (0 - buy; 1 - sell)
    //         \id\: 87447785
    //         },
    //     \event\: \trade\,
    //     \channel\: \live_trades_btcusd\
    // }
void MDEngineBitstamp::onTrade(Document& json)
{

    // vector<String> data = split(json["channel"].GetString(),"_"); // btcusd
    // std::string ticker = data[3]+data[4]+data[5]+"_"+data[0]+data[1]+data[2]; //usd_btc
    std::string ticker = "usd_btc";
     KF_LOG_INFO(logger, "MDEngineBitstamp::onTrade: (symbol) " << ticker.c_str());
        LFL2TradeField trade;
        memset(&trade, 0, sizeof(trade));
        strcpy(trade.InstrumentID, ticker.c_str());
        strcpy(trade.ExchangeID, "bitstamp");

        trade.Price = std::round(json["data"].GetObject()["price"].GetDouble() * scale_offset);
        double amount = json["data"].GetObject()["amount"].GetDouble();
        uint64_t volume = 0;
        if(amount < 0) {
            volume = std::round(-1 * amount * scale_offset);
        } else {
            volume = std::round( amount * scale_offset);
             }

            if(json["data"].GetObject()["type"].GetInt() == 0){
                strcpy(trade.MakerOrderID,std::to_string(json["data"].GetObject()["buy_order_id"].GetInt()).c_str());
                strcpy(trade.TakerOrderID,std::to_string(json["data"].GetObject()["sell_order_id"].GetInt()).c_str());
            }
            else{
                strcpy(trade.MakerOrderID,std::to_string(json["data"].GetObject()["sell_order_id"].GetInt()).c_str());
                strcpy(trade.TakerOrderID,std::to_string(json["data"].GetObject()["buy_order_id"].GetInt()).c_str());
            };
            trade.Volume = volume;
            trade.OrderBSFlag[0] = json["data"].GetObject()["type"].GetInt() == 0 ? 'B' : 'S';
            strcpy(trade.TradeID,std::to_string(json["data"].GetObject()["id"].GetInt()).c_str());
            strcpy(trade.TradeTime,((std::string)json["data"].GetObject()["microtimestamp"].GetString()).c_str());

            KF_LOG_INFO(logger, "MDEngineBitstamp::[onTrade]"  <<
                                                                " (Price)" << trade.Price <<
                                                                " (trade.Volume)" << trade.Volume);
            on_trade(&trade);
}

 // {
        // "data": {
        //         "timestamp": "1557731190", 
        //         "microtimestamp": "1557731190857523", 
        //         "bids": [["7055.00", "0.00720000"], ["7052.76", "2.00000000"], ["7051.05", "0.02000000"]],
        //         "asks": [["7057.17", "2.00000000"],  ["7180.00", "2.00000000"], ["7184.58", "0.20000000"]]
        //         }, 
        // "event": "data", 
        // "channel": "order_book_btcusd"
        // }
void MDEngineBitstamp::onBook(Document& json)
{
    
    //vector<String> data = split(json["channel"].GetString(),"_"); // btcusd
    //std::string ticker = data[3]+data[4]+data[5]+"_"+data[0]+data[1]+data[2]; //usd_btc
    std::string ticker = "usd_btc";
    KF_LOG_INFO(logger, "MDEngineBitstamp::onBook: (symbol) " << ticker.c_str());
    try
    {
        KF_LOG_DEBUG(logger, "onBook start");
        if (!json.HasMember("data"))
        {
            return;
        }
        auto& data = json["data"];
        if (!data.HasMember("bids"))
        {
            return;
        }
        auto& bids = data["bids"];

        if (!data.HasMember("asks"))
        {
            return;
        }
        auto& asks = data["asks"];
        LFPriceBook20Field priceBook {0};
        strcpy(priceBook.ExchangeID, "bitstamp");
        strcpy(priceBook.InstrumentID, ticker.c_str());
        if(bids.IsArray())
        {
            int i = 0;
            for(i = 0; i < std::min((int)bids.Size(),book_depth_count); i++)
            {
                priceBook.BidLevels[i].price = std::round(bids[i][0].GetDouble() * SCALE_OFFSET);
                priceBook.BidLevels[i].volume = std::round(bids[i][1].GetDouble() * SCALE_OFFSET);
            }
            priceBook.BidLevelCount = i;
        }
        if (asks.IsArray())
        {
            int i = 0;
            for(i = 0; i < std::min((int)asks.Size(),book_depth_count); ++i)
            {
                priceBook.AskLevels[i].price = std::round(asks[i][0].GetDouble() * SCALE_OFFSET);
                priceBook.AskLevels[i].volume = std::round(asks[i][1].GetDouble() * SCALE_OFFSET);
            }
            priceBook.AskLevelCount = i;
        }
        on_price_book_update(&priceBook);
    }
    catch (const std::exception& e)
    {
        KF_LOG_INFO(logger, "onBook,{error:"<< e.what()<<"}");
    }
     KF_LOG_DEBUG(logger, "onBooka end");
}

std::string MDEngineBitstamp::parseJsonToString(Document &d)
{
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);

    return buffer.GetString();
}



// vector<string> split(const string &s, const string &seperator){  //字符串分割
//   vector<string> result;
//   typedef string::size_type string_size;
//   string_size i = 0;
  
//   while(i != s.size()){
//     //找到字符串中首个不等于分隔符的字母；
//     int flag = 0;
//     while(i != s.size() && flag == 0){
//       flag = 1;
//       for(string_size x = 0; x < seperator.size(); ++x)
//     　　if(s[i] == seperator[x]){
//       　　++i;
//       　　flag = 0;
//      　　 break;
//     　　}
//     }
    
//     //找到又一个分隔符，将两个分隔符之间的字符串取出；
//     flag = 0;
//     string_size j = i;
//     while(j != s.size() && flag == 0){
//       for(string_size x = 0; x < seperator.size(); ++x)
//     　　if(s[j] == seperator[x]){
//       　　flag = 1;
//      　　 break;
//     　　}
//       if(flag == 0) 
//     　　++j;
//     }
//     if(i != j){
//       result.push_back(s.substr(i, j-i));
//       i = j;
//     }
//   }
//   return result;
// }

void MDEngineBitstamp::loop()
{
    while(isRunning)
    {
        int n = lws_service( context, rest_get_interval_ms );
        std::cout << " 3.1415 loop() lws_service (n)" << n << std::endl;
    }
}

BOOST_PYTHON_MODULE(libbitstampmd)
{
    using namespace boost::python;
    class_<MDEngineBitstamp, boost::shared_ptr<MDEngineBitstamp> >("Engine")
            .def(init<>())
            .def("init", &MDEngineBitstamp::initialize)
            .def("start", &MDEngineBitstamp::start)
            .def("stop", &MDEngineBitstamp::stop)
            .def("logout", &MDEngineBitstamp::logout)
            .def("wait_for_stop", &MDEngineBitstamp::wait_for_stop);
}
