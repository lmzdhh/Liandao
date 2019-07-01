#include "MDEnginePoloniex.h"
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

static MDEnginePoloniex* global_md = nullptr;

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
                "md-poloniex",
                ws_service_cb,
                        0,
                            524288,
        },
        { NULL, NULL, 0, 0 } /* terminator */
};


MDEnginePoloniex::MDEnginePoloniex(): IMDEngine(SOURCE_POLONIEX)
{
    logger = yijinjing::KfLog::getLogger("MdEngine.Poloniex");
}

void MDEnginePoloniex::load(const json& j_config)
{
    book_depth_count = j_config["book_depth_count"].get<int>();
    trade_count = j_config["trade_count"].get<int>();
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();
    KF_LOG_INFO(logger, "MDEnginePoloniex:: rest_get_interval_ms: " << rest_get_interval_ms);
    baseUrl = j_config["baseUrl"].get<std::string>();
    path = j_config["path"].get<std::string>();

    coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
    coinPairWhiteList.Debug_print();

    makeWebsocketSubscribeJsonString();
    debug_print(websocketSubscribeJsonString);

    //display usage:
    if(coinPairWhiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "MDEnginePoloniex::lws_write_subscribe: subscribeCoiQuote is empty. please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"tBTCUSDT\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"tETCETH\"");
        KF_LOG_ERROR(logger, "},");
    }
    priceBook20Assembler.SetLevel(book_depth_count);
    KF_LOG_INFO(logger, "MDEnginePoloniex::load:  book_depth_count: "
            << book_depth_count << " trade_count: " << trade_count << " rest_get_interval_ms: " << rest_get_interval_ms);
}

void MDEnginePoloniex::makeWebsocketSubscribeJsonString()
{
    std::unordered_map<std::string, std::string>::iterator map_itr;
    map_itr = coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().begin();
    while(map_itr != coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().end()) {
        KF_LOG_DEBUG(logger, "[makeWebsocketSubscribeJsonString] keyIsExchangeSideWhiteList (strategy_coinpair) " << map_itr->first << " (exchange_coinpair) "<< map_itr->second);

        std::string jsonBookString = createBookJsonString(map_itr->second);
        websocketSubscribeJsonString.push_back(jsonBookString);

        map_itr++;
    }
}

void MDEnginePoloniex::debug_print(std::vector<std::string> &subJsonString)
{
    size_t count = subJsonString.size();
    KF_LOG_INFO(logger, "[debug_print] websocketSubscribeJsonString (count) " << count);

    for (size_t i = 0; i < count; i++)
    {
        KF_LOG_INFO(logger, "[debug_print] websocketSubscribeJsonString (subJsonString) " << subJsonString[i]);
    }
}

void MDEnginePoloniex::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "MDEnginePoloniex::connect:");
    connected = true;
}

void MDEnginePoloniex::login(long timeout_nsec) {
    KF_LOG_INFO(logger, "MDEnginePoloniex::login:");
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
        KF_LOG_INFO(logger, "MDEnginePoloniex::login: context created.");
    }

    if (context == NULL) {
        KF_LOG_ERROR(logger, "MDEnginePoloniex::login: context is NULL. return");
        return;
    }

    int logs = LLL_ERR | LLL_DEBUG | LLL_WARN |LLL_INFO;
    lws_set_log_level(logs, NULL);

    struct lws_client_connect_info ccinfo = {0};

    static std::string host  = baseUrl;
    //static std::string path = path;
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
    KF_LOG_INFO(logger, "MDEnginePoloniex::login: Connecting to " <<  ccinfo.host << ":" << ccinfo.port << ":" << ccinfo.path);

    if (wsi == NULL) {
        KF_LOG_ERROR(logger, "MDEnginePoloniex::login: wsi create error.");
        return;
    }
    KF_LOG_INFO(logger, "MDEnginePoloniex::login: wsi create success.");

    logged_in = true;
}

void MDEnginePoloniex::set_reader_thread()
{
    IMDEngine::set_reader_thread();
    rest_thread = ThreadPtr(new std::thread(boost::bind(&MDEnginePoloniex::loop, this)));
}

void MDEnginePoloniex::logout()
{
    KF_LOG_INFO(logger, "MDEnginePoloniex::logout:");
}

void MDEnginePoloniex::release_api()
{
    KF_LOG_INFO(logger, "MDEnginePoloniex::release_api:");
}

void MDEnginePoloniex::subscribeMarketData(const vector<string>& instruments, const vector<string>& markets)
{
    KF_LOG_INFO(logger, "MDEnginePoloniex::subscribeMarketData:");
}

