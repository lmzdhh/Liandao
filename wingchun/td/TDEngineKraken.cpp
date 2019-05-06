#include "TDEngineKraken.h"
#include "longfist/ctp.h"
#include "longfist/LFUtils.h"
#include "TypeConvert.hpp"
#include <boost/algorithm/string.hpp>

#include <writer.h>
#include <stringbuffer.h>
#include <document.h>
#include <iostream>
#include <string>
#include <sstream>
#include <stdio.h>
#include <assert.h>
#include <mutex>
#include <chrono>
#include <time.h>
#include <math.h>
#include <zlib.h>
#include <string.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/bio.h>
#include "../../utils/crypto/openssl_util.h"

using cpr::Post;
using cpr::Delete;
using cpr::Url;
using cpr::Body;
using cpr::Header;
using cpr::Parameters;
using cpr::Payload;
using cpr::Timeout;

using rapidjson::StringRef;
using rapidjson::Writer;
using rapidjson::StringBuffer;
using rapidjson::Document;
using rapidjson::SizeType;
using rapidjson::Value;
using std::string;
using std::to_string;
using std::stod;
using std::stoi;
using utils::crypto::hmac_sha256;
using utils::crypto::hmac_sha256_byte;
using utils::crypto::base64_encode;
using utils::crypto::base64_url_encode;
USING_WC_NAMESPACE

TDEngineKraken::TDEngineKraken(): ITDEngine(SOURCE_KRAKEN)
{
    logger = yijinjing::KfLog::getLogger("TradeEngine.Kraken");
    KF_LOG_INFO(logger, "[TDEngineKraken]");

    mutex_order_and_trade = new std::mutex();
    mutex_response_order_status = new std::mutex();
    mutex_orderaction_waiting_response = new std::mutex();
}

TDEngineKraken::~TDEngineKraken()
{
    if(mutex_order_and_trade != nullptr) delete mutex_order_and_trade;
    if(mutex_response_order_status != nullptr) delete mutex_response_order_status;
    if(mutex_orderaction_waiting_response != nullptr) delete mutex_orderaction_waiting_response;
}
static TDEngineKraken* global_md = nullptr;
//web socket代码
static int ws_service_cb( struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len )
{
    std::stringstream ss;
    ss << "[ws_service_cb] lws_callback,reason=" << reason << ",";
    switch( reason )
    {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
        {//lws callback client established
            global_md->on_lws_open(wsi);
            //lws_callback_on_writable( wsi );
            break;
        }
        case LWS_CALLBACK_PROTOCOL_INIT:
        {//lws callback protocol init
            ss << "LWS_CALLBACK_PROTOCOL_INIT.";
            //global_md->writeInfoLog(ss.str());
            break;
        }
        case LWS_CALLBACK_CLIENT_RECEIVE:
        {//lws callback client receive
            ss << "LWS_CALLBACK_CLIENT_RECEIVE.";
            //global_md->writeInfoLog(ss.str());
            if(global_md)
            {//统一接收，不同订阅返回数据不同解析
                global_md->on_lws_data(wsi, (char *)in, len);
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_WRITEABLE:
        {//lws callback client writeable
            ss << "LWS_CALLBACK_CLIENT_WRITEABLE.";
            //global_md->writeInfoLog(ss.str());
            int ret = 0;
            if(global_md)
            {//统一发送，不同订阅不同请求
                ret = global_md->on_lws_write_subscribe(wsi);
            }
            break;
        }
        case LWS_CALLBACK_CLOSED:
        {//lws callback close
            ss << "LWS_CALLBACK_CLOSED.";
            global_md->on_lws_close(wsi);
            break;
        }
        case LWS_CALLBACK_WSI_DESTROY:
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        {//lws callback client connection error
            ss << "LWS_CALLBACK_CLIENT_CONNECTION_ERROR.";
            //global_md->writeInfoLog(ss.str());
            if(global_md)
            {
                global_md->on_lws_connection_error(wsi);
            }
            break;
        }
        default:
            ss << "default Info.";
            //global_md->writeInfoLog(ss.str());
            break;
    }

    return 0;
}
void TDEngineKraken::on_lws_open(struct lws* wsi){
    KF_LOG_INFO(logger,"[on_lws_open] ");
    krakenAuth(findAccountUnitKrakenByWebsocketConn(wsi));
    KF_LOG_INFO(logger,"[on_lws_open] finished ");
}
//cys websocket connect
void TDEngineKraken::Ping(struct lws* conn)
{
    StringBuffer sbPing;
    Writer<StringBuffer> writer(sbPing);
    writer.StartObject();
    writer.Key("type");
    writer.String("ping");
    writer.EndObject();
    std::string strPing = sbPing.GetString();
    unsigned char msg[512];
    memset(&msg[LWS_PRE], 0, 512-LWS_PRE);
    int length = strPing.length();
    KF_LOG_INFO(logger, "TDEngineKraken::lws_write_ping: " << strPing.c_str() << " ,len = " << length);
    strncpy((char *)msg+LWS_PRE, strPing.c_str(), length);
    int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);
}
void TDEngineKraken::Pong(struct lws* conn,long long ping){
    KF_LOG_INFO(logger,"[Pong] pong the ping of websocket");
    StringBuffer sbPing;
    Writer<StringBuffer> writer(sbPing);
    writer.StartObject();
    writer.Key("op");
    writer.String("pong");
    writer.Key("ts");
    writer.Int64(ping);
    writer.EndObject();
    std::string strPong = sbPing.GetString();
    unsigned char msg[512];
    memset(&msg[LWS_PRE], 0, 512-LWS_PRE);
    int length = strPong.length();
    KF_LOG_INFO(logger, "[Pong] data " << strPong.c_str() << " ,len = " << length);
    strncpy((char *)msg+LWS_PRE, strPong.c_str(), length);
    int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);
}
void TDEngineKraken::on_lws_receive_orders(struct lws* conn,Document& json){
    KF_LOG_INFO(logger,"[on_lws_receive_orders]");
    std::lock_guard<std::mutex> guard_mutex(*mutex_response_order_status);
    std::lock_guard<std::mutex> guard_mutex_order_action(*mutex_orderaction_waiting_response);
    AccountUnitKraken& unit = findAccountUnitKrakenByWebsocketConn(conn);
    rapidjson::Value &data=json["data"];
    KF_LOG_INFO(logger, "[on_lws_receive_orders] receive_order:");
    if(data.HasMember("order-id")){
        KF_LOG_INFO(logger, "[on_lws_receive_orders] (receive success)");
        string remoteOrderId=std::to_string(data["order-id"].GetInt64());
        std::map<std::string,LFRtnOrderField>::iterator restOrderStatus=unit.restOrderStatusMap.find(remoteOrderId);
        if(restOrderStatus==unit.restOrderStatusMap.end()){
            KF_LOG_ERROR(logger,"[on_lws_receive_orders] rest receive no order id, save int websocketOrderStatusMap");
            unit.websocketOrderStatusMap.push_back(parseJsonToString(json));
        }else{
            handleResponseOrderStatus(unit, restOrderStatus->second, json);
            LfOrderStatusType orderStatus=GetOrderStatus(json["data"]["order-state"].GetString());
            if(orderStatus == LF_CHAR_AllTraded  || orderStatus == LF_CHAR_Canceled
                || orderStatus == LF_CHAR_Error){
                unit.restOrderStatusMap.erase(remoteOrderId);
            }
        }
    } else {
        KF_LOG_INFO(logger, "[on_lws_receive_orders] (reveive failed)");
        std::string errorMsg;
        int errorId = json["err-code"].GetInt();
        if(json.HasMember("err-msg") && json["err-msg"].IsString()){
            errorMsg = json["err-msg"].GetString();
        }
        KF_LOG_ERROR(logger, "[on_lws_receive_orders] get_order fail."<< " (errorId)" << errorId<< " (errorMsg)" << errorMsg);
    }
}
void TDEngineKraken::on_lws_data(struct lws* conn, const char* data, size_t len)
{
    KF_LOG_INFO(logger, "[on_lws_data] (data) " << data);
    //std::string strData = dealDataSprit(data);
    Document json;
    json.Parse(data);
    if(json.HasParseError()||!json.IsObject()){
        KF_LOG_ERROR(logger, "[cys_on_lws_data] parse to json error ");
        return;
    }
    if(json.HasMember("op")||json.HasMember("ping"))
    {
        if ((json.HasMember("status") && json["status"].GetString()!="ok")||      
              (json.HasMember("err-code")&&json["err-code"].GetInt()!=0) ) {
            int errorCode = json["err-code"].GetInt();
            std::string errorMsg = json["err-msg"].GetString();
            KF_LOG_ERROR(logger, "[on_lws_data] (err-code) "<<errorCode<<" (errMsg) " << errorMsg);
        } else if (json.HasMember("op")) {
            std::string op = json["op"].GetString();
            if (op == "notify") {
                string topic=json["topic"].GetString();
                if(topic.substr(0,topic.find("."))=="orders"){
                    on_lws_receive_orders(conn,json);
                }
            } else if (op == "ping") {
                long long ping=json["ts"].GetInt64();
                Pong(conn,ping);
            } else if (op == "auth") {
                isAuth=kraken_auth;
                int userId=json["data"]["user-id"].GetInt();
                KF_LOG_INFO(logger,"[on_lws_data] cys_krakenAuth success. authed user-id "<<userId);
            }
        } else if (json.HasMember("ch")) {

        } else if (json.HasMember("ping")) {
            long long ping=json["ts"].GetInt64();
            Pong(conn,ping);
        } else if (json.HasMember("subbed")) {

        }
    } else
    {
        KF_LOG_ERROR(logger, "[on_lws_data] . parse json error(data): " << data);
    }

}
std::string TDEngineKraken::makeSubscribeOrdersUpdate(AccountUnitKraken& unit){
    StringBuffer sbUpdate;
    Writer<StringBuffer> writer(sbUpdate);
    writer.StartObject();
    writer.Key("op");
    writer.String("sub");
    writer.Key("cid");
    writer.String(unit.spotAccountId.c_str());
    writer.Key("topic");
    writer.String("orders.*");
    writer.Key("model");
    writer.String("0");
    writer.EndObject();
    std::string strUpdate = sbUpdate.GetString();
    return strUpdate;
}
AccountUnitKraken& TDEngineKraken::findAccountUnitKrakenByWebsocketConn(struct lws * websocketConn){
    for (size_t idx = 0; idx < account_units.size(); idx++) {
        AccountUnitKraken &unit = account_units[idx];
        if(unit.webSocketConn == websocketConn) {
            return unit;
        }
    }
    return account_units[0];
}
int TDEngineKraken::subscribeTopic(struct lws* conn,string strSubscribe){
    unsigned char msg[1024];
    memset(&msg[LWS_PRE], 0, 1024-LWS_PRE);
    int length = strSubscribe.length();
    KF_LOG_INFO(logger, "[on_lws_write_subscribe] " << strSubscribe.c_str() << " ,len = " << length);
    strncpy((char *)msg+LWS_PRE, strSubscribe.c_str(), length);
    //请求
    int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);
    lws_callback_on_writable(conn);
    return ret;
}
int TDEngineKraken::on_lws_write_subscribe(struct lws* conn){
    //KF_LOG_INFO(logger, "[on_lws_write_subscribe]" );
    int ret = 0;
    AccountUnitKraken& unit=findAccountUnitKrakenByWebsocketConn(conn);
    if(isAuth==kraken_auth&&isOrders != orders_sub){
        isOrders=orders_sub;
        string strSubscribe = makeSubscribeOrdersUpdate(unit);
        ret=subscribeTopic(conn,strSubscribe);
    }
    return ret;
}

