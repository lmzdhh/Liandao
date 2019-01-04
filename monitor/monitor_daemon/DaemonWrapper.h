//
// Created by wang on 11/18/18.
//

#ifndef KUNGFU_DEAMONWRAPPER_H
#define KUNGFU_DEAMONWRAPPER_H
#include "KfLog.h"
#include "MONITOR_DECLARE.h"
USING_YJJ_NAMESPACE
MONITOR_NAMESPACE_START
class Daemon;
class DaemonWrapper
{
public:
    DaemonWrapper();
    ~DaemonWrapper();
public:
    bool init(const std::string& json);
    bool start();
    void stop();
    void wait();
private:
    Daemon*  m_daemon;
    KfLogPtr m_logger;

};

MONITOR_NAMESPACE_END

#endif //KUNGFU_DEAMONWRAPPER_H