int MDEnginePoloniex::lws_write_subscribe(struct lws* conn)
{
    KF_LOG_INFO(logger, "MDEnginePoloniex::lws_write_subscribe: (subscribe_index)" << subscribe_index);

    if(websocketSubscribeJsonString.size() == 0) return 0;
    //sub depth
    if(subscribe_index >= websocketSubscribeJsonString.size())
    {
        //subscribe_index = 0;
        KF_LOG_INFO(logger, "MDEnginePoloniex::lws_write_subscribe: (none reset subscribe_index = 0, just return 0)");
	    return 0;
    }

    unsigned char msg[512];
    memset(&msg[LWS_PRE], 0, 512-LWS_PRE);

    std::string jsonString = websocketSubscribeJsonString[subscribe_index++];

    KF_LOG_INFO(logger, "MDEnginePoloniex::lws_write_subscribe: " << jsonString.c_str() << " ,after ++, (subscribe_index)" << subscribe_index);
    int length = jsonString.length();

    strncpy((char *)msg+LWS_PRE, jsonString.c_str(), length);
    int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);

    if(subscribe_index < websocketSubscribeJsonString.size())
    {
        lws_callback_on_writable( conn );
        KF_LOG_INFO(logger, "MDEnginePoloniex::lws_write_subscribe: (subscribe_index < websocketSubscribeJsonString) call lws_callback_on_writable");
    }

    return ret;
}

void MDEnginePoloniex::on_lws_data(struct lws* conn, const char* data, size_t len)
{
    KF_LOG_INFO(logger, "MDEnginePoloniex::on_lws_data: " << data);
    Document json;
    json.Parse(data);

    if(json.HasParseError()) {
        KF_LOG_ERROR(logger, "MDEnginePoloniex::on_lws_data. parse json error: " << data);
        return;
    }


    if(json.IsArray()){
        int channelId = json.GetArray()[0].GetInt();
        //get initialization infomation
        if(channelId!=1010){
            KF_LOG_INFO(logger,"MDEnginePology::on_lws_data: getInfo: operation: "<<json.GetArray()[2].GetArray()[0].GetArray()[0].GetString());
            if(strcmp(json.GetArray()[2].GetArray()[0].GetArray()[0].GetString(),"i")==0){
                KF_LOG_INFO(logger,"MDEnginePology::on_lws_data: getInfo: inistial");
                GetINitializationInfomation(json,channelId,true);
            }
            else{
                KF_LOG_INFO(logger,"MDEnginePology::on_lw:s_data: getInfo: getchange");
                GetINitializationInfomation(json,channelId,false);
            }
        }
    }
    else{
        KF_LOG_INFO(logger,"MDEnginePology::on_lws_data: different data");
    }
}


