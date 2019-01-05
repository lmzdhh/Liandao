//
// Created by wang on 11/10/18.
//
#ifndef KUNGFU_MONITORCLIENT_H
#define KUNGFU_MONITORCLIENT_H
#include <KfLog.h>
#include <string>
#include <memory>
#include "../common/MONITOR_DECLARE.h"
USING_YJJ_NAMESPACE
MONITOR_NAMESPACE_START

class MonitorClientSpi
{
public:
    virtual void OnMessage(const std::string& ) {}
};
using MonitorClientSpiPtr = MonitorClientSpi*;
class MonitorClient
{
public:
    static std::shared_ptr<MonitorClient> create();
public:
    virtual ~MonitorClient(){}
public:
    virtual void setCallback(MonitorClientSpiPtr) = 0;
    virtual void init(KfLogPtr) = 0;
    //url format is xxx://xxx.xxx.xxx:xxx/
    virtual bool connect(const std::string& url) = 0;
    //{"type":"login","clientType":"md","name":"xxx"}
    virtual bool login(const std::string&) = 0;
    virtual void logout() = 0;
    virtual void sendmsg(const std::string& json) = 0;
protected:
    MonitorClient(){}
};
using  MonitorClientPtr = std::shared_ptr<MonitorClient>;
MONITOR_NAMESPACE_END
#endif //KUNGFU_MONITORCLIENT_H
