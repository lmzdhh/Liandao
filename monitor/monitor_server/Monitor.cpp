//
// Created by wang on 11/5/18.
//
#include "Monitor.h"
#include <signal.h>
#include <memory>
#include <stdio.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
using rapidjson::Document;
using namespace rapidjson;

MONITOR_NAMESPACE_START

volatile int g_signal_received = -1;
Monitor::Monitor():m_isRunning(true)
{
    m_logger = yijinjing::KfLog::getLogger("MonitorServer");
}

Monitor::~Monitor()
{
    release();
}

bool Monitor::init(const std::string& url)
{
    KF_LOG_DEBUG(m_logger, "Monitor init start");
    if(!parseAddress(url))
    {
        return false;
    }
    m_hub.onConnection(std::bind(&Monitor::onConnection,this, std::placeholders::_1, std::placeholders::_2));
    m_hub.onDisconnection(std::bind(&Monitor::onDisconnection,this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,std::placeholders::_4));
    m_hub.onMessage(std::bind(&Monitor::onMessage,this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,std::placeholders::_4));
    m_hub.onPong(std::bind(&Monitor::onPong,this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    m_hub.onPing(std::bind(&Monitor::onPing,this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    startPingThread();
    KF_LOG_DEBUG(m_logger, "Monitor init end");
    return true;
}

bool Monitor::start()
{
    if(!m_hub.listen(m_monitorUrl.ip.c_str(), m_monitorUrl.port))
    {
        KF_LOG_INFO(m_logger, "Monitor start error");
        return false;
    }
    KF_LOG_INFO(m_logger, "Monitor start");
    return true;
}

void Monitor::stop()
{
    release();
    KF_LOG_INFO(m_logger, "Monitor stop");
}

void Monitor::wait()
{
    KF_LOG_INFO(m_logger, "Monitor wait");
    while(g_signal_received < 0)
    {
        m_hub.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void Monitor::release()
{
    //release by order
    stopPingThread();
    g_signal_received = SIGQUIT;
}

void Monitor::onMessage(WebSocket<SERVER> * ws, char * data, size_t len, OpCode)
{
    KF_LOG_DEBUG(m_logger, "received data,msg@"<< data);
    Document json;
    json.Parse(data);
    if(json.HasParseError())
    {
        KF_LOG_ERROR(m_logger, "msg parse error");
        return;
    }
    //do login
    if(json.HasMember("name"))
    {
        if (json["name"].IsString())
        {
            std::unique_lock<std::mutex> l(m_userMutex);
            auto userIter = m_users.find(ws);
            if( userIter != m_users.end())
            {
                m_users[ws].name = json["name"].GetString();
            }
        }
    }
}

void Monitor::onConnection(WebSocket<SERVER> * ws, HttpRequest req)
{
    {
        std::unique_lock<std::mutex> l(m_userMutex);
        //insert user
        m_users[ws];
    }
 }

void Monitor::onDisconnection(WebSocket<SERVER> * ws, int code, char *message, size_t length)
{
    std::unique_lock<std::mutex> l(m_userMutex);
    auto cur_iter = m_users.find(ws);
    if(cur_iter != m_users.end())
    {
        KF_LOG_DEBUG(m_logger, "user disconnect, name@" << cur_iter->second.name << " ws@" << ws <<" code@" << code);
        m_users.erase(cur_iter);
    }
}

void Monitor::onPing(WebSocket<SERVER> *, char *, size_t)
{

}

void Monitor::onPong(WebSocket<SERVER> * ws, char * value, size_t len)
{
    int64_t pongValue = std::atoll(value);
    {
        std::unique_lock<std::mutex> l(m_userMutex);
        auto cur_iter = m_users.find(ws);
        if(cur_iter != m_users.end())
        {
            cur_iter->second.pongValue = pongValue;
            KF_LOG_DEBUG(m_logger, "user pong, name@" << cur_iter->second.name << "ws@" << ws << " pong value@" << pongValue);
        }
    }
}

void Monitor::startPingThread()
{
    m_pingThread = std::thread([&]{
        while (m_isRunning)
        {
            {
                std::unique_lock<std::mutex> l(m_userMutex);
                for (auto& var : m_users)
                {
                    //check client timeout
                    if(var.second.pingValue - var.second.pongValue >= 2)
                    {
                        var.first->close();
                        KF_LOG_DEBUG(m_logger, "user@" << var.second.name << " ws@" << var.first << " is timeout, close it");
                        continue;
                    }
                    //send ping msg
                    var.first->ping(std::to_string(++(var.second.pingValue)).c_str());
                    KF_LOG_DEBUG(m_logger, "user ping, name@" << var.second.name << " ws@" << var.first << " ping value@" << var.second.pingValue);
                    printf("user ping,name@%s,ws@%d,ping@%ld\n",var.second.name.c_str(), var.first, var.second.pingValue);
                }
            }
            {
                std::unique_lock<std::mutex> l(m_waitMutex);
                if(m_isRunning)
                {
                    m_waitCond.wait_for(l, std::chrono::seconds(10));
                }
            }

        }
    });
}

void Monitor::stopPingThread()
{
    {
        std::unique_lock<std::mutex> l(m_waitMutex);
        m_isRunning = false;
        m_waitCond.notify_all();
    }
    if (m_pingThread.joinable())
    {
        m_pingThread.join();
    }
}

bool Monitor::parseAddress(const std::string &monitor_url)
{
    try
    {
        std::vector<std::string> result;
        //url format is xxx://xxx.xxx.xxx:xxx/
        boost::split(result, monitor_url, boost::is_any_of("//:/"));
        if (result.size() != 6)
        {
            KF_LOG_INFO(m_logger, "parse monitor url error, must be xxx://xxx.xxx.xxx:xxx/,url@" << monitor_url);
            return false;
        }
        m_monitorUrl.protocol = result[0] + "://";
        m_monitorUrl.ip = result[3];
        m_monitorUrl.port = std::atoi(result[4].c_str());
        m_monitorUrl.path = result[5];
        KF_LOG_INFO(m_logger, "parseAddress, protocol@" << m_monitorUrl.protocol << \
                              ",ip@" << m_monitorUrl.ip << \
                              ",port@" << m_monitorUrl.port << \
                              ",path@" << m_monitorUrl.path);
        return  true;
    }
    catch (std::exception& e)
    {
        KF_LOG_INFO(m_logger, "parseAddress, exception:"<< e.what());
    }
    return false;
}
MONITOR_NAMESPACE_END
USING_MONITOR_NAMESPACE
int main()
{
    Monitor m;
    m.start();
    m.wait();
    return 0;
}