void MDEnginePoloniex::GetINitializationInfomation(Document& json, int channlId, bool isInistial)
{
    KF_LOG_INFO(logger,"MDEnginePoloniex::GetINitializationInfomation::isInistial  :"<<isInistial);
    std::string ticker;

    if(isInistial){
        std::string tickerB = json.GetArray()[2].GetArray()[0].GetArray()[1]["currencyPair"].GetString();

        ticker = coinPairWhiteList.GetKeyByValue(tickerB);
        if(ticker.length()==0) return;

        SubscribeChannel newChannel;
        newChannel.channelId = channlId;
        newChannel.exchange_coinpair = tickerB;
        websocketSubscribeChannel.push_back(newChannel);
        debug_print(websocketSubscribeChannel);

        KF_LOG_INFO(logger,"MDEnginePoloniex::GetINitializationInfomation"<<ticker);

        for(auto& m : json.GetArray()[2].GetArray()[0].GetArray()[1]["orderBook"].GetArray()[0].GetObject()){
            priceBook20Assembler.UpdateAskPrice(ticker,std::round(std::stod(m.name.GetString())*scale_offset),std::round(std::stod(m.value.GetString())*scale_offset));
            //KF_LOG_INFO(logger, "MDEnginePoloniex::onDepth: on_price_book_update : jsonAsk : price :"<<m.name.GetString()<<"   value :"<<m.value.GetString());
        }

        for(auto& m : json.GetArray()[2].GetArray()[0].GetArray()[1]["orderBook"].GetArray()[1].GetObject()){
            priceBook20Assembler.UpdateBidPrice(ticker,std::round(std::stod(m.name.GetString())*scale_offset),std::round(std::stod(m.value.GetString())*scale_offset));
            //KF_LOG_INFO(logger, "MDEnginePoloniex::onDepth: on_price_book_update : jsonBid : price :"<<m.name.GetString()<<"   value :"<<m.value.GetString());
        }

        LFPriceBook20Field md;
        memset(&md, 0, sizeof(md));
        if(priceBook20Assembler.Assembler(ticker, md)) {
            strcpy(md.ExchangeID, "poloniex");

            KF_LOG_INFO(logger, "MDEnginePoloniex::onDepth: on_price_book_update");
            on_price_book_update(&md);
        }
    }
    else{
        KF_LOG_INFO(logger,"MDEnginePoloniex::GetINitializationInfomation: operation : onrun");
        SubscribeChannel channel = findByChannelID(channlId);
        ticker = coinPairWhiteList.GetKeyByValue(channel.exchange_coinpair);
        KF_LOG_INFO(logger,"MDEnginePoloniex::GetINitializationInfomation: operation : ticker :"<<ticker);
        if(ticker.length()==0) return;

        int len = json.GetArray()[2].Size();
        KF_LOG_INFO(logger,"MDEnginePoloniex::GetINitializationInfomation: operation : len : "<<len);
        for(int i = 0; i < len; i++){
            KF_LOG_INFO(logger,"MDEnginePoloniex::GetINitializationInfomation: operation on Array[2]: "<<json.GetArray()[2].GetArray()[i].GetArray()[0].GetString());
            if(strcmp(json.GetArray()[2].GetArray()[i].GetArray()[0].GetString(),"o")==0){
                KF_LOG_INFO(logger,"MDEnginePoloniex::GetINitializationInfomation: operation : o");
                int isBookBuy = json.GetArray()[2].GetArray()[i].GetArray()[1].GetInt();
                std::string priceString = json.GetArray()[2].GetArray()[i].GetArray()[2].GetString();
                int64_t price = std::round(std::stod(priceString)*scale_offset);
                uint64_t amount = std::round(std::stod(json.GetArray()[2].GetArray()[i].GetArray()[3].GetString())*scale_offset);
                if(amount>0){
                    if(isBookBuy==1){
                        KF_LOG_INFO(logger,"MDEnginePoloniex::GetINitializationInfomation: operation : o : buy & amount!=0");
                        priceBook20Assembler.UpdateBidPrice(ticker,price,amount);
                    }
                    else {
                        KF_LOG_INFO(logger,"MDEnginePoloniex::GetINitializationInfomation: operation : o : sell & amount!=0");
                        priceBook20Assembler.UpdateAskPrice(ticker,price,amount);
                    }
                }
                else if(amount==0){
                    if(isBookBuy==1){
                        KF_LOG_INFO(logger,"MDEnginePoloniex::GetINitializationInfomation: operation : o : buy & amount==0");
                        priceBook20Assembler.EraseBidPrice(ticker,price);
                    }
                    else {
                        KF_LOG_INFO(logger,"MDEnginePoloniex::GetINitializationInfomation: operation : o : sell & amount==0");
                        priceBook20Assembler.EraseAskPrice(ticker,price);
                    }
                }
                else KF_LOG_INFO(logger,"MDEnginePoloniex::GetINitializationInfomation: operation : o : false");
                LFPriceBook20Field md;
                memset(&md, 0, sizeof(md));
                if(priceBook20Assembler.Assembler(ticker, md)) {
                    strcpy(md.ExchangeID, "poloniex");

                    KF_LOG_INFO(logger, "MDEnginePoloniex::onDepth: on_price_book_update");
                    on_price_book_update(&md);
                }
            }
            else if(strcmp(json.GetArray()[2].GetArray()[i].GetArray()[0].GetString(),"t")==0){
                KF_LOG_INFO(logger,"MDEnginePoloniex::GetINitializationInfomation: operation : t");
                LFL2TradeField trade;
                memset(&trade, 0, sizeof(trade));
                strcpy(trade.InstrumentID, ticker.c_str());
                strcpy(trade.ExchangeID, "poloniex");
                trade.Price = std::round(std::stod(json.GetArray()[2].GetArray()[i].GetArray()[3].GetString())*scale_offset);
                int isTradeBuy = json.GetArray()[2].GetArray()[i].GetArray()[2].GetInt();
                uint64_t amount = std::round(std::stod(json.GetArray()[2].GetArray()[i].GetArray()[4].GetString())*scale_offset);
                trade.Volume = amount;
                trade.OrderBSFlag[0] = isTradeBuy == 1 ? 'B' : 'S';
                KF_LOG_INFO(logger, "MDEnginePoloniex::[GetINitializationInfomation] (ticker)" << ticker <<
                                                                           " (Price)" << trade.Price <<
                                                                           " (trade.Volume)" << trade.Volume);
                time_t t;
                double tdouble = json.GetArray()[2].GetArray()[i].GetArray()[5].GetDouble();
                t = static_cast<long>(tdouble);
                tzset();
                struct tm *gmt;
                gmt = gmtime(&t);
                char timeChar[9];
                if(gmt->tm_hour>10){
                    timeChar[0] = '0'+gmt->tm_hour/10;
                    timeChar[1] = '0'+gmt->tm_hour%10;
                }
                else{
                    timeChar[0] = '0';
                    timeChar[1] = '0'+gmt->tm_hour;
                }
                timeChar[2] = ':';
                if(gmt->tm_min>10){
                    timeChar[3] = '0'+gmt->tm_min/10;
                    timeChar[4] = '0'+gmt->tm_min%10;
                }
                else{
                    timeChar[3] = '0';
                    timeChar[4] = '0'+gmt->tm_min;
                }
                timeChar[5] = ':';
                if(gmt->tm_sec>10){
                    timeChar[6] = '0'+gmt->tm_sec/10;
                    timeChar[7] = '0'+gmt->tm_sec%10;
                }
                else{
                    timeChar[6] = '0';
                    timeChar[7] = '0'+gmt->tm_sec;
                }
                strcpy(trade.TradeTime,timeChar);
                KF_LOG_INFO(logger, "GMT is: %s"<<asctime(gmt));
                
                on_trade(&trade);
            }
        }
    }
}



