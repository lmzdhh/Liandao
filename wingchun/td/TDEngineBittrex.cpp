#include "TDEngineBittrex.h"
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
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
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
using std::move;
using utils::crypto::hmac_sha256;
using utils::crypto::hmac_sha256_byte;
using utils::crypto::hmac_sha512;
using utils::crypto::base64_encode;
using utils::crypto::base64_url_encode;
USING_WC_NAMESPACE

TDEngineBittrex::TDEngineBittrex(): ITDEngine(SOURCE_BITTREX)
{
    logger = yijinjing::KfLog::getLogger("TradeEngine.Bittrex");
    KF_LOG_INFO(logger, "[TDEngineBittrex]");

    mutex_order_and_trade = new std::mutex();
    mutex_response_order_status = new std::mutex();
    mutex_orderaction_waiting_response = new std::mutex();
}

TDEngineBittrex::~TDEngineBittrex()
{
    if(mutex_order_and_trade != nullptr) delete mutex_order_and_trade;
    if(mutex_response_order_status != nullptr) delete mutex_response_order_status;
    if(mutex_orderaction_waiting_response != nullptr) delete mutex_orderaction_waiting_response;
}
static TDEngineBittrex* global_md = nullptr;
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
void TDEngineBittrex::on_lws_open(struct lws* wsi){
    KF_LOG_INFO(logger,"[on_lws_open] ");
    //bittrexAuth(findAccountUnitBittrexByWebsocketConn(wsi));
    KF_LOG_INFO(logger,"[on_lws_open] finished ");
}
//cys websocket connect
void TDEngineBittrex::Ping(struct lws* conn)
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
    KF_LOG_INFO(logger, "TDEngineBittrex::lws_write_ping: " << strPing.c_str() << " ,len = " << length);
    strncpy((char *)msg+LWS_PRE, strPing.c_str(), length);
    int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);
}
void TDEngineBittrex::Pong(struct lws* conn,long long ping){
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

void TDEngineBittrex::on_lws_data(struct lws* conn, const char* data, size_t len)
{
    KF_LOG_INFO(logger, "[on_lws_data] (data) " << data);
    //std::string strData = dealDataSprit(data);
    Document json;
    json.Parse(data);
    if(json.HasParseError()||!json.IsObject()){
        KF_LOG_ERROR(logger, "[cys_on_lws_data] parse to json error ");
        return;
    }

}
std::string TDEngineBittrex::makeSubscribeOrdersUpdate(AccountUnitBittrex& unit){
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
AccountUnitBittrex& TDEngineBittrex::findAccountUnitBittrexByWebsocketConn(struct lws * websocketConn){
    for (size_t idx = 0; idx < account_units.size(); idx++) {
        AccountUnitBittrex &unit = account_units[idx];
        if(unit.webSocketConn == websocketConn) {
            return unit;
        }
    }
    return account_units[0];
}
int TDEngineBittrex::subscribeTopic(struct lws* conn,string strSubscribe){
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
int TDEngineBittrex::on_lws_write_subscribe(struct lws* conn){
    //KF_LOG_INFO(logger, "[on_lws_write_subscribe]" );
    int ret = 0;
    AccountUnitBittrex& unit=findAccountUnitBittrexByWebsocketConn(conn);
    if(isAuth==bittrex_auth&&isOrders != orders_sub){
        isOrders=orders_sub;
        string strSubscribe = makeSubscribeOrdersUpdate(unit);
        ret=subscribeTopic(conn,strSubscribe);
    }
    return ret;
}

void TDEngineBittrex::on_lws_connection_error(struct lws* conn){
    KF_LOG_ERROR(logger, "TDEngineBittrex::on_lws_connection_error. login again.");
    //clear the price book, the new websocket will give 200 depth on the first connect, it will make a new price book
    m_isPong = false;
    isAuth = nothing;isOrders=nothing;
    long timeout_nsec = 0;
    AccountUnitBittrex& unit=findAccountUnitBittrexByWebsocketConn(conn);
    lws_login(unit,0);
}
void TDEngineBittrex::on_lws_close(struct lws* conn){
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
void TDEngineBittrex::writeInfoLog(std::string strInfo){
    KF_LOG_INFO(logger,strInfo);
}
void TDEngineBittrex::writeErrorLog(std::string strError)
{
    KF_LOG_ERROR(logger, strError);
}

int64_t TDEngineBittrex::getMSTime(){
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return  timestamp;
}
std::string TDEngineBittrex::hmac_sha512_bittrex(const std::string &uri, const std::string &secret) {
    unsigned char *digest;
    KF_LOG_INFO(logger,"[hmac_sha512_bittrex] (digest) ");
    digest = HMAC(EVP_sha512(),
                  reinterpret_cast<const unsigned char *>(secret.c_str()), secret.length(),
                  reinterpret_cast<const unsigned char *>(uri.c_str()), uri.length(),
                  NULL, NULL);
    KF_LOG_INFO(logger,"[hmac_sha512_bittrex] (hmac) ");
    char sha512_str[HMAC_MAX_MD_CBLOCK];
    for (int i = 0; i < 64; i++)
        sprintf(&sha512_str[i * 2], "%02x", (unsigned int) digest[i]);
    KF_LOG_INFO(logger,"[hmac_sha512_bittrex] (sprintf) ");
    return std::move(std::string(sha512_str));
}
std::string TDEngineBittrex::getBittrexSignature(std::string& message,std::string& secret,AccountUnitBittrex& unit){
    KF_LOG_INFO(logger,"[getBittrexSignature] (message) " << message << " (secret) " << secret);
    string hash = hmac_sha512_bittrex(message,secret);
    KF_LOG_INFO(logger,"[getBittrexSignature] (hash) " << hash);
    return hash;
}
//cys edit from bittrex api
std::mutex g_httpMutex;
cpr::Response TDEngineBittrex::Get(const std::string& method_url,const std::string& body, std::string postData,AccountUnitBittrex& unit)
{
    int64_t nonce = getTimestamp();
    string nonceStr=std::to_string(nonce);
    KF_LOG_INFO(logger,"[Get] (nonce) "<<nonceStr);
    if(postData == ""){
        postData=postData+"apikey="+unit.api_key+"&nonce="+nonceStr;
    }else{
        postData=postData+"&apikey="+unit.api_key+"&nonce="+nonceStr;
    }
    
    string message = unit.baseUrl+method_url+"?"+postData;
 
    string strSignature=getBittrexSignature(message,unit.secret_key,unit);

    string url = unit.baseUrl + method_url+"?"+postData;

    std::unique_lock<std::mutex> lock(g_httpMutex);
    const auto response = cpr::Get(Url{url},
                                   Header{{"apisign", strSignature}}, Timeout{10000} );
    lock.unlock();
    //if(response.text.length()<500){
    KF_LOG_INFO(logger, "[Get] (url) " << url << " (response.status_code) " << response.status_code <<
        " (response.error.message) " << response.error.message <<" (response.text) " << response.text.c_str());
    //}
    return response;
}
//cys edit
cpr::Response TDEngineBittrex::Post(const std::string& method_url,const std::string& body,std::string postData, AccountUnitBittrex& unit)
{
    int64_t nonce = getTimestamp();
    string nonceStr=std::to_string(nonce);
    KF_LOG_INFO(logger,"[Post] (nonce) "<<nonceStr);
    if(postData == ""){
        postData=postData+"apikey="+unit.api_key+"&nonce="+nonceStr;
    }else{
        postData=postData+"&apikey="+unit.api_key+"&nonce="+nonceStr;
    }
    string message = unit.baseUrl+method_url+"?"+postData;
    string strSignature=getBittrexSignature(message,unit.secret_key,unit);
    KF_LOG_INFO(logger,"[Post] (strSignature) "<<strSignature);

    string url = unit.baseUrl + method_url;
    std::unique_lock<std::mutex> lock(g_httpMutex);
    auto response = cpr::Post(Url{url}, Header{
                                {"apisign", strSignature}},Body{postData},Timeout{30000});
    lock.unlock();
    //if(response.text.length()<500){
    KF_LOG_INFO(logger, "[POST] (url) " << url <<" (body) "<< body<< " \n(response.status_code) " << response.status_code
        <<" (response.error.message) " << response.error.message <<" (response.text) " << response.text.c_str());
    //}
    return response;
}
void TDEngineBittrex::init()
{
    genUniqueKey();
    ITDEngine::init();
    JournalPair tdRawPair = getTdRawJournalPair(source_id);
    raw_writer = yijinjing::JournalSafeWriter::create(tdRawPair.first, tdRawPair.second, "RAW_" + name());
    KF_LOG_INFO(logger, "[init]");
}

void TDEngineBittrex::pre_load(const json& j_config)
{
    KF_LOG_INFO(logger, "[pre_load]");
}

void TDEngineBittrex::resize_accounts(int account_num)
{
    account_units.resize(account_num);
    KF_LOG_INFO(logger, "[resize_accounts]");
}

TradeAccount TDEngineBittrex::load_account(int idx, const json& j_config)
{
    KF_LOG_INFO(logger, "[load_account]");
    // internal load
    string api_key = j_config["APIKey"].get<string>();
    string secret_key = j_config["SecretKey"].get<string>();
    string passphrase = j_config["passphrase"].get<string>();
    //https://api.bittrex.pro
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

    AccountUnitBittrex& unit = account_units[idx];
    unit.api_key = api_key;
    unit.secret_key = secret_key;
    unit.passphrase = passphrase;
    unit.baseUrl = baseUrl;

    KF_LOG_INFO(logger, "[load_account] (api_key)" << api_key << " (baseUrl)" << unit.baseUrl 
                                                   << " (spotAccountId) "<<unit.spotAccountId
                                                   << " (marginAccountId) "<<unit.marginAccountId);


    unit.coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
    unit.coinPairWhiteList.Debug_print();

    unit.positionWhiteList.ReadWhiteLists(j_config, "positionWhiteLists");
    unit.positionWhiteList.Debug_print();

    //display usage:
    if(unit.coinPairWhiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "TDEngineBittrex::load_account: please add whiteLists in kungfu.json like this :");
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
    cancel_order(unit,"code","e1751360-a64c-458a-aaa8-ff9834ca6a28",json);
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

void TDEngineBittrex::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[connect]");
    for (size_t idx = 0; idx < account_units.size(); idx++)
    {
        AccountUnitBittrex& unit = account_units[idx];
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

void TDEngineBittrex::getPriceVolumePrecision(AccountUnitBittrex& unit){
    KF_LOG_INFO(logger,"[getPriceVolumePrecision]");
    Document json;
    const auto response = Get("/public/getmarkets","","",unit);
    json.Parse(response.text.c_str());
    if(json.HasMember("success") && json["success"].GetBool() && json.HasMember("result") && json["result"].IsArray()){
        int len = json["result"].Size(),i;
        auto result = json["result"].GetArray();
        for (i = 0; i < len; i++){
            rapidjson::Value account=result[i].GetObject();
            PriceVolumePrecision stPriceVolumePrecision;
            stPriceVolumePrecision.symbol=account["MarketName"].GetString();
            std::string ticker = unit.coinPairWhiteList.GetKeyByValue(stPriceVolumePrecision.symbol);
            if(ticker.length()==0){
                //KF_LOG_ERROR(logger,"[getPriceVolumePrecision] (No such symbol in whitelist) "<<stPriceVolumePrecision.symbol);
                continue;
            }
            stPriceVolumePrecision.baseCurrency=account["BaseCurrency"].GetString();
            stPriceVolumePrecision.quoteCurrency=account["MarketCurrency"].GetString();
            //stPriceVolumePrecision.pricePrecision=account["pair_decimals"].GetInt();
            //stPriceVolumePrecision.amountPrecision=account["lot_decimals"].GetInt();
            stPriceVolumePrecision.minTradeSize = account["MinTradeSize"].GetDouble();
            unit.mapPriceVolumePrecision.insert(std::make_pair(stPriceVolumePrecision.symbol,stPriceVolumePrecision));
            KF_LOG_INFO(logger,"[getPriceVolumePrecision] symbol "<<stPriceVolumePrecision.symbol);
        }
        KF_LOG_INFO(logger,"[getPriceVolumePrecision] (map size) "<<unit.mapPriceVolumePrecision.size());
    }
}
void TDEngineBittrex::lws_login(AccountUnitBittrex& unit, long timeout_nsec){
    KF_LOG_INFO(logger, "[TDEngineBittrex::lws_login]");
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
    KF_LOG_INFO(logger, "[TDEngineBittrex::lws_login] context created.");


    if (context == NULL) {
        KF_LOG_ERROR(logger, "[TDEngineBittrex::lws_login] context is NULL. return");
        return;
    }

    // Set up the client creation info
    static std::string host  = "api.bittrex.pro";
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

    KF_LOG_INFO(logger, "[TDEngineBittrex::login] address = " << clientConnectInfo.address << ",path = " << clientConnectInfo.path);
    //建立websocket连接
    unit.webSocketConn = lws_client_connect_via_info(&clientConnectInfo);
    if (unit.webSocketConn == NULL) {
        KF_LOG_ERROR(logger, "[TDEngineBittrex::lws_login] wsi create error.");
        return;
    }
    KF_LOG_INFO(logger, "[TDEngineBittrex::login] wsi create success.");
}
void TDEngineBittrex::login(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[TDEngineBittrex::login]");
    connect(timeout_nsec);
}

void TDEngineBittrex::logout()
{
    KF_LOG_INFO(logger, "[logout]");
}

void TDEngineBittrex::release_api()
{
    KF_LOG_INFO(logger, "[release_api]");
}

bool TDEngineBittrex::is_logged_in() const{
    KF_LOG_INFO(logger, "[is_logged_in]");
    for (auto& unit: account_units)
    {
        if (!unit.logged_in)
            return false;
    }
    return true;
}

bool TDEngineBittrex::is_connected() const{
    KF_LOG_INFO(logger, "[is_connected]");
    return is_logged_in();
}


std::string TDEngineBittrex::GetSide(const LfDirectionType& input) {
    if (LF_CHAR_Buy == input) {
        return "buy";
    } else if (LF_CHAR_Sell == input) {
        return "sell";
    } else {
        return "";
    }
}

LfDirectionType TDEngineBittrex::GetDirection(std::string input) {
    if ("buy" == input) {
        return LF_CHAR_Buy;
    } else if ("sell" == input) {
        return LF_CHAR_Sell;
    } else {
        return LF_CHAR_Buy;
    }
}

std::string TDEngineBittrex::GetType(const LfOrderPriceTypeType& input) {
    if (LF_CHAR_LimitPrice == input) {
        return "limit";
    } else if (LF_CHAR_AnyPrice == input) {
        return "market";
    } else {
        return "";
    }
}

LfOrderPriceTypeType TDEngineBittrex::GetPriceType(std::string input) {
    if ("limit" == input) {
        return LF_CHAR_LimitPrice;
    } else if ("market" == input) {
        return LF_CHAR_AnyPrice;
    } else {
        return '0';
    }
}
//订单状态，pending 提交, open 部分成交, closed , open 成交, canceled 已撤销,expired 失效
LfOrderStatusType TDEngineBittrex::GetOrderStatus(std::string state) {

    if(state == "canceled"){
        return LF_CHAR_Canceled;
    }else if(state == "pending"){
        return LF_CHAR_NotTouched;
    }else if(state == "closed"){
        return LF_CHAR_Error;
    }else if(state == "expired"){
        return LF_CHAR_Error;
    }else if(state == "open"){
        return LF_CHAR_NoTradeQueueing;
    }
    return LF_CHAR_Unknown;
}

/**
 * req functions
 * 查询账户持仓
 */
void TDEngineBittrex::req_investor_position(const LFQryPositionField* data, int account_index, int requestId){
    KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId);

    AccountUnitBittrex& unit = account_units[account_index];
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
    if(d.IsObject() && d.HasMember("success"))
    {
        bool isSuccess=d["success"].GetBool();
        errorId = 0;
        KF_LOG_INFO(logger, "[req_investor_position] (errorId) " << errorId);
        if(!isSuccess) {
            errorId=520;
            if (d.HasMember("message")) {
                errorMsg=d["message"].GetString();
            }
            KF_LOG_ERROR(logger, "[req_investor_position] failed!" << " (rid) " << requestId << " (errorId) " << errorId
                                                                   << " (errorMsg) " << errorMsg);
            raw_writer->write_error_frame(&pos, sizeof(LFRspPositionField), source_id, MSG_TYPE_LF_RSP_POS_BITTREX, 1, requestId, errorId, errorMsg.c_str());
        }
    }
    send_writer->write_frame(data, sizeof(LFQryPositionField), source_id, MSG_TYPE_LF_QRY_POS_BITTREX, 1, requestId);

    std::vector<LFRspPositionField> tmp_vector;
    KF_LOG_INFO(logger, "[req_investor_position] (result)");
    if(!d.HasParseError() && d.HasMember("result") && d["result"].IsArray())
    {
        auto accounts = d["result"].GetArray();
        int len = d["result"].Size(), i;
        for(i = 0; i < len; i++){
            rapidjson::Value account = accounts[i].GetObject();
            string symbol = account["Currency"].GetString();
            pos.Position = std::round(account["Balance"].GetDouble() * scale_offset);
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

void TDEngineBittrex::req_qry_account(const LFQryAccountField *data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_qry_account]");
}

void TDEngineBittrex::dealPriceVolume(AccountUnitBittrex& unit,const std::string& symbol,int64_t nPrice,int64_t nVolume,
            std::string& nDealPrice,std::string& nDealVolume){
    KF_LOG_DEBUG(logger, "[dealPriceVolume] (symbol) " << symbol);
    KF_LOG_DEBUG(logger, "[dealPriceVolume] (price) " << nPrice);
    KF_LOG_DEBUG(logger, "[dealPriceVolume] (volume) " << nVolume);
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
        KF_LOG_INFO(logger,"[dealPriceVolume] (minTradeSize) " << it->second.minTradeSize);
        if(tDealVolume < it->second.minTradeSize){
            KF_LOG_INFO(logger,"[dealPriceVolume] volume is too low to trade.");
            return;
        }
        char chP[16],chV[16];
        sprintf(chP,"%.8lf",nPrice*1.0/scale_offset);
        sprintf(chV,"%.8lf",nVolume*1.0/scale_offset);
        nDealPrice=chP;
        KF_LOG_INFO(logger,"[dealPriceVolume] (chP) "<<chP<<" (nDealPrice) "<<nDealPrice);
        //nDealPrice=nDealPrice.substr(0,nDealPrice.find(".")+(pPrecision==0?pPrecision:(pPrecision+1)));
        nDealVolume=chV;
         KF_LOG_INFO(logger,"[dealPriceVolume]  (chP) "<<chV<<" (nDealVolume) "<<nDealVolume);
        //nDealVolume=nDealVolume.substr(0,nDealVolume.find(".")+(vPrecision==0?vPrecision:(vPrecision+1)));
    }
    KF_LOG_INFO(logger, "[dealPriceVolume]  (symbol)" << ticker << " (Volume)" << nVolume << " (Price)" << nPrice
                                                      << " (FixedVolume)" << nDealVolume << " (FixedPrice)" << nDealPrice);
}
//发单
void TDEngineBittrex::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time){
    //on_rtn_order(NULL);
    AccountUnitBittrex& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_insert]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Tid)" << data->InstrumentID
                                              << " (Volume)" << data->Volume
                                              << " (LimitPrice)" << data->LimitPrice
                                              << " (OrderRef)" << data->OrderRef);
    send_writer->write_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BITTREX, 1/*ISLAST*/, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_insert]: not in WhiteList, ignore it  (rid)" << requestId <<
                                                                                      " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BITTREX, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_insert] (exchange_ticker)" << ticker);
    Document d;
    std::string fixedPrice="0";
    std::string fixedVolume="0";
    dealPriceVolume(unit,data->InstrumentID,data->LimitPrice,data->Volume,fixedPrice,fixedVolume);
    if(fixedVolume == "0" || fixedPrice == "0"){
        KF_LOG_DEBUG(logger, "[req_order_insert] fixed Volume error (no ticker)" << ticker);
        errorId = 200;
        errorMsg = data->InstrumentID;
        errorMsg += " : no such ticker";
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BITTREX, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_INFO(logger,"[req_order_insert] cys_ticker "<<ticker.c_str());
    //lock
    send_order(unit, ticker, GetSide(data->Direction),GetType(data->OrderPriceType), fixedVolume, fixedPrice, d);
    //not expected response
    if(!d.IsObject()){
        errorId = 100;
        errorMsg = "send_order http response has parse error or is not json. please check the log";
        KF_LOG_ERROR(logger, "[req_order_insert] send_order error!  (rid)" << requestId << " (errorId)" <<
                                                                           errorId << " (errorMsg) " << errorMsg);
    } else  if(d.HasMember("success")&&d.HasMember("result")&&d["success"].GetBool()){//发单成功
        
        rapidjson::Value result = d["result"].GetObject();
        bool isSuccess = d["success"].GetBool();
        KF_LOG_INFO(logger,"[req_order_insert] (isSuccess) "<< isSuccess);
        if(isSuccess) {
            //if send successful and the exchange has received ok, then add to  pending query order list
            std::string remoteOrderId = result["uuid"].GetString();
            //fix defect of use the old value
            localOrderRefRemoteOrderId[std::string(data->OrderRef)] = remoteOrderId;
            KF_LOG_INFO(logger, "[req_order_insert] after send  (rid)" << requestId << " (OrderRef) " <<
                                                                       data->OrderRef << " (remoteOrderId) "
                                                                       << remoteOrderId);
            PendingOrderStatus pOrderStatus;
            //初始化
            memset(&pOrderStatus, 0, sizeof(PendingOrderStatus));
            LFRtnOrderField *rtn_order = &pOrderStatus.rtn_order;
            strncpy(rtn_order->BusinessUnit,remoteOrderId.c_str(),64);
            rtn_order->OrderStatus = LF_CHAR_NotTouched;
            rtn_order->VolumeTraded = 0;
            rtn_order->VolumeTotalOriginal = data->Volume;
            rtn_order->LimitPrice = data->LimitPrice;
            
            strcpy(rtn_order->ExchangeID, "bittrex");
            strncpy(rtn_order->UserID, unit.api_key.c_str(), 16);
            strncpy(rtn_order->InstrumentID, data->InstrumentID, 31);
            rtn_order->Direction = data->Direction;
            //No this setting on Bittrex
            rtn_order->TimeCondition = LF_CHAR_GTC;
            rtn_order->OrderPriceType = data->OrderPriceType;
            strncpy(rtn_order->OrderRef, data->OrderRef, 13);
            rtn_order->VolumeTotalOriginal = data->Volume;
            rtn_order->LimitPrice = data->LimitPrice;
            rtn_order->VolumeTotal = data->Volume;

            on_rtn_order(rtn_order);
            raw_writer->write_frame(rtn_order, sizeof(LFRtnOrderField),
                                    source_id, MSG_TYPE_LF_RTN_TRADE_BITTREX,
                                    1, (rtn_order->RequestID > 0) ? rtn_order->RequestID : -1);

            KF_LOG_DEBUG(logger, "[req_order_insert] (addNewQueryOrdersAndTrades)" );
            pOrderStatus.averagePrice = 0;
            addNewQueryOrdersAndTrades(unit, pOrderStatus, remoteOrderId);

            raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BITTREX, 1,
                                          requestId, errorId, errorMsg.c_str());
            KF_LOG_DEBUG(logger, "[req_order_insert] success" );
            return;
        }else {
            //errorId = std::round(std::stod(d["id"].GetString()));
            errorId=520;
            if (d.HasMember("message") && d["message"].GetString()!="") {
                errorMsg = d["message"].GetString();
            }
            KF_LOG_ERROR(logger, "[req_order_insert] send_order error!  (rid)" << requestId << " (errorId)" <<
                errorId << " (errorMsg) " << errorMsg);
        }
    }else if(d.HasMember("success")&&!d["success"].GetBool()){
        //errorId = std::round(std::stod(d["id"].GetString()));
        errorId=520;
        if (d.HasMember("message") && d["message"].GetString()!="") {
            errorMsg = d["message"].GetString();
        }
        KF_LOG_ERROR(logger, "[req_order_insert] send_order error!  (rid)" << requestId << " (errorId)" <<
            errorId << " (errorMsg) " << errorMsg);
    }
    //unlock
    if(errorId != 0)
    {
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BITTREX, 1, requestId, errorId, errorMsg.c_str());
    }
}

