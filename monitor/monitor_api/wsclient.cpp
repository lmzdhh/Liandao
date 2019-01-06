//
// Created by wang on 11/10/18.
//
#include "wsclient.h"
#include <stdio.h>
#include <thread>
#include <libwebsockets.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include "rapidjson/rapidjson.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
using rapidjson::Document;
using namespace rapidjson;
using namespace std;
MONITOR_NAMESPACE_START
static int lwsEventCallback( struct lws *conn, enum lws_callback_reasons reason, void *user, void* data, size_t len );
static struct lws_protocols lwsProtocols[] { {"", lwsEventCallback, 0, 65535}, {NULL, NULL, 0, 0} };
wsclient* g_wsclient = nullptr;

wsclient::wsclient()
{
}

wsclient::~wsclient()
{
    release();
}
void wsclient::init(KfLogPtr logger)
{
    m_logger = logger;
}

void wsclient::release()
{
    m_isRunning = false;
    if (m_wsThread.joinable())
    {
        m_wsThread.join();
    }
}

bool wsclient::connect(const std::string& url)
{
    g_wsclient = this;
    if(!parseAddress(url))
    {
        return false;
    }
    if (!createWsContext())
    {
        return false;
    }
    if(!createConnection())
    {
        return false;
    }
    m_wsThread = std::thread([&]{
        KF_LOG_INFO(m_logger, "start event thread:"<<std::this_thread::get_id());
        lwsEventLoop();
        KF_LOG_INFO(m_logger, "exit event thread:"<<std::this_thread::get_id());
    });
    return  true;
}

//{"type":"login","clientType":"md","name":"xxx"}
bool wsclient::login(const std::string& name)
{
    KF_LOG_INFO(m_logger, "login daemon server");
    try
    {
        std::vector<std::string> results;
        //td_huobi
        boost::split(results, name, boost::is_any_of("_"));
        if (results.size() != 2)
        {
            KF_LOG_INFO(m_logger, "parse module name error, must be xxx:xxx");
            return false;
        }
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        writer.StartObject();
        writer.Key("messageType");
        writer.String("login");
        writer.Key("clientType");
        writer.String(results[0].c_str());
        writer.Key("name");
        writer.String(results[1].c_str());
        writer.EndObject();
        sendmsg(buffer.GetString());
    }
    catch (const std::exception& e)
    {
        KF_LOG_INFO(m_logger, "login exception@" << e.what());
    }
    return  true;
}

void wsclient::logout() {}

bool wsclient::createWsContext()
{
    struct lws_context_creation_info creation_info{};
    creation_info.port = CONTEXT_PORT_NO_LISTEN;
    creation_info.protocols = lwsProtocols;
    creation_info.gid = -1;
    creation_info.uid = -1;
    creation_info.max_http_header_pool = 1024;
    creation_info.fd_limit_per_thread = 1024;
    m_lwsContext = lws_create_context( &creation_info );
    if (!m_lwsContext)
    {
        KF_LOG_INFO(m_logger, "daemon api create context error");
        return false;
    }
    KF_LOG_INFO(m_logger, "daemon api create context success");
    return true;
}

bool wsclient::createConnection()
{
    struct lws_client_connect_info conn_info {};
    conn_info.context 	= m_lwsContext;
    conn_info.protocol = m_monitorUrl.protocol.c_str();
    conn_info.address = m_monitorUrl.ip.c_str();
    conn_info.port = m_monitorUrl.port;
    conn_info.path = m_monitorUrl.path.c_str();
    conn_info.host 	= conn_info.address;
    conn_info.origin = conn_info.address;
    conn_info.ssl_connection = LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
    m_lwsConnection = lws_client_connect_via_info(&conn_info);
    if(m_lwsConnection)
    {
        KF_LOG_INFO(m_logger, "daemon api connect to " << m_monitorUrl.protocol << conn_info.address <<":" << conn_info.port << "/"<< m_monitorUrl.path << " success");
        return  true;

    }
    KF_LOG_INFO(m_logger, "daemon api create connect error");
    return false;
}

