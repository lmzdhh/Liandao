//
// Created by wang on 11/18/18.
//
#include "Daemon.h"
#include <iostream>
#include <thread>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
using namespace std;
MONITOR_NAMESPACE_START
volatile int g_signal_received = -1;
Daemon::Daemon(KfLogPtr log):m_logger(log)
{
    m_isRunning = true;
}

Daemon::~Daemon()
{
    stop();
}

bool Daemon::init(DaemonConfig&& config)
{
    m_config = config;
    if (!parseUrl())
    {
        KF_LOG_INFO(m_logger, "Daemon init error");
        exit(-1);
    }
    m_hub.onConnection(std::bind(&Daemon::onConnection,this, std::placeholders::_1, std::placeholders::_2));
    m_hub.onDisconnection(std::bind(&Daemon::onDisconnection,this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,std::placeholders::_4));
    m_hub.onMessage(std::bind(&Daemon::onMessage,this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,std::placeholders::_4));
    m_hub.onPong(std::bind(&Daemon::onPong,this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    m_hub.onPing(std::bind(&Daemon::onPing,this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    startPingThread();
    startReqThread();
    KF_LOG_INFO(m_logger, "Daemon init ok");
    return true;
}

bool Daemon::start()
{
    if(!m_hub.listen(m_url.ip.c_str(), m_url.port))
    {
        KF_LOG_INFO(m_logger, "Daemon start error");
        return false;
    }
    KF_LOG_INFO(m_logger, "Daemon listen on " << m_url.ip << ":" << m_url.port) ;
    return true;
}

void Daemon::stop()
{
    stopPingThread();
    stopReqThread();
}

void Daemon::wait()
{
    KF_LOG_INFO(m_logger, "Daemon poll");
    while( m_isRunning && g_signal_received < 0)
    {
        m_hub.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

//{"messageType":"login","name":"xxx","clientType":"md"}
void Daemon::onMessage(WebSocket<SERVER> *ws, char *data, size_t len, OpCode)
{
    std::string value(data, len);
    KF_LOG_DEBUG(m_logger, "received data,ws:" << ws << ",msg:"<< value);
    Document json;
    json.Parse(value.c_str());
    if(json.HasParseError())
    {
        KF_LOG_ERROR(m_logger, "msg parse error");
        return;
    }
    if(!json.HasMember("messageType") || !json["messageType"].IsString())
    {
        KF_LOG_ERROR(m_logger, "msg format error");
        return;
    }
    //do login
    if (!std::string("login").compare(json["messageType"].GetString()))
    {
        doLogin(json,ws);
    }
}

void Daemon::doLogin(const rapidjson::Document& login, WebSocket<SERVER> *ws)
{
    if (!login["name"].IsString() || !login["clientType"].IsString())
    {
        return;
    }
    std::string name = login["name"].GetString();
    std::string clientType = login["clientType"].GetString();
    {
        std::unique_lock<std::mutex> l(m_clientMutex);
        auto userIter = m_clients.find(ws);
        if( userIter != m_clients.end())
        {
            m_clients[ws].clientInfo.name = name;
            m_clients[ws].clientInfo.type = clientType;
        }
    }
    KF_LOG_DEBUG(m_logger, "user login,name:" << name << ",ws:" << ws);
}

void Daemon::onConnection(WebSocket<SERVER> *ws, HttpRequest)
{
    std::unique_lock<std::mutex> l(m_clientMutex);
    m_clients[ws];
    KF_LOG_DEBUG(m_logger, "user connected,name:none" << ",ws:" << ws);
}

void Daemon::onDisconnection(WebSocket<SERVER> *ws, int code, char *, size_t)
{
    std::unique_lock<std::mutex> l(m_clientMutex);
    auto cur_iter = m_clients.find(ws);
    if(cur_iter != m_clients.end())
    {
        KF_LOG_DEBUG(m_logger, "user disconnect,name:" << cur_iter->second.clientInfo.name << ",ws:" << ws <<",code:" << code);
        push(std::make_shared<RestartReq>(cur_iter->second.clientInfo.name, cur_iter->second.clientInfo.type, m_config.scriptPath));
        m_clients.erase(cur_iter);
    }
}

void Daemon::onPing(WebSocket<SERVER> *, char *, size_t)
{

}

void Daemon::onPong(WebSocket<SERVER> *ws, char *data, size_t len)
{
    std::string value(data, len);
    int64_t pongValue = std::strtoll(value.c_str(),NULL,10);
    {
        std::unique_lock<std::mutex> l(m_clientMutex);
        auto cur_iter = m_clients.find(ws);
        if(cur_iter != m_clients.end())
        {
            cur_iter->second.pongValue = pongValue;
            KF_LOG_DEBUG(m_logger, "user pong,name:" << cur_iter->second.clientInfo.name << ",ws:" << ws << ",pong value:" << pongValue);
        }
    }
}

//url -> 127.0.0.1:8989
bool Daemon::parseUrl()
{
    try
    {
        std::vector<std::string> result;
        //url format is xxx.xxx.xxx:xxx
        boost::split(result, m_config.localHost, boost::is_any_of(":"));
        if (result.size() != 2)
        {
            KF_LOG_INFO(m_logger, "parse daemon local host error,must be xxx.xxx.xxx:xxx,cur host:" << m_config.localHost);
            return false;
        }
        m_url.ip = result[0];
        m_url.port = std::atoi(result[1].c_str());
        KF_LOG_INFO(m_logger,"parse daemon local host,ip:" << m_url.ip << ",port:" << m_url.port);
        return  true;
    }
    catch (std::exception& e)
    {
        KF_LOG_INFO(m_logger, "parse daemon local host,exception:"<< e.what());
    }
    return false;
}

void Daemon::startPingThread()
{
    m_pingThread = std::thread([&]{
        KF_LOG_INFO(m_logger, "start ping thread:" << std::this_thread::get_id());
        while (m_isRunning)
        {
            checkClient();
            std::unique_lock<std::mutex> l(m_pingMutex);
            if(m_isRunning)
            {
                m_pingCond.wait_for(l, std::chrono::seconds(10));
            }
        }
        KF_LOG_INFO(m_logger, "exit ping thread:" << std::this_thread::get_id());
    });
}

void Daemon::checkClient()
{
    std::unique_lock<std::mutex> l(m_clientMutex);
    for (auto clientIter = m_clients.begin(); clientIter != m_clients.end();)
    {
        if (!m_isRunning)
        {
            return;
        }
        //check client timeout
        auto& clientInfo = clientIter->second;
        if(clientInfo.pingValue - clientInfo.pongValue >= 2)
        {
            clientIter->first->close();
            KF_LOG_DEBUG(m_logger, "user:" << clientInfo.clientInfo.name << ",ws:" << clientIter->first << " is timeout,close it");
            push(std::make_shared<RestartReq>(clientInfo.clientInfo.name, clientInfo.clientInfo.type, m_config.scriptPath));
            clientIter = m_clients.erase(clientIter);
            continue;
        }
        //send ping msg
        clientIter->first->ping(std::to_string(++clientInfo.pingValue).c_str());
        KF_LOG_DEBUG(m_logger, "user ping,name:" << clientInfo.clientInfo.name << ",ws:" << clientIter->first << ",ping value:" << clientInfo.pingValue);
        ++clientIter;
    }
}

void Daemon::stopPingThread()
{
    {
        std::unique_lock<std::mutex> l(m_pingMutex);
        m_isRunning = false;
        m_pingCond.notify_all();
    }
    if (m_pingThread.joinable())
    {
        m_pingThread.join();
    }
}

void Daemon::startReqThread()
{
    m_rqThread = std::thread([&]{
        KF_LOG_INFO(m_logger, "start request thread:" << std::this_thread::get_id());
        while (m_isRunning)
        {
            std::unique_lock<std::mutex> l(m_rqMutex);
            while (m_rq.empty() && m_isRunning)
            {
                m_rqCond.wait_for(l, std::chrono::seconds(10));
            }
            if (m_isRunning)
            {
                auto req = pop();
                if (req)
                {
                    req->execute();
                }
            }
        }
        KF_LOG_INFO(m_logger, "exit request thread:" << std::this_thread::get_id());
    });
}

void Daemon::stopReqThread()
{
    {
        std::unique_lock<std::mutex> l(m_rqMutex);
        m_isRunning = false;
        m_rqCond.notify_all();
    }
    if (m_rqThread.joinable())
    {
        m_rqThread.join();
    }
}

void Daemon::push(IRequestPtr req)
{
    std::unique_lock<std::mutex> l(m_rqMutex);
    if(m_rq.size() > 10000)
    {
        m_rq.pop();
    }
    m_rq.push(req);
}

IRequestPtr Daemon::pop()
{
    if(!m_rq.empty())
    {
        auto req = m_rq.front();
        m_rq.pop();
        return  req;
    }
    return IRequestPtr();
}

MONITOR_NAMESPACE_END