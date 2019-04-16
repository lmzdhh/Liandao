#include "TDEngineHuobi.h"
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

TDEngineHuobi::TDEngineHuobi(): ITDEngine(SOURCE_HUOBI)
{
    logger = yijinjing::KfLog::getLogger("TradeEngine.Huobi");
    KF_LOG_INFO(logger, "[TDEngineHuobi]");

    mutex_order_and_trade = new std::mutex();
    mutex_response_order_status = new std::mutex();
    mutex_orderaction_waiting_response = new std::mutex();
}

TDEngineHuobi::~TDEngineHuobi()
{
    if(mutex_order_and_trade != nullptr) delete mutex_order_and_trade;
    if(mutex_response_order_status != nullptr) delete mutex_response_order_status;
    if(mutex_orderaction_waiting_response != nullptr) delete mutex_orderaction_waiting_response;
}

static TDEngineHuobi* global_md = nullptr;

static int ws_service_cb( struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len )
{
    std::stringstream ss;
    ss << "lws_callback,reason=" << reason << ",";
    switch( reason )
    {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
        {//lws callback client established
            ss << "LWS_CALLBACK_CLIENT_ESTABLISHED.";
            global_md->writeErrorLog(ss.str());
            //lws_callback_on_writable( wsi );
            break;
        }
        case LWS_CALLBACK_PROTOCOL_INIT:
        {//lws callback protocol init
            ss << "LWS_CALLBACK_PROTOCOL_INIT.";
            global_md->writeErrorLog(ss.str());
            break;
        }
        case LWS_CALLBACK_CLIENT_RECEIVE:
        {//
            ss << "LWS_CALLBACK_CLIENT_RECEIVE.";
            global_md->writeErrorLog(ss.str());
            if(global_md)
            {
                global_md->on_lws_data(wsi, (const char*)in, len);
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_WRITEABLE:
        {
            ss << "LWS_CALLBACK_CLIENT_WRITEABLE.";
            global_md->writeErrorLog(ss.str());
            int ret = 0;
            if(global_md)
            {
                ret = global_md->lws_write_subscribe(wsi);
            }
            break;
        }
        case LWS_CALLBACK_CLOSED:
        {
            // ss << "LWS_CALLBACK_CLOSED.";
            // global_md->writeErrorLog(ss.str());
            // break;
        }
        case LWS_CALLBACK_WSI_DESTROY:
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        {
            // ss << "LWS_CALLBACK_CLIENT_CONNECTION_ERROR.";
            global_md->writeErrorLog(ss.str());
            if(global_md)
            {
                global_md->on_lws_connection_error(wsi);
            }
            break;
        }
        default:
            global_md->writeErrorLog(ss.str());
            break;
    }

    return 0;
}

std::string TDEngineHuobi::getId()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return  std::to_string(timestamp);
}

void TDEngineHuobi::onPong(struct lws* conn)
{
    Ping(conn);
}

void TDEngineHuobi::Ping(struct lws* conn)
{
    m_shouldPing = false;
    StringBuffer sbPing;
    Writer<StringBuffer> writer(sbPing);
    writer.StartObject();
    writer.Key("id");
    writer.String(getId().c_str());
    writer.Key("type");
    writer.String("ping");
    writer.EndObject();
    std::string strPing = sbPing.GetString();
    unsigned char msg[512];
    memset(&msg[LWS_PRE], 0, 512-LWS_PRE);
    int length = strPing.length();
    KF_LOG_INFO(logger, "TDEngineHuobi::lws_write_ping: " << strPing.c_str() << " ,len = " << length);
    strncpy((char *)msg+LWS_PRE, strPing.c_str(), length);
    int ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);
}

void TDEngineHuobi::on_lws_data(struct lws* conn, const char* data, size_t len)
{
    //std::string strData = dealDataSprit(data);
    KF_LOG_INFO(logger, "TDEngineHuobi::on_lws_data: " << data);
    Document json;
    json.Parse(data);

    if(!json.HasParseError() && json.IsObject() && json.HasMember("type") && json["type"].IsString())
    {
        if(strcmp(json["type"].GetString(), "welcome") == 0)
        {
            KF_LOG_INFO(logger, "MDEngineHuobi::on_lws_data: welcome");
            lws_callback_on_writable(conn);
        }
        if(strcmp(json["type"].GetString(), "pong") == 0)
        {
            KF_LOG_INFO(logger, "MDEngineHuobi::on_lws_data: pong");
            m_isPong = true;
            m_conn = conn;
        }
        if(strcmp(json["type"].GetString(), "message") == 0)
        {
            if(strcmp(json["subject"].GetString(), "trade.l2update") == 0)
            {
            }
        }
    } else
    {
        KF_LOG_ERROR(logger, "MDEngineHuobi::on_lws_data . parse json error: " << data);
    }

}

std::string TDEngineHuobi::makeSubscribeL3Update(const std::map<std::string,int>& mapAllSymbols)
{
    StringBuffer sbUpdate;
    Writer<StringBuffer> writer(sbUpdate);
    writer.StartObject();
    writer.Key("id");
    writer.String(getId().c_str());
    writer.Key("type");
    writer.String("subscribe");
    writer.Key("topic");
    std::string strTopic = "/market/level2:";
    for(const auto&  pair : mapAllSymbols)
    {
        strTopic += pair.first + ",";
    }
    strTopic.pop_back();
    writer.String(strTopic.c_str());
    writer.Key("privateChannel");
    writer.String("false");
    writer.Key("response");
    writer.String("true");
    writer.EndObject();
    std::string strUpdate = sbUpdate.GetString();

    return strUpdate;
}

int TDEngineHuobi::lws_write_subscribe(struct lws* conn)
{
    KF_LOG_INFO(logger, "TDEngineHuobi::lws_write_subscribe:" );

    int ret = 0;

    if(!m_isSubL3)
    {
        m_isSubL3 = true;

        std::map<std::string,int> mapAllSymbols;
        for(auto& unit : account_units)
        {
            for(auto& pair :  unit.coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList())
            {
                mapAllSymbols[pair.second] = 0;
            }
        }

        std::string strSubscribe = makeSubscribeL3Update(mapAllSymbols);
        unsigned char msg[1024];
        memset(&msg[LWS_PRE], 0, 1024-LWS_PRE);
        int length = strSubscribe.length();
        KF_LOG_INFO(logger, "TDEngineHuobi::lws_write_subscribe: " << strSubscribe.c_str() << " ,len = " << length);
        strncpy((char *)msg+LWS_PRE, strSubscribe.c_str(), length);
        ret = lws_write(conn, &msg[LWS_PRE], length,LWS_WRITE_TEXT);
        lws_callback_on_writable(conn);
    }
    else
    {
        if(m_shouldPing)
        {
            m_isPong = false;
            Ping(conn);
        }
    }

    return ret;
}

