#include "MDEngineBittrex.h"
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

static MDEngineBittrex* global_md = nullptr;

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

MDEngineBittrex::MDEngineBittrex(): IMDEngine(SOURCE_BITTREX)
{
    logger = yijinjing::KfLog::getLogger("MdEngine.Bittrex");
}

void MDEngineBittrex::load(const json& j_config)
{
    book_depth_count = j_config["book_depth_count"].get<int>();
    trade_count = j_config["trade_count"].get<int>();
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();
    KF_LOG_INFO(logger, "MDEngineBittrex:: rest_get_interval_ms: " << rest_get_interval_ms);


    coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
    coinPairWhiteList.Debug_print();

    makeWebsocketSubscribeJsonString();
    debug_print(websocketSubscribeJsonString);

    //display usage:
    if(coinPairWhiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "MDEngineBittrex::lws_write_subscribe: subscribeCoinBaseQuote is empty. please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"tBTCUSDT\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"tETCETH\"");
        KF_LOG_ERROR(logger, "},");
    }

    KF_LOG_INFO(logger, "MDEngineBittrex::load:  book_depth_count: "
            << book_depth_count << " trade_count: " << trade_count << " rest_get_interval_ms: " << rest_get_interval_ms);
}

void MDEngineBittrnex::makeWebsocketSubscribeJsonString()
{
    std::unordered_map<std::string, std::string>::iterator map_itr;
    map_itr = coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().begin();
    while(map_itr != coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().end()) {
        KF_LOG_DEBUG(logger, "[makeWebsocketSubscribeJsonString] keyIsExchangeSideWhiteList (strategy_coinpair) " << map_itr->first << " (exchange_coinpair) "<< map_itr->second);

        std::string jsonBookString = createBookJsonString(map_itr->second);
        websocketSubscribeJsonString.push_back(jsonBookString);

        std::string jsonTradeString = createTradeJsonString(map_itr->second);
        websocketSubscribeJsonString.push_back(jsonTradeString);

        map_itr++;
    }
}

void MDEngineBittrex::debug_print(std::vector<std::string> &subJsonString)
{
    size_t count = subJsonString.size();
    KF_LOG_INFO(logger, "[debug_print] websocketSubscribeJsonString (count) " << count);

    for (size_t i = 0; i < count; i++)
    {
        KF_LOG_INFO(logger, "[debug_print] websocketSubscribeJsonString (subJsonString) " << subJsonString[i]);
    }
}

void MDEngineBittrex::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "MDEngineBittrex::connect:");
    connected = true;
}

void MDEngineBittrex::login(long timeout_nsec) {
    KF_LOG_INFO(logger, "MDEngineBittrex::login:");
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
        KF_LOG_INFO(logger, "MDEngineBittrex::login: context created.");
    }

    if (context == NULL) {
        KF_LOG_ERROR(logger, "MDEngineBittrex::login: context is NULL. return");
        return;
    }

    int logs = LLL_ERR | LLL_DEBUG | LLL_WARN;
    lws_set_log_level(logs, NULL);

    struct lws_client_connect_info ccinfo = {0};

    static std::string host  = "api.bittrex.com";
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
    KF_LOG_INFO(logger, "MDEngineBittrex::login: Connecting to " <<  ccinfo.host << ":" << ccinfo.port << ":" << ccinfo.path);

    if (wsi == NULL) {
        KF_LOG_ERROR(logger, "MDEngineBittrex::login: wsi create error.");
        return;
    }
    KF_LOG_INFO(logger, "MDEngineBittrex::login: wsi create success.");

    logged_in = true;
}

void MDEngineBittrex::set_reader_thread()
{
    IMDEngine::set_reader_thread();

    rest_thread = ThreadPtr(new std::thread(boost::bind(&MDEngineBittrex::loop, this)));
}   

void MDEngineBittrex::logout()
{
    KF_LOG_INFO(logger, "MDEngineBittrex::logout:");
}

void MDEngineBittrex::release_api()
{
    KF_LOG_INFO(logger, "MDEngineBittrex::release_api:");
}

// void MDEngineBittrex::subscribeMarketData(const vector<string>& instruments, const vector<string>& markets)
// {
//     KF_LOG_INFO(logger, "MDEngineBittrex::subscribeMarketData:");
// }

