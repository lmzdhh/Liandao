//
// Created by wang on 11/18/18.
//

#ifndef KUNGFU_DEAMON_H
#define KUNGFU_DEAMON_H
#include "DaemonWrapper.h"
#include <thread>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "MONITOR_DECLARE.h"
#include "uWS.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "process/process.h"
#include <queue>
#include <set>
using rapidjson::Document;
using namespace rapidjson;
using namespace uWS;
MONITOR_NAMESPACE_START
extern volatile int g_signal_received;
extern DaemonConfig g_daemon_config;
struct DaemonConfig
{
    std::string ip{};
    int         port{};
    std::string scriptPath{};
    std::set<std::string> whiteList{};
};
struct ClientInfo
{
    std::string name{};
    std::string type{};
};
struct ClientManager
{
    ClientInfo clientInfo;
    int64_t     pingValue = 0;
    int64_t     pongValue = 0;
};
class IRequest
{
public:
    IRequest(const std::string& name, const std::string& type):m_name(name),m_type(type){}
    virtual ~IRequest(){}
    virtual int execute(){}
protected:
    std::string m_name;
    std::string m_type;
};
using IRequestPtr = std::shared_ptr<IRequest>;

class RestartReq:public IRequest
{
private:
    std::vector<std::string> m_args;
    KfLogPtr                 m_logger;
public:
    RestartReq(const std::string& name, const std::string& type, const std::string& script):IRequest(name, type)
    {
        m_logger = yijinjing::KfLog::getLogger("Monitor.Daemon");
        m_args.push_back(script);
        m_args.push_back(m_type + "_" + m_name);
        m_args.push_back(m_type == "td"? "1":"0");
    }
    virtual int execute() override
    {
        try
        {
            if (m_args.size() != 3)
            {
                KF_LOG_INFO(m_logger, "restart error:must 3 args");
                return -1;
            }
            if (g_daemon_config.whiteList.find(m_type + "_" + m_name) == g_daemon_config.whiteList.end())
            {
                KF_LOG_INFO(m_logger, "restart info:" << m_name <<" needn't be restarted");
                return -1;
            }
            KF_LOG_DEBUG(m_logger, "restart command args:bash " << m_args[0]<< " " << m_args[1] << " " << m_args[2]);
            procxx::process restart("bash", m_args[0], m_args[1], m_args[2]);
            procxx::process::limits_t limits;
            limits.cpu_time(30);
            restart.limit(limits);
            restart.exec();
            restart.wait();
            auto ret = restart.code();
            KF_LOG_INFO(m_logger, "restart "<< m_args[0] <<" over,code:"<< ret);
            return ret;
        }
        catch(const std::exception& e)
        {
            KF_LOG_INFO(m_logger, "restart "<< m_args[0] <<" exception:"<< e.what()<<",code:-1");
        }
        return -1;
    }
};
using CLIENT_MANAGER_MAP = std::map<WebSocket<SERVER>*, ClientManager>;
class Daemon
{
public:
    Daemon(KfLogPtr);
    ~Daemon();
public:
    bool init();
    bool start();
    void stop();
    void wait();
public:
    void onMessage(WebSocket<SERVER> *, char *, size_t, OpCode);
    void onConnection(WebSocket<SERVER> *, HttpRequest);
    void onDisconnection(WebSocket<SERVER> *, int code, char *, size_t);
    void onPing(WebSocket<SERVER> *, char *, size_t);
    void onPong(WebSocket<SERVER> *, char *, size_t);
private:
    void doLogin(const rapidjson::Document& login, WebSocket<SERVER> *ws);
private:
    bool parseUrl();
    void startPingThread();
    void stopPingThread();
    void checkClient();
private:
    void startReqThread();
    void stopReqThread();
    void push(IRequestPtr);
    IRequestPtr pop();
private:
    Hub                     m_hub;
    KfLogPtr                m_logger;
    std::atomic<bool>       m_isRunning;
private:
    std::thread             m_pingThread;
    std::mutex              m_pingMutex;
    std::condition_variable m_pingCond;
private:
    CLIENT_MANAGER_MAP      m_clients;
    std::mutex              m_clientMutex;
private:
    std::thread             m_rqThread;
    std::queue<IRequestPtr> m_rq;
    std::mutex              m_rqMutex;
    std::condition_variable m_rqCond;
};
MONITOR_NAMESPACE_END
#endif //KUNGFU_DEAMON_Hd