void TDEngineHuobi::on_lws_connection_error(struct lws* conn)
{
    KF_LOG_ERROR(logger, "TDEngineHuobi::on_lws_connection_error. login again.");
    //clear the price book, the new websocket will give 200 depth on the first connect, it will make a new price book
    m_isPong = false;
    m_shouldPing = true;
    //no use it
    long timeout_nsec = 0;
    //reset sub
    m_isSubL3 = false;

    login(timeout_nsec);
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
int lws_write_subscribe(struct lws* conn);
void on_lws_connection_error(struct lws* conn);

enum protocolList {
    PROTOCOL_TEST,

    PROTOCOL_LIST_COUNT
};

struct session_data {
    int fd;
};

void TDEngineHuobi::writeErrorLog(std::string strError)
{
    KF_LOG_ERROR(logger, strError);
}

bool TDEngineHuobi::getToken(Document& d)
{
    int nTryCount = 0;
    cpr::Response response;
    do{
        std::string url = "https://api.kucoin.com/api/v1/bullet-public";
        response = cpr::Post(Url{url.c_str()}, Parameters{});

    }while(++nTryCount < max_rest_retry_times && response.status_code != 200);

    if(response.status_code != 200)
    {
        KF_LOG_ERROR(logger, "TDEngineHuobi::login::getToken Error");
        return false;
    }

    KF_LOG_INFO(logger, "TDEngineHuobi::getToken: " << response.text.c_str());

    d.Parse(response.text.c_str());
    return true;
}


bool TDEngineHuobi::getServers(Document& d)
{
    m_vstServerInfos.clear();
    m_strToken = "";
    if(d.HasMember("data"))
    {
        auto& data = d["data"];
        if(data.HasMember("token"))
        {
            m_strToken = data["token"].GetString();
            if(data.HasMember("instanceServers"))
            {
                int nSize = data["instanceServers"].Size();
                for(int nPos = 0;nPos<nSize;++nPos)
                {
                    ServerInfo stServerInfo;
                    auto& server = data["instanceServers"].GetArray()[nPos];
                    if(server.HasMember("pingInterval"))
                    {
                        stServerInfo.nPingInterval = server["pingInterval"].GetInt();
                    }
                    if(server.HasMember("pingTimeOut"))
                    {
                        stServerInfo.nPingTimeOut = server["pingTimeOut"].GetInt();
                    }
                    if(server.HasMember("endpoint"))
                    {
                        stServerInfo.strEndpoint = server["endpoint"].GetString();
                    }
                    if(server.HasMember("protocol"))
                    {
                        stServerInfo.strProtocol = server["protocol"].GetString();
                    }
                    if(server.HasMember("encrypt"))
                    {
                        stServerInfo.bEncrypt = server["encrypt"].GetBool();
                    }
                    m_vstServerInfos.push_back(stServerInfo);
                }
            }
        }
    }
    if(m_strToken == "" || m_vstServerInfos.empty())
    {
        KF_LOG_ERROR(logger, "TDEngineHuobi::login::getServers Error");
        return false;
    }
    return true;
}

int64_t TDEngineHuobi::getMSTime()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return  timestamp;
}
std::string TDEngineHuobi::getHuobiSignatrue(std::string parameters[],int psize,std::string timestamp,std::string method_url,
                                                std::string reqType,AccountUnitHuobi& unit){
    std::string strAccessKeyId=unit.api_key;
    std::string strSignatureMethod="HmacSHA256";
    std::string strSignatureVersion="2";
    std::string strSign = reqType+"api.huobi.pro\n" + method_url+"\n"+
                            "AccessKeyId="+strAccessKeyId+"&"+
                            "SignatureMethod="+strSignatureMethod+"&"+
                            "SignatureVersion="+strSignatureVersion+"&"+
                            "Timestamp="+timestamp;
    KF_LOG_INFO(logger, "[getHuobiSignatrue] strSign = " << strSign );
    unsigned char* strHmac = hmac_sha256_byte(unit.secret_key.c_str(),strSign.c_str());
    KF_LOG_INFO(logger, "[getHuobiSignatrue] strHmac = " << strHmac );
    std::string strSignatrue = base64_url_encode(strHmac,32);
    KF_LOG_INFO(logger, "[getHuobiSignatrue] Signatrue = " << strSignatrue );
    return strSignatrue;
}
char TDEngineHuobi::dec2hexChar(short int n) {
	if (0 <= n && n <= 9) {
		return char(short('0') + n);
	}
	else if (10 <= n && n <= 15) {
		return char(short('A') + n - 10);
	}
	else {
		return char(0);
	}
}
std::string TDEngineHuobi::escapeURL(const string &URL)
{
	string result = "";
	for (unsigned int i = 0; i < URL.size(); i++) {
		char c = URL[i];
		if (
			('0' <= c && c <= '9') ||
			('a' <= c && c <= 'z') ||
			('A' <= c && c <= 'Z') ||
			c == '/' || c == '.'
			) {
			result += c;
		}
		else {
			int j = (short int)c;
			if (j < 0) {
				j += 256;
			}
			int i1, i0;
			i1 = j / 16;
			i0 = j - i1 * 16;
			result += '%';
			result += dec2hexChar(i1);
			result += dec2hexChar(i0);
		}
	}
	return result;
}
//cys edit from huobi api
std::mutex g_httpMutex;
cpr::Response TDEngineHuobi::Get(const std::string& method_url,const std::string& body, AccountUnitHuobi& unit)
{
    std::string strTimestamp = getHuobiTime();
    string strSignatrue=getHuobiSignatrue(NULL,0,strTimestamp,method_url,"GET\n",unit);
    string url = unit.baseUrl + method_url+"?"+"AccessKeyId="+unit.api_key+"&"+
                    "SignatureMethod=HmacSHA256&"+
                    "SignatureVersion=2&"+
                    "Timestamp="+strTimestamp+"&"+
                    "Signature="+strSignatrue;
    std::unique_lock<std::mutex> lock(g_httpMutex);
    const auto response = cpr::Get(Url{url},
                                   Header{{"Content-Type", "application/json"}}, Timeout{10000} );
    lock.unlock();
    KF_LOG_INFO(logger, "[Get] (url) " << url << " (response.status_code) " << response.status_code <<
                                       " (response.error.message) " << response.error.message <<
                                       " (response.text) " << response.text.c_str());
    return response;
}
//cys edit
cpr::Response TDEngineHuobi::Post(const std::string& method_url,const std::string& body, AccountUnitHuobi& unit)
{
    std::string strTimestamp = getHuobiTime();
    string strSignatrue=getHuobiSignatrue(NULL,0,strTimestamp,method_url,"POST\n",unit);
    string url = unit.baseUrl + method_url+"?"+"AccessKeyId="+unit.api_key+"&"+
                    "SignatureMethod=HmacSHA256&"+
                    "SignatureVersion=2&"+
                    "Timestamp="+strTimestamp+"&"+
                    "Signature="+strSignatrue;
    std::unique_lock<std::mutex> lock(g_httpMutex);
    auto response = cpr::Post(Url{url}, Header{{"Content-Type", "application/json"}},
                              Body{body},Timeout{30000});
    lock.unlock();
    KF_LOG_INFO(logger, "[POST] (url) " << url <<"(body) "<< body<< " (response.status_code) " << response.status_code <<
                                        " (response.error.message) " << response.error.message <<
                                        " (response.text) " << response.text.c_str());
    return response;
}
void TDEngineHuobi::init()
{
    genUniqueKey();
    ITDEngine::init();
    JournalPair tdRawPair = getTdRawJournalPair(source_id);
    raw_writer = yijinjing::JournalSafeWriter::create(tdRawPair.first, tdRawPair.second, "RAW_" + name());
    KF_LOG_INFO(logger, "[init]");
}

void TDEngineHuobi::pre_load(const json& j_config)
{
    KF_LOG_INFO(logger, "[pre_load]");
}

void TDEngineHuobi::resize_accounts(int account_num)
{
    account_units.resize(account_num);
    KF_LOG_INFO(logger, "[resize_accounts]");
}

TradeAccount TDEngineHuobi::load_account(int idx, const json& j_config)
{
    KF_LOG_INFO(logger, "[load_account]");
    // internal load
    string api_key = j_config["APIKey"].get<string>();
    string secret_key = j_config["SecretKey"].get<string>();
    string passphrase = j_config["passphrase"].get<string>();
    //https://api.huobi.pro
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

    AccountUnitHuobi& unit = account_units[idx];
    unit.api_key = api_key;
    unit.secret_key = secret_key;
    unit.passphrase = passphrase;
    unit.baseUrl = baseUrl;

    KF_LOG_INFO(logger, "[load_account] (api_key)" << api_key << " (baseUrl)" << unit.baseUrl << " (accountId)"<<unit.accountId);

//test rs256
    //  std::string data ="{}";
    //  std::string signature =utils::crypto::rsa256_private_sign(data, g_private_key);
    // std::string sign = base64_encode((unsigned char*)signature.c_str(), signature.size());
    //std::cout  << "[TDEngineHuobi] (test rs256-base64-sign)" << sign << std::endl;

    //std::string decodeStr = utils::crypto::rsa256_pub_verify(data,signature, g_public_key);
    //std::cout  << "[TDEngineHuobi] (test rs256-verify)" << (decodeStr.empty()?"yes":"no") << std::endl;

    unit.coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
    unit.coinPairWhiteList.Debug_print();

    unit.positionWhiteList.ReadWhiteLists(j_config, "positionWhiteLists");
    unit.positionWhiteList.Debug_print();

    //display usage:
    if(unit.coinPairWhiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "TDEngineHuobi::load_account: please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"btcusdt\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"etceth\"");
        KF_LOG_ERROR(logger, "},");
    }
    unit.accountId=getAccountId(unit);
    //test
    Document json;
    get_account(unit, json);
    printResponse(json);
    cancel_order(unit,"code","1",json);
    //cancel_all_orders(unit, "btc_usd", json);
    printResponse(json);
    //getPriceIncrement(unit);
    // set up
    TradeAccount account = {};
    //partly copy this fields
    strncpy(account.UserID, api_key.c_str(), 16);
    strncpy(account.Password, secret_key.c_str(), 21);
    return account;
}