void TDEngineKraken::on_lws_connection_error(struct lws* conn){
    KF_LOG_ERROR(logger, "TDEngineKraken::on_lws_connection_error. login again.");
    //clear the price book, the new websocket will give 200 depth on the first connect, it will make a new price book
    m_isPong = false;
    isAuth = nothing;isOrders=nothing;
    long timeout_nsec = 0;
    AccountUnitKraken& unit=findAccountUnitKrakenByWebsocketConn(conn);
    lws_login(unit,0);
}
void TDEngineKraken::on_lws_close(struct lws* conn){
    isAuth=nothing;isOrders=nothing;
    KF_LOG_INFO(logger,"[websocket close]");
}
static struct lws_protocols protocols[] =
        {
                {
                        "ws",
                        ws_service_cb,
                              0,
                                 65536,
                },
                { NULL, NULL, 0, 0 } /* terminator */
        };
int on_lws_write_subscribe(struct lws* conn);
void on_lws_connection_error(struct lws* conn);

struct session_data {
    int fd;
};
void TDEngineKraken::writeInfoLog(std::string strInfo){
    KF_LOG_INFO(logger,strInfo);
}
void TDEngineKraken::writeErrorLog(std::string strError)
{
    KF_LOG_ERROR(logger, strError);
}

int64_t TDEngineKraken::getMSTime(){
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return  timestamp;
}
// helper function to compute SHA256:
std::vector<unsigned char> TDEngineKraken::sha256(string& data){
   std::vector<unsigned char> digest(SHA256_DIGEST_LENGTH);
   SHA256_CTX ctx;
   SHA256_Init(&ctx);
   SHA256_Update(&ctx, data.c_str(), data.length());
   SHA256_Final(digest.data(), &ctx);

   return digest;
}
vector<unsigned char> TDEngineKraken::hmac_sha512_kraken(vector<unsigned char>& data,vector<unsigned char> key){   
   unsigned int len = EVP_MAX_MD_SIZE;
   std::vector<unsigned char> digest(len);

   HMAC_CTX ctx;
   HMAC_CTX_init(&ctx);

   HMAC_Init_ex(&ctx, key.data(), key.size(), EVP_sha512(), NULL);
   HMAC_Update(&ctx, data.data(), data.size());
   HMAC_Final(&ctx, digest.data(), &len);
   
   HMAC_CTX_cleanup(&ctx);
   
   return digest;
}
std::string TDEngineKraken::b64_encode(const std::vector<unsigned char>& data) {
   BIO* b64 = BIO_new(BIO_f_base64());
   BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

   BIO* bmem = BIO_new(BIO_s_mem());
   b64 = BIO_push(b64, bmem);
   
   BIO_write(b64, data.data(), data.size());
   BIO_flush(b64);

   BUF_MEM* bptr = NULL;
   BIO_get_mem_ptr(b64, &bptr);
   
   std::string output(bptr->data, bptr->length);
   BIO_free_all(b64);

   return output;
}
std::vector<unsigned char> TDEngineKraken::b64_decode(const std::string& data) {
   BIO* b64 = BIO_new(BIO_f_base64());
   BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

   BIO* bmem = BIO_new_mem_buf((void*)data.c_str(),data.length());
   bmem = BIO_push(b64, bmem);
   
   std::vector<unsigned char> output(data.length());
   int decoded_size = BIO_read(bmem, output.data(), output.size());
   BIO_free_all(bmem);

   if (decoded_size < 0)
      throw std::runtime_error("failed while decoding base64.");
   
   return output;
}
std::string TDEngineKraken::getKrakenSignature(std::string& path,std::string& nonce, std::string postdata,AccountUnitKraken& unit){
   // add path to data to encrypt
   std::vector<unsigned char> data(path.begin(), path.end());

   // concatenate nonce and postdata and compute SHA256
   string np=nonce + postdata;
   std::vector<unsigned char> nonce_postdata = sha256(np);

   // concatenate path and nonce_postdata (path + sha256(nonce + postdata))
   data.insert(data.end(), nonce_postdata.begin(), nonce_postdata.end());

   // and compute HMAC
   return b64_encode(hmac_sha512_kraken(data, b64_decode(unit.secret_key)));
}
//cys edit from kraken api
std::mutex g_httpMutex;
cpr::Response TDEngineKraken::Get(const std::string& method_url,const std::string& body, std::string postData,AccountUnitKraken& unit)
{
    string url = unit.baseUrl + method_url+"?"+postData;
    std::unique_lock<std::mutex> lock(g_httpMutex);
    const auto response = cpr::Get(Url{url},
                                   Header{{}}, Timeout{10000} );
    lock.unlock();
    //if(response.text.length()<500){
    KF_LOG_INFO(logger, "[Get] (url) " << url << " (response.status_code) " << response.status_code <<
        " (response.error.message) " << response.error.message <<" (response.text) " << response.text.c_str());
    //}
    return response;
}
//cys edit
cpr::Response TDEngineKraken::Post(const std::string& method_url,const std::string& body,std::string postData, AccountUnitKraken& unit)
{
    int64_t nonce = getTimestamp();
    string nonceStr=std::to_string(nonce);
    KF_LOG_INFO(logger,"[Post] (nonce) "<<nonceStr);
    string s1="nonce=";
    postData=s1+nonceStr+"&"+postData;
    string path = method_url;
    string strSignature=getKrakenSignature(path,nonceStr,postData,unit);
    KF_LOG_INFO(logger,"[Post] (strSignature) "<<strSignature);

    string url = unit.baseUrl + method_url;
    std::unique_lock<std::mutex> lock(g_httpMutex);
    auto response = cpr::Post(Url{url}, Header{
                                {"API-Key", unit.api_key},
                                {"API-Sign",strSignature}},Body{postData},Timeout{30000});
    lock.unlock();
    //if(response.text.length()<500){
    KF_LOG_INFO(logger, "[POST] (url) " << url <<" (body) "<< body<< " \n(response.status_code) " << response.status_code
        <<" (response.error.message) " << response.error.message <<" (response.text) " << response.text.c_str());
    //}
    return response;
}
void TDEngineKraken::init()
{
    genUniqueKey();
    ITDEngine::init();
    JournalPair tdRawPair = getTdRawJournalPair(source_id);
    raw_writer = yijinjing::JournalSafeWriter::create(tdRawPair.first, tdRawPair.second, "RAW_" + name());
    KF_LOG_INFO(logger, "[init]");
}

