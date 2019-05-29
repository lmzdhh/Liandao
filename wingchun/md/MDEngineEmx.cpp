#include "MDEngineEmx.h"
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

static MDEngineEmx* global_md = nullptr;

/*quest3 fxw v4 starts*/
// int MDEngineBitfinex::GetSnapShotAndRtn(std::string ticker)//v1
// {
// 	std::string symbol = coinPairWhiteList_rest.GetValueByKey(ticker);
// 	std::string requestPath = "/v1/book/";
// 	std::string body = "";
// 	string url = "wss://api.emx.com" + requestPath + symbol;//complete url
// 	KF_LOG_DEBUG(logger, "[quest2v4 fxw GetSnapShot]the url we ask :" << url);
// 	cpr::Response response = Get(
// 		Url{ url }, cpr::VerifySsl{ false },
// 		cpr::Body{ body },
// 		cpr::Timeout{ 10000 }
// 	);
// 	if (response.status_code >= 200 && response.status_code <= 299)
// 	{
// 		KF_LOG_DEBUG(logger, "[quest2v4 fxw GetSnapShot]request succeeded,the text is :" << response.text.c_str());
// 		Document d;
// 		d.Parse(response.text.c_str());
// 		LFPriceBook20Field md;
// 		strcpy(md.ExchangeID, "emx");
// 		strcpy(md.InstrumentID, ticker.c_str());
// 		md.UpdateMicroSecond = 0;
// 		md.Status = 0;
// 		if (d.HasMember("bids"))
// 		{
// 			auto& bids = d["bids"];
// 			if (bids.IsArray() && bids.Size() > 0)
// 			{
// 				auto size = std::min((int)bids.Size(), 20);
// 				for (int i = 0; i < size; ++i)
// 				{
// 					md.BidLevels[i].price = stod(bids.GetArray()[i]["price"].GetString()) * scale_offset;
// 					md.BidLevels[i].volume = stod(bids.GetArray()[i]["amount"].GetString()) * scale_offset;
// 					KF_LOG_DEBUG(logger, "[quest2v4]bids price:" << md.BidLevels[i].price << "volume:" << md.BidLevels[i].volume);
// 				}
// 				md.BidLevelCount = size;
// 			}
// 		}
// 		if (d.HasMember("asks"))
// 		{
// 			auto& asks = d["asks"];

// 			if (asks.IsArray() && asks.Size() > 0)
// 			{
// 				auto size = std::min((int)asks.Size(), 20);

// 				for (int i = 0; i < size; ++i)
// 				{
// 					md.AskLevels[i].price = stod(asks.GetArray()[i]["price"].GetString()) * scale_offset;
// 					md.AskLevels[i].volume = stod(asks.GetArray()[i]["amount"].GetString()) * scale_offset;
// 					KF_LOG_DEBUG(logger, "[quest2v4]asks price:" << md.AskLevels[i].price << "volume:" << md.AskLevels[i].volume);
// 				}
// 				md.AskLevelCount = size;
// 			}
// 		}
// 		if (md.BidLevels[0].price > md.AskLevels[0].price)
// 			md.Status = 2;
// 		else md.Status = 0;
// 		timer = getTimestamp();
// 		on_price_book_update(&md);
// 		KF_LOG_DEBUG(logger, "[quest2v4 fxw GetSnapShot]snapshot price book update succeeded");
// 	}
// 	else
// 	{
// 		KF_LOG_DEBUG(logger, "[quest2 fxw GetSnapShot]request failed");
// 		KF_LOG_DEBUG(logger, "[quest2 fxw GetSnapShot](response.status_code)" << response.status_code << "(response.text)" << response.text.c_str());
// 	}
// 	return  0;
// }

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

MDEngineEmx::MDEngineEmx(): IMDEngine(SOURCE_EMX)
{
    logger = yijinjing::KfLog::getLogger("MdEngine.Emx");
}

void MDEngineEmx::load(const json& j_config) 
{
    book_depth_count = j_config["book_depth_count"].get<int>();
    trade_count = j_config["trade_count"].get<int>();
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();
    KF_LOG_INFO(logger, "MDEngineEmx:: rest_get_interval_ms: " << rest_get_interval_ms);


    coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
    coinPairWhiteList.Debug_print();

    makeWebsocketSubscribeJsonString();
    debug_print(websocketSubscribeJsonString);

    //display usage:
    if(coinPairWhiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "MDEngineEmx::lws_write_subscribe: subscribeCoinBaseQuote is empty. please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_m19\": \"BTCM19\",");
       // KF_LOG_ERROR(logger, "     \"etc_eth\": \"tETCETH\"");
        KF_LOG_ERROR(logger, "},");
    }

    KF_LOG_INFO(logger, "MDEngineEmx::load:  book_depth_count: "
            << book_depth_count << " trade_count: " << trade_count << " rest_get_interval_ms: " << rest_get_interval_ms);
}