void TDEngineHuobi::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[connect]");
    for (size_t idx = 0; idx < account_units.size(); idx++)
    {
        AccountUnitHuobi& unit = account_units[idx];
        unit.logged_in = true;
        KF_LOG_INFO(logger, "[connect] (api_key)" << unit.api_key);
        // Document doc;
        //std::string requestPath = "/key";
        // const auto response = Get(requestPath,"{}",unit);
        //  getResponse(response.status_code, response.text, response.error.message, doc);
        // if ( !unit.logged_in && doc.HasMember("code"))
        //{
        //   int code = doc["code"].GetInt();
        //   unit.logged_in = (code == 0);
        //}
    }
}
//火币暂时未用到
void TDEngineHuobi::getPriceIncrement(AccountUnitHuobi& unit)
{
    auto& coinPairWhiteList = unit.coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList();
    for(auto& pair : coinPairWhiteList)
    {
        Document json;
        const auto response = Get("/api/v1/symbols/" + pair.second,"",unit);
        json.Parse(response.text.c_str());
        const static std::string strSuccesse = "200000";
        if(json.HasMember("code") && json["code"].GetString() == strSuccesse)
        {
            auto& data = json["data"];
            PriceIncrement stPriceIncrement;
            stPriceIncrement.nBaseMinSize = std::round(std::stod(data["baseMinSize"].GetString())* scale_offset);
            stPriceIncrement.nPriceIncrement = std::round(std::stod(data["priceIncrement"].GetString()) * scale_offset);
            stPriceIncrement.nQuoteIncrement = std::round(std::stod(data["quoteIncrement"].GetString()) * scale_offset);
            unit.mapPriceIncrement.insert(std::make_pair(pair.first,stPriceIncrement));
            KF_LOG_INFO(logger, "[getPriceIncrement] (BaseMinSize )" << stPriceIncrement.nBaseMinSize << "(PriceIncrement)" << stPriceIncrement.nPriceIncrement
                                                                     << "(QuoteIncrement)" << stPriceIncrement.nQuoteIncrement);
        }
    }
}

void TDEngineHuobi::login(long timeout_nsec)
{
    KF_LOG_INFO(logger, "TDEngineHuobi::login:");

    global_md = this;

    Document d;
    if(!getToken(d))
    {
        return;
    }
    if(!getServers(d))
    {
        return;
    }
    m_isSubL3 = false;
    global_md = this;
    int inputPort = 8443;
    int logs = LLL_ERR | LLL_DEBUG | LLL_WARN;

    struct lws_context_creation_info ctxCreationInfo;
    struct lws_client_connect_info clientConnectInfo;
    struct lws *wsi = NULL;
    struct lws_protocols protocol;

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

    protocol.name  = protocols[PROTOCOL_TEST].name;
    protocol.callback = &ws_service_cb;
    protocol.per_session_data_size = sizeof(struct session_data);
    protocol.rx_buffer_size = 0;
    protocol.id = 0;
    protocol.user = NULL;

    context = lws_create_context(&ctxCreationInfo);
    KF_LOG_INFO(logger, "TDEngineHuobi::login: context created.");


    if (context == NULL) {
        KF_LOG_ERROR(logger, "TDEngineHuobi::login: context is NULL. return");
        return;
    }

    // Set up the client creation info
    auto& stServerInfo = m_vstServerInfos.front();
    std::string strAddress = stServerInfo.strEndpoint;
    size_t nAddressEndPos = strAddress.find_last_of('/');
    std::string strPath = strAddress.substr(nAddressEndPos);
    strPath += "?token=";
    strPath += m_strToken;
    strPath += "&[connectId=" +  getId() +"]";
    strAddress = strAddress.substr(0,nAddressEndPos);
    strAddress = strAddress.substr(strAddress.find_last_of('/') + 1);
    clientConnectInfo.address = strAddress.c_str();
    clientConnectInfo.path = strPath.c_str(); // Set the info's path to the fixed up url path
    clientConnectInfo.context = context;
    clientConnectInfo.port = 443;
    clientConnectInfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
    clientConnectInfo.host =strAddress.c_str();
    clientConnectInfo.origin = strAddress.c_str();
    clientConnectInfo.ietf_version_or_minus_one = -1;
    clientConnectInfo.protocol = protocols[PROTOCOL_TEST].name;
    clientConnectInfo.pwsi = &wsi;

    KF_LOG_INFO(logger, "TDEngineHuobi::login: address = " << clientConnectInfo.address << ",path = " << clientConnectInfo.path);

    wsi = lws_client_connect_via_info(&clientConnectInfo);
    if (wsi == NULL) {
        KF_LOG_ERROR(logger, "TDEngineHuobi::login: wsi create error.");
        return;
    }
    KF_LOG_INFO(logger, "TDEngineHuobi::login: wsi create success.");

    connect(timeout_nsec);
}

void TDEngineHuobi::logout()
{
    KF_LOG_INFO(logger, "[logout]");
}

void TDEngineHuobi::release_api()
{
    KF_LOG_INFO(logger, "[release_api]");
}

bool TDEngineHuobi::is_logged_in() const
{
    KF_LOG_INFO(logger, "[is_logged_in]");
    for (auto& unit: account_units)
    {
        if (!unit.logged_in)
            return false;
    }
    return true;
}

bool TDEngineHuobi::is_connected() const
{
    KF_LOG_INFO(logger, "[is_connected]");
    return is_logged_in();
}


std::string TDEngineHuobi::GetSide(const LfDirectionType& input) {
    if (LF_CHAR_Buy == input) {
        return "buy";
    } else if (LF_CHAR_Sell == input) {
        return "sell";
    } else {
        return "";
    }
}

LfDirectionType TDEngineHuobi::GetDirection(std::string input) {
    if ("buy" == input) {
        return LF_CHAR_Buy;
    } else if ("sell" == input) {
        return LF_CHAR_Sell;
    } else {
        return LF_CHAR_Buy;
    }
}

std::string TDEngineHuobi::GetType(const LfOrderPriceTypeType& input) {
    if (LF_CHAR_LimitPrice == input) {
        return "limit";
    } else if (LF_CHAR_AnyPrice == input) {
        return "market";
    } else {
        return "";
    }
}

LfOrderPriceTypeType TDEngineHuobi::GetPriceType(std::string input) {
    if ("limit" == input) {
        return LF_CHAR_LimitPrice;
    } else if ("market" == input) {
        return LF_CHAR_AnyPrice;
    } else {
        return '0';
    }
}
//订单状态，﻿open（未成交）、filled（已完成）、canceled（已撤销）、cancel（撤销中）、partially-filled（部分成交）
LfOrderStatusType TDEngineHuobi::GetOrderStatus(bool isCancel,int64_t nSize,int64_t nDealSize) {

    if(isCancel)
    {
        return LF_CHAR_Canceled;
    }
    if(nDealSize == 0)
    {
        return LF_CHAR_NotTouched;
    }
    if(nSize > nDealSize)
    {
        return  LF_CHAR_PartTradedQueueing;
    }
    return LF_CHAR_AllTraded;
}