void TDEngineKraken::pre_load(const json& j_config)
{
    KF_LOG_INFO(logger, "[pre_load]");
}

void TDEngineKraken::resize_accounts(int account_num)
{
    account_units.resize(account_num);
    KF_LOG_INFO(logger, "[resize_accounts]");
}

TradeAccount TDEngineKraken::load_account(int idx, const json& j_config)
{
    KF_LOG_INFO(logger, "[load_account]");
    // internal load
    string api_key = j_config["APIKey"].get<string>();
    string secret_key = j_config["SecretKey"].get<string>();
    string passphrase = j_config["passphrase"].get<string>();
    //https://api.kraken.pro
    string baseUrl = j_config["baseUrl"].get<string>();
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();

    if(j_config.find("orderaction_max_waiting_seconds") != j_config.end()) {
        orderaction_max_waiting_seconds = j_config["orderaction_max_waiting_seconds"].get<int>();
    }
    KF_LOG_INFO(logger, "[load_account] (orderaction_max_waiting_seconds)" << orderaction_max_waiting_seconds);

    if(j_config.find("max_rest_retry_times") != j_config.end()) {
        max_rest_retry_times = j_config["max_rest_retry_times"].get<int>();
    }
    KF_LOG_INFO(logger, "[load_account] (max_rest_retry_times)" << max_rest_retry_times);


    if(j_config.find("retry_interval_milliseconds") != j_config.end()) {
        retry_interval_milliseconds = j_config["retry_interval_milliseconds"].get<int>();
    }
    KF_LOG_INFO(logger, "[load_account] (retry_interval_milliseconds)" << retry_interval_milliseconds);

    AccountUnitKraken& unit = account_units[idx];
    unit.api_key = api_key;
    unit.secret_key = secret_key;
    unit.passphrase = passphrase;
    unit.baseUrl = baseUrl;

    KF_LOG_INFO(logger, "[load_account] (api_key)" << api_key << " (baseUrl)" << unit.baseUrl 
                                                   << " (spotAccountId) "<<unit.spotAccountId
                                                   << " (marginAccountId) "<<unit.marginAccountId);

    //test rs256
    //  std::string data ="{}";
    //  std::string signature =utils::crypto::rsa256_private_sign(data, g_private_key);
    // std::string sign = base64_encode((unsigned char*)signature.c_str(), signature.size());
    //std::cout  << "[TDEngineKraken] (test rs256-base64-sign)" << sign << std::endl;

    //std::string decodeStr = utils::crypto::rsa256_pub_verify(data,signature, g_public_key);
    //std::cout  << "[TDEngineKraken] (test rs256-verify)" << (decodeStr.empty()?"yes":"no") << std::endl;

    unit.coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
    unit.coinPairWhiteList.Debug_print();

    unit.positionWhiteList.ReadWhiteLists(j_config, "positionWhiteLists");
    unit.positionWhiteList.Debug_print();

    //display usage:
    if(unit.coinPairWhiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "TDEngineKraken::load_account: please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"btcusdt\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"etceth\"");
        KF_LOG_ERROR(logger, "},");
    }
    //test
    Document json;
    get_account(unit, json);
    //printResponse(json);
    //cancel_order(unit,"code","1",json);
    //printResponse(json);
    getPriceVolumePrecision(unit);
    // set up
    TradeAccount account = {};
    //partly copy this fields
    strncpy(account.UserID, api_key.c_str(), 16);
    strncpy(account.Password, secret_key.c_str(), 21);
    //web socket登陆
    //login(0);
    return account;
}

void TDEngineKraken::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[connect]");
    for (size_t idx = 0; idx < account_units.size(); idx++)
    {
        AccountUnitKraken& unit = account_units[idx];
        //unit.logged_in = true;
        KF_LOG_INFO(logger, "[connect] (api_key)" << unit.api_key);
        if (!unit.logged_in)
        {
            KF_LOG_INFO(logger, "[connect] (account id) "<<unit.spotAccountId<<" login.");
            lws_login(unit, 0);
            //set true to for let the kungfuctl think td is running.
            unit.logged_in = true;
        }
    }
}

void TDEngineKraken::getPriceVolumePrecision(AccountUnitKraken& unit){
    KF_LOG_INFO(logger,"[getPriceVolumePrecision]");
    Document json;
    const auto response = Get("/0/public/AssetPairs","","",unit);
    json.Parse(response.text.c_str());
    int errLen=json["error"].Size();
    if(json.HasMember("result") && errLen == 0){
        rapidjson::Value result=json["result"].GetObject();
        for (rapidjson::Value::ConstMemberIterator itr = result.MemberBegin();itr != result.MemberEnd(); ++itr){
            auto key = (itr->name).GetString();
            rapidjson::Value account=result[key].GetObject();
            PriceVolumePrecision stPriceVolumePrecision;
            stPriceVolumePrecision.symbol=account["altname"].GetString();
            std::string ticker = unit.coinPairWhiteList.GetKeyByValue(stPriceVolumePrecision.symbol);
            if(ticker.length()==0){
                //KF_LOG_ERROR(logger,"[getPriceVolumePrecision] (No such symbol in whitelist) "<<stPriceVolumePrecision.symbol);
                continue;
            }
            stPriceVolumePrecision.baseCurrency=account["base"].GetString();
            stPriceVolumePrecision.quoteCurrency=account["quote"].GetString();
            stPriceVolumePrecision.pricePrecision=account["pair_decimals"].GetInt();
            stPriceVolumePrecision.amountPrecision=account["lot_decimals"].GetInt();
            unit.mapPriceVolumePrecision.insert(std::make_pair(stPriceVolumePrecision.symbol,stPriceVolumePrecision));
            KF_LOG_INFO(logger,"[getPriceVolumePrecision] symbol "<<stPriceVolumePrecision.symbol);
        }
        KF_LOG_INFO(logger,"[getPriceVolumePrecision] (map size) "<<unit.mapPriceVolumePrecision.size());
    }
}
void TDEngineKraken::krakenAuth(AccountUnitKraken& unit){
    KF_LOG_INFO(logger, "[krakenAuth] auth");
    std::string strTimestamp = std::to_string(getTimestamp());
    std::string timestamp = std::to_string(getTimestamp());
    std::string strAccessKeyId=unit.api_key;
    std::string strSignatureMethod="HmacSHA256";
    std::string strSignatureVersion="2";
    string reqType="GET\n";
    std::string strSign = reqType+"api.kraken.pro\n" + "/ws/v1\n"+
                            "AccessKeyId="+strAccessKeyId+"&"+
                            "SignatureMethod="+strSignatureMethod+"&"+
                            "SignatureVersion="+strSignatureVersion+"&"+
                            "Timestamp="+strTimestamp;
    KF_LOG_INFO(logger, "[krakenAuth] strSign = " << strSign );
    unsigned char* strHmac = hmac_sha256_byte(unit.secret_key.c_str(),strSign.c_str());
    KF_LOG_INFO(logger, "[krakenAuth] strHmac = " << strHmac );
    std::string strSignatrue = base64_encode(strHmac,32);
    KF_LOG_INFO(logger, "[krakenAuth] Signatrue = " << strSignatrue );
    StringBuffer sbUpdate;
    Writer<StringBuffer> writer(sbUpdate);
    writer.StartObject();
    writer.Key("AccessKeyId");
    writer.String(unit.api_key.c_str());
    writer.Key("SignatureMethod");
    writer.String("HmacSHA256");
    writer.Key("SignatureVersion");
    writer.String("2");
    writer.Key("Timestamp");
    writer.String(timestamp.c_str());
    writer.Key("Signature");
    writer.String(strSignatrue.c_str());
    writer.Key("op");
    writer.String("auth");
    writer.EndObject();
    std::string strSubscribe = sbUpdate.GetString();
    unsigned char msg[1024];
    memset(&msg[LWS_PRE], 0, 1024-LWS_PRE);
    int length = strSubscribe.length();
    KF_LOG_INFO(logger, "[krakenAuth] auth data " << strSubscribe.c_str() << " ,len = " << length);
    strncpy((char *)msg+LWS_PRE, strSubscribe.c_str(), length);
    //请求
    int ret = lws_write(unit.webSocketConn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);
    lws_callback_on_writable(unit.webSocketConn);
    KF_LOG_INFO(logger, "[krakenAuth] auth success...");
}
void TDEngineKraken::lws_login(AccountUnitKraken& unit, long timeout_nsec){
    KF_LOG_INFO(logger, "[TDEngineKraken::lws_login]");
    global_md = this;
    isAuth=nothing;
    isOrders=nothing;
    global_md = this;
    int inputPort = 443;
    int logs = LLL_ERR | LLL_DEBUG | LLL_WARN;

    struct lws_context_creation_info ctxCreationInfo;
    struct lws_client_connect_info clientConnectInfo;
    //struct lws *wsi = NULL;

    memset(&ctxCreationInfo, 0, sizeof(ctxCreationInfo));
    memset(&clientConnectInfo, 0, sizeof(clientConnectInfo));

    ctxCreationInfo.port = CONTEXT_PORT_NO_LISTEN;
    ctxCreationInfo.iface = NULL;
    ctxCreationInfo.protocols = protocols;
    ctxCreationInfo.ssl_cert_filepath = NULL;
    ctxCreationInfo.ssl_private_key_filepath = NULL;
    ctxCreationInfo.extensions = NULL;
    ctxCreationInfo.gid = -1;
    ctxCreationInfo.uid = -1;
    ctxCreationInfo.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    ctxCreationInfo.fd_limit_per_thread = 1024;
    ctxCreationInfo.max_http_header_pool = 1024;
    ctxCreationInfo.ws_ping_pong_interval=1;
    ctxCreationInfo.ka_time = 10;
    ctxCreationInfo.ka_probes = 10;
    ctxCreationInfo.ka_interval = 10;

    context = lws_create_context(&ctxCreationInfo);
    KF_LOG_INFO(logger, "[TDEngineKraken::lws_login] context created.");


    if (context == NULL) {
        KF_LOG_ERROR(logger, "[TDEngineKraken::lws_login] context is NULL. return");
        return;
    }

    // Set up the client creation info
    static std::string host  = "api.kraken.pro";
    static std::string path = "/ws/v1";
    clientConnectInfo.address = host.c_str();
    clientConnectInfo.path = path.c_str(); // Set the info's path to the fixed up url path
    
    clientConnectInfo.context = context;
    clientConnectInfo.port = 443;
    clientConnectInfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
    clientConnectInfo.host = host.c_str();
    clientConnectInfo.origin = "origin";
    clientConnectInfo.ietf_version_or_minus_one = -1;
    clientConnectInfo.protocol = protocols[0].name;
    clientConnectInfo.pwsi = &unit.webSocketConn;

    KF_LOG_INFO(logger, "[TDEngineKraken::login] address = " << clientConnectInfo.address << ",path = " << clientConnectInfo.path);
    //建立websocket连接
    unit.webSocketConn = lws_client_connect_via_info(&clientConnectInfo);
    if (unit.webSocketConn == NULL) {
        KF_LOG_ERROR(logger, "[TDEngineKraken::lws_login] wsi create error.");
        return;
    }
    KF_LOG_INFO(logger, "[TDEngineKraken::login] wsi create success.");
}
void TDEngineKraken::login(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[TDEngineKraken::login]");
    connect(timeout_nsec);
}