// {
// "type": "subscribe",
// "contract_codes": ["BTCM19"," "," "],
// "channels": ["level2"]
// }
std::string MDEngineEmx::createJsonString(std::vector<string> &exchange_coinpair,int type)
{

    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();
    writer.Key("type");
    writer.String("subscribe");

    writer.Key("contract_codes");
    writer.StartArray();
    for(auto& coinpair:exchange_coinpair){
        writer.String(coinpair.c_str());
    } 
    writer.EndArray();

    writer.Key("channels");
    writer.StartArray();
    if(type == 0) writer.String("level2");
    else writer.String("ticker");
    writer.EndArray();
    
    writer.EndObject();
    return s.GetString();
}


void MDEngineEmx::makeWebsocketSubscribeJsonString()//创建请求
{
    std::unordered_map<std::string, std::string>::iterator map_itr;
    map_itr = coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().begin();
    std::vector<string> array_coinpair ;
    while(map_itr != coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().end()) {
        KF_LOG_DEBUG(logger, "[makeWebsocketSubscribeJsonString] keyIsExchangeSideWhiteList (strategy_coinpair) " << map_itr->first << " (exchange_coinpair) "<< map_itr->second);

        
        array_coinpair.push_back(map_itr->second);
        

        map_itr++;
    }
    std::string jsonBookString = createJsonString(array_coinpair,0);
    websocketSubscribeJsonString.push_back(jsonBookString);

    std::string jsonTradeString = createJsonString(array_coinpair,1);
    websocketSubscribeJsonString.push_back(jsonTradeString);
}

void MDEngineEmx::debug_print(std::vector<std::string> &subJsonString)
{
    size_t count = subJsonString.size();
    KF_LOG_INFO(logger, "[debug_print] websocketSubscribeJsonString (count) " << count);

    for (size_t i = 0; i < count; i++)
    {
        KF_LOG_INFO(logger, "[debug_print] websocketSubscribeJsonString (subJsonString) " << subJsonString[i]);
    }
}

void MDEngineEmx::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "MDEngineEmx::connect:");
    connected = true;
}

void MDEngineEmx::login(long timeout_nsec) {//连接到服务器
    KF_LOG_INFO(logger, "MDEngineEmx::login:");
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
        KF_LOG_INFO(logger, "MDEngineEmx::login: context created.");
    }

    if (context == NULL) {
        KF_LOG_ERROR(logger, "MDEngineEmx::login: context is NULL. return");
        return;
    }

    int logs = LLL_ERR | LLL_DEBUG | LLL_WARN;
    lws_set_log_level(logs, NULL);

    struct lws_client_connect_info ccinfo = {0};

    static std::string host  = "api.emx.com";
    static std::string path = "/";
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
    KF_LOG_INFO(logger, "MDEngineEmx::login: Connecting to " <<  ccinfo.host << ":" << ccinfo.port << ":" << ccinfo.path);

    if (wsi == NULL) {
        KF_LOG_ERROR(logger, "MDEngineEmx::login: wsi create error.");
        return;
    }
    KF_LOG_INFO(logger, "MDEngineEmx::login: wsi create success.");

    logged_in = true;
}

void MDEngineEmx::set_reader_thread()
{
    IMDEngine::set_reader_thread();

    rest_thread = ThreadPtr(new std::thread(boost::bind(&MDEngineEmx::loop, this)));
}

void MDEngineEmx::logout()
{
    KF_LOG_INFO(logger, "MDEngineEmx::logout:");
}

void MDEngineEmx::release_api()
{
    KF_LOG_INFO(logger, "MDEngineEmx::release_api:");
}

void MDEngineEmx::subscribeMarketData(const vector<string>& instruments, const vector<string>& markets)
{
    KF_LOG_INFO(logger, "MDEngineEmx::subscribeMarketData:");
}



