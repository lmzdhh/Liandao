//
// Created by wang on 11/10/18.
//
#ifndef KUNGFU_WEBSOCKETCLIENT_H
#define KUNGFU_WEBSOCKETCLIENT_H
#include "YJJ_DECLARE.h"
#include "MonitorClient.h"
#include <thread>
#include "../common/MONITOR_DECLARE.h"
#include <mutex>
#include <queue>
#include <Poco/URI.h>
struct lws_context;
struct lws;
MONITOR_NAMESPACE_START
class wsclient :public MonitorClient
{
public:
    wsclient();

    virtual ~wsclient();
public:
    virtual void setCallback(MonitorClientSpiPtr) override;
    virtual void init(KfLogPtr) override;
    //url format is xxx://xxx.xxx.xxx:xxx/
    virtual bool connect(const std::string& url) override;
    virtual bool login(const std::string& name, const std::string& type) override;
    virtual void logout() override;
    virtual void sendmsg(const std::string& json) override;
public:
    void onMessage(struct lws*, char*, size_t);
    void onClose(struct lws*);
    void onWrite(struct lws*);
private:
    void release();
    bool createWsContext();
    bool createConnection();
    bool parseAddress(const std::string& monitor_url);
    void lwsWrite(std::string&& msg);
    void lwsEventLoop();
private:
    void push(const std::string&);
    std::string pop();
private:
    struct lws_context*         m_lwsContext = nullptr;
    struct lws*                 m_lwsConnection = nullptr;
    std::thread                 m_wsThread;
    bool                        m_isRunning = true;
    UrlInfo                     m_monitorUrl;
    KfLogPtr                    m_logger;
private:
    std::mutex                  m_spiMutex;
    MonitorClientSpiPtr         m_spi = nullptr;
private:
    std::mutex                  m_queueMutex;
    std::queue<std::string>     m_msgQueue;
};
MONITOR_NAMESPACE_END
#endif //KUNGFU_WEBSOCKETCLIENT_H
