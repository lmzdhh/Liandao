#include "MDEngineKraken.h"
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

static MDEngineKraken* global_md = nullptr;


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

MDEngineKraken::MDEngineKraken(): IMDEngine(SOURCE_KRAKEN)
{
    logger = yijinjing::KfLog::getLogger("MdEngine.Kraken");
}

void MDEngineKraken::load(const json& j_config)
{
    book_depth_count = j_config["book_depth_count"].get<int>();
    trade_count = j_config["trade_count"].get<int>();
    baseUrl = j_config["baseUrl"].get<std::string>();

//    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();
//    KF_LOG_INFO(logger, "MDEngineKraken:: rest_get_interval_ms: " << rest_get_interval_ms);


    coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
    coinPairWhiteList.Debug_print();

    makeWebsocketSubscribeJsonString();
    debug_print(websocketSubscribeJsonString);

    //display usage:
    if(coinPairWhiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "MDEngineKraken::lws_write_subscribe: subscribeCoinBaseQuote is empty. please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"tBTCUSDT\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"tETCETH\"");
        KF_LOG_ERROR(logger, "},");
    }

    priceBook20Assembler.SetLevel(book_depth_count);

    KF_LOG_INFO(logger, "MDEngineKraken::load:  book_depth_count: "
            << book_depth_count << " trade_count: " << trade_count <<
            " baseUrl: " << baseUrl);
}

void MDEngineKraken::makeWebsocketSubscribeJsonString()
{
    std::unordered_map<std::string, std::string>::iterator map_itr;
    map_itr = coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().begin();
    while(map_itr != coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().end()) {
        KF_LOG_DEBUG(logger, "[makeWebsocketSubscribeJsonString] keyIsExchangeSideWhiteList (strategy_coinpair) " << map_itr->first << " (exchange_coinpair) "<< map_itr->second);

        std::string jsonBookString = createBookJsonString(map_itr->second);
        websocketSubscribeJsonString.push_back(jsonBookString);

        std::string jsonTradeString = createTradeJsonString(map_itr->second);
        websocketSubscribeJsonString.push_back(jsonTradeString);

        std::string jsonOhlcString = createOhlcJsonString(map_itr->second);
        websocketSubscribeJsonString.push_back(jsonOhlcString);

        map_itr++;
    }
}

void MDEngineKraken::debug_print(std::vector<std::string> &subJsonString)
{
    size_t count = subJsonString.size();
    KF_LOG_INFO(logger, "[debug_print] websocketSubscribeJsonString (count) " << count);

    for (size_t i = 0; i < count; i++)
    {
        KF_LOG_INFO(logger, "[debug_print] websocketSubscribeJsonString (subJsonString) " << subJsonString[i]);
    }
}

void MDEngineKraken::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "MDEngineKraken::connect:");
    connected = true;
}

void MDEngineKraken::login(long timeout_nsec) {
    KF_LOG_INFO(logger, "MDEngineKraken::login:");
    global_md = this;

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
    KF_LOG_INFO(logger, "MDEngineKraken::login: context created.");

    if (context == NULL) {
        KF_LOG_ERROR(logger, "MDEngineKraken::login: context is NULL. return");
        return;
    }

    struct lws_client_connect_info ccinfo;
    memset(&ccinfo, 0, sizeof(ccinfo));
    struct lws *wsi = NULL;
    std::string host = "ws.kraken.com";
    //std::string path = "/ws/2";
    std::string path = "/";

    ccinfo.context 	= context;
    ccinfo.address 	= host.c_str();
    ccinfo.port 	= 443;
    ccinfo.path 	= path.c_str();
    ccinfo.host 	= host.c_str();
    ccinfo.origin 	= host.c_str();
    ccinfo.protocol = protocols[0].name;
    //ccinfo.pwsi     = &wsi;
    ccinfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
    ccinfo.ietf_version_or_minus_one = -1;

    wsi = lws_client_connect_via_info(&ccinfo);
    KF_LOG_INFO(logger, "MDEngineKraken::login: Connecting to " <<  ccinfo.host << ":" << ccinfo.port << ":" << ccinfo.path);

    if (wsi == NULL) {
        KF_LOG_ERROR(logger, "MDEngineKraken::login: wsi create error.");
        return;
    }
    KF_LOG_INFO(logger, "MDEngineKraken::login: test login #7 " );
    KF_LOG_INFO(logger, "MDEngineKraken::login: wsi create success.");

    logged_in = true;
}