int MDEngineEmx::lws_write_subscribe(struct lws* conn)
{
    KF_LOG_INFO(logger, "MDEngineEmx::lws_write_subscribe: (subscribe_index)" << subscribe_index);

    //有待发送的数据，先把待发送的发完，在继续订阅逻辑  
    if(websocketPendingSendMsg.size() > 0) {
        unsigned char msg[512];
        memset(&msg[LWS_PRE], 0, 512-LWS_PRE);

        std::string jsonString = websocketPendingSendMsg[websocketPendingSendMsg.size() - 1];
        websocketPendingSendMsg.pop_back();
        KF_LOG_INFO(logger, "MDEngineEmx::lws_write_subscribe: websocketPendingSendMsg" << jsonString.c_str());
        int length = jsonString.length();

        strncpy((char *)msg+LWS_PRE, jsonString.c_str(), length);
        int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);

        if(websocketPendingSendMsg.size() > 0)
        {    //still has pending send data, emit a lws_callback_on_writable()
            lws_callback_on_writable( conn );
            KF_LOG_INFO(logger, "MDEngineEmx::lws_write_subscribe: (websocketPendingSendMsg,size)" << websocketPendingSendMsg.size());
        }
        return ret;
    }

    if(websocketSubscribeJsonString.size() == 0) return 0;//
    //sub depth
    if(subscribe_index >= websocketSubscribeJsonString.size())
    {
        //subscribe_index = 0;
        KF_LOG_INFO(logger, "MDEngineEmx::lws_write_subscribe: (none reset subscribe_index = 0, just return 0)");
	    return 0;
    }

    unsigned char msg[512];
    memset(&msg[LWS_PRE], 0, 512-LWS_PRE);

    std::string jsonString = websocketSubscribeJsonString[subscribe_index++];

    KF_LOG_INFO(logger, "MDEngineEmx::lws_write_subscribe: " << jsonString.c_str() << " ,after ++, (subscribe_index)" << subscribe_index);
    int length = jsonString.length();

    strncpy((char *)msg+LWS_PRE, jsonString.c_str(), length);
    int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);

    if(subscribe_index < websocketSubscribeJsonString.size())
    {
        lws_callback_on_writable( conn );
        KF_LOG_INFO(logger, "MDEngineEmx::lws_write_subscribe: (subscribe_index < websocketSubscribeJsonString) call lws_callback_on_writable");
    }

    return ret;
}

//{"type":"subscriptions","channels":[{"name":"level2","contract_codes":["BTCM19"]}]} 
//{\channel\:\heartbeat\,\time\:\2019-05-27T08:52:04.663Z\} 要过滤掉
void MDEngineEmx::on_lws_data(struct lws* conn, const char* data, size_t len)
{
    KF_LOG_INFO(logger, "MDEngineEmx::on_lws_data: " << data<<",len:"<<len);
    Document json;
    json.Parse(data);

    if(json.HasParseError()) {
        KF_LOG_ERROR(logger, "MDEngineEmx::on_lws_data. parse json error: " << data);
        return;
    }
   
    if(json.HasMember("channels")){
        KF_LOG_INFO(logger, "MDEngineEmx::on_lws_data: subscriptions info  " );
    }
    else if(strcmp(json["channel"].GetString(),"heartbeat") == 0){
        KF_LOG_INFO(logger, "MDEngineEmx::on_lws_data: heartbeat info  " );
    }
    
    else if(strcmp(json["channel"].GetString(),"level2") == 0){
            onBook(json);
    }
    else if(strcmp(json["channel"].GetString(),"ticker") == 0){
            onTrade(json);
    }
    else KF_LOG_INFO(logger, "MDEngineEmx::on_lws_data: unknown data: " << parseJsonToString(json));
}


void MDEngineEmx::on_lws_connection_error(struct lws* conn) //liu
{
    KF_LOG_ERROR(logger, "MDEngineEmx::on_lws_connection_error.");
    //market logged_in false;
    logged_in = false;
    KF_LOG_ERROR(logger, "MDEngineEmx::on_lws_connection_error. login again.");
    //clear the price book, the new websocket will give 200 depth on the first connect, it will make a new price book
    priceBook20Assembler.clearPriceBook();
    //no use it
    long timeout_nsec = 0;
    //reset sub
    subscribe_index = 0;

    login(timeout_nsec);
}

int64_t MDEngineEmx::getTimestamp()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp;
}



void MDEngineEmx::debug_print(std::vector<SubscribeChannel> &websocketSubscribeChannel)
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