int MDEngineBittrex::lws_write_subscribe(struct lws* conn)
{
    KF_LOG_INFO(logger, "MDEngineBittrex::lws_write_subscribe: (subscribe_index)" << subscribe_index);

    //有待发送的数据，先把待发送的发完，在继续订阅逻辑。  ping?
    if(websocketPendingSendMsg.size() > 0) {
        unsigned char msg[512];
        memset(&msg[LWS_PRE], 0, 512-LWS_PRE);

        std::string jsonString = websocketPendingSendMsg[websocketPendingSendMsg.size() - 1];
        websocketPendingSendMsg.pop_back();
        KF_LOG_INFO(logger, "MDEngineBittrex::lws_write_subscribe: websocketPendingSendMsg" << jsonString.c_str());
        int length = jsonString.length();

        strncpy((char *)msg+LWS_PRE, jsonString.c_str(), length);
        int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);

        if(websocketPendingSendMsg.size() > 0)
        {    //still has pending send data, emit a lws_callback_on_writable()
            lws_callback_on_writable( conn );
            KF_LOG_INFO(logger, "MDEngineBittrex::lws_write_subscribe: (websocketPendingSendMsg,size)" << websocketPendingSendMsg.size());
        }
        return ret;
    }

    if(websocketSubscribeJsonString.size() == 0) return 0;
    //sub depth
    if(subscribe_index >= websocketSubscribeJsonString.size())
    {
        //subscribe_index = 0;
        KF_LOG_INFO(logger, "MDEngineBittrex::lws_write_subscribe: (none reset subscribe_index = 0, just return 0)");
	    return 0;
    }

    unsigned char msg[512];
    memset(&msg[LWS_PRE], 0, 512-LWS_PRE);

    std::string jsonString = websocketSubscribeJsonString[subscribe_index++];

    KF_LOG_INFO(logger, "MDEngineBittrex::lws_write_subscribe: " << jsonString.c_str() << " ,after ++, (subscribe_index)" << subscribe_index);
    int length = jsonString.length();

    strncpy((char *)msg+LWS_PRE, jsonString.c_str(), length);
    int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);

    if(subscribe_index < websocketSubscribeJsonString.size())
    {
        lws_callback_on_writable( conn );
        KF_LOG_INFO(logger, "MDEngineBittrex::lws_write_subscribe: (subscribe_index < websocketSubscribeJsonString) call lws_callback_on_writable");
    }

    return ret;
}

void MDEngineBittrex::on_lws_data(struct lws* conn, const char* data, size_t len)
{
    KF_LOG_INFO(logger, "MDEngineBittrex::on_lws_data: " << data);
    Document json;
    json.Parse(data);


    if(json.HasParseError()) {
        KF_LOG_ERROR(logger, "MDEngineBittrex::on_lws_data. parse json error: " << data);
        return;
    }

    /*{ //data
  "buy": [
    {
      "quantity": 12.37,
      "rate": 32.55412402
    }
  ],
  "sell": [
    {
      "quantity": 12.37,
      "rate": 32.55412402
    }
  ]
}*/
    if(json.IsObject && json.HasMember("buy") &&json.HasMember("sell")){
        KF_LOG_INFO(logger, "MDEngineBittrex::on_lws_data: is orderbook(buy and sell)");
        onOrderBook(json);
    }

    else if(json.IsObject && json.HasMember("MarketName")){
        onOrderBook(json);
    }
}