void MDEngineKraken::set_reader_thread()
{
    IMDEngine::set_reader_thread();

    rest_thread = ThreadPtr(new std::thread(boost::bind(&MDEngineKraken::loop, this)));
}

void MDEngineKraken::logout()
{
    KF_LOG_INFO(logger, "MDEngineKraken::logout:");
}

void MDEngineKraken::release_api()
{
    KF_LOG_INFO(logger, "MDEngineKraken::release_api:");
}

void MDEngineKraken::subscribeMarketData(const vector<string>& instruments, const vector<string>& markets)
{
    KF_LOG_INFO(logger, "MDEngineKraken::subscribeMarketData:");
}

int MDEngineKraken::lws_write_subscribe(struct lws* conn)
{
    KF_LOG_INFO(logger, "MDEngineKraken::lws_write_subscribe: (subscribe_index)" << subscribe_index);

    //有待发送的数据，先把待发送的发完，在继续订阅逻辑。  ping?
    if(websocketPendingSendMsg.size() > 0) {
        unsigned char msg[512];
        memset(&msg[LWS_PRE], 0, 512-LWS_PRE);

        std::string jsonString = websocketPendingSendMsg[websocketPendingSendMsg.size() - 1];
        websocketPendingSendMsg.pop_back();
        KF_LOG_INFO(logger, "MDEngineKraken::lws_write_subscribe: websocketPendingSendMsg" << jsonString.c_str());
        int length = jsonString.length();

        strncpy((char *)msg+LWS_PRE, jsonString.c_str(), length);
        int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);

        if(websocketPendingSendMsg.size() > 0)
        {    //still has pending send data, emit a lws_callback_on_writable()
            lws_callback_on_writable( conn );
            KF_LOG_INFO(logger, "MDEngineKraken::lws_write_subscribe: (websocketPendingSendMsg,size)" << websocketPendingSendMsg.size());
        }
        return ret;
    }

    if(websocketSubscribeJsonString.size() == 0) return 0;
    //sub depth
    if(subscribe_index >= websocketSubscribeJsonString.size())
    {
        //subscribe_index = 0;
        KF_LOG_INFO(logger, "MDEngineKraken::lws_write_subscribe: (none reset subscribe_index = 0, just return 0)");
	    return 0;
    }

    unsigned char msg[512];
    memset(&msg[LWS_PRE], 0, 512-LWS_PRE);

    std::string jsonString = websocketSubscribeJsonString[subscribe_index++];

    KF_LOG_INFO(logger, "MDEngineKraken::lws_write_subscribe: " << jsonString.c_str() << " ,after ++, (subscribe_index)" << subscribe_index);
    int length = jsonString.length();

    strncpy((char *)msg+LWS_PRE, jsonString.c_str(), length);
    int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);

    if(subscribe_index < websocketSubscribeJsonString.size())
    {
        lws_callback_on_writable( conn );
        KF_LOG_INFO(logger, "MDEngineKraken::lws_write_subscribe: (subscribe_index < websocketSubscribeJsonString) call lws_callback_on_writable");
    }

    return ret;
}
void MDEngineKraken::onPing(struct lws* conn, Document& json)
{
    KF_LOG_INFO(logger, "MDEngineBitfinex::onPing: " << parseJsonToString(json));
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();
    writer.Key("event");
    writer.String("pong");

    writer.Key("reqid");
    writer.Int(json["reqid"].GetInt());

    writer.EndObject();

    std::string result = s.GetString();
    KF_LOG_INFO(logger, "MDEngineBitfinex::onPing: (Pong)" << result);
    //emit a callback
    lws_callback_on_writable( conn );
}
void MDEngineKraken::on_lws_data(struct lws* conn, const char* data, size_t len)
{
    KF_LOG_INFO(logger, "MDEngineKraken::on_lws_data: " << data);
    Document json;
    json.Parse(data);

    if(json.HasParseError()) {
        KF_LOG_ERROR(logger, "MDEngineKraken::on_lws_data. parse json error: " << data);
        return;
    }

    if(json.IsObject() && json.HasMember("event")) {
        if (strcmp(json["event"].GetString(), "info") == 0) {
            KF_LOG_INFO(logger, "MDEngineKraken::on_lws_data: is info");
        //    onInfo(json);
        } else if (strcmp(json["event"].GetString(), "ping") == 0) {
            KF_LOG_INFO(logger, "MDEngineKraken::on_lws_data: is ping");
            onPing(conn, json);
        } else if (strcmp(json["event"].GetString(), "subscriptionStatus") == 0) {
            KF_LOG_INFO(logger, "MDEngineKraken::on_lws_data: is subscriptionStatus");
            onSubscribed(json);
        }
    }

    //data
    if(json.IsObject()) {
        int chanId = json["connectionID"].GetInt();
        KF_LOG_INFO(logger, "MDEngineKraken::on_lws_data: (chanId)" << chanId);

        SubscribeChannel channel = findByChannelID( chanId );
        if (channel.channelId == 0) {
            KF_LOG_ERROR(logger, "MDEngineKraken::on_lws_data: EMPTY_CHANNEL (chanId)" << chanId);
        } else {
            if (channel.subType == book_channel) {
                KF_LOG_INFO(logger, "MDEngineKraken::on_lws_data: is book");
                onBook(channel, json);
            } else if (channel.subType == trade_channel) {
                KF_LOG_INFO(logger, "MDEngineKraken::on_lws_data: is trade");
                onTrade(channel, json);
            } else if (channel.subType == ohlc_channel) {
                KF_LOG_INFO(logger, "MDEngineKraken::on_lws_data: is ohlc");
                onOhlc(channel, json);
            } else {
                KF_LOG_INFO(logger, "MDEngineKraken::on_lws_data: unknown array data: " << data);
            }
        }
    }
}


