//
// Created by wang on 11/5/18.
//
#ifndef KUNGFU_MONITOR_H
#define KUNGFU_MONITOR_H
#include "KfLog.h"
#include <map>
#include "uWS.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "../common/MONITOR_DECLARE.h"
USING_YJJ_NAMESPACE
using namespace uWS;
using namespace std;

MONITOR_NAMESPACE_START
extern  volatile int g_signal_received;
struct UserInfo
{
    std::string name{};
    int64_t     pingValue=0;
    int64_t     pongValue=0;
};
using USER_MAP = std::map<WebSocket<SERVER>*,UserInfo>;

class Monitor
{
public:
    Monitor();
    ~Monitor();
public:
    //url format is xxx://xxx.xxx.xxx:xxx/
    bool init(const std::string& url);
    bool start();
    void stop();
    void wait();
public:
    void onMessage(WebSocket<SERVER> *, char *, size_t, OpCode);
    void onConnection(WebSocket<SERVER> *, HttpRequest);
    void onDisconnection(WebSocket<SERVER> *, int code, char *message, size_t length);
    void onPing(WebSocket<SERVER> *, char *, size_t);
    void onPong(WebSocket<SERVER> *, char *, size_t);
private:
    void release();
    void startPingThread();
    void stopPingThread();
    bool parseAddress(const std::string& monitor_url);
private:
    Hub                     m_hub;
    KfLogPtr                m_logger;
    std::thread             m_pingThread;
    std::atomic<bool>       m_isRunning;
    UrlInfo                 m_monitorUrl;
private:
    std::mutex              m_userMutex;
    USER_MAP                m_users;
private:
    std::mutex              m_waitMutex;
    std::condition_variable m_waitCond;
};
MONITOR_NAMESPACE_END
#endif //KUNGFU_MONITOR_H