/**
 * req functions
 * 查询账户持仓
 */
void TDEngineHuobi::req_investor_position(const LFQryPositionField* data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId);

    AccountUnitHuobi& unit = account_units[account_index];
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
    //调用get函数获取用户信息，保存在d中https://api.huobi.pro/v1/accout/accouts
/*
{
  "data": {
    "id": 100009,
    "type": "spot",
    "state": "working",
    "list": [
      {
        "currency": "usdt",
        "type": "trade",
        "balance": "5007.4362872650"
      },
      {
        "currency": "usdt",
        "type": "frozen",
        "balance": "348.1199920000"
      }
    ],
    "user-id": 10000
  }
}
错误码
错误信息返回格式
{
  "id": "id generate by client",
  "status": "error",
  "err-code": "err-code",
  "err-msg": "err-message",
  "ts": 1487152091345
}
*/
    get_account(unit, d);
    KF_LOG_INFO(logger, "[req_investor_position] (get_account)" );
    if(d.IsObject() && d.HasMember("status"))
    {
        std::string status=d["status"].GetString();
        KF_LOG_INFO(logger, "[req_investor_position] (get status)" );
        //errorId =  std::round(std::stod(d["id"].GetString()));
        errorId = 404;
        KF_LOG_INFO(logger, "[req_investor_position] (status)" << status);
        KF_LOG_INFO(logger, "[req_investor_position] (errorId)" << errorId);
        if(status != "ok") {
            if (d.HasMember("err-msg") && d["err-msg"].IsString()) {
                std::string tab="\t";
                errorMsg = d["err-code"].GetString()+tab+d["err-msg"].GetString();
            }
            KF_LOG_ERROR(logger, "[req_investor_position] failed!" << " (rid)" << requestId << " (errorId)" << errorId
                                                                   << " (errorMsg) " << errorMsg);
            raw_writer->write_error_frame(&pos, sizeof(LFRspPositionField), source_id, MSG_TYPE_LF_RSP_POS_HUOBI, 1, requestId, errorId, errorMsg.c_str());
        }
    }
    send_writer->write_frame(data, sizeof(LFQryPositionField), source_id, MSG_TYPE_LF_QRY_POS_HUOBI, 1, requestId);
/*账户余额
GET /v1/account/accounts/{account-id}/balance
{
  "data": {
    "id": 100009,
    "type": "spot",
    "state": "working",
    "list": [
      {
        "currency": "usdt",
        "type": "trade",
        "balance": "5007.4362872650"
      },
      {
        "currency": "usdt",
        "type": "frozen",
        "balance": "348.1199920000"
      }
    ],
    "user-id": 10000
  }
}
*/
    std::vector<LFRspPositionField> tmp_vector;
    if(!d.HasParseError() && d.HasMember("data"))
    {
        auto& accounts = d["data"]["list"];
        size_t len = d["data"]["list"].Size();
        KF_LOG_INFO(logger, "[req_investor_position] (accounts.length)" << len);
        for(size_t i = 0; i < len; i++)
        {
            std::string symbol = accounts.GetArray()[i]["currency"].GetString();
            KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId << " (symbol) " << symbol);
            pos.Position = std::round(std::stod(accounts.GetArray()[i]["balance"].GetString()) * scale_offset);
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

void TDEngineHuobi::req_qry_account(const LFQryAccountField *data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_qry_account]");
}

void TDEngineHuobi::dealPriceVolume(AccountUnitHuobi& unit,const std::string& symbol,int64_t nPrice,int64_t nVolume,int64_t& nDealPrice,int64_t& nDealVolume)
{
    KF_LOG_DEBUG(logger, "[dealPriceVolume] (symbol)" << symbol);
    auto it = unit.mapPriceIncrement.find(symbol);
    if(it == unit.mapPriceIncrement.end())
    {
        KF_LOG_INFO(logger, "[dealPriceVolume] symbol not find :" << symbol);
        nDealVolume = 0;
        return ;
    }
    else
    {
        if(it->second.nBaseMinSize > nVolume)
        {
            KF_LOG_INFO(logger, "[dealPriceVolume] (Volume) "  << nVolume  << " <  (BaseMinSize)  "  << it->second.nBaseMinSize << " (symbol)" << symbol);
            nDealVolume = 0;
            return ;
        }
        nDealVolume =  it->second.nQuoteIncrement  > 0 ? nVolume / it->second.nQuoteIncrement * it->second.nQuoteIncrement : nVolume;
        nDealPrice = it->second.nPriceIncrement > 0 ? nPrice / it->second.nPriceIncrement * it->second.nPriceIncrement : nPrice;
    }
    KF_LOG_INFO(logger, "[dealPriceVolume]  (symbol)" << symbol << " (Volume)" << nVolume << " (Price)" << nPrice
                                                      << " (FixedVolume)" << nDealVolume << " (FixedPrice)" << nDealPrice);
}

void TDEngineHuobi::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitHuobi& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_insert]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Tid)" << data->InstrumentID
                                              << " (Volume)" << data->Volume
                                              << " (LimitPrice)" << data->LimitPrice
                                              << " (OrderRef)" << data->OrderRef);
    send_writer->write_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_HUOBI, 1/*ISLAST*/, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_insert]: not in WhiteList, ignore it  (rid)" << requestId <<
                                                                                      " (errorId)" << errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_HUOBI, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_insert] (exchange_ticker)" << ticker);

    double funds = 0;
    Document d;
    /*int64_t fixedPrice = 0;
    int64_t fixedVolume = 0;
    dealPriceVolume(unit,data->InstrumentID,data->LimitPrice,data->Volume,fixedPrice,fixedVolume);
    if(fixedVolume == 0)
    {
        KF_LOG_DEBUG(logger, "[req_order_insert] fixed Volume error" << ticker);
        errorId = 200;
        errorMsg = data->InstrumentID;
        errorMsg += " : quote less than baseMinSize";
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_HUOBI, 1, requestId, errorId, errorMsg.c_str());
        return;
    }*/
    send_order(unit, ticker.c_str(), GetSide(data->Direction).c_str(),
            GetType(data->OrderPriceType).c_str(), data->Volume*1.0/scale_offset, data->LimitPrice*1.0/scale_offset, funds, d);
    //not expected response
    if(!d.IsObject())
    {
        errorId = 100;
        errorMsg = "send_order http response has parse error or is not json. please check the log";
        KF_LOG_ERROR(logger, "[req_order_insert] send_order error!  (rid)" << requestId << " (errorId)" <<
                                                                           errorId << " (errorMsg) " << errorMsg);
    } else  if(d.HasMember("status"))
    {
        std::string status =d["status"].GetString();
        if(status == "ok") {
            //if send successful and the exchange has received ok, then add to  pending query order list
            std::string remoteOrderId = d["data"].GetString();
            //fix defect of use the old value
            localOrderRefRemoteOrderId[std::string(data->OrderRef)] = remoteOrderId;
            KF_LOG_INFO(logger, "[req_order_insert] after send  (rid)" << requestId << " (OrderRef) " <<
                                                                       data->OrderRef << " (remoteOrderId) "
                                                                       << remoteOrderId);
            LFRtnOrderField rtn_order;
            memset(&rtn_order, 0, sizeof(LFRtnOrderField));

            rtn_order.OrderStatus = LF_CHAR_NotTouched;
            rtn_order.VolumeTraded = 0;

            //first send onRtnOrder about the status change or VolumeTraded change
            strcpy(rtn_order.ExchangeID, "huobi");
            strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
            strncpy(rtn_order.InstrumentID, data->InstrumentID, 31);
            rtn_order.Direction = data->Direction;
            //No this setting on Huobi
            rtn_order.TimeCondition = LF_CHAR_GTC;
            rtn_order.OrderPriceType = data->OrderPriceType;
            strncpy(rtn_order.OrderRef, data->OrderRef, 13);
            rtn_order.VolumeTotalOriginal = data->Volume;
            rtn_order.LimitPrice = data->LimitPrice;
            rtn_order.VolumeTotal = data->Volume;

            std::string strOrderID = remoteOrderId;
            strncpy(rtn_order.BusinessUnit,strOrderID.c_str(),21);
            on_rtn_order(&rtn_order);
            raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                    source_id, MSG_TYPE_LF_RTN_ORDER_HUOBI,
                                    1, (rtn_order.RequestID > 0) ? rtn_order.RequestID : -1);

            KF_LOG_DEBUG(logger, "[req_order_insert] (addNewQueryOrdersAndTrades)" );
            char noneStatus = LF_CHAR_NotTouched;
            addNewQueryOrdersAndTrades(unit, data->InstrumentID, data->OrderRef, noneStatus, 0, remoteOrderId);

            //success, only record raw data
            raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_HUOBI, 1,
                                          requestId, errorId, errorMsg.c_str());
            KF_LOG_DEBUG(logger, "[req_order_insert] success" );
            return;

        }else {
            //errorId = std::round(std::stod(d["id"].GetString()));
            errorId=404;
            if(d.HasMember("err-msg") && d["err-msg"].IsString())
            {
                std::string tab="\t";
                errorMsg = d["err-code"].GetString()+tab+d["err-msg"].GetString();
            }
            KF_LOG_ERROR(logger, "[req_order_insert] send_order error!  (rid)" << requestId << " (errorId)" <<
                                                                               errorId << " (errorMsg) " << errorMsg);
        }
    }
    if(errorId != 0)
    {
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_HUOBI, 1, requestId, errorId, errorMsg.c_str());
    }
}