void MDEngineKraken::on_lws_connection_error(struct lws* conn)
{
    KF_LOG_ERROR(logger, "MDEngineKraken::on_lws_connection_error.");
    //market logged_in false;
    logged_in = false;
    KF_LOG_ERROR(logger, "MDEngineKraken::on_lws_connection_error. login again.");
    //clear the price book, the new websocket will give 200 depth on the first connect, it will make a new price book
    priceBook20Assembler.clearPriceBook();
    //no use it
    long timeout_nsec = 0;
    //reset sub
    subscribe_index = 0;

    login(timeout_nsec);
}


int64_t MDEngineKraken::getTimestamp()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp;
}


//kraken
/*{
  "channelID": 10001,
  "event": "subscriptionStatus",
  "status": "subscribed",
  "pair": "XBT/EUR",
  "subscription": {
    "name": "ticker"
  }

}*/
void MDEngineKraken::onSubscribed(Document& json)
{
    KF_LOG_INFO(logger, "MDEngineKraken::onSubscribed: " << parseJsonToString(json));

    if(json.HasMember("channelId") && json.HasMember("pair") && json.HasMember("subscription")) {
        int chanId = json["channelId"].GetInt();
        std::string coinpair = json["pair"].GetString();

        if(strcmp(json["subscription"]["name"].GetString(), "trades") == 0) {
            SubscribeChannel newChannel;
            newChannel.channelId = chanId;
            newChannel.subType = trade_channel;
            newChannel.exchange_coinpair = coinpair;
            websocketSubscribeChannel.push_back(newChannel);
        }

        if(strcmp(json["subscription"]["name"].GetString(), "book") == 0) {
            SubscribeChannel newChannel;
            newChannel.channelId = chanId;
            newChannel.subType = book_channel;
            newChannel.exchange_coinpair = coinpair;
            websocketSubscribeChannel.push_back(newChannel);
        }

        if(strcmp(json["subscription"]["name"].GetString(), "ohlc") == 0) {
            SubscribeChannel newChannel;
            newChannel.channelId = chanId;
            newChannel.subType = ohlc_channel;
            newChannel.exchange_coinpair = coinpair;
            websocketSubscribeChannel.push_back(newChannel);
        }
    }

    debug_print(websocketSubscribeChannel);
}