bool wsclient::parseAddress(const std::string& monitor_url)
{
    try
    {
        Poco::URI uri(monitor_url);
        m_monitorUrl.protocol = uri.getScheme();
        m_monitorUrl.ip = uri.getHost();
        m_monitorUrl.port = uri.getPort();
        m_monitorUrl.path = uri.getPath();
        KF_LOG_INFO(m_logger, "daemon api parse daemon url success,scheme@"<<m_monitorUrl.protocol<<",host@"<<m_monitorUrl.ip<<",port@"<<m_monitorUrl.port);
        return  true;
        /*
        std::vector<std::string> result;
        //url format is xxx://xxx.xxx.xxx:xxx/
        boost::split(result, monitor_url, boost::is_any_of("//:/"));
        if (result.size() != 6)
        {
            KF_LOG_INFO(m_logger, "daemon api parse daemon url error, must be xxx://xxx.xxx.xxx:xxx/");
            return false;
        }
        m_monitorUrl.protocol = result[0] + "://";
        m_monitorUrl.ip = result[3];
        m_monitorUrl.port = std::atoi(result[4].c_str());
        m_monitorUrl.path = result[5];
        KF_LOG_INFO(m_logger, "daemon api parse daemon url ok:"<<monitor_url);
        return  true;
         */
    }
    catch (std::exception& e)
    {
        KF_LOG_INFO(m_logger, "daemon api parse daemon url exception@" << e.what());
    }
    return false;
}

void wsclient::lwsWrite(std::string&& msg)
{
    if (!msg.empty())
    {
        KF_LOG_DEBUG(m_logger, "send msg to daemon:" << msg);
        msg.insert(msg.begin(),  LWS_PRE, 0x00);
        lws_write(m_lwsConnection, (uint8_t*)(msg.data() + LWS_PRE), msg.size() - LWS_PRE, LWS_WRITE_TEXT);
    }
}

void wsclient::lwsEventLoop()
{
    while( m_isRunning)
    {
        lws_service(m_lwsContext, 100);
    }
}

void wsclient::onMessage(struct lws*, char* msg, size_t)
{
    std::unique_lock<std::mutex> l(m_spiMutex);
    if (m_spi)
    {
        m_spi->OnMessage(msg);
    }

}

void wsclient::onClose(struct lws*)
{
    KF_LOG_DEBUG(m_logger, "daemon api onClose");
}

void wsclient::onWrite(struct lws*)
{
    lwsWrite(pop());
}

void wsclient::setCallback(MonitorClientSpiPtr p)
{
    std::unique_lock<std::mutex> l(m_spiMutex);
    m_spi = p;
}

void wsclient::sendmsg(const std::string &json)
{
    push(json);
}

void wsclient::push(const std::string & json)
{
    std::unique_lock<std::mutex> l(m_queueMutex);
    if(m_msgQueue.size() > 10000)
    {
        m_msgQueue.pop();
    }
    m_msgQueue.push(json);
}

std::string wsclient::pop()
{
    std::unique_lock<std::mutex> l(m_queueMutex);
    if(!m_msgQueue.empty())
    {
        auto msg = m_msgQueue.front();
        m_msgQueue.pop();
        return  msg;
    }
    return std::string{};
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
        case LWS_CALLBACK_CLIENT_RECEIVE:
        {
            if(g_wsclient)
            {
                g_wsclient->onMessage(conn, (char*)data, len);
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_WRITEABLE:
        {
            if(g_wsclient)
            {
                g_wsclient->onWrite(conn);
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_CLOSED:
        case LWS_CALLBACK_CLOSED:
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        {
            if(g_wsclient)
            {
                g_wsclient->onClose(conn);
            }
            break;
        }
        default:
            break;
    }
    return 0;
}

MONITOR_NAMESPACE_END
/*USING_MONITOR_NAMESPACE
int main()
{
    auto client = MonitorClient::create();
    client->connect("ws://127.0.0.1:45678/");
    client->login("ma");
    while(1)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return 0;
}*/