void TDEngineKraken::logout()
{
    KF_LOG_INFO(logger, "[logout]");
}

void TDEngineKraken::release_api()
{
    KF_LOG_INFO(logger, "[release_api]");
}

bool TDEngineKraken::is_logged_in() const{
    KF_LOG_INFO(logger, "[is_logged_in]");
    for (auto& unit: account_units)
    {
        if (!unit.logged_in)
            return false;
    }
    return true;
}

bool TDEngineKraken::is_connected() const{
    KF_LOG_INFO(logger, "[is_connected]");
    return is_logged_in();
}


std::string TDEngineKraken::GetSide(const LfDirectionType& input) {
    if (LF_CHAR_Buy == input) {
        return "buy";
    } else if (LF_CHAR_Sell == input) {
        return "sell";
    } else {
        return "";
    }
}

LfDirectionType TDEngineKraken::GetDirection(std::string input) {
    if ("buy" == input) {
        return LF_CHAR_Buy;
    } else if ("sell" == input) {
        return LF_CHAR_Sell;
    } else {
        return LF_CHAR_Buy;
    }
}

std::string TDEngineKraken::GetType(const LfOrderPriceTypeType& input) {
    if (LF_CHAR_LimitPrice == input) {
        return "limit";
    } else if (LF_CHAR_AnyPrice == input) {
        return "market";
    } else {
        return "";
    }
}

LfOrderPriceTypeType TDEngineKraken::GetPriceType(std::string input) {
    if ("limit" == input) {
        return LF_CHAR_LimitPrice;
    } else if ("market" == input) {
        return LF_CHAR_AnyPrice;
    } else {
        return '0';
    }
}
//订单状态，pending 提交, open 部分成交, closed , open 成交, canceled 已撤销,expired 失效
LfOrderStatusType TDEngineKraken::GetOrderStatus(std::string state) {

    if(state == "canceled"){
        return LF_CHAR_Canceled;
    }else if(state == "pending"){
        return LF_CHAR_NotTouched;
    }else if(state == "closed"){
        return LF_CHAR_Error;
    }else if(state == "expired"){
        return LF_CHAR_Error;
    }else if(state == "open"){
        return LF_CHAR_PartTradedQueueing;
    }
    return LF_CHAR_Unknown;
}

/**
 * req functions
 * 查询账户持仓
 */
void TDEngineKraken::req_investor_position(const LFQryPositionField* data, int account_index, int requestId){
    KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId);

    AccountUnitKraken& unit = account_units[account_index];
    unit.userref=std::to_string(requestId);
    KF_LOG_INFO(logger, "[req_investor_position] (api_key)" << unit.api_key << " (InstrumentID) " << data->InstrumentID);

    LFRspPositionField pos;
    memset(&pos, 0, sizeof(LFRspPositionField));
    strncpy(pos.BrokerID, data->BrokerID, 11);
    strncpy(pos.InvestorID, data->InvestorID, 19);
    strncpy(pos.InstrumentID, data->InstrumentID, 31);
    pos.PosiDirection = LF_CHAR_Long;
    pos.HedgeFlag = LF_CHAR_Speculation;
    pos.Position = 0;
    pos.YdPosition = 0;
    pos.PositionCost = 0;

    int errorId = 0;
    std::string errorMsg = "";
    Document d;
    get_account(unit, d);
    KF_LOG_INFO(logger, "[req_investor_position] (get_account)" );
    if(d.IsObject() && d.HasMember("error"))
    {
        size_t isError=d["error"].Size();
        errorId = 0;
        KF_LOG_INFO(logger, "[req_investor_position] (errorId) " << errorId);
        if(isError != 0) {
            errorId=520;
            if (d.HasMember("error") && d["error"].IsArray()) {
                int i;
                for(i=0;i<d["error"].Size();i++){
                    errorMsg=errorMsg+d["error"].GetArray()[i].GetString()+"\n";
                }
            }
            KF_LOG_ERROR(logger, "[req_investor_position] failed!" << " (rid)" << requestId << " (errorId)" << errorId
                                                                   << " (errorMsg) " << errorMsg);
            raw_writer->write_error_frame(&pos, sizeof(LFRspPositionField), source_id, MSG_TYPE_LF_RSP_POS_KRAKEN, 1, requestId, errorId, errorMsg.c_str());
        }
    }
    send_writer->write_frame(data, sizeof(LFQryPositionField), source_id, MSG_TYPE_LF_QRY_POS_KRAKEN, 1, requestId);
    //{"error":[],"result":{"ZEUR":"10.0000"}}
    std::vector<LFRspPositionField> tmp_vector;
    KF_LOG_INFO(logger, "[req_investor_position] (result)");
    if(!d.HasParseError() && d.HasMember("result"))
    {
        Value accounts = d["result"].GetObject();
        for (rapidjson::Value::ConstMemberIterator itr = accounts.MemberBegin();itr != accounts.MemberEnd(); ++itr){
            //itr->name.GetString(), itr->value.GetType()
            std::string symbol = itr->name.GetString();
            KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId << " (symbol) " << symbol);
            pos.Position = std::round(std::stod(itr->value.GetString()) * scale_offset);
            tmp_vector.push_back(pos);
            KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId 
                            << " (symbol) " << symbol << " (position) " << pos.Position);
        }
    }

    //send the filtered position
    int position_count = tmp_vector.size();
    if(position_count > 0) {
        for (int i = 0; i < position_count; i++) {
            on_rsp_position(&tmp_vector[i], i == (position_count - 1), requestId, errorId, errorMsg.c_str());
        }
    }
    else
    {
        KF_LOG_INFO(logger, "[req_investor_position] (!findSymbolInResult) (requestId)" << requestId);
        on_rsp_position(&pos, 1, requestId, errorId, errorMsg.c_str());
    }
}