void MDEngineKraken::debug_print(std::vector<SubscribeChannel> &websocketSubscribeChannel)
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

SubscribeChannel MDEngineKraken::findByChannelID(int channelId)
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


void MDEngineKraken::onTrade(SubscribeChannel &channel, Document& json)
{
    KF_LOG_INFO(logger, "MDEngineKraken::onTrade: (coinpair) " << channel.exchange_coinpair);

    std::string ticker = coinPairWhiteList.GetKeyByValue(channel.exchange_coinpair);
    if(ticker.length() == 0) {
        return;
    }

    int size = json.GetArray().Size();
    if(size < 2) return;

    int last_element = size - 1;
    if (json.GetArray()[last_element].IsArray()) {
        int len = json.GetArray()[last_element].Size();
        if (len == 0) return;

        if(json.GetArray()[last_element].GetArray()[0].IsArray())
        {
            /* snapshot
             * 
             * */
            for (int i = 0; i < len; i++) {
               KF_LOG_INFO(logger, " (0)price" << json.GetArray()[last_element].GetArray()[i].GetArray()[0].GetFloat() );
               KF_LOG_INFO(logger, " (1)volume" << json.GetArray()[last_element].GetArray()[i].GetArray()[1].GetFloat() );
               KF_LOG_INFO(logger, " (2)time" << json.GetArray()[last_element].GetArray()[i].GetArray()[2].GetFloat() );
               KF_LOG_INFO(logger, " (3)side" << json.GetArray()[last_element].GetArray()[i].GetArray()[3].GetString() );
               KF_LOG_INFO(logger, " (4)orderType" << json.GetArray()[last_element].GetArray()[i].GetArray()[3].GetString() );
               KF_LOG_INFO(logger, " (5)misc" << json.GetArray()[last_element].GetArray()[i].GetArray()[3].GetString() );
               

                LFL2TradeField trade;
                memset(&trade, 0, sizeof(trade));
                strcpy(trade.InstrumentID, ticker.c_str());
                strcpy(trade.ExchangeID, "kraken");

                trade.Price = std::round(json.GetArray()[last_element].GetArray()[i].GetArray()[0].GetFloat() * scale_offset);
                // double amount = json.GetArray()[last_element].GetArray()[i].GetArray()[2].GetDouble();
                // uint64_t volume = 0;
                // if(amount < 0) {
                //     volume = std::round(-1 * amount * scale_offset);
                // } else {
                //     volume = std::round( amount * scale_offset);
                // }

                trade.Volume = std::round(json.GetArray()[last_element].GetArray()[i].GetArray()[1].GetFloat() * scale_offset);
                //trade.OrderBSFlag[0] = amount < 0 ? 'B' : 'S';
                if (strcmp(json.GetArray()[last_element].GetArray()[i].GetArray()[3].GetString() ,"b")==0) {
                    trade.OrderBSFlag[0] = 'B';
                }else
                {
                    trade.OrderBSFlag[0] = 'S';
                }
                
                KF_LOG_INFO(logger, "MDEngineKraken::[onTrade] (ticker)" << ticker <<
                                                                           " (Price)" << trade.Price <<
                                                                           " (trade.Volume)" << trade.Volume);
                on_trade(&trade);

            }
        } else {
            /*update
             * [
              CHANNEL_ID,
              <"te", "tu">,
              [
                ID,
                MTS,
                AMOUNT,
                PRICE
              ]
            ]
            //±AMOUNT	float	How much was bought (positive) or sold (negative).
            //正的是: S     (maker is buyer )
            //负的是: B
            //[1,"te",[279619192,1534151024181,-0.05678467,6485.1]]
             * */

 
            return;
        }
    }
}