void MDEnginePoloniex::on_lws_connection_error(struct lws* conn)
{
    KF_LOG_ERROR(logger, "MDEnginePoloniex::on_lws_connection_error.");
    //market logged_in false;
    logged_in = false;
    KF_LOG_ERROR(logger, "MDEnginePoloniex::on_lws_connection_error. login again.");
    //clear the price book, the new websocket will give 200 depth on the first connect, it will make a new price book
    priceBook20Assembler.clearPriceBook();
    //no use it
    long timeout_nsec = 0;
    //reset sub
    subscribe_index = 0;

    login(timeout_nsec);
}


int64_t MDEnginePoloniex::getTimestamp()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp;
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
void MDEnginePoloniex::onInfo(Document& json)
{
    KF_LOG_INFO(logger, "MDEnginePoloniex::onInfo: " << parseJsonToString(json));
}




void MDEnginePoloniex::debug_print(std::vector<SubscribeChannel> &websocketSubscribeChannel)
{
    size_t count = websocketSubscribeChannel.size();
    KF_LOG_INFO(logger, "[debug_print] websocketSubscribeChannel (count) " << count);

    for (size_t i = 0; i < count; i++)
    {
        KF_LOG_INFO(logger,  " (exchange_coinpair)" << websocketSubscribeChannel[i].exchange_coinpair <<
                            " (channelId)" << websocketSubscribeChannel[i].channelId);
    }
}

SubscribeChannel MDEnginePoloniex::findByChannelID(int channelId)
{
    size_t count = websocketSubscribeChannel.size();

    for (size_t i = 0; i < count; i++)
    {
        if(channelId == websocketSubscribeChannel[i].channelId) {
            return websocketSubscribeChannel[i];
        }
    }
    return EMPTY_CHANNEL;
}





std::string MDEnginePoloniex::parseJsonToString(Document &d)
{
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);

    return buffer.GetString();
}


//{ "command": "subscribe", "channel": "BTC_BTS" }
std::string MDEnginePoloniex::createBookJsonString(std::string exchange_coinpair)
{
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();
    writer.Key("command");
    writer.String("subscribe");

    writer.Key("channel");
    writer.String(exchange_coinpair.c_str());

    writer.EndObject();
    return s.GetString();
}


void MDEnginePoloniex::loop()
{
    while(isRunning)
    {
        int n = lws_service( context, rest_get_interval_ms );
        std::cout << " 3.1415 loop() lws_service (n)" << n << std::endl;
    }
}

BOOST_PYTHON_MODULE(libpoloniexmd)
{
    using namespace boost::python;
    class_<MDEnginePoloniex, boost::shared_ptr<MDEnginePoloniex> >("Engine")
            .def(init<>())
            .def("init", &MDEnginePoloniex::initialize)
            .def("start", &MDEnginePoloniex::start)
            .def("stop", &MDEnginePoloniex::stop)
            .def("logout", &MDEnginePoloniex::logout)
            .def("wait_for_stop", &MDEnginePoloniex::wait_for_stop);
}