void MDEngineBittrex::onOrderBook(Document &json){
    KF_LOG_INFO(logger, "MDEngineBittrex::onOrderBook: (buy and sell) ";

    std::string ticker; TODO();

        if(json.IsObject && json.HasMember("buy") &&json.HasMember("sell"))//buy and sell
        {/*{ //data,buy has many elements
        "buy": [
            {
            "quantity": 12.37, //volume
            "rate": 32.55412402  //price
            }
        ],
        "sell": [
            {
            "quantity": 12.37,
            "rate": 32.55412402
            }
        ]
        }*/
        int buy_len = json["buy"].GetArray().Size() ;//数组元素个数
        int sell_len = json["sell"].GetArray().Size(); 
            for (int i = 0; i < buy_len; i++) {
                int64_t price = std::round(json["buy"].GetArray()[1].GetDouble() * scale_offset);
                uint64_t volume = std::round(json["buy"].GetArray()[0].GetDouble() * scale_offset);

                priceBook20Assembler.UpdateBidPrice(ticker, price, volume); //buy
            }
               
            for (int i = 0; i < sell_len; i++) {
                int64_t price = std::round(json["sell"].GetArray()[1].GetDouble() * scale_offset);
                uint64_t volume = std::round(json["sell"].GetArray()[0].GetDouble() * scale_offset);

                priceBook20Assembler.UpdateAskPrice(ticker, price, volume); //sell
            }
//                KF_LOG_INFO(logger, " (0)" << json.GetArray()[last_element].GetArray()[i].GetArray()[0].GetDouble() );
//                KF_LOG_INFO(logger, " (1)" << json.GetArray()[last_element].GetArray()[i].GetArray()[1].GetInt() );
//                KF_LOG_INFO(logger, " (2)" << json.GetArray()[last_element].GetArray()[i].GetArray()[2].GetDouble() );
            }
        } else { //update infomation
            /*
             { 
                 "MarketName": "string", 
                 "Nonce": "int", //时间
                 "Buys": 
                 [ { "Type": "int", 
                 "Balance": "decimal",  //price
                 "Available": "decimal" //volume
                  } ], 
                 "Sells": [ { "Type": "int", 
                 "Balance": "decimal", 
                 "Available": "decimal"
                  } ], 
                 "Fills": [ { "FillId": "int",  //用于trade
                 "OrderType": "string", 
                 "Rate": "decimal",
                  "Quantity": "decimal", 
                  "TimeStamp": "date" 
                  } ]
            0 = Add, 1 = Remove, 2 = Update
             * */
            int buys_size = json["buys"].GetArray().Size();
            int sells_size = json["sells"].GetArray.Size();

            for(int i=0;i<buys_size;i++){ //buy

                    int64_t price = std::round(json["buys"].GetArray()[i]["Balance"].GetDouble() * scale_offset);
                    uint64_t volume = std::round(json["buys"].GetArray()[i]["Available"].GetDouble() * scale_offset);

                if(json["buys"].GetArray[i]["Type"].GetInt() == 0 || json["buys"].GetArray[i]["Type"].GetInt() == 2 ){//add
                    
                    priceBook20Assembler.UpdateBidPrice(ticker, price, volume); //buy
                }
                else if(json["buys"].GetArray[i]["Type"].GetInt() == 1){ // remove

                    priceBook20Assembler.EraseBidPrice(ticker, price); //buy
                }
            }

            for(int i=0;i<sells_size;i++){ //sell

                    int64_t price = std::round(json["sells"].GetArray()[i]["Balance"].GetDouble() * scale_offset);
                    uint64_t volume = std::round(json["sells"].GetArray()[i]["Available"].GetDouble() * scale_offset);

                if(json["sells"].GetArray[i]["Type"].GetInt() == 0 || json["sells"].GetArray[i]["Type"].GetInt() == 2 ){//add
                    
                    priceBook20Assembler.UpdateAskPrice(ticker, price, volume); //sell
                }
                else if(json["sells"].GetArray[i]["sells"].GetInt() == 1){ // remove

                    priceBook20Assembler.EraseAskPrice(ticker, price); //sell
                }
            }
//            KF_LOG_INFO(logger, " update(0)" << json.GetArray()[last_element].GetArray()[0].GetDouble() );
//            KF_LOG_INFO(logger, " update(1)" << json.GetArray()[last_element].GetArray()[1].GetInt() );
//            KF_LOG_INFO(logger, " update(2)"<< json.GetArray()[last_element].GetArray()[2].GetDouble() );
        }
    }

    // has any update
    LFPriceBook20Field md;
    memset(&md, 0, sizeof(md));
    if(priceBook20Assembler.Assembler(ticker, md)) {
        strcpy(md.ExchangeID, "bittrex");
        KF_LOG_INFO(logger, "MDEngineBittrex::onDepth: on_price_book_update");

        on_price_book_update(&md);
    }
}


void MDEngineBittrex::on_lws_connection_error(struct lws* conn)
{
    KF_LOG_ERROR(logger, "MDEngineBittrex::on_lws_connection_error.");
    //market logged_in false;
    logged_in = false;
    KF_LOG_ERROR(logger, "MDEngineBittrex::on_lws_connection_error. login again.");
    //clear the price book, the new websocket will give 200 depth on the first connect, it will make a new price book
    priceBook20Assembler.clearPriceBook();
    //no use it
    long timeout_nsec = 0;
    //reset sub
    subscribe_index = 0;

    login(timeout_nsec);
}


int64_t MDEngineBittrex::getTimestamp()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp;
}


void MDEngineBittrex::loop()
{
    while(isRunning)
    {
        int n = lws_service( context, rest_get_interval_ms );
        std::cout << " 3.1415 loop() lws_service (n)" << n << std::endl;
    }
}

BOOST_PYTHON_MODULE(libbittrexmd)
{
    using namespace boost::python;
    class_<MDEngineBittrex, boost::shared_ptr<MDEngineBittrex> >("Engine")
            .def(init<>())
            .def("init", &MDEngineBittrex::initialize)
            .def("start", &MDEngineBittrex::start)
            .def("stop", &MDEngineBittrex::stop)
            .def("logout", &MDEngineBittrex::logout)
            .def("wait_for_stop", &MDEngineBittrex::wait_for_stop);
}