void MDEngineKraken::onOhlc(SubscribeChannel &channel, Document& json)
{
    KF_LOG_INFO(logger, "MDEngineKraken::onOhlc ");
    std::string ticker = coinPairWhiteList.GetKeyByValue(channel.exchange_coinpair);
    if(ticker.length() == 0) {
        KF_LOG_INFO(logger, "received ohlc does not have valid data");
        return;
    }
    KF_LOG_INFO(logger, "MDEngineKraken::onOhlc: (ticker) " << ticker);
    int size = json.GetArray().Size();
    int last_element = size - 1;

    if(json.GetArray()[last_element].IsArray() )
    {
        LFBarMarketDataField market;
        memset(&market, 0, sizeof(market));
        strcpy(market.InstrumentID, ticker.c_str());
        strcpy(market.ExchangeID, "kraken");

        struct tm cur_tm, start_tm, end_tm;
		time_t now = time(0);
		cur_tm = *localtime(&now);
		strftime(market.TradingDay, 9, "%Y%m%d", &cur_tm);
		
        int64_t ttime = std::round(json.GetArray()[last_element].GetArray()[0].GetFloat() * 1000); 
        int64_t endtime = std::round(json.GetArray()[last_element].GetArray()[1].GetFloat() * 1000);
		int64_t nStartTime = endtime - ttime;
		int64_t nEndTime = endtime;
		market.StartUpdateMillisec = nStartTime;
		int ms = nStartTime % 1000;
		nStartTime/= 1000;
		start_tm = *localtime((time_t*)(&nStartTime));
		sprintf(market.StartUpdateTime,"%02d:%02d:%02d.%03d", start_tm.tm_hour,start_tm.tm_min,start_tm.tm_sec,ms);
		market.EndUpdateMillisec = nEndTime;
		ms = nEndTime%1000;
		nEndTime/= 1000;
		end_tm =  *localtime((time_t*)(&nEndTime));
		//strftime(market.EndUpdateTime,13, "%H:%M:%S", &end_tm);
		sprintf(market.EndUpdateTime,"%02d:%02d:%02d.%03d", end_tm.tm_hour,end_tm.tm_min,end_tm.tm_sec,ms);

		market.PeriodMillisec = 60000;
		market.Open = std::round(json.GetArray()[last_element].GetArray()[2].GetFloat() * scale_offset);
		market.Close = std::round(json.GetArray()[last_element].GetArray()[5].GetFloat() * scale_offset);
		market.Low = std::round(json.GetArray()[last_element].GetArray()[4].GetFloat() * scale_offset);
		market.High = std::round(json.GetArray()[last_element].GetArray()[3].GetFloat() * scale_offset);		
		market.Volume = std::round(json.GetArray()[last_element].GetArray()[7].GetFloat() * scale_offset);
		auto itPrice = priceBook.find(channel.exchange_coinpair);
		if(itPrice != priceBook.end())
		{
			market.BestBidPrice = itPrice->second.BidLevels[0].price;
			market.BestAskPrice = itPrice->second.AskLevels[0].price;
		}
		on_market_bar_data(&market);
    }
}