void TDEngineKraken::req_qry_account(const LFQryAccountField *data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_qry_account]");
}

void TDEngineKraken::dealPriceVolume(AccountUnitKraken& unit,const std::string& symbol,int64_t nPrice,int64_t nVolume,
            std::string& nDealPrice,std::string& nDealVolume){
    KF_LOG_DEBUG(logger, "[dealPriceVolume] (symbol)" << symbol);
    KF_LOG_DEBUG(logger, "[dealPriceVolume] (price)" << nPrice);
    KF_LOG_DEBUG(logger, "[dealPriceVolume] (volume)" << nVolume);
    std::string ticker = unit.coinPairWhiteList.GetValueByKey(symbol);
    auto it = unit.mapPriceVolumePrecision.find(ticker);
    if(it == unit.mapPriceVolumePrecision.end())
    {
        KF_LOG_INFO(logger, "[dealPriceVolume] symbol not find :" << ticker);
        nDealVolume = "0";
        return ;
    }
    else
    {
        KF_LOG_INFO(logger,"[dealPriceVolume] (deal price and volume precision)");
        int pPrecision=it->second.pricePrecision;
        int vPrecision=it->second.amountPrecision;
        KF_LOG_INFO(logger,"[dealPriceVolume] (pricePrecision) "<<pPrecision<<" (amountPrecision) "<<vPrecision);
        double tDealPrice=nPrice*1.0/scale_offset;
        double tDealVolume=nVolume*1.0/scale_offset;
        KF_LOG_INFO(logger,"[dealPriceVolume] (tDealPrice) "<<tDealPrice<<" (tDealVolume) "<<tDealVolume);
        char chP[16],chV[16];
        sprintf(chP,"%.8lf",nPrice*1.0/scale_offset);
        sprintf(chV,"%.8lf",nVolume*1.0/scale_offset);
        nDealPrice=chP;
        KF_LOG_INFO(logger,"[dealPriceVolume] (chP) "<<chP<<" (nDealPrice) "<<nDealPrice);
        nDealPrice=nDealPrice.substr(0,nDealPrice.find(".")+pPrecision+1);
        nDealVolume=chV;
         KF_LOG_INFO(logger,"[dealPriceVolume]  (chP) "<<chV<<" (nDealVolume) "<<nDealVolume);
        nDealVolume=nDealVolume.substr(0,nDealVolume.find(".")+vPrecision+1);
    }
    KF_LOG_INFO(logger, "[dealPriceVolume]  (symbol)" << ticker << " (Volume)" << nVolume << " (Price)" << nPrice
                                                      << " (FixedVolume)" << nDealVolume << " (FixedPrice)" << nDealPrice);
}
//发单
void TDEngineKraken::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time){
    //on_rtn_order(NULL);
    AccountUnitKraken& unit = account_units[account_index];
    unit.userref=std::to_string(requestId);
    KF_LOG_DEBUG(logger, "[req_order_insert]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Tid)" << data->InstrumentID
                                              << " (Volume)" << data->Volume
                                              << " (LimitPrice)" << data->LimitPrice
                                              << " (OrderRef)" << data->OrderRef);
    send_writer->write_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_KRAKEN, 1/*ISLAST*/, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_insert]: not in WhiteList, ignore it  (rid)" << requestId <<
                                                                                      " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_KRAKEN, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_insert] (exchange_ticker)" << ticker);
    Document d;
    std::string fixedPrice;
    std::string fixedVolume;
    dealPriceVolume(unit,data->InstrumentID,data->LimitPrice,data->Volume,fixedPrice,fixedVolume);
    if(fixedVolume == "0"){
        KF_LOG_DEBUG(logger, "[req_order_insert] fixed Volume error (no ticker)" << ticker);
        errorId = 200;
        errorMsg = data->InstrumentID;
        errorMsg += " : no such ticker";
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_KRAKEN, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_INFO(logger,"[req_order_insert] cys_ticker "<<ticker.c_str());
    //lock
    send_order(unit, unit.userref, ticker, GetSide(data->Direction),GetType(data->OrderPriceType), fixedVolume, fixedPrice, d);
    //not expected response
    if(!d.IsObject()){
        errorId = 100;
        errorMsg = "send_order http response has parse error or is not json. please check the log";
        KF_LOG_ERROR(logger, "[req_order_insert] send_order error!  (rid)" << requestId << " (errorId)" <<
                                                                           errorId << " (errorMsg) " << errorMsg);
    } else  if(d.HasMember("result")){//发单成功
        rapidjson::Value result=d["result"].GetObject();
        int errLen=d["error"].Size();
        if(errLen == 0) {
            //if send successful and the exchange has received ok, then add to  pending query order list
            std::string remoteOrderId = result["txid"].GetString();
            //fix defect of use the old value
            localOrderRefRemoteOrderId[std::string(data->OrderRef)] = remoteOrderId;
            KF_LOG_INFO(logger, "[req_order_insert] after send  (rid)" << requestId << " (OrderRef) " <<
                                                                       data->OrderRef << " (remoteOrderId) "
                                                                       << remoteOrderId);
            LFRtnOrderField rtn_order;
            memset(&rtn_order, 0, sizeof(LFRtnOrderField));
            strncpy(rtn_order.BusinessUnit,remoteOrderId.c_str(),21);
            rtn_order.OrderStatus = LF_CHAR_NotTouched;
            rtn_order.VolumeTraded = 0;
            
            strcpy(rtn_order.ExchangeID, "kraken");
            strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
            strncpy(rtn_order.InstrumentID, data->InstrumentID, 31);
            rtn_order.Direction = data->Direction;
            //No this setting on Kraken
            rtn_order.TimeCondition = LF_CHAR_GTC;
            rtn_order.OrderPriceType = data->OrderPriceType;
            strncpy(rtn_order.OrderRef, data->OrderRef, 13);
            rtn_order.VolumeTotalOriginal = data->Volume;
            rtn_order.LimitPrice = data->LimitPrice;
            rtn_order.VolumeTotal = data->Volume;

            on_rtn_order(&rtn_order);
            raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                    source_id, MSG_TYPE_LF_RTN_TRADE_KRAKEN,
                                    1, (rtn_order.RequestID > 0) ? rtn_order.RequestID : -1);

            KF_LOG_DEBUG(logger, "[req_order_insert] (addNewQueryOrdersAndTrades)" );

            addNewQueryOrdersAndTrades(unit, rtn_order, remoteOrderId);

            raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_KRAKEN, 1,
                                          requestId, errorId, errorMsg.c_str());
            KF_LOG_DEBUG(logger, "[req_order_insert] success" );
            return;
        }else {
            //errorId = std::round(std::stod(d["id"].GetString()));
            errorId=520;
            if (d.HasMember("error") && d["error"].IsArray()) {
                int i;
                for(i=0;i<d["error"].Size();i++){
                    errorMsg=errorMsg+d["error"].GetArray()[i].GetString()+"\t";
                }
            }
            KF_LOG_ERROR(logger, "[req_order_insert] send_order error!  (rid)" << requestId << " (errorId)" <<
                                                                               errorId << " (errorMsg) " << errorMsg);
        }
    }
    //unlock
    if(errorId != 0)
    {
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_KRAKEN, 1, requestId, errorId, errorMsg.c_str());
    }
}