// {
//   channel: "ticker",
//   type: "update",
//   data: {
//     contract_code: "BTCU18",
//     last_trade: {
//       price: "7500.25",
//       volume: "12.5478",
//       imbalance: "-2.0000",
//       logical_time: "2018-08-31T07:26:31.000Z",
//       auction_code: "BTCU18-2018-08-31T07:26:31.000Z"
//     },
//     quote: {
//       ask: "7500.24",
//       ask_size: "122.1218",
//       bid: "7500.23",
//       bid_size: "143.2843"
//      },
//      fair_price: "7500.23",
//      index_price: "7500.50",
//      mark_price: "7500.50",
//      after_auction_code: "BTCU18-2018-08-31T07:26:37.000Z"
//    }
// }
void MDEngineEmx::onTrade(Document& json)
{
        auto& data = json["data"];
        std::string ticker = coinPairWhiteList.GetKeyByValue(data["contract_code"].GetString());
        KF_LOG_INFO(logger, "MDEngineEmx::onTrade: (ticker) " << ticker.c_str());
        LFL2TradeField trade;
        memset(&trade, 0, sizeof(trade));
        strcpy(trade.InstrumentID, ticker.c_str());
        strcpy(trade.ExchangeID, "Emx");
        auto& last_trade = data["last_trade"];

        trade.Price = std::round(stod(last_trade["price"].GetString()) * scale_offset);
        double amount = stod(last_trade["volume"].GetString());
        uint64_t volume = 0;

         volume = std::round( amount * scale_offset);

            // if(data["type"].GetInt() == 0){
            //     KF_LOG_INFO(logger,"type = 0");
            //     KF_LOG_INFO(logger,"order_id_type is ");
            //     strcpy(trade.MakerOrderID,std::to_string(data["buy_order_id"].GetInt64()).c_str());
            //     KF_LOG_INFO(logger,"sell_order_id_type is ");
            //     strcpy(trade.TakerOrderID,std::to_string(data["sell_order_id"].GetInt64()).c_str());
            // }
            // else{
            //      KF_LOG_INFO(logger,"type = 1");
            //     strcpy(trade.MakerOrderID,std::to_string(data["sell_order_id"].GetInt64()).c_str());
            //     KF_LOG_INFO(logger,"sell_order_id_type is ");
            //     strcpy(trade.TakerOrderID,std::to_string(data["buy_order_id"].GetInt64()).c_str());
            // };
            trade.Volume = volume;
            //trade.OrderBSFlag[0] = data["type"].GetInt() == 0 ? 'B' : 'S';
            //KF_LOG_INFO(logger,"id_type");
            //strcpy(trade.TradeID,std::to_string(data["id"].GetInt64()).c_str());
            strcpy(trade.TradeTime,((std::string)last_trade["logical_time"].GetString()).c_str());

            KF_LOG_INFO(logger, "MDEngineEmx::[onTrade]"  <<
                                                                " (Price)" << trade.Price <<
                                                                " (trade.Volume)" << trade.Volume);
            on_trade(&trade);

}
// {
//   channel: "level2",
//   type: "snapshot",
//   data: {
//     contract_code: "BTCZ18",
//     bids: [[ "7500.00", "100.2341" ], [ "7500.01", "56.2151" ], ...],
//     asks: [[ "7500.02", "12.4372" ], [ "7500.03", "32.1839" ], ...]
//   }
// }
void MDEngineEmx::onBook(Document& json)
{

        KF_LOG_DEBUG(logger, "onBook start");
        if (!json.HasMember("data"))
        {
            KF_LOG_INFO(logger, "MDEngineEmx::onBook: no member:data ");
            return;
        }
        auto& data = json["data"];

        std::string ticker = coinPairWhiteList.GetKeyByValue(data["contract_code"].GetString());
        KF_LOG_INFO(logger, "MDEngineEmx::onBook: (symbol) " << ticker.c_str());

        int64_t price;
        double dAmount;
        uint64_t amount;
        uint64_t volume = 0;

        int i = 0;
         
        if(strcmp(json["type"].GetString(),"snapshot") == 0){

            auto& bids = data["bids"];
            auto& asks = data["asks"];
            KF_LOG_INFO(logger,"MDEngineEmx::onBook:snapshot");

                //KF_LOG_INFO(logger,"MDEngineEmx::onBook:bids");
                
                for(i = 0; i < std::min((int)bids.Size(),book_depth_count); i++)
                {
                   
                    price = std::round(stod(bids[i].GetArray()[0].GetString()) * SCALE_OFFSET);
                    volume = std::round(stod(bids[i].GetArray()[1].GetString()) * SCALE_OFFSET);
                     KF_LOG_INFO(logger, "MDEngineBitfinex::onBook: bids: " <<" (ticker)"
                                                                << ticker << " (price)" 
                                                                << price << " (volume)" << volume <<"(i)"<<i);
                    priceBook20Assembler.UpdateBidPrice(ticker, price, volume);
                }
 
                //KF_LOG_INFO(logger,"MDEngineEmx::onBook:asks");

                for(i = 0; i < std::min((int)asks.Size(),book_depth_count); ++i)
                {

                    price = std::round(stod(asks[i].GetArray()[0].GetString()) * SCALE_OFFSET);
                    volume = std::round(stod(asks[i].GetArray()[1].GetString()) * SCALE_OFFSET);
                    KF_LOG_INFO(logger, "MDEngineBitfinex::onBook: asks: " <<" (ticker)"
                                                                << ticker << " (price)" 
                                                                << price << " (volume)" << volume <<"(i)"<<i);
                    priceBook20Assembler.UpdateAskPrice(ticker, price, volume);
                }
        }
// {
//   channel: "level2",
//   type: "update",
//   data: {
//     contract_code: "BTCZ18",
//     after_auction_code: "BTCU18-2018-09-03T19:57:41.000Z",
//     changes: [
//       [ "bid", "7500.00", "75.2341" ],
//       [ "ask", "7500.02", "13.9372" ],
//       [ "ask", "7500.03", "0" ]
//     ]
//   }
// }
        else if(strcmp(json["type"].GetString() , "update") == 0){
            KF_LOG_INFO(logger,"MDEngineEmx::onBook:update");

            auto& changes = data["changes"];
            
            std::string type;
            for(i = 0; i < (int)changes.Size(); i++){

                price = std::round(stod(changes.GetArray()[i].GetArray()[1].GetString()) * scale_offset);
                dAmount = stod(changes.GetArray()[i].GetArray()[2].GetString());
				amount = std::round(dAmount * scale_offset);
                type = changes.GetArray()[i].GetArray()[0].GetString();
                KF_LOG_INFO(logger, "MDEngineBitfinex::onBook: (type) "<< type <<" (ticker)"
                                                                << ticker << " (price)" 
                                                                << price << " (amount)" << amount);

                if(type == "bid") {
                    if(amount == 0){
                         //KF_LOG_INFO(logger,"bid erase");
                        priceBook20Assembler.EraseBidPrice(ticker, price);
                    }
                    else {
                        //KF_LOG_INFO(logger,"bid update");
                        priceBook20Assembler.UpdateBidPrice(ticker,price,amount);
                    }
                }
                else if(type == "ask") {
                    if(amount == 0 ) {
                       // KF_LOG_INFO(logger,"ask erase");
                        priceBook20Assembler.EraseAskPrice(ticker, price);
                    }
                    else {
                        //KF_LOG_INFO(logger,"ask update");
                        priceBook20Assembler.UpdateAskPrice(ticker,price,amount);
                    }
                }
            }
        }

        LFPriceBook20Field md;
	    memset(&md, 0, sizeof(md));
	    if (priceBook20Assembler.Assembler(ticker, md)) {
            strcpy(md.ExchangeID, "emx");

            KF_LOG_INFO(logger, "MDEngineBitfinex::onDepth: on_price_book_update");
        }
        
        on_price_book_update(&md);
        KF_LOG_DEBUG(logger, "onBooka end");
}

std::string MDEngineEmx::parseJsonToString(Document &d)
{
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);

    return buffer.GetString();
}

void MDEngineEmx::loop()
{
    while(isRunning)
    {
        int n = lws_service( context, rest_get_interval_ms );
        std::cout << " 3.1415 loop() lws_service (n)" << n << std::endl;
    }
}

BOOST_PYTHON_MODULE(libemxmd)
{
    using namespace boost::python;
    class_<MDEngineEmx, boost::shared_ptr<MDEngineEmx> >("Engine")
            .def(init<>())
            .def("init", &MDEngineEmx::initialize)
            .def("start", &MDEngineEmx::start)
            .def("stop", &MDEngineEmx::stop)
            .def("logout", &MDEngineEmx::logout)
            .def("wait_for_stop", &MDEngineEmx::wait_for_stop);
}