void MDEngineKraken::onBook(SubscribeChannel &channel, Document& json)
{
    KF_LOG_INFO(logger, "MDEngineKraken::onBook: (coinpair) " << channel.exchange_coinpair);

    std::string ticker = coinPairWhiteList.GetKeyByValue(channel.exchange_coinpair);
    if(ticker.length() == 0) {
        return;
    }

    KF_LOG_INFO(logger, "MDEngineKraken::onBook: (ticker) " << ticker);

    int size = json.GetArray().Size();
    int last_element = size - 1;

    //kraken
    if(json.GetArray()[last_element].IsObject() ) 
    {

        if(json.GetArray()[last_element].HasMember("as") && 
            json.GetArray()[last_element]["as"].IsArray() && 
            json.GetArray()[last_element].HasMember("bs") &&
            json.GetArray()[last_element]["bs"].IsArray() )
        {
            /* snapshot
                [
                    0,
                    {
                        "as": [
                        [
                            "5541.30000",
                            "2.50700000",
                            "1534614248.123678"
                        ],
                        [
                            "5541.80000",
                            "0.33000000",
                            "1534614098.345543"
                        ],
                        [
                            "5542.70000",
                            "0.64700000",
                            "1534614244.654432"
                        ]
                        ],
                        "bs": [
                        [
                            "5541.20000",
                            "1.52900000",
                            "1534614248.765567"
                        ],
                        [
                            "5539.90000",
                            "0.30000000",
                            "1534614241.769870"
                        ],
                        [
                            "5539.50000",
                            "5.00000000",
                            "1534613831.243486"
                        ]
                        ]
                    }
                ]

            */
            int len_as = json.GetArray()[last_element]["as"].GetArray().Size(); 
            int len_bs = json.GetArray()[last_element]["bs"].GetArray().Size();
            if(len_as == 0 || len_bs == 0) return;

           for(int i = 0; i < len_as; i++)
           {
               int64_t price = std::round(json.GetArray()[last_element]["as"].GetArray()[i].GetArray()[0].GetFloat() * scale_offset);
               uint64_t volume = std::round(json.GetArray()[last_element]["as"].GetArray()[i].GetArray()[1].GetFloat() * scale_offset);
               priceBook20Assembler.UpdateAskPrice(ticker, price, volume);
           }
           for(int i = 0; i < len_bs; i++)
           {
               int64_t price = std::round(json.GetArray()[last_element]["bs"].GetArray()[i].GetArray()[0].GetFloat() * scale_offset);
               uint64_t volume = std::round(json.GetArray()[last_element]["bs"].GetArray()[i].GetArray()[1].GetFloat() * scale_offset);
               priceBook20Assembler.UpdateBidPrice(ticker, price, volume);
           }
        } 
        else if( (json.GetArray()[last_element].HasMember("a") && 
            json.GetArray()[last_element]["a"].IsArray() ) || 
            (json.GetArray()[last_element].HasMember("b") &&
            json.GetArray()[last_element]["b"].IsArray() ) )
        {
            /*  update
                [
                    1234,
                    {"a": [
                        [
                        "5541.30000",
                        "2.50700000",
                        "1534614248.456738"
                        ],
                        [
                        "5542.50000",
                        "0.40100000",
                        "1534614248.456738"
                        ]
                    ]
                    },
                    {"b": [
                        [
                        "5541.30000",
                        "0.00000000",
                        "1534614335.345903"
                        ]
                    ]
                    }
                ]
            */
           if(json.GetArray()[last_element].HasMember("a"))
           {
               int len_a = json.GetArray()[last_element]['a'].GetArray().Size();
               for(int i = 0; i < len_a; i++)
               {
                   int64_t price = std::round(json.GetArray()[last_element]['a'].GetArray()[i].GetArray()[0].GetFloat() * scale_offset);
                   uint64_t volume = std::round(json.GetArray()[last_element]['a'].GetArray()[i].GetArray()[1].GetFloat() * scale_offset);
                   if(volume == 0)
                   {
                       priceBook20Assembler.EraseAskPrice(ticker, price);
                   }
                   else
                   {
                       priceBook20Assembler.UpdateAskPrice(ticker, price, volume);
                   }
               }
           }
           if(json.GetArray()[last_element].HasMember("b"))
           {
               int len_a = json.GetArray()[last_element]['b'].GetArray().Size();
               for(int i = 0; i < len_a; i++)
               {
                   int64_t price = std::round(json.GetArray()[last_element]['a'].GetArray()[i].GetArray()[0].GetFloat() * scale_offset);
                   uint64_t volume = std::round(json.GetArray()[last_element]['a'].GetArray()[i].GetArray()[1].GetFloat() * scale_offset);
                   if(volume == 0)
                   {
                       priceBook20Assembler.EraseBidPrice(ticker, price);
                   }
                   else
                   {
                       priceBook20Assembler.UpdateBidPrice(ticker, price, volume);
                   }
               }
           }
        }

    }

    LFPriceBook20Field md;
    memset(&md, 0, sizeof(md));
    if(priceBook20Assembler.Assembler(ticker, md)) {
        strcpy(md.ExchangeID, "kraken");

        priceBook[ticker] = md;
        KF_LOG_INFO(logger, "MDEngineKraken::onDepth: on_price_book_update");
        on_price_book_update(&md);
    }

}