void TDEngineKraken::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time){
    AccountUnitKraken& unit = account_units[account_index];
    unit.userref=std::to_string(requestId);
    KF_LOG_DEBUG(logger, "[req_order_action]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Iid)" << data->InvestorID
                                              << " (OrderRef)" << data->OrderRef
                                              << " (KfOrderID)" << data->KfOrderID);

    send_writer->write_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_KRAKEN, 1, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_action]: not in WhiteList , ignore it: (rid)" << requestId << " (errorId)" <<
                                                                                       errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_KRAKEN, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_action] (exchange_ticker)" << ticker);

    std::map<std::string, std::string>::iterator itr = localOrderRefRemoteOrderId.find(data->OrderRef);
    std::string remoteOrderId;
    if(itr == localOrderRefRemoteOrderId.end()) {
        errorId = 1;
        std::stringstream ss;
        ss << "[req_order_action] not found in localOrderRefRemoteOrderId map (orderRef) " << data->OrderRef;
        errorMsg = ss.str();
        KF_LOG_ERROR(logger, "[req_order_action] not found in localOrderRefRemoteOrderId map. "
                << " (rid)" << requestId << " (orderRef)" << data->OrderRef << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_KRAKEN, 1, requestId, errorId, errorMsg.c_str());
        return;
    } else {
        remoteOrderId = itr->second;
        KF_LOG_DEBUG(logger, "[req_order_action] found in localOrderRefRemoteOrderId map (orderRef) "
                << data->OrderRef << " (remoteOrderId) " << remoteOrderId);
    }

    Document d;
    cancel_order(unit, ticker, remoteOrderId, d);

    if(!d.HasParseError() && d.HasMember("error")&&d["error"].Size()==0) {
        errorId = 0;
        KF_LOG_INFO(logger,"[req_order_action] (response) " << parseJsonToString(d));
    }else{
        errorId = 520;
        if (d.HasMember("error") && d["error"].IsArray()) {
            int i;
            for(i=0;i<d["error"].Size();i++){
                errorMsg=errorMsg+d["error"].GetArray()[i].GetString()+"\n";
            }
        }
        KF_LOG_ERROR(logger, "[req_order_action] cancel_order failed!" << " (rid)" << requestId
                                                                       << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }
    if(errorId != 0)
    {
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_KRAKEN, 1, requestId, errorId, errorMsg.c_str());

    } else {
        KF_LOG_INFO(logger,"[req_order_action] cancel order success");
    }
}
//对于每个撤单指令发出后30秒（可配置）内，如果没有收到回报，就给策略报错（撤单被拒绝，pls retry)
void TDEngineKraken::addRemoteOrderIdOrderActionSentTime(const LFOrderActionField* data, int requestId, const std::string& remoteOrderId){
    std::lock_guard<std::mutex> guard_mutex_order_action(*mutex_orderaction_waiting_response);

    OrderActionSentTime newOrderActionSent;
    newOrderActionSent.requestId = requestId;
    newOrderActionSent.sentNameTime = getTimestamp();
    memcpy(&newOrderActionSent.data, data, sizeof(LFOrderActionField));
    remoteOrderIdOrderActionSentTime[remoteOrderId] = newOrderActionSent;
}
//cys no use
void TDEngineKraken::GetAndHandleOrderTradeResponse(){
    // KF_LOG_INFO(logger, "[GetAndHandleOrderTradeResponse]" );
    //every account
    for (size_t idx = 0; idx < account_units.size(); idx++)
    {
        AccountUnitKraken& unit = account_units[idx];
        if (!unit.logged_in)
        {
            continue;
        }
        //将新订单放到提交缓存中
        moveNewOrderStatusToPending(unit);
        retrieveOrderStatus(unit);
    }//end every account
}

//订单状态cys not use
void TDEngineKraken::retrieveOrderStatus(AccountUnitKraken& unit){

    std::lock_guard<std::mutex> guard_mutex(*mutex_response_order_status);
    std::lock_guard<std::mutex> guard_mutex_order_action(*mutex_orderaction_waiting_response);

    std::vector<LFRtnOrderField>::iterator orderStatusIterator;
    for(orderStatusIterator = unit.pendingOrderStatus.begin(); orderStatusIterator != unit.pendingOrderStatus.end();)
    {

        std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(orderStatusIterator->InstrumentID));
        if(ticker.length() == 0) {
            KF_LOG_INFO(logger, "[retrieveOrderStatus]: not in WhiteList , ignore it:" << orderStatusIterator->InstrumentID);
            continue;
        }
        KF_LOG_INFO(logger, "[retrieveOrderStatus] get_order " << "( account.api_key) " << unit.api_key
                                                               << "  (account.pendingOrderStatus.InstrumentID) " << orderStatusIterator->InstrumentID
                                                               << "  (account.pendingOrderStatus.OrderRef) " << orderStatusIterator->OrderRef
                                                               << "  (account.pendingOrderStatus.remoteOrderId) " << orderStatusIterator->BusinessUnit
                                                               << "  (account.pendingOrderStatus.OrderStatus) " << orderStatusIterator->OrderStatus
                                                               << "  (exchange_ticker)" << ticker
        );

        Document d;
        query_order(unit, ticker,orderStatusIterator->BusinessUnit, d);
        //订单状态，pending 提交, open 成交, canceled 已撤销, expired已失效, closed 
        if(d.HasParseError()) {
            //HasParseError, skip
            KF_LOG_ERROR(logger, "[retrieveOrderStatus] get_order response HasParseError " << " (symbol)" << orderStatusIterator->InstrumentID
                                                                                           << " (orderRef)" << orderStatusIterator->OrderRef
                                                                                           << " (remoteOrderId) " << orderStatusIterator->BusinessUnit);
            continue;
        }
        KF_LOG_INFO(logger, "[retrieveOrderStatus] query_order:");
        if(d.HasMember("error") && d["error"].Size()==0)
        {
            KF_LOG_INFO(logger, "[retrieveOrderStatus] (query success)");
            rapidjson::Value data = d["result"].GetObject();
            ResponsedOrderStatus responsedOrderStatus;
            responsedOrderStatus.ticker = ticker;
            //已成交总金额
            double dDealFunds = std::stod(data["cost"].GetString());
            //已成交数量
            double dDealSize = std::stod(data["vol_exec"].GetString());
            responsedOrderStatus.averagePrice = dDealSize > 0 ? std::round(dDealFunds / dDealSize * scale_offset): 0;
            responsedOrderStatus.orderId = orderStatusIterator->BusinessUnit;
            rapidjson::Value descr=data["descr"].GetObject();
            //报单价格条件
            responsedOrderStatus.OrderPriceType = GetPriceType(descr["ordertype"].GetString());
            //买卖方向
            responsedOrderStatus.Direction = GetDirection(descr["type"].GetString());
            //已成交数量
            int64_t nDealSize = std::round(dDealSize * scale_offset);
            //总量
            int64_t nSize = std::round(std::stod(data["vol"].GetString()) * scale_offset);
            responsedOrderStatus.price = std::round(std::stod(data["price"].GetString()) * scale_offset);
            responsedOrderStatus.volume = nSize;
            //单次成交数量
            responsedOrderStatus.VolumeTraded = nDealSize;
            if(responsedOrderStatus.VolumeTraded>0){
                orderStatusIterator->VolumeTraded+=responsedOrderStatus.VolumeTraded;
            }
            //单次未成交数量
            responsedOrderStatus.openVolume =  nSize - orderStatusIterator->VolumeTraded;
            LfOrderStatusType orderStatus = GetOrderStatus(data["status"].GetString());
            if(orderStatus == LF_CHAR_PartTradedQueueing&&orderStatusIterator->VolumeTraded>=responsedOrderStatus.volume){
                orderStatus = LF_CHAR_AllTraded;
            }
            responsedOrderStatus.OrderStatus = orderStatus;
            handlerResponseOrderStatus(unit, orderStatusIterator, responsedOrderStatus);

            //OrderAction发出以后，有状态回来，就清空这次OrderAction的发送状态，不必制造超时提醒信息
            remoteOrderIdOrderActionSentTime.erase(orderStatusIterator->BusinessUnit);
        } else {
            KF_LOG_INFO(logger, "[retrieveOrderStatus] (query failed)");
            std::string errorMsg;
            std::string errorId = "520";
            if (d.HasMember("error") && d["error"].IsArray()) {
                int i;
                for(i=0;i<d["error"].Size();i++){
                    errorMsg=errorMsg+d["error"].GetArray()[i].GetString()+"\t";
                }
            }
            KF_LOG_ERROR(logger, "[retrieveOrderStatus] get_order fail." << " (symbol)" << orderStatusIterator->InstrumentID
                                                                         << " (orderRef)" << orderStatusIterator->OrderRef
                                                                         << " (errorId)" << errorId
                                                                         << " (errorMsg)" << errorMsg);
        }

        //remove order when finish
        if(orderStatusIterator->OrderStatus == LF_CHAR_AllTraded  || orderStatusIterator->OrderStatus == LF_CHAR_Canceled
           || orderStatusIterator->OrderStatus == LF_CHAR_Error)
        {
            KF_LOG_INFO(logger, "[retrieveOrderStatus] remove a pendingOrderStatus.");
            orderStatusIterator = unit.pendingOrderStatus.erase(orderStatusIterator);
        } else {
            ++orderStatusIterator;
        }
    }
}
void TDEngineKraken::addNewQueryOrdersAndTrades(AccountUnitKraken& unit, LFRtnOrderField rtnOrder, std::string& remoteOrderId){
    KF_LOG_DEBUG(logger, "[addNewQueryOrdersAndTrades]" );
    //add new orderId for GetAndHandleOrderTradeResponse
    std::lock_guard<std::mutex> guard_mutex(*mutex_order_and_trade);

    unit.newOrderStatus.push_back(rtnOrder);

    KF_LOG_INFO(logger, "[addNewQueryOrdersAndTrades] (InstrumentID) " << rtnOrder.InstrumentID
                                                                       << " (OrderRef) " << rtnOrder.OrderRef
                                                                       << " (remoteOrderId) " << rtnOrder.BusinessUnit
                                                                       << "(VolumeTraded)" << rtnOrder.VolumeTraded);
}


