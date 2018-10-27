//
// Created by wang on 10/20/18.
//
#include "MDEngineHuobi.h"
#include <stringbuffer.h>
#include <writer.h>
#include <document.h>
#include <libwebsockets.h>
#include <algorithm>
#include "../../utils/common/Utils.h"
using rapidjson::Document;
using namespace rapidjson;
using namespace kungfu;
using namespace std;
#define  SCALE_OFFSET 1e8
WC_NAMESPACE_START
//lws event function
static int lwsEventCallback( struct lws *conn, enum lws_callback_reasons reason, void *user, void* data, size_t len );
static  struct lws_protocols  lwsProtocols [] {{"md-protocol", lwsEventCallback, 0, 65536,}, { NULL, NULL, 0, 0 }};

MDEngineHuobi* MDEngineHuobi::m_instance = nullptr;

MDEngineHuobi::MDEngineHuobi(): IMDEngine(SOURCE_HUOBI)
{
    logger = yijinjing::KfLog::getLogger("MdEngine.Huobi");
    KF_LOG_DEBUG(logger, "MDEngineHuobi construct");
}

MDEngineHuobi::~MDEngineHuobi()
{
    if (m_thread)
    {
        if(m_thread->joinable())
        {
            m_thread->join();
        }
    }
    KF_LOG_DEBUG(logger, "MDEngineHuobi deconstruct");
}

void MDEngineHuobi::set_reader_thread()
{
    IMDEngine::set_reader_thread();
    m_thread = ThreadPtr(new std::thread(boost::bind(&MDEngineHuobi::lwsEventLoop, this)));
}

void MDEngineHuobi::load(const json& config)
{
    KF_LOG_INFO(logger, "load config");
    m_priceBookNum = config["book_depth_count"].get<int>();
    m_whiteList.ReadWhiteLists(config, "whiteLists");
    m_whiteList.Debug_print();
    genSubscribeString();
}

void MDEngineHuobi::genSubscribeString()
{
    auto& symbol_map = m_whiteList.GetKeyIsStrategyCoinpairWhiteList();
    for(const auto& var : symbol_map)
    {
        m_subcribeJsons.push_back(genDepthString(var.second));
        m_subcribeJsons.push_back(genDepthString(var.second));
    }
}

std::string MDEngineHuobi::genDepthString(const std::string& symbol)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("sub");
    std::string sub_value ("market");
    sub_value += symbol+"depth.step0";
    writer.String(sub_value.c_str());
    writer.Key("id");
    writer.String(std::to_string(m_id++).c_str());
    writer.EndObject();
    return buffer.GetString();
}

std::string MDEngineHuobi::genTradeString(const std::string& symbol)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("sub");
    std::string sub_value("market");
    sub_value += symbol+"trade.detail";
    writer.String(sub_value.c_str());
    writer.Key("id");
    writer.String(std::to_string(m_id++).c_str());
    writer.EndObject();
    return buffer.GetString();
}

 std::string MDEngineHuobi::genPongString(const std::string& pong)
 {
     rapidjson::StringBuffer buffer;
     rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
     writer.StartObject();
     writer.Key("pong");
     writer.String(pong.c_str());
     writer.EndObject();
     return buffer.GetString();

 }

void MDEngineHuobi::connect(long)
{
    KF_LOG_INFO(logger, "connect");
    m_connected = true;
}

void MDEngineHuobi::login(long)
{
    KF_LOG_INFO(logger, "login start");
    m_instance = this;
    struct lws_context_creation_info creation_info;
    memset(&creation_info, 0x00, sizeof(creation_info));
    creation_info.port = CONTEXT_PORT_NO_LISTEN;
    creation_info.protocols = lwsProtocols;
    creation_info.gid = -1;
    creation_info.uid = -1;
    creation_info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    creation_info.max_http_header_pool = 1024;
    creation_info.fd_limit_per_thread = 1024;
    m_lwsContext = lws_create_context( &creation_info );
    createConnection();
}

void MDEngineHuobi::logout()
{
    lws_context_destroy(m_lwsContext);
    m_logged_in = false;
    KF_LOG_INFO(logger, "logout");
}

