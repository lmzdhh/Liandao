//
// Created by wang on 11/18/18.
//
#include "DaemonWrapper.h"
#include <boost/python.hpp>
#include <pythonrun.h>
#include <memory>
#include "json.hpp"
#include <csignal>
#include "Daemon.h"
using namespace nlohmann;
using namespace boost::python;
using namespace boost;

MONITOR_NAMESPACE_START

static void signal_handler(int signum)
{
    g_signal_received = signum;
}

DaemonWrapper::DaemonWrapper()
{
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGHUP,  signal_handler);
    std::signal(SIGQUIT, signal_handler);
    std::signal(SIGKILL, signal_handler);
    m_logger = yijinjing::KfLog::getLogger("Monitor.Daemon");
    m_daemon = new Daemon(m_logger);

}

DaemonWrapper::~DaemonWrapper()
{
    if(m_daemon)
    {
        delete m_daemon;
        m_daemon = nullptr;
    }
}

bool DaemonWrapper::init(const string &json)
{
    if(m_daemon)
    {
        nlohmann::json jsonConfig = nlohmann::json::parse(json);
        DaemonConfig config;
        config.localHost = jsonConfig["localHost"].get<std::string>();
        config.scriptPath= jsonConfig["scriptPath"].get<std::string>();
        if (m_daemon->init(std::move(config)))
        {
            return true;
        }
    }
    exit(0);
}

bool DaemonWrapper::start()
{
    if (m_daemon)
    {
        if (m_daemon->start())
        {
            return true;
        }
    }
    exit(0);
}

void DaemonWrapper::stop()
{
    if (m_daemon)
    {
        m_daemon->stop();
    }
}

void DaemonWrapper::wait()
{
    if (m_daemon)
    {
        m_daemon->wait();
    }
}

BOOST_PYTHON_MODULE(libmonitordaemon)
{
    class_<DaemonWrapper, std::shared_ptr<DaemonWrapper>>("Monitor")
            .def(init<>())
            .def("init", &DaemonWrapper::init)
            .def("start", &DaemonWrapper::start)
            .def("stop", &DaemonWrapper::stop)
            .def("wait_for_stop", &DaemonWrapper::wait);
}
MONITOR_NAMESPACE_END