void TDEngineBittrex::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time){
    AccountUnitBittrex& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_action]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Iid)" << data->InvestorID
                                              << " (OrderRef)" << data->OrderRef
                                              << " (KfOrderID)" << data->KfOrderID);

    send_writer->write_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BITTREX, 1, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_action]: not in WhiteList , ignore it: (rid)" << requestId << " (errorId)" <<
                                                                                       errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BITTREX, 1, requestId, errorId, errorMsg.c_str());
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
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BITTREX, 1, requestId, errorId, errorMsg.c_str());
        return;
    } else {
        remoteOrderId = itr->second;
        KF_LOG_DEBUG(logger, "[req_order_action] found in localOrderRefRemoteOrderId map (orderRef) "
                << data->OrderRef << " (remoteOrderId) " << remoteOrderId);
    }

    Document d;
    cancel_order(unit, ticker, remoteOrderId, d);

    if(!d.HasParseError() && d.HasMember("success")&&d["success"].GetBool()) {
        errorId = 0;
        /*std::vector<PendingOrderStatus>::iterator itr;
        for(itr = unit.pendingOrderStatus.begin(); itr != unit.pendingOrderStatus.end();){
            string oldRemoteOrderId=itr->rtn_order.BusinessUnit;
            KF_LOG_INFO(logger,"[req_order_action] (oldRemoteOrderId) " << oldRemoteOrderId <<" (remoteOrderId) "<<remoteOrderId);
            if(remoteOrderId == oldRemoteOrderId){
                orderIsCanceled(unit,&(itr->rtn_order));
                unit.pendingOrderStatus.erase(itr);
            }else{
                itr++;
            }
        }*/
        KF_LOG_INFO(logger,"[req_order_action] (canceled order id) " << remoteOrderId);
    }else{
        errorId = 520;
        if (d.HasMember("message") && d["message"].GetString()!="") {
            errorMsg = d["message"].GetString();
        }
        KF_LOG_ERROR(logger, "[req_order_action] cancel_order failed!" << " (rid)" << requestId
                                                                       << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }
    if(errorId != 0)
    {
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BITTREX, 1, requestId, errorId, errorMsg.c_str());

    } else {
        
        KF_LOG_INFO(logger,"[req_order_action] cancel order success");
    }
}
//对于每个撤单指令发出后30秒（可配置）内，如果没有收到回报，就给策略报错（撤单被拒绝，pls retry)
void TDEngineBittrex::addRemoteOrderIdOrderActionSentTime(const LFOrderActionField* data, int requestId, const std::string& remoteOrderId){
    std::lock_guard<std::mutex> guard_mutex_order_action(*mutex_orderaction_waiting_response);

    OrderActionSentTime newOrderActionSent;
    newOrderActionSent.requestId = requestId;
    newOrderActionSent.sentNameTime = getTimestamp();
    memcpy(&newOrderActionSent.data, data, sizeof(LFOrderActionField));
    remoteOrderIdOrderActionSentTime[remoteOrderId] = newOrderActionSent;
}
//cys no use
void TDEngineBittrex::GetAndHandleOrderTradeResponse(){
    // KF_LOG_INFO(logger, "[GetAndHandleOrderTradeResponse]" );
    //every account
    for (size_t idx = 0; idx < account_units.size(); idx++)
    {
        AccountUnitBittrex& unit = account_units[idx];
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
void TDEngineBittrex::retrieveOrderStatus(AccountUnitBittrex& unit){

    std::lock_guard<std::mutex> guard_mutex(*mutex_response_order_status);
    std::lock_guard<std::mutex> guard_mutex_order_action(*mutex_orderaction_waiting_response);

    std::vector<PendingOrderStatus>::iterator orderStatusIterator;
    for(orderStatusIterator = unit.pendingOrderStatus.begin(); orderStatusIterator != unit.pendingOrderStatus.end();)
    {

        std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(orderStatusIterator->rtn_order.InstrumentID));
        if(ticker.length() == 0) {
            KF_LOG_INFO(logger, "[retrieveOrderStatus]: not in WhiteList , ignore it:" << orderStatusIterator->rtn_order.InstrumentID);
            unit.pendingOrderStatus.erase(orderStatusIterator);
            continue;
        }
        KF_LOG_INFO(logger, "[retrieveOrderStatus] (get_order)" << "  (account.api_key) " << unit.api_key
            << "  (account.pendingOrderStatus.InstrumentID) " << orderStatusIterator->rtn_order.InstrumentID
            << "  (account.pendingOrderStatus.OrderRef) " << orderStatusIterator->rtn_order.OrderRef
            << "  (account.pendingOrderStatus.remoteOrderId) " << orderStatusIterator->rtn_order.BusinessUnit
            << "  (account.pendingOrderStatus.OrderStatus) " << orderStatusIterator->rtn_order.OrderStatus
            << "  (exchange_ticker)" << ticker
        );
        string remoteOrderId = orderStatusIterator->rtn_order.BusinessUnit;
        Document d;
        query_order(unit, ticker,remoteOrderId, d);
        //订单状态，pending 提交, open 成交, canceled 已撤销, expired已失效, closed 
        if(d.HasParseError()) {
            //HasParseError, skip
            KF_LOG_ERROR(logger, "[retrieveOrderStatus] get_order response HasParseError " << " (symbol)" << orderStatusIterator->rtn_order.InstrumentID
                                                                                           << " (orderRef)" << orderStatusIterator->rtn_order.OrderRef
                                                                                           << " (remoteOrderId) " << remoteOrderId);
            continue;
        }
        KF_LOG_INFO(logger, "[retrieveOrderStatus] query_order:");
        if(d.HasMember("success") && d["success"].GetBool()){
            rapidjson::Value data = d["result"].GetObject();
            if(!data.HasMember("OrderUuid")||remoteOrderId!=data["OrderUuid"].GetString()){
                KF_LOG_INFO(logger,"[retrieveOrderStatus] no OrderUuid");
                return;
            }
            KF_LOG_INFO(logger, "[retrieveOrderStatus] (query success)");
            ResponsedOrderStatus responsedOrderStatus;
            responsedOrderStatus.ticker = ticker;
            //平均价格
            responsedOrderStatus.averagePrice = std::round((data["PricePerUnit"].IsNumber()?data["PricePerUnit"].GetDouble():0) * scale_offset);
            //累计成交价格
            responsedOrderStatus.PriceTraded = std::round(data["Price"].GetDouble() * scale_offset);
            //总量
            responsedOrderStatus.volume = std::round(data["Quantity"].GetDouble() * scale_offset);
            //未成交数量
            responsedOrderStatus.openVolume =  std::round(data["QuantityRemaining"].GetDouble() * scale_offset);
            //累计成交数量
            responsedOrderStatus.VolumeTraded = responsedOrderStatus.volume - responsedOrderStatus.openVolume;
            //订单状态
            bool Closed = false,CancelInitiated = false;
            CancelInitiated = data["CancelInitiated"].GetBool();
            if(CancelInitiated){
                responsedOrderStatus.OrderStatus = LF_CHAR_Canceled;
            }else{
                responsedOrderStatus.OrderStatus = LF_CHAR_NoTradeQueueing;
            }
            //订单信息处理
            handlerResponseOrderStatus(unit, orderStatusIterator, responsedOrderStatus);

            //OrderAction发出以后，有状态回来，就清空这次OrderAction的发送状态，不必制造超时提醒信息
            remoteOrderIdOrderActionSentTime.erase(orderStatusIterator->rtn_order.BusinessUnit);
        }else{
            KF_LOG_INFO(logger, "[retrieveOrderStatus] (query failed)");
            std::string errorMsg = "";
            std::string errorId = "520";
            if (d.HasMember("message") && d["message"].GetString() != ""){
                errorMsg = d["message"].GetString();
            }
            KF_LOG_ERROR(logger, "[retrieveOrderStatus] get_order fail." << " (symbol)" << orderStatusIterator->rtn_order.InstrumentID
                                                                         << " (orderRef)" << orderStatusIterator->rtn_order.OrderRef
                                                                         << " (errorId)" << errorId
                                                                         << " (errorMsg)" << errorMsg);
        }

        //remove order when finish
        if(orderStatusIterator->rtn_order.OrderStatus == LF_CHAR_AllTraded  
           || orderStatusIterator->rtn_order.OrderStatus == LF_CHAR_Canceled
           || orderStatusIterator->rtn_order.OrderStatus == LF_CHAR_Error)
        {
            KF_LOG_INFO(logger, "[retrieveOrderStatus] remove a pendingOrderStatus.");
            orderStatusIterator = unit.pendingOrderStatus.erase(orderStatusIterator);
        } else {
            ++orderStatusIterator;
        }
    }
}
void TDEngineBittrex::addNewQueryOrdersAndTrades(AccountUnitBittrex& unit, PendingOrderStatus pOrderStatus, std::string& remoteOrderId){
    KF_LOG_DEBUG(logger, "[addNewQueryOrdersAndTrades]" );
    //add new orderId for GetAndHandleOrderTradeResponse
    std::lock_guard<std::mutex> guard_mutex(*mutex_order_and_trade);

    unit.newOrderStatus.push_back(pOrderStatus);

    KF_LOG_INFO(logger, "[addNewQueryOrdersAndTrades] (InstrumentID) " << pOrderStatus.rtn_order.InstrumentID
                                                                       << " (OrderRef) " << pOrderStatus.rtn_order.OrderRef
                                                                       << " (remoteOrderId) " << pOrderStatus.rtn_order.BusinessUnit
                                                                       << "(VolumeTraded)" << pOrderStatus.rtn_order.VolumeTraded);
}


void TDEngineBittrex::moveNewOrderStatusToPending(AccountUnitBittrex& unit)
{
    //KF_LOG_DEBUG(logger, "[moveNewOrderStatusToPending]" );
    std::lock_guard<std::mutex> pending_guard_mutex(*mutex_order_and_trade);
    std::lock_guard<std::mutex> response_guard_mutex(*mutex_response_order_status);


    std::vector<PendingOrderStatus>::iterator newOrderStatusIterator;
    for(newOrderStatusIterator = unit.newOrderStatus.begin(); newOrderStatusIterator != unit.newOrderStatus.end();)
    {
        unit.pendingOrderStatus.push_back(*newOrderStatusIterator);
        newOrderStatusIterator = unit.newOrderStatus.erase(newOrderStatusIterator);
    }
    //KF_LOG_DEBUG(logger, "[moveNewOrderStatusToPending] (pendingOrderStatus size) " << unit.pendingOrderStatus.size());
}
//cys no use
void TDEngineBittrex::set_reader_thread()
{
    ITDEngine::set_reader_thread();

    KF_LOG_INFO(logger, "[set_reader_thread] rest_thread start on TDEngineBittrex::loop");
    rest_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineBittrex::loop, this)));

    KF_LOG_INFO(logger, "[set_reader_thread] orderaction_timeout_thread start on TDEngineBittrex::loopOrderActionNoResponseTimeOut");
    orderaction_timeout_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineBittrex::loopOrderActionNoResponseTimeOut, this)));
}
//cys no use
void TDEngineBittrex::loop()
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