void MDEngineHuobi::createConnection()
{
    KF_LOG_INFO(logger, "connect server start");
    struct lws_client_connect_info conn_info = {0};
    conn_info.context 	= m_lwsContext;
    conn_info.address 	= "wss://api.huobi.pro";
    conn_info.port 	= 443;
    conn_info.path 	= "/ws";
    conn_info.host 	= lws_canonical_hostname(m_lwsContext);
    conn_info.origin 	= "origin";
    conn_info.protocol = lwsProtocols[0].name;
    conn_info.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
    m_lwsConnection = lws_client_connect_via_info(&conn_info);
    if(!m_lwsConnection)
    {
        KF_LOG_INFO(logger, "connect server error");
        return ;
    }
    KF_LOG_INFO(logger, "connect server success");
    m_logged_in = true;
}

void MDEngineHuobi::lwsEventLoop()
{
    while(isRunning)
    {
        lws_service(m_lwsContext, 500);
    }
}

void MDEngineHuobi::sendMessage(std::string&& msg)
{
    lws_write(m_lwsConnection, (unsigned char*)(msg.data() + LWS_PRE), msg.size(), LWS_WRITE_TEXT);
}

void MDEngineHuobi::onMessage(struct lws* conn, char* data, size_t len)
{
    KF_LOG_DEBUG(logger, "received data from huobi start");
    try
    {
        Document json;
        json.Parse(LDUtils::gzip_decompress(std::string(data,len)).c_str());
        if(json.HasParseError())
        {
            KF_LOG_DEBUG(logger, "received data from huobi failed,json parse error");
            return;
        }
        if(json.HasMember("pong"))
        {
            parsePingMsg(json);
        }
        else if(json.HasMember("id"))
        {
            //rsp sub;
            parseRspSubscribe(json);
        }
        else if(json.HasMember("ch"))
        {
            //rtn sub
            parseSubscribeData(json);
        }
    }
    catch(const std::exception& e)
    {
        KF_LOG_ERROR(logger, "received data from huobi exception,{error:" << e.what() << "}");
    }
    catch(...)
    {
        KF_LOG_ERROR(logger, "received data from huobi system exception");
    }
    KF_LOG_DEBUG(logger, "received data from huobi end");
}

void MDEngineHuobi::onClose(struct lws* conn)
{
    reset();
    login(0);
}
void MDEngineHuobi::reset()
{
    KF_LOG_DEBUG(logger, "reset");
    m_logged_in = false;
    m_subcribeIndex = 0;
}

void MDEngineHuobi::onWrite(struct lws* conn)
{
    KF_LOG_DEBUG(logger, "subcribe start");
    if (m_subcribeJsons.empty() || m_subcribeIndex == -1)
    {
        KF_LOG_DEBUG(logger, "subcribe ignore");
        return;
    }
    KF_LOG_DEBUG(logger, "subcribe #" << m_subcribeIndex);
    auto symbol = m_subcribeJsons[m_subcribeIndex++];
    sendMessage(std::move(symbol));
    if(m_subcribeIndex >= m_subcribeJsons.size())
    {
        m_subcribeIndex = -1;
        KF_LOG_DEBUG(logger, "subcribe end");
        return;
    }
    if(isRunning)
    {
        lws_callback_on_writable(conn);
    }
    KF_LOG_DEBUG(logger, "subcribe continue");
}

 void MDEngineHuobi::parsePingMsg(const rapidjson::Document& json)
 {
     //{"pong": 18212553000}
     try
     {
         auto pong = genPongString(std::to_string(json["ping"].GetInt64()));
         KF_LOG_DEBUG(logger, "send pong msg to server,{ pong:" << pong << " }");
         sendMessage(std::move(pong));
     }
     catch (const std::exception& e)
     {
         KF_LOG_INFO(logger, "parsePingMsg,{error:"<< e.what()<<"}");
     }
 }

/*
 * right subcribe rsp
{
"id": "id1",
"status": "ok",
"subbed": "market.btcusdt.kline.1min",
"ts": 1489474081631
}
*/