std::string MDEngineKraken::parseJsonToString(Document &d)
{
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);

    return buffer.GetString();
}


//{ "event": "subscribe", "channel": "book",  "pair": "XBT/USD" }
//{ "event": "subscribe", "pair": (pair1,pair2,pair3), "name": "book"}
std::string MDEngineKraken::createBookJsonString(std::string exchange_coinpair)
{
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();

    writer.Key("event");
    writer.String("subscribe");

    writer.Key("pair");
    writer.StartArray();
    writer.String(exchange_coinpair.c_str());
    writer.EndArray();

    writer.Key("subscription");
    writer.StartObject();
    writer.Key("name");
    writer.String("book");
    writer.Key("depth");
    writer.Int(book_depth_count);
    writer.EndObject();

    writer.EndObject();
    return s.GetString();
}

//{ "event": "subscribe", "channel": "trades",  "symbol": "tETHBTC" }
std::string MDEngineKraken::createTradeJsonString(std::string exchange_coinpair)
{
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();
    
    writer.Key("event");
    writer.String("subscribe");

    writer.Key("pair");
    writer.StartArray();
    writer.String(exchange_coinpair.c_str());
    writer.EndArray();

    writer.Key("subscription");
    writer.StartObject();
    writer.Key("name");
    writer.String("trade");
    // writer.Key("depth");
    // writer.Int(25);
    writer.EndObject();

    writer.EndObject();
    return s.GetString();
}

//{ "event": "ohlc", "channel": "trades",  "symbol": "tETHBTC" }
std::string MDEngineKraken::createOhlcJsonString(std::string exchange_coinpair)
{
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();
    
    writer.Key("event");
    writer.String("subscribe");

    writer.Key("pair");
    writer.StartArray();
    writer.String(exchange_coinpair.c_str());
    writer.EndArray();

    writer.Key("subscription");
    writer.StartObject();
    writer.Key("name");
    writer.String("ohlc");
    // writer.Key("depth");
    // writer.Int(25);
    writer.EndObject();

    writer.EndObject();
    return s.GetString();
}

void MDEngineKraken::loop()
{
    while(isRunning)
    {
        int n = lws_service( context, rest_get_interval_ms );
        std::cout << " 3.1415 loop() lws_service (n)" << n << std::endl;
        KF_LOG_INFO(logger, "MDEngineKraken::loop:n=lws_service: "<<n);
    }
}

BOOST_PYTHON_MODULE(libkrakenmd)
{
    using namespace boost::python;
    class_<MDEngineKraken, boost::shared_ptr<MDEngineKraken> >("Engine")
            .def(init<>())
            .def("init", &MDEngineKraken::initialize)
            .def("start", &MDEngineKraken::start)
            .def("stop", &MDEngineKraken::stop)
            .def("logout", &MDEngineKraken::logout)
            .def("wait_for_stop", &MDEngineKraken::wait_for_stop);
}
