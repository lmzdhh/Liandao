#include "TDEngineBinance.h"
#include "longfist/ctp.h"
#include "longfist/LFUtils.h"
#include "TypeConvert.hpp"
#include <boost/algorithm/string.hpp>

USING_WC_NAMESPACE

TDEngineBinance::TDEngineBinance(): ITDEngine(SOURCE_BINANCE),need_settleConfirm(true), need_authenticate(false), curAccountIdx(-1)
{
    logger = yijinjing::KfLog::getLogger("TradeEngine.BINANCE");
    KF_LOG_INFO(logger, "[ATTENTION] default to confirm settlement and no authentication!");
}

void TDEngineBinance::init()
{
    ITDEngine::init();
    JournalPair tdRawPair = getTdRawJournalPair(source_id);
    raw_writer = yijinjing::JournalWriter::create(tdRawPair.first, tdRawPair.second, "RAW_" + name());
}

void TDEngineBinance::pre_load(const json& j_config)
{
    KF_LOG_INFO(logger, "pre_load");
}

void TDEngineBinance::resize_accounts(int account_num)
{
    KF_LOG_INFO(logger, "resize_accounts");
}

TradeAccount TDEngineBinance::load_account(int idx, const json& j_config)
{
    KF_LOG_INFO(logger, "load_account");
}

void TDEngineBinance::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "connect");
}

void TDEngineBinance::login(long timeout_nsec)
{
    KF_LOG_INFO(logger, "login");
}

void TDEngineBinance::logout()
{
    KF_LOG_INFO(logger, "logout");
}

void TDEngineBinance::release_api()
{
    KF_LOG_INFO(logger, "release_api");
}

bool TDEngineBinance::is_logged_in() const
{
    KF_LOG_INFO(logger, "is_logged_in");
    return true;
}

bool TDEngineBinance::is_connected() const
{
    KF_LOG_INFO(logger, "is_connected");
    return true;
}

/**
 * req functions
 */
void TDEngineBinance::req_investor_position(const LFQryPositionField* data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "req_investor_position");
}

void TDEngineBinance::req_qry_account(const LFQryAccountField *data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "req_qry_account");
}

void TDEngineBinance::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time)
{
    KF_LOG_INFO(logger, "req_order_insert");
}

void TDEngineBinance::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time)
{
    KF_LOG_INFO(logger, "req_order_action");
}

/*
 * SPI functions
 */
void TDEngineBinance::OnFrontConnected()
{
    KF_LOG_INFO(logger, "OnFrontConnected");
    account_units[curAccountIdx].connected = true;
}

void TDEngineBinance::OnFrontDisconnected(int nReason)
{
    KF_LOG_INFO(logger, "OnFrontDisconnected");
}

#define GBK2UTF8(msg) kungfu::yijinjing::gbk2utf8(string(msg))

void TDEngineBinance::OnRspAuthenticate(CThostFtdcRspAuthenticateField *pRspAuthenticateField,
                                    CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    KF_LOG_INFO(logger, "OnRspAuthenticate");
}

void TDEngineBinance::OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin, CThostFtdcRspInfoField *pRspInfo,
                                 int nRequestID, bool bIsLast)
{
    KF_LOG_INFO(logger, "OnRspUserLogin");
}

void TDEngineBinance::OnRspSettlementInfoConfirm(CThostFtdcSettlementInfoConfirmField *pSettlementInfoConfirm,
                                             CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    KF_LOG_INFO(logger, "OnRspSettlementInfoConfirm");
}

void TDEngineBinance::OnRspUserLogout(CThostFtdcUserLogoutField *pUserLogout, CThostFtdcRspInfoField *pRspInfo,
                                  int nRequestID, bool bIsLast)
{
   KF_LOG_INFO(logger, "OnRspUserLogout");
}

void TDEngineBinance::OnRspOrderInsert(CThostFtdcInputOrderField *pInputOrder, CThostFtdcRspInfoField *pRspInfo,
                                    int nRequestID, bool bIsLast)
{
    KF_LOG_INFO(logger, "OnRspOrderInsert");
}
void TDEngineBinance::OnRspOrderAction(CThostFtdcInputOrderActionField *pInputOrderAction,
                                   CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
   KF_LOG_INFO(logger, "OnRspOrderAction");
}

void TDEngineBinance::OnRspQryInvestorPosition(CThostFtdcInvestorPositionField *pInvestorPosition, CThostFtdcRspInfoField *pRspInfo,
                                     int nRequestID, bool bIsLast)
{
   KF_LOG_INFO(logger, "OnRspQryInvestorPosition");
}

void TDEngineBinance::OnRtnOrder(CThostFtdcOrderField *pOrder)
{
    KF_LOG_INFO(logger, "OnRtnOrder");
}

void TDEngineBinance::OnRtnTrade(CThostFtdcTradeField *pTrade)
{
    KF_LOG_INFO(logger, "OnRtnTrade");
}

void TDEngineBinance::OnRspQryTradingAccount(CThostFtdcTradingAccountField *pTradingAccount,
                                         CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    KF_LOG_INFO(logger, "OnRspQryTradingAccount");
}

BOOST_PYTHON_MODULE(libbinancetd)
{
    using namespace boost::python;
    class_<TDEngineBinance, boost::shared_ptr<TDEngineBinance> >("Engine")
    .def(init<>())
    .def("init", &TDEngineBinance::initialize)
    .def("start", &TDEngineBinance::start)
    .def("stop", &TDEngineBinance::stop)
    .def("logout", &TDEngineBinance::logout)
    .def("wait_for_stop", &TDEngineBinance::wait_for_stop);
}