/*
 * error subcribe rsp
{
"id": "id2",
"status": "error",
"err-code": "bad-request",
"err-msg": "invalid topic market.invalidsymbol.kline.1min",
"ts": 1494301904959
}
 */
 void MDEngineHuobi::parseRspSubscribe(const rapidjson::Document& json)
 {
     if (0 == strcmp(json["status"].GetString(), "error"))
     {
         //ignore failed subcribe
         KF_LOG_INFO(logger, "subscribe sysmbol error");
     }
 }

 void MDEngineHuobi::parseSubscribeData(const rapidjson::Document& json)
 {
     KF_LOG_DEBUG(logger, "parseSubscribeData start");
     auto ch = LDUtils::split(json["ch"].GetString(), ".");
     if(ch.size() != 4)
     {
         KF_LOG_DEBUG(logger, "parseSubscribeData [ch] split error");
        return;
     }
     auto instrument = m_whiteList.GetKeyByValue(ch[1]);
     if(instrument.empty())
     {
         KF_LOG_DEBUG(logger, "parseSubscribeData whiteList has no this {symbol:"<<instrument<<"}");
         return;
     }
     if (ch[2] == "depth")
     {
         doDepthData(json, instrument);
         return;
     }
     if (ch[2] == "trade")
     {
         doTradeData(json, instrument);
         return;
     }
 }

 void MDEngineHuobi::doDepthData(const rapidjson::Document& json, const std::string& instrument)
 {
    try
    {
        KF_LOG_DEBUG(logger, "doDepthData start");
        auto& bids = json["tick"]["bids"];
        auto& asks = json["tick"]["asks"];
        LFPriceBook20Field priceBook {0};
        strncpy(priceBook.ExchangeID, "huobi", std::min<size_t>(sizeof(priceBook.ExchangeID)-1, 5));
        strncpy(priceBook.InstrumentID, instrument.c_str(),std::min(sizeof(priceBook.InstrumentID)-1, instrument.size()));
        for(auto i = 0; i < std::min((int)bids.Size(),m_priceBookNum); ++i)
        {
            priceBook.AskLevels[i].price = std::round(bids[i][0].GetDouble() * SCALE_OFFSET);
            priceBook.AskLevels[i].volume = std::round(bids[i][1].GetDouble() * SCALE_OFFSET);
        }
        on_price_book_update(&priceBook);
    }
    catch (const std::exception& e)
    {
        KF_LOG_INFO(logger, "doDepthData,{error:"<< e.what()<<"}");
    }
     KF_LOG_DEBUG(logger, "doDepthData end");
 }

 void MDEngineHuobi::doTradeData(const rapidjson::Document& json, const std::string& instrument)
 {
     try
     {
         KF_LOG_DEBUG(logger, "doTradeData start");
         if(json["data"].Empty())
         {
             return;
         }
         LFL2TradeField trade{0};
         strncpy(trade.ExchangeID, "huobi", std::min((int)sizeof(trade.ExchangeID)-1, 5));
         strncpy(trade.InstrumentID, instrument.c_str(),std::min(sizeof(trade.InstrumentID)-1, instrument.size()));
         auto& first_data = json["data"][0];
         trade.Volume =  std::round(first_data["amount"].GetDouble() * SCALE_OFFSET);
         trade.Price  =  std::round(first_data["price"].GetDouble() * SCALE_OFFSET);
         std::string side = first_data["direction"].GetString();
         trade.OrderBSFlag[0] = side == "buy" ? 'B' : 'S';
         on_trade(&trade);
     }
     catch (const std::exception& e)
     {
         KF_LOG_INFO(logger, "doTradeData,{error:"<< e.what()<<"}");
     }
     KF_LOG_DEBUG(logger, "doTradeData end");
 }

 int lwsEventCallback( struct lws *conn, enum lws_callback_reasons reason, void *, void *data , size_t len )
{
    switch( reason )
    {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
        {
            lws_callback_on_writable( conn );
            break;
        }
        case LWS_CALLBACK_PROTOCOL_INIT:
        {
            break;
        }
        case LWS_CALLBACK_CLIENT_RECEIVE:
        {
            if(MDEngineHuobi::m_instance)
            {
                MDEngineHuobi::m_instance->onMessage(conn, (char*)data, len);
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_CLOSED:
        {
            if(MDEngineHuobi::m_instance)
            {
                MDEngineHuobi::m_instance->onClose(conn);
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_RECEIVE_PONG:
        {
            break;
        }
        case LWS_CALLBACK_CLIENT_WRITEABLE:
        {
            if(MDEngineHuobi::m_instance)
            {
                MDEngineHuobi::m_instance->onWrite(conn);
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
            if(MDEngineHuobi::m_instance)
            {
                MDEngineHuobi::m_instance->onClose(conn);
            }
            break;
        }
        default:
            break;
    }
    return 0;
}

BOOST_PYTHON_MODULE(libhuobimd)
{
    using namespace boost::python;
    class_<MDEngineHuobi, boost::shared_ptr<MDEngineHuobi> >("Engine")
            .def(init<>())
            .def("init", &MDEngineHuobi::initialize)
            .def("start", &MDEngineHuobi::start)
            .def("stop", &MDEngineHuobi::stop)
            .def("logout", &MDEngineHuobi::logout)
            .def("wait_for_stop", &MDEngineHuobi::wait_for_stop);
}

WC_NAMESPACE_END