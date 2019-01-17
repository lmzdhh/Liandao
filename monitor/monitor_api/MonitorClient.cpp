//
// Created by wang on 11/10/18.
//
#include "MonitorClient.h"
#include "wsclient.h"
MONITOR_NAMESPACE_START

std::shared_ptr<MonitorClient> MonitorClient::create()
{
    return  std::make_shared<wsclient>();
}

MONITOR_NAMESPACE_END