void TDEngineKraken::moveNewOrderStatusToPending(AccountUnitKraken& unit)
{
    std::lock_guard<std::mutex> pending_guard_mutex(*mutex_order_and_trade);
    std::lock_guard<std::mutex> response_guard_mutex(*mutex_response_order_status);


    std::vector<LFRtnOrderField>::iterator newOrderStatusIterator;
    for(newOrderStatusIterator = unit.newOrderStatus.begin(); newOrderStatusIterator != unit.newOrderStatus.end();)
    {
        unit.pendingOrderStatus.push_back(*newOrderStatusIterator);
        newOrderStatusIterator = unit.newOrderStatus.erase(newOrderStatusIterator);
    }
}
//cys no use
void TDEngineKraken::set_reader_thread()
{
    ITDEngine::set_reader_thread();

    KF_LOG_INFO(logger, "[set_reader_thread] rest_thread start on TDEngineKraken::loop");
    rest_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineKraken::loop, this)));

    KF_LOG_INFO(logger, "[set_reader_thread] orderaction_timeout_thread start on TDEngineKraken::loopOrderActionNoResponseTimeOut");
    orderaction_timeout_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineKraken::loopOrderActionNoResponseTimeOut, this)));
}
//cys no use
void TDEngineKraken::loop()
{
    KF_LOG_INFO(logger, "[loop] (isRunning) " << isRunning);
    while(isRunning)
    {
        using namespace std::chrono;
        auto current_ms = duration_cast< milliseconds>(system_clock::now().time_since_epoch()).count();
        if(last_rest_get_ts != 0 && (current_ms - last_rest_get_ts) < rest_get_interval_ms)
        {
            continue;
        }

        last_rest_get_ts = current_ms;
        GetAndHandleOrderTradeResponse();
    }
}


void TDEngineKraken::loopOrderActionNoResponseTimeOut()
{
    KF_LOG_INFO(logger, "[loopOrderActionNoResponseTimeOut] (isRunning) " << isRunning);
    while(isRunning)
    {
        orderActionNoResponseTimeOut();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void TDEngineKraken::orderActionNoResponseTimeOut(){
    //    KF_LOG_DEBUG(logger, "[orderActionNoResponseTimeOut]");
    int errorId = 100;
    std::string errorMsg = "OrderAction has none response for a long time(" + std::to_string(orderaction_max_waiting_seconds) + " s), please send OrderAction again";

    std::lock_guard<std::mutex> guard_mutex_order_action(*mutex_orderaction_waiting_response);

    int64_t currentNano = getTimestamp();
    int64_t timeBeforeNano = currentNano - orderaction_max_waiting_seconds * 1000;
    //    KF_LOG_DEBUG(logger, "[orderActionNoResponseTimeOut] (currentNano)" << currentNano << " (timeBeforeNano)" << timeBeforeNano);
    std::map<std::string, OrderActionSentTime>::iterator itr;
    for(itr = remoteOrderIdOrderActionSentTime.begin(); itr != remoteOrderIdOrderActionSentTime.end();)
    {
        if(itr->second.sentNameTime < timeBeforeNano)
        {
            KF_LOG_DEBUG(logger, "[orderActionNoResponseTimeOut] (remoteOrderIdOrderActionSentTime.erase remoteOrderId)" << itr->first );
            on_rsp_order_action(&itr->second.data, itr->second.requestId, errorId, errorMsg.c_str());
            itr = remoteOrderIdOrderActionSentTime.erase(itr);
        } else {
            ++itr;
        }
    }
    //    KF_LOG_DEBUG(logger, "[orderActionNoResponseTimeOut] (remoteOrderIdOrderActionSentTime.size)" << remoteOrderIdOrderActionSentTime.size());
}

void TDEngineKraken::printResponse(const Document& d){
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);
    KF_LOG_INFO(logger, "[printResponse] ok (text) " << buffer.GetString());
}

void TDEngineKraken::getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& json)
{
    if(http_status_code >= HTTP_RESPONSE_OK && http_status_code <= 299)
    {
        json.Parse(responseText.c_str());
    } else if(http_status_code == 0)
    {
        json.SetObject();
        Document::AllocatorType& allocator = json.GetAllocator();
        int errorId = 1;
        json.AddMember("code", errorId, allocator);
        KF_LOG_INFO(logger, "[getResponse] (errorMsg)" << errorMsg);
        rapidjson::Value val;
        val.SetString(errorMsg.c_str(), errorMsg.length(), allocator);
        json.AddMember("msg", val, allocator);
    } else
    {
        Document d;
        d.Parse(responseText.c_str());
        KF_LOG_INFO(logger, "[getResponse] (err) (responseText)" << responseText.c_str());
        json.SetObject();
        Document::AllocatorType& allocator = json.GetAllocator();
        json.AddMember("code", http_status_code, allocator);

        rapidjson::Value val;
        val.SetString(errorMsg.c_str(), errorMsg.length(), allocator);
        json.AddMember("msg", val, allocator);
    }
}

void TDEngineKraken::get_account(AccountUnitKraken& unit, Document& json)
{
    KF_LOG_INFO(logger, "[get_account]");
    string path="/0/private/Balance";

    const auto response = Post(path,"","",unit);
    json.Parse(response.text.c_str());
    //KF_LOG_INFO(logger, "[get_account] (account info) "<<response.text.c_str());
    return ;
}
std::string TDEngineKraken::createInsertOrdertring(string pair,string type,string ordertype,string price,string volume,
        string oflags,string userref){
    string s="";
    s=s+"pair="+pair+"&"+
        "type="+type+"&"+
        "ordertype="+ordertype+"&"+
        "price="+price+"&"+
        "volume="+volume+"&"+
        "userref="+userref;

    return s;
}
void TDEngineKraken::send_order(AccountUnitKraken& unit, string userref, string code,
                        string side, string type, string volume, string price, Document& json){
    KF_LOG_INFO(logger, "[send_order]");
    KF_LOG_INFO(logger, "[send_order] (code) "<<code);
    int retry_times = 0;
    cpr::Response response;
    bool should_retry = false;
    do {
        should_retry = false;
        string path = "/0/private/AddOrder";
        string postData=createInsertOrdertring(code, side, type, price,volume,"",userref);

        response = Post(path,postData,postData,unit);

        KF_LOG_INFO(logger, "[send_order] (url) " << path << " (response.status_code) " << response.status_code 
                                                  << " (response.error.message) " << response.error.message 
                                                  <<" (response.text) " << response.text.c_str() << " (retry_times)" << retry_times);

        //json.Clear();
        getResponse(response.status_code, response.text, response.error.message, json);
        //has error and find the 'error setting certificate verify locations' error, should retry
        if(shouldRetry(json)) {
            should_retry = true;
            retry_times++;
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_milliseconds));
        }
    } while(should_retry && retry_times < max_rest_retry_times);

    KF_LOG_INFO(logger, "[send_order] out_retry (response.status_code) " << response.status_code <<" (response.error.message) " 
                                                                        << response.error.message << " (response.text) " << response.text.c_str() );

    //getResponse(response.status_code, response.text, response.error.message, json);
}
/*火币返回数据格式
    {
  "status": "ok",
  "ch": "market.btcusdt.kline.1day",
  "ts": 1499223904680,
  "data": // per API response data in nested JSON object
    }
*/
bool TDEngineKraken::shouldRetry(Document& doc)
{
    bool ret = false;
    std::string strCode ;
    if(doc.HasMember("status"))
    {
        strCode = doc["status"].GetString();
    }
    bool isObJect = doc.IsObject();
    if(!isObJect || strCode != "ok")
    {
        ret = true;
    }
    KF_LOG_INFO(logger, "[shouldRetry] isObJect = " << isObJect << ",strCode = " << strCode);
    return ret;
}