void TDEngineHuobi::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitHuobi& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_action]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Iid)" << data->InvestorID
                                              << " (OrderRef)" << data->OrderRef
                                              << " (KfOrderID)" << data->KfOrderID);

    send_writer->write_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_HUOBI, 1, requestId);

    int errorId = 0;
    std::string errorMsg = "";

    std::string ticker = unit.coinPairWhiteList.GetValueByKey(std::string(data->InstrumentID));
    if(ticker.length() == 0) {
        errorId = 200;
        errorMsg = std::string(data->InstrumentID) + " not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_action]: not in WhiteList , ignore it: (rid)" << requestId << " (errorId)" <<
                                                                                       errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_HUOBI, 1, requestId, errorId, errorMsg.c_str());
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
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_HUOBI, 1, requestId, errorId, errorMsg.c_str());
        return;
    } else {
        remoteOrderId = itr->second;
        KF_LOG_DEBUG(logger, "[req_order_action] found in localOrderRefRemoteOrderId map (orderRef) "
                << data->OrderRef << " (remoteOrderId) " << remoteOrderId);
    }

    Document d;
    cancel_order(unit, ticker, remoteOrderId, d);

    std::string strSuccessCode =  "ok";
    if(!d.HasParseError() && d.HasMember("status") && strSuccessCode != d["status"].GetString()) {
        errorId = 404;
        if(d.HasMember("err-msg") && d["err-msg"].IsString())
        {
            std::string tab="\t";
            errorMsg = d["err-code"].GetString()+tab+d["err-msg"].GetString();
        }
        KF_LOG_ERROR(logger, "[req_order_action] cancel_order failed!" << " (rid)" << requestId
                                                                       << " (errorId)" << errorId << " (errorMsg) " << errorMsg);
    }
    if(errorId != 0)
    {
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_HUOBI, 1, requestId, errorId, errorMsg.c_str());

    } else {
        //addRemoteOrderIdOrderActionSentTime( data, requestId, remoteOrderId);
        // addRemoteOrderIdOrderActionSentTime( data, requestId, remoteOrderId);
        //TODO:   onRtn order/on rtn trade
    }
}
//对于每个撤单指令发出后30秒（可配置）内，如果没有收到回报，就给策略报错（撤单被拒绝，pls retry)
void TDEngineHuobi::addRemoteOrderIdOrderActionSentTime(const LFOrderActionField* data, int requestId, const std::string& remoteOrderId)
{
    std::lock_guard<std::mutex> guard_mutex_order_action(*mutex_orderaction_waiting_response);

    OrderActionSentTime newOrderActionSent;
    newOrderActionSent.requestId = requestId;
    newOrderActionSent.sentNameTime = getTimestamp();
    memcpy(&newOrderActionSent.data, data, sizeof(LFOrderActionField));
    remoteOrderIdOrderActionSentTime[remoteOrderId] = newOrderActionSent;
}

void TDEngineHuobi::GetAndHandleOrderTradeResponse()
{
    // KF_LOG_INFO(logger, "[GetAndHandleOrderTradeResponse]" );
    //every account
    for (size_t idx = 0; idx < account_units.size(); idx++)
    {
        AccountUnitHuobi& unit = account_units[idx];
        if (!unit.logged_in)
        {
            continue;
        }
        moveNewOrderStatusToPending(unit);
        retrieveOrderStatus(unit);
    }//end every account
}

//订单状态
void TDEngineHuobi::retrieveOrderStatus(AccountUnitHuobi& unit)
{
    //KF_LOG_INFO(logger, "[retrieveOrderStatus] order_size:"<< unit.pendingOrderStatus.size());
    std::lock_guard<std::mutex> guard_mutex(*mutex_response_order_status);
    std::lock_guard<std::mutex> guard_mutex_order_action(*mutex_orderaction_waiting_response);

    std::vector<PendingOrderStatus>::iterator orderStatusIterator;

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
                                                               << "  (account.pendingOrderStatus.remoteOrderId) " << orderStatusIterator->remoteOrderId
                                                               << "  (account.pendingOrderStatus.OrderStatus) " << orderStatusIterator->OrderStatus
                                                               << "  (exchange_ticker)" << ticker
        );

        Document d;
        query_order(unit, ticker,orderStatusIterator->remoteOrderId, d);
        //parse order status
        //订单状态，submitting , submitted 已提交, partial-filled 部分成交, partial-canceled 部分成交撤销, filled 完全成交, canceled 已撤销
        if(d.HasParseError()) {
            //HasParseError, skip
            KF_LOG_ERROR(logger, "[retrieveOrderStatus] get_order response HasParseError " << " (symbol)" << orderStatusIterator->InstrumentID
                                                                                           << " (orderRef)" << orderStatusIterator->OrderRef
                                                                                           << " (remoteOrderId) " << orderStatusIterator->remoteOrderId);
            continue;
        }
        const std::string strSuccessCode = "200000";
        KF_LOG_INFO(logger, "[retrieveOrderStatus] query_order:");
        if(d.HasMember("code") && strSuccessCode ==  d["code"].GetString())
        {
            KF_LOG_INFO(logger, "[retrieveOrderStatus] (query success)");
            rapidjson::Value &data = d["data"];
            ResponsedOrderStatus responsedOrderStatus;
            responsedOrderStatus.ticker = ticker;
            double dDealFunds = std::stod(data["dealFunds"].GetString());
            double dDealSize = std::stod(data["dealSize"].GetString());
            responsedOrderStatus.averagePrice = dDealSize > 0 ? std::round(dDealFunds / dDealSize * scale_offset): 0;
            responsedOrderStatus.orderId = orderStatusIterator->remoteOrderId;
            //报单价格条件
            responsedOrderStatus.OrderPriceType = GetPriceType(data["type"].GetString());
            //买卖方向
            responsedOrderStatus.Direction = GetDirection(data["side"].GetString());
            //报单状态
            int64_t nDealSize = std::round(dDealSize * scale_offset);
            int64_t nSize = std::round(std::stod(data["size"].GetString()) * scale_offset);
            responsedOrderStatus.OrderStatus = GetOrderStatus(data["cancelExist"].GetBool(),nSize,nDealSize);
            responsedOrderStatus.price = std::round(std::stod(data["price"].GetString()) * scale_offset);
            responsedOrderStatus.volume = nSize;
            //今成交数量
            responsedOrderStatus.VolumeTraded = nDealSize;
            responsedOrderStatus.openVolume =  nSize - nDealSize;

            handlerResponseOrderStatus(unit, orderStatusIterator, responsedOrderStatus);

            //OrderAction发出以后，有状态回来，就清空这次OrderAction的发送状态，不必制造超时提醒信息
            remoteOrderIdOrderActionSentTime.erase(orderStatusIterator->remoteOrderId);
        } else {
            KF_LOG_INFO(logger, "[retrieveOrderStatus] (query failed)");
            std::string errorMsg;
            std::string errorId = d["code"].GetString();
            if(d.HasMember("msg") && d["msg"].IsString())
            {
                errorMsg = d["msg"].GetString();
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
        //KF_LOG_INFO(logger, "[retrieveOrderStatus] move to next pendingOrderStatus.");
    }
}