void TDEngineBittrex::loopOrderActionNoResponseTimeOut()
{
    KF_LOG_INFO(logger, "[loopOrderActionNoResponseTimeOut] (isRunning) " << isRunning);
    while(isRunning)
    {
        orderActionNoResponseTimeOut();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void TDEngineBittrex::orderActionNoResponseTimeOut(){
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

void TDEngineBittrex::printResponse(const Document& d){
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);
    KF_LOG_INFO(logger, "[printResponse] ok (text) " << buffer.GetString());
}

void TDEngineBittrex::getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& json)
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

void TDEngineBittrex::get_account(AccountUnitBittrex& unit, Document& json)
{
    KF_LOG_INFO(logger, "[get_account]");
    string path="/account/getbalances";

    const auto response = Get(path,"","",unit);
    json.Parse(response.text.c_str());
    //KF_LOG_INFO(logger, "[get_account] (account info) "<<response.text.c_str());
    return ;
}
std::string TDEngineBittrex::createInsertOrdertring(string pair,string price,string volume){
    string s="";
    s=s+"market="+pair+"&"+
        "quantity="+volume+"&"+
        "rate="+price;

    return s;
}
void TDEngineBittrex::send_order(AccountUnitBittrex& unit, string code,
                        string side, string type, string volume, string price, Document& json){
    KF_LOG_INFO(logger, "[send_order]");
    KF_LOG_INFO(logger, "[send_order] (code) "<<code);
    if(type != "limit"){
        KF_LOG_INFO(logger,"[send_order] bittrex doesn't support other type orders except limit.");
        return;
    }
    int retry_times = 0;
    cpr::Response response;
    bool should_retry = false;
    do {
        should_retry = false;
        string path = "/market/";
        path = path + side+type;
        string postData=createInsertOrdertring(code, price,volume);

        response = Get(path,"",postData,unit);

        KF_LOG_INFO(logger, "[send_order] (url) " << path << " (response.status_code) " << response.status_code 
                                                  << " (response.error.message) " << response.error.message 
                                                  << " (retry_times)" << retry_times);

        //json.Clear();
        getResponse(response.status_code, response.text, response.error.message, json);
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

bool TDEngineBittrex::shouldRetry(Document& doc)
{
    bool ret = false;
    bool isSuccess = true;
    if(doc.HasMember("success"))
    {
        isSuccess = doc["success"].GetBool();
    }
    if(!doc.IsObject()||(!isSuccess)){
        ret = true;
    }
    KF_LOG_INFO(logger, "[shouldRetry] ret = " << ret << ", isSuccess = " << isSuccess);
    return ret;
}

void TDEngineBittrex::cancel_order(AccountUnitBittrex& unit, std::string code, std::string orderId, Document& json)
{
    KF_LOG_INFO(logger, "[cancel_order]");

    int retry_times = 0;
    cpr::Response response;
    bool should_retry = false;
    do {
        should_retry = false;
        std::string path="/market/cancel";
        std::string postData="uuid=";
        postData=postData+orderId;

        response = Get(path,"",postData,unit);

        getResponse(response.status_code, response.text, response.error.message, json);

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
void TDEngineBittrex::query_order(AccountUnitBittrex& unit, std::string code, std::string orderId, Document& json)
{
    KF_LOG_INFO(logger, "[query_order] start");
    //bittrex查询订单详情
    string getPath = "/account/getorder";
    string s1="uuid=";
    string postData=s1+orderId;

    auto response = Get(getPath,"",postData,unit);
    json.Parse(response.text.c_str());
    KF_LOG_INFO(logger, "[query_order] end");
}
void TDEngineBittrex::orderIsCanceled(AccountUnitBittrex& unit, LFRtnOrderField* rtn_order){
    rtn_order->OrderStatus = LF_CHAR_Canceled;
    //累计成交数量
    //rtn_order.VolumeTraded;
    //剩余未成交数量
    //rtn_order->VolumeTotal = rtn_order.VolumeTotalOriginal-rtn_order->VolumeTraded;
    on_rtn_order(rtn_order);
    raw_writer->write_frame(&(*rtn_order), sizeof(LFRtnOrderField),source_id, MSG_TYPE_LF_RTN_TRADE_BITTREX,1, 
            (rtn_order->RequestID > 0) ? rtn_order->RequestID: -1);
}
void TDEngineBittrex::handlerResponseOrderStatus(AccountUnitBittrex& unit, std::vector<PendingOrderStatus>::iterator itr,
         ResponsedOrderStatus& responsedOrderStatus)
{
    KF_LOG_INFO(logger, "[handlerResponseOrderStatus]");
    LfOrderStatusType orderStatus = responsedOrderStatus.OrderStatus;
    if(orderStatus == LF_CHAR_NoTradeQueueing){
        if(responsedOrderStatus.VolumeTraded == responsedOrderStatus.volume){
            orderStatus = LF_CHAR_AllTraded;
        }else if(responsedOrderStatus.VolumeTraded == 0){
            orderStatus = LF_CHAR_NotTouched;
        }else if(responsedOrderStatus.VolumeTraded < responsedOrderStatus.volume){
            orderStatus = LF_CHAR_PartTradedQueueing;
        }
                    
    }
    if(orderStatus == itr->rtn_order.OrderStatus&&responsedOrderStatus.VolumeTraded == itr->rtn_order.VolumeTraded){//no change
        KF_LOG_INFO(logger,"[handlerResponseOrderStatus] order status not change, return nothing.");
        return;
    }
    itr->rtn_order.OrderStatus = orderStatus;
    //单次成交量
    uint64_t singleVolume = responsedOrderStatus.VolumeTraded - itr->rtn_order.VolumeTraded;
    //单次成交价
    double oldAmount = itr->rtn_order.VolumeTraded/(scale_offset*1.0) * itr->averagePrice/(scale_offset*1.0);
    double newAmount = responsedOrderStatus.VolumeTraded/(scale_offset*1.0) * responsedOrderStatus.averagePrice/(scale_offset*1.0);
    double singlePrice = newAmount - oldAmount;
    uint64_t oldVolumeTraded = itr->rtn_order.VolumeTraded;
    //累计成交数量
    itr->rtn_order.VolumeTraded=responsedOrderStatus.VolumeTraded;
    //剩余未成交数量
    itr->rtn_order.VolumeTotal = itr->rtn_order.VolumeTotalOriginal-itr->rtn_order.VolumeTraded;
    itr->averagePrice = responsedOrderStatus.averagePrice;
    on_rtn_order(&(itr->rtn_order));
    raw_writer->write_frame(&(itr->rtn_order), sizeof(LFRtnOrderField),source_id, MSG_TYPE_LF_RTN_TRADE_BITTREX,1, (itr->rtn_order.RequestID > 0) ? itr->rtn_order.RequestID: -1);

    if(oldVolumeTraded!=itr->rtn_order.VolumeTraded){
        //send OnRtnTrade
        LFRtnTradeField rtn_trade;
        memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
        strcpy(rtn_trade.ExchangeID, "bittrex");
        strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
        strncpy(rtn_trade.InstrumentID, itr->rtn_order.InstrumentID, 31);
        strncpy(rtn_trade.OrderRef, itr->rtn_order.OrderRef, 13);
        rtn_trade.Direction = itr->rtn_order.Direction;
        //单次成交数量
        rtn_trade.Volume = singleVolume;
        //单次成交价格
        rtn_trade.Price = std::round(singlePrice*scale_offset);
        strncpy(rtn_trade.OrderSysID,itr->rtn_order.BusinessUnit,64);
        on_rtn_trade(&rtn_trade);

        raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
            source_id, MSG_TYPE_LF_RTN_TRADE_BITTREX, 1, -1);

        KF_LOG_INFO(logger, "[on_rtn_trade 1] (InstrumentID)" << rtn_trade.InstrumentID << "(Direction)" << rtn_trade.Direction
                << "(Volume)" << rtn_trade.Volume << "(Price)" <<  rtn_trade.Price);
    }

}

std::string TDEngineBittrex::parseJsonToString(Document &d){
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);

    return buffer.GetString();
}


inline int64_t TDEngineBittrex::getTimestamp(){
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp;
}

void TDEngineBittrex::genUniqueKey(){
    struct tm cur_time = getCurLocalTime();
    //SSMMHHDDN
    char key[11]{0};
    snprintf((char*)key, 11, "%02d%02d%02d%02d%1s", cur_time.tm_sec, cur_time.tm_min, cur_time.tm_hour, cur_time.tm_mday, m_engineIndex.c_str());
    m_uniqueKey = key;
}
//clientid =  m_uniqueKey+orderRef
std::string TDEngineBittrex::genClinetid(const std::string &orderRef){
    static int nIndex = 0;
    return m_uniqueKey + orderRef + std::to_string(nIndex++);
}

#define GBK2UTF8(msg) kungfu::yijinjing::gbk2utf8(string(msg))
BOOST_PYTHON_MODULE(libbittrextd){
    using namespace boost::python;
    class_<TDEngineBittrex, boost::shared_ptr<TDEngineBittrex> >("Engine")
     .def(init<>())
        .def("init", &TDEngineBittrex::initialize)
        .def("start", &TDEngineBittrex::start)
        .def("stop", &TDEngineBittrex::stop)
        .def("logout", &TDEngineBittrex::logout)
        .def("wait_for_stop", &TDEngineBittrex::wait_for_stop);
}