void TDEngineKraken::cancel_order(AccountUnitKraken& unit, std::string code, std::string orderId, Document& json)
{
    KF_LOG_INFO(logger, "[cancel_order]");

    int retry_times = 0;
    cpr::Response response;
    bool should_retry = false;
    do {
        should_retry = false;
        std::string path="/0/private/CancelOrder";
        std::string postData="txid=";
        postData=postData+orderId;

        response = Post(path,postData,postData,unit);

        //json.Clear();
        getResponse(response.status_code, response.text, response.error.message, json);
        //has error and find the 'error setting certificate verify locations' error, should retry
        if(shouldRetry(json)) {
            should_retry = true;
            retry_times++;
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_milliseconds));
        }
    } while(should_retry && retry_times < max_rest_retry_times);


    KF_LOG_INFO(logger, "[cancel_order] out_retry " << retry_times << " (response.status_code) " << response.status_code <<
                                                    " (response.error.message) " << response.error.message <<
                                                    " (response.text) " << response.text.c_str() );

    //getResponse(response.status_code, response.text, response.error.message, json);
}
void TDEngineKraken::query_order(AccountUnitKraken& unit, std::string code, std::string orderId, Document& json)
{
    KF_LOG_INFO(logger, "[query_order]");
    //kraken查询订单详情
    string getPath = "/0/private/QueryOrders";
    string s1="trades=",s2="userref=",s3="txid=";
    string postData=s1+"true&"+s2+unit.userref+"&"+s3+orderId;

    auto response = Post(getPath,postData,postData,unit);
    json.Parse(response.text.c_str());
    KF_LOG_DEBUG(logger,"[query_order] response "<<response.text.c_str());
    //getResponse(response.status_code, response.text, response.error.message, json);
}


void TDEngineKraken::handlerResponseOrderStatus(AccountUnitKraken& unit, std::vector<LFRtnOrderField>::iterator orderStatusIterator,
         ResponsedOrderStatus& responsedOrderStatus)
{
    KF_LOG_INFO(logger, "[handlerResponseOrderStatus]");
    if( responsedOrderStatus.OrderStatus == LF_CHAR_NotTouched && responsedOrderStatus.OrderStatus 
            == orderStatusIterator-> OrderStatus){//no change
        return;
    }
    orderStatusIterator->OrderStatus = responsedOrderStatus.OrderStatus;
    //累计成交数量
    //orderStatusIterator.VolumeTraded;
    //剩余未成交数量
    orderStatusIterator->VolumeTotal = responsedOrderStatus.volume-orderStatusIterator->VolumeTraded;
    on_rtn_order(&(*orderStatusIterator));
    raw_writer->write_frame(orderStatusIterator, sizeof(LFRtnOrderField),source_id, MSG_TYPE_LF_RTN_TRADE_KRAKEN,
        1, (orderStatusIterator->RequestID > 0) ? orderStatusIterator->RequestID: -1);

    //send OnRtnTrade
    LFRtnTradeField rtn_trade;
    memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
    strcpy(rtn_trade.ExchangeID, "kraken");
    strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
    strncpy(rtn_trade.InstrumentID, orderStatusIterator->InstrumentID, 31);
    strncpy(rtn_trade.OrderRef, orderStatusIterator->OrderRef, 13);
    rtn_trade.Direction = orderStatusIterator->Direction;
    //单次成交数量
    rtn_trade.Volume = responsedOrderStatus.VolumeTraded;
    rtn_trade.Price =std::round(std::stod(ResponsedOrderStatus.price)*scale_offset);//(newAmount - oldAmount)/(rtn_trade.Volume);
    strncpy(rtn_trade.OrderSysID,orderStatusIterator->BusinessUnit,31);
    on_rtn_trade(&rtn_trade);

    raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
        source_id, MSG_TYPE_LF_RTN_TRADE_KRAKEN, 1, -1);

    KF_LOG_INFO(logger, "[on_rtn_trade 1] (InstrumentID)" << rtn_trade.InstrumentID << "(Direction)" << rtn_trade.Direction
                << "(Volume)" << rtn_trade.Volume << "(Price)" <<  rtn_trade.Price);
}
void TDEngineKraken::handleResponseOrderStatus(AccountUnitKraken& unit, LFRtnOrderField& rtn_order, 
                                        Document& json){
    KF_LOG_INFO(logger, "[handleResponseOrderStatus]");
    if(!json.HasMember("data")){
        KF_LOG_ERROR(logger,"[handleResponseOrderStatus] no data segment");
        return;
    }
    auto& data=json["data"];
    if(!data.HasMember("filled-cash-amount")||!data.HasMember("filled-amount")||!data.HasMember("unfilled-amount")
        ||!data.HasMember("order-type")||!data.HasMember("order-amount")||!data.HasMember("order-state")
        ||!data.HasMember("order-price")||!data.HasMember("price")){
        KF_LOG_ERROR(logger,"[handleResponseOrderStatus] no child segment");
        return;
    }
    //单次成交总金额
    double dDealFunds = std::stod(data["filled-cash-amount"].GetString());
    //单次成交数量
    double dDealSize = std::stod(data["filled-amount"].GetString());
    //单次成交数量
    int64_t nDealSize = std::round(dDealSize * scale_offset);
    int64_t averagePrice = dDealSize > 0 ? std::round(dDealFunds / dDealSize * scale_offset): 0;
    //单次未成交数量
    double unfilledAmount=std::stod(data["unfilled-amount"].GetString());
    //单次未成交数量
    int64_t nUnfilledAmount = std::round(unfilledAmount * scale_offset);
    //报单价格条件
    LfOrderPriceTypeType orderPriceType = GetPriceType(data["order-type"].GetString());
    //买卖方向
    LfDirectionType direction = GetDirection(data["order-type"].GetString());
    //总量
    int64_t nVolume = std::round(std::stod(data["order-amount"].GetString()) * scale_offset);
    //报单状态
    LfOrderStatusType orderStatus=GetOrderStatus(data["order-state"].GetString());
    //总价
    int64_t price = std::round(std::stod(data["order-price"].GetString()) * scale_offset);
    int64_t volumeTraded = nVolume-nUnfilledAmount;
    if( (orderStatus == LF_CHAR_NotTouched && LF_CHAR_PartTradedQueueing == orderStatus || 
            orderStatus == rtn_order.OrderStatus) && 
            volumeTraded == rtn_order.VolumeTraded){//no change
        return;
    }
    rtn_order.OrderStatus = orderStatus;
    //累计成交数量
    rtn_order.VolumeTraded = volumeTraded;
    //剩余数量
    rtn_order.VolumeTotal = nUnfilledAmount;
    on_rtn_order(&rtn_order);
    raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),source_id, MSG_TYPE_LF_RTN_TRADE_KRAKEN,
        1, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);

    //send OnRtnTrade
    LFRtnTradeField rtn_trade;
    memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
    strcpy(rtn_trade.ExchangeID, "kraken");
    strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
    strncpy(rtn_trade.InstrumentID, rtn_order.InstrumentID, 31);
    strncpy(rtn_trade.OrderRef, rtn_order.OrderRef, 13);
    rtn_trade.Direction = rtn_order.Direction;
    //单次成交数量
    rtn_trade.Volume = nDealSize;
    rtn_trade.Price =std::round(std::stod(data["price"].GetString())*scale_offset);//(newAmount - oldAmount)/(rtn_trade.Volume);
    strncpy(rtn_trade.OrderSysID,rtn_order.BusinessUnit,31);
    on_rtn_trade(&rtn_trade);

    raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
        source_id, MSG_TYPE_LF_RTN_TRADE_KRAKEN, 1, -1);

    KF_LOG_INFO(logger, "[on_rtn_trade 1] (InstrumentID)" << rtn_trade.InstrumentID << "(Direction)" << rtn_trade.Direction
                << "(Volume)" << rtn_trade.Volume << "(Price)" <<  rtn_trade.Price);

}
std::string TDEngineKraken::parseJsonToString(Document &d){
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);

    return buffer.GetString();
}


inline int64_t TDEngineKraken::getTimestamp(){
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp;
}

void TDEngineKraken::genUniqueKey(){
    struct tm cur_time = getCurLocalTime();
    //SSMMHHDDN
    char key[11]{0};
    snprintf((char*)key, 11, "%02d%02d%02d%02d%1s", cur_time.tm_sec, cur_time.tm_min, cur_time.tm_hour, cur_time.tm_mday, m_engineIndex.c_str());
    m_uniqueKey = key;
}
//clientid =  m_uniqueKey+orderRef
std::string TDEngineKraken::genClinetid(const std::string &orderRef){
    static int nIndex = 0;
    return m_uniqueKey + orderRef + std::to_string(nIndex++);
}

#define GBK2UTF8(msg) kungfu::yijinjing::gbk2utf8(string(msg))
BOOST_PYTHON_MODULE(libkrakentd){
    using namespace boost::python;
    class_<TDEngineKraken, boost::shared_ptr<TDEngineKraken> >("Engine")
     .def(init<>())
        .def("init", &TDEngineKraken::initialize)
        .def("start", &TDEngineKraken::start)
        .def("stop", &TDEngineKraken::stop)
        .def("logout", &TDEngineKraken::logout)
        .def("wait_for_stop", &TDEngineKraken::wait_for_stop);
}
