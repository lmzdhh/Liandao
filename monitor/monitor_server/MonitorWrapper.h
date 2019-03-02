//
// Created by wang on 11/11/18.
//
#ifndef KUNGFU_MONITORWRAPPER_H
#define KUNGFU_MONITORWRAPPER_H
#include "Monitor.h"
MONITOR_NAMESPACE_START

class MonitorWrapper
{
public:
    MonitorWrapper();
    ~MonitorWrapper();
public:
    bool init(const string& json);
    bool start();
    void stop();
    void wait();
private:
    Monitor* m_monitor = nullptr;
};

MONITOR_NAMESPACE_END
#endif //KUNGFU_MONITORWRAPPER_H