void TDEngineHuobi::addNewQueryOrdersAndTrades(AccountUnitHuobi& unit, const char_31 InstrumentID,
                                                const char_21 OrderRef, const LfOrderStatusType OrderStatus,
                                                const uint64_t VolumeTraded, const std::string& remoteOrderId)
{
    KF_LOG_DEBUG(logger, "[addNewQueryOrdersAndTrades]" );
    //add new orderId for GetAndHandleOrderTradeResponse
    std::lock_guard<std::mutex> guard_mutex(*mutex_order_and_trade);

    PendingOrderStatus status;
    strncpy(status.InstrumentID, InstrumentID, 31);
    strncpy(status.OrderRef, OrderRef, 21);
    status.OrderStatus = OrderStatus;
    status.VolumeTraded = VolumeTraded;
    status.averagePrice = 0.0;
    status.remoteOrderId = remoteOrderId;
    unit.newOrderStatus.push_back(status);
    KF_LOG_INFO(logger, "[addNewQueryOrdersAndTrades] (InstrumentID) " << status.InstrumentID
                                                                       << " (OrderRef) " << status.OrderRef
                                                                       << " (remoteOrderId) " << status.remoteOrderId
                                                                       << "(VolumeTraded)" << VolumeTraded);
}


void TDEngineHuobi::moveNewOrderStatusToPending(AccountUnitHuobi& unit)
{
    std::lock_guard<std::mutex> pending_guard_mutex(*mutex_order_and_trade);
    std::lock_guard<std::mutex> response_guard_mutex(*mutex_response_order_status);


    std::vector<PendingOrderStatus>::iterator newOrderStatusIterator;
    for(newOrderStatusIterator = unit.newOrderStatus.begin(); newOrderStatusIterator != unit.newOrderStatus.end();)
    {
        unit.pendingOrderStatus.push_back(*newOrderStatusIterator);
        newOrderStatusIterator = unit.newOrderStatus.erase(newOrderStatusIterator);
    }
}

void TDEngineHuobi::set_reader_thread()
{
    ITDEngine::set_reader_thread();

    KF_LOG_INFO(logger, "[set_reader_thread] rest_thread start on TDEngineHuobi::loop");
    rest_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineHuobi::loopwebsocket, this)));

    KF_LOG_INFO(logger, "[set_reader_thread] orderaction_timeout_thread start on TDEngineHuobi::loopOrderActionNoResponseTimeOut");
    orderaction_timeout_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineHuobi::loopOrderActionNoResponseTimeOut, this)));
}

void TDEngineHuobi::loopwebsocket()
{
    time_t nLastTime = time(0);

    while(isRunning)
    {
        time_t nNowTime = time(0);
        if(m_isPong && (nNowTime - nLastTime>= 30))
        {
            m_isPong = false;
            nLastTime = nNowTime;
            KF_LOG_INFO(logger, "TDEngineHuobi::loop: last time = " <<  nLastTime << ",now time = " << nNowTime << ",m_isPong = " << m_isPong);
            m_shouldPing = true;
            lws_callback_on_writable(m_conn);
        }
        //KF_LOG_INFO(logger, "TDEngineHuobi::loop:lws_service");
        lws_service( context, rest_get_interval_ms );
    }
}

void TDEngineHuobi::loop()
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


