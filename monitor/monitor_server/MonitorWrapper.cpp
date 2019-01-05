//
// Created by wang on 11/11/18.
//
#include "MonitorWrapper.h"
#include "Monitor.h"
#include <boost/python.hpp>
#include <pythonrun.h>
#include <memory>
#include <stdio.h>
#include "json.hpp"
using namespace nlohmann;
using namespace boost::python;
using namespace boost;

MONITOR_NAMESPACE_START

static void signal_handler(int signum)
{
    g_signal_received = signum;
}

MonitorWrapper::MonitorWrapper()
{
    m_monitor = new Monitor();
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGHUP,  signal_handler);
    std::signal(SIGQUIT, signal_handler);
    std::signal(SIGKILL, signal_handler);
}

MonitorWrapper::~MonitorWrapper()
{
    if(m_monitor)
    {
        delete m_monitor;
        m_monitor = nullptr;
    }
}

bool MonitorWrapper::init(const string &json)
{
    if (m_monitor)
    {
        nlohmann::json config = nlohmann::json::parse(json);
        std::string url = config["url"].get<std::string>();
        printf("%s\n",url.c_str());
        return m_monitor->init(url);
    }
}

bool MonitorWrapper::start()
{
    if (m_monitor)
    {
        return m_monitor->start();
    }
}

void MonitorWrapper::stop()
{
    if (m_monitor)
    {
        m_monitor->stop();
    }
}

void MonitorWrapper::wait()
{
    if (m_monitor)
    {
        m_monitor->wait();
    }
}

BOOST_PYTHON_MODULE(libmonitorserver)
{
    class_<MonitorWrapper, std::shared_ptr<MonitorWrapper>>("Monitor")
            .def(init<>())
            .def("init", &MonitorWrapper::init)
            .def("start", &MonitorWrapper::start)
            .def("stop", &MonitorWrapper::stop)
            .def("wait_for_stop", &MonitorWrapper::wait);
}
MONITOR_NAMESPACE_END