void TDEngineHuobi::loopOrderActionNoResponseTimeOut()
{
    KF_LOG_INFO(logger, "[loopOrderActionNoResponseTimeOut] (isRunning) " << isRunning);
    while(isRunning)
    {
        orderActionNoResponseTimeOut();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void TDEngineHuobi::orderActionNoResponseTimeOut()
{
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

void TDEngineHuobi::printResponse(const Document& d)
{
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);
    KF_LOG_INFO(logger, "[printResponse] ok (text) " << buffer.GetString());
}

void TDEngineHuobi::getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& json)
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

void TDEngineHuobi::get_account(AccountUnitHuobi& unit, Document& json)
{
    KF_LOG_INFO(logger, "[get_account]");
    /*
    账户余额
    查询指定账户的余额，支持以下账户：
    spot：现货账户， margin：杠杆账户，otc：OTC 账户，point：点卡账户
    HTTP 请求
    GET /v1/account/accounts/{account-id}/balance
    */
   std::string getPath="/v1/account/accounts/";
    std::string requestPath = getPath+unit.accountId+"/balance";
    const auto response = Get(requestPath,"{}",unit);
    json.Parse(response.text.c_str());
    KF_LOG_INFO(logger, "[get_account] (account info) "<<response.text.c_str());
    return ;
}
std::string TDEngineHuobi::getAccountId(AccountUnitHuobi& unit){
    KF_LOG_DEBUG(logger,"[getAccountID] ");
    std::string getPath="/v1/account/accounts/";
    const auto resp = Get("/v1/account/accounts","{}",unit);
    Document j;
    j.Parse(resp.text.c_str());
    int n=j["data"].Size();
    std::string type="spot";//现货账户
    std::string accountId;
    for(int i=0;i<n;i++){
        if(type==j["data"].GetArray()[i]["type"].GetString()){
            accountId=std::to_string(j["data"].GetArray()[i]["id"].GetInt());
            break;
        }
    }
    KF_LOG_DEBUG(logger,"[getAccountID] (accountId) "<<accountId);
    return accountId;
}
std::string TDEngineHuobi::getHuobiTime(){
    time_t timep;
    time (&timep);
    char tmp[64];
    strftime(tmp, sizeof(tmp), "%Y-%m-%dT%H%%3A%M%%3A%S",gmtime(&timep) );
    std::string huobiTime=tmp;
    return huobiTime;
}
/*
{
  "account-id": "100009",
  "amount": "10.1",
  "price": "100.1",
  "source": "api",
  "symbol": "ethusdt",
  "type": "buy-limit"
}
 * */
std::string TDEngineHuobi::createInsertOrdertring(const char *accountId,
        const char *amount, const char *price, const char *source, const char *symbol,const char *type)
{
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();
    writer.Key("account-id");
    writer.String(accountId);
    writer.Key("amount");
    writer.String(amount);
    writer.Key("price");
    writer.String(price);
    writer.Key("source");
    writer.String(source);
    writer.Key("symbol");
    writer.String(symbol);
    writer.Key("type");
    writer.String(type);
    writer.EndObject();
    return s.GetString();
}
/*火币下单请求参数
    {
        "account-id": "100009",
        "amount": "10.1",
        "price": "100.1",
        "source": "api",
        "symbol": "ethusdt",
        "type": "buy-limit"
    }
*/
void TDEngineHuobi::send_order(AccountUnitHuobi& unit, const char *code,
                                 const char *side, const char *type, double size, double price, double funds, Document& json)
{
    KF_LOG_INFO(logger, "[send_order]");
    std::string s=side,t=type;
    std::string st=s+"-"+t;
    int retry_times = 0;
    cpr::Response response;
    bool should_retry = false;
    do {
        should_retry = false;
        //火币下单post /v1/order/orders/place
        std::string requestPath = "/v1/order/orders/place";
        response = Post(requestPath,createInsertOrdertring(unit.accountId.c_str(), std::to_string(size).c_str(), std::to_string(price).c_str(),
                        "api",code,st.c_str()),unit);

        KF_LOG_INFO(logger, "[send_order] (url) " << requestPath << " (response.status_code) " << response.status_code 
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
bool TDEngineHuobi::shouldRetry(Document& doc)
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
/*
响应数据
参数名称	是否必须	数据类型	描述	取值范围
success-count	true	int	成功取消的订单数	
failed-count	true	int	取消失败的订单数	
next-id	true	long	下一个符合取消条件的订单号	
*/
void TDEngineHuobi::cancel_all_orders(AccountUnitHuobi& unit, std::string code, Document& json)
{
    KF_LOG_INFO(logger, "[cancel_all_orders]");
    std::string accountId = unit.accountId;
    //火币post批量撤销订单
    std::string requestPath = "/v1/order/orders/batchCancelOpenOrders";
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    writer.StartObject();
    writer.Key("account-id");
    writer.String(accountId.c_str());
    writer.Key("symbol");
    writer.String("btc");
    writer.Key("side");
    writer.String("buy");
    writer.Key("size");
    writer.Int(100);
    //write Signature
    writer.EndObject();
    auto response = Post(requestPath,s.GetString(),unit);
    getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineHuobi::cancel_order(AccountUnitHuobi& unit, std::string code, std::string orderId, Document& json)
{
    KF_LOG_INFO(logger, "[cancel_order]");

    int retry_times = 0;
    cpr::Response response;
    bool should_retry = false;
    do {
        should_retry = false;
        //火币post撤单请求
        std::string postPath="/v1/order/orders/";
        std::string requestPath = postPath+ orderId + "/submitcancel";
        response = Post(requestPath,"",unit);

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
/*
响应数据
字段名称	是否必须	数据类型	描述	取值范围
account-id	true	long	账户 ID	
amount	    true	string	订单数量	
canceled-at	false	long	订单撤销时间	
created-at	true	long	订单创建时间	
field-amount	true	string	已成交数量	
field-cash-amount	true	string	已成交总金额	
field-fees	true	string	已成交手续费（买入为币，卖出为钱）	
finished-at	false	long	订单变为终结态的时间，不是成交时间，包含“已撤单”状态	
id	true	long	订单ID	
price	    true	string	订单价格	
source	    true	string	订单来源	api
state	    true	string	订单状态	submitting , submitted 已提交, partial-filled 部分成交, partial-canceled 部分成交撤销, filled 完全成交, canceled 已撤销
symbol	    true	string	交易对	btcusdt, ethbtc, rcneth ...
type	    true	string	订单类型	buy-market：市价买, sell-market：市价卖, buy-limit：限价买, sell-limit：限价卖, buy-ioc：IOC买单, sell-ioc：IOC卖单
*/
void TDEngineHuobi::query_order(AccountUnitHuobi& unit, std::string code, std::string orderId, Document& json)
{
    KF_LOG_INFO(logger, "[query_order]");
    //火币get查询订单详情
    std::string getPath = "/v1/order/orders/";
    std::string requestPath = getPath + orderId;
    auto response = Get(requestPath,"",unit);
    json.Parse(response.text.c_str());
    KF_LOG_DEBUG(logger,"[query_order] response "<<response.text.c_str());
    //getResponse(response.status_code, response.text, response.error.message, json);
}



void TDEngineHuobi::handlerResponseOrderStatus(AccountUnitHuobi& unit, std::vector<PendingOrderStatus>::iterator orderStatusIterator, ResponsedOrderStatus& responsedOrderStatus)
{
    KF_LOG_INFO(logger, "[handlerResponseOrderStatus]");

    if( (responsedOrderStatus.OrderStatus == 'b' && '1' == orderStatusIterator-> OrderStatus || responsedOrderStatus.OrderStatus == orderStatusIterator-> OrderStatus) && responsedOrderStatus.VolumeTraded == orderStatusIterator->VolumeTraded)
    {//no change
        return;
    }
    int64_t newAveragePrice = responsedOrderStatus.averagePrice;
    //cancel 需要特殊处理
    if(LF_CHAR_Canceled == responsedOrderStatus.OrderStatus)  {
        /*
         * 因为restful查询有间隔时间，订单可能会先经历过部分成交，然后才达到的cancnel，所以得到cancel不能只认为是cancel，还需要判断有没有部分成交过。
        这时候需要补状态，补一个on rtn order，一个on rtn trade。
        这种情况仅cancel才有, 部分成交和全成交没有此问题。
        当然，也要考虑，如果上一次部分成交已经被抓取到的并返回过 on rtn order/on rtn trade，那么就不需要补了
         //2018-09-12.  不清楚websocket会不会有这个问题，先做同样的处理
        */
        //虽然是撤单状态，但是已经成交的数量和上一次记录的数量不一样，期间一定发生了部分成交. 要补发 LF_CHAR_PartTradedQueueing
        if(responsedOrderStatus.VolumeTraded != orderStatusIterator->VolumeTraded) {
            //if status is LF_CHAR_Canceled but traded valume changes, emit onRtnOrder/onRtnTrade of LF_CHAR_PartTradedQueueing
            LFRtnOrderField rtn_order;
            memset(&rtn_order, 0, sizeof(LFRtnOrderField));

            std::string strOrderID = orderStatusIterator->remoteOrderId;
            strncpy(rtn_order.BusinessUnit,strOrderID.c_str(),21);

            rtn_order.OrderStatus = LF_CHAR_PartTradedNotQueueing;
            rtn_order.VolumeTraded = responsedOrderStatus.VolumeTraded;
            //first send onRtnOrder about the status change or VolumeTraded change
            strcpy(rtn_order.ExchangeID, "huobi");
            strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
            strncpy(rtn_order.InstrumentID, orderStatusIterator->InstrumentID, 31);
            rtn_order.Direction = responsedOrderStatus.Direction;
            //No this setting on Huobi
            rtn_order.TimeCondition = LF_CHAR_GTC;
            rtn_order.OrderPriceType = responsedOrderStatus.OrderPriceType;
            strncpy(rtn_order.OrderRef, orderStatusIterator->OrderRef, 13);
            rtn_order.VolumeTotalOriginal = responsedOrderStatus.volume;
            rtn_order.LimitPrice = responsedOrderStatus.price;
            //剩余数量
            rtn_order.VolumeTotal = responsedOrderStatus.openVolume;

            //经过2018-08-20讨论，这个on rtn order 可以不必发送了, 只记录raw有这么回事就行了。只补发一个 on rtn trade 就行了。
            //on_rtn_order(&rtn_order);
            raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                    source_id, MSG_TYPE_LF_RTN_ORDER_HUOBI,
                                    1, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);


            //send OnRtnTrade
            LFRtnTradeField rtn_trade;
            memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
            strcpy(rtn_trade.ExchangeID, "huobi");
            strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
            strncpy(rtn_trade.InstrumentID, orderStatusIterator->InstrumentID, 31);
            strncpy(rtn_trade.OrderRef, orderStatusIterator->OrderRef, 13);
            rtn_trade.Direction = rtn_order.Direction;
            double oldAmount = (double)orderStatusIterator->VolumeTraded/scale_offset * orderStatusIterator->averagePrice/scale_offset*1.0;
            double newAmount = (double)rtn_order.VolumeTraded/scale_offset * newAveragePrice/scale_offset*1.0;

            //calculate the volumn and price (it is average too)
            rtn_trade.Volume = rtn_order.VolumeTraded - orderStatusIterator->VolumeTraded;
            double price = (newAmount - oldAmount)/((double)rtn_trade.Volume/scale_offset);
            rtn_trade.Price =(price + 0.000000001)*scale_offset;//(newAmount - oldAmount)/(rtn_trade.Volume);
            strncpy(rtn_trade.OrderSysID,strOrderID.c_str(),31);
            on_rtn_trade(&rtn_trade);
            raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
                                    source_id, MSG_TYPE_LF_RTN_TRADE_HUOBI, 1, -1);

            KF_LOG_INFO(logger, "[on_rtn_trade 1] (InstrumentID)" << rtn_trade.InstrumentID << "(Direction)" << rtn_trade.Direction
                                                                  << "(Volume)" << rtn_trade.Volume << "(Price)" <<  rtn_trade.Price);


        }

        //emit the LF_CHAR_Canceled status
        LFRtnOrderField rtn_order;
        memset(&rtn_order, 0, sizeof(LFRtnOrderField));

        std::string strOrderID = orderStatusIterator->remoteOrderId;
        strncpy(rtn_order.BusinessUnit,strOrderID.c_str(),21);

        rtn_order.OrderStatus = LF_CHAR_Canceled;
        rtn_order.VolumeTraded = responsedOrderStatus.VolumeTraded;

        //first send onRtnOrder about the status change or VolumeTraded change
        strcpy(rtn_order.ExchangeID, "huobi");
        strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
        strncpy(rtn_order.InstrumentID, orderStatusIterator->InstrumentID, 31);
        rtn_order.Direction = responsedOrderStatus.Direction;
        //Huobi has no this setting
        rtn_order.TimeCondition = LF_CHAR_GTC;
        rtn_order.OrderPriceType = responsedOrderStatus.OrderPriceType;
        strncpy(rtn_order.OrderRef, orderStatusIterator->OrderRef, 13);
        rtn_order.VolumeTotalOriginal = responsedOrderStatus.volume;
        rtn_order.LimitPrice = responsedOrderStatus.price;
        //剩余数量
        rtn_order.VolumeTotal = responsedOrderStatus.openVolume;

        on_rtn_order(&rtn_order);
        raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                source_id, MSG_TYPE_LF_RTN_ORDER_HUOBI,
                                1, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);

        KF_LOG_INFO(logger, "[on_rtn_order] (InstrumentID)" << rtn_order.InstrumentID << "(OrderStatus)" <<  rtn_order.OrderStatus
                                                            << "(Volume)" << rtn_order.VolumeTotalOriginal << "(VolumeTraded)" << rtn_order.VolumeTraded);


        //third, update last status for next query_order
        orderStatusIterator->OrderStatus = rtn_order.OrderStatus;
        orderStatusIterator->VolumeTraded = rtn_order.VolumeTraded;
        orderStatusIterator->averagePrice = newAveragePrice;

    }
    else
    {
        //if status changed or LF_CHAR_PartTradedQueueing but traded valume changes, emit onRtnOrder
        LFRtnOrderField rtn_order;
        memset(&rtn_order, 0, sizeof(LFRtnOrderField));

        std::string strOrderID = orderStatusIterator->remoteOrderId;
        strncpy(rtn_order.BusinessUnit,strOrderID.c_str(),21);

        KF_LOG_INFO(logger, "[handlerResponseOrderStatus] VolumeTraded Change  LastOrderPsp:" << orderStatusIterator->VolumeTraded << ", NewOrderRsp: " << responsedOrderStatus.VolumeTraded  <<
                                                                                              " NewOrderRsp.Status " << responsedOrderStatus.OrderStatus);
        if(responsedOrderStatus.OrderStatus == LF_CHAR_NotTouched && responsedOrderStatus.VolumeTraded != orderStatusIterator->VolumeTraded) {
            rtn_order.OrderStatus = LF_CHAR_PartTradedQueueing;
        } else{
            rtn_order.OrderStatus = responsedOrderStatus.OrderStatus;
        }
        rtn_order.VolumeTraded = responsedOrderStatus.VolumeTraded;

        //first send onRtnOrder about the status change or VolumeTraded change
        strcpy(rtn_order.ExchangeID, "huobi");
        strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
        strncpy(rtn_order.InstrumentID, orderStatusIterator->InstrumentID, 31);
        rtn_order.Direction = responsedOrderStatus.Direction;
        //No this setting on Huobi
        rtn_order.TimeCondition = LF_CHAR_GTC;
        rtn_order.OrderPriceType = responsedOrderStatus.OrderPriceType;
        strncpy(rtn_order.OrderRef, orderStatusIterator->OrderRef, 13);
        rtn_order.VolumeTotalOriginal = responsedOrderStatus.volume;
        rtn_order.LimitPrice = responsedOrderStatus.price;
        rtn_order.VolumeTotal = responsedOrderStatus.openVolume;

        on_rtn_order(&rtn_order);
        raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                source_id, MSG_TYPE_LF_RTN_ORDER_HUOBI,
                                1, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);

        KF_LOG_INFO(logger, "[on_rtn_order] (InstrumentID)" << rtn_order.InstrumentID << "(OrderStatus)" <<  rtn_order.OrderStatus
                                                            << "(Volume)" << rtn_order.VolumeTotalOriginal << "(VolumeTraded)" << rtn_order.VolumeTraded);

        int64_t newAveragePrice = responsedOrderStatus.averagePrice;
        //second, if the status is PartTraded/AllTraded, send OnRtnTrade
        if(rtn_order.OrderStatus == LF_CHAR_AllTraded ||
           (LF_CHAR_PartTradedQueueing == rtn_order.OrderStatus
            && rtn_order.VolumeTraded != orderStatusIterator->VolumeTraded))
        {
            LFRtnTradeField rtn_trade;
            memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
            strcpy(rtn_trade.ExchangeID, "huobi");
            strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
            strncpy(rtn_trade.InstrumentID, orderStatusIterator->InstrumentID, 31);
            strncpy(rtn_trade.OrderRef, orderStatusIterator->OrderRef, 13);
            rtn_trade.Direction = rtn_order.Direction;
            double oldAmount = (double)orderStatusIterator->VolumeTraded/scale_offset * orderStatusIterator->averagePrice/scale_offset*1.0;
            double newAmount = (double)rtn_order.VolumeTraded/scale_offset * newAveragePrice/scale_offset*1.0;

            //calculate the volumn and price (it is average too)
            rtn_trade.Volume = rtn_order.VolumeTraded - orderStatusIterator->VolumeTraded;
            double price = (newAmount - oldAmount)/((double)rtn_trade.Volume/scale_offset);
            rtn_trade.Price = (price + 0.000000001)*scale_offset;//(newAmount - oldAmount)/(rtn_trade.Volume);
            strncpy(rtn_trade.OrderSysID,strOrderID.c_str(),31);
            on_rtn_trade(&rtn_trade);
            raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
                                    source_id, MSG_TYPE_LF_RTN_TRADE_HUOBI, 1, -1);

            KF_LOG_INFO(logger, "[on_rtn_trade] (InstrumentID)" << rtn_trade.InstrumentID << "(Direction)" << rtn_trade.Direction
                                                                << "(Volume)" << rtn_trade.Volume << "(Price)" <<  rtn_trade.Price);

        }
        //third, update last status for next query_order
        orderStatusIterator->OrderStatus = rtn_order.OrderStatus;
        orderStatusIterator->VolumeTraded = rtn_order.VolumeTraded;
        orderStatusIterator->averagePrice = newAveragePrice;
    }
}

std::string TDEngineHuobi::parseJsonToString(Document &d)
{
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);

    return buffer.GetString();
}


inline int64_t TDEngineHuobi::getTimestamp()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timestamp;
}

void TDEngineHuobi::genUniqueKey()
{
    struct tm cur_time = getCurLocalTime();
    //SSMMHHDDN
    char key[11]{0};
    snprintf((char*)key, 11, "%02d%02d%02d%02d%1s", cur_time.tm_sec, cur_time.tm_min, cur_time.tm_hour, cur_time.tm_mday, m_engineIndex.c_str());
    m_uniqueKey = key;
}

//clientid =  m_uniqueKey+orderRef
std::string TDEngineHuobi::genClinetid(const std::string &orderRef)
{
    static int nIndex = 0;
    return m_uniqueKey + orderRef + std::to_string(nIndex++);
}


#define GBK2UTF8(msg) kungfu::yijinjing::gbk2utf8(string(msg))

BOOST_PYTHON_MODULE(libhuobitd)
{
    using namespace boost::python;
    class_<TDEngineHuobi, boost::shared_ptr<TDEngineHuobi> >("Engine")
     .def(init<>())
        .def("init", &TDEngineHuobi::initialize)
        .def("start", &TDEngineHuobi::start)
        .def("stop", &TDEngineHuobi::stop)
        .def("logout", &TDEngineHuobi::logout)
        .def("wait_for_stop", &TDEngineHuobi::wait_for_stop);
}
