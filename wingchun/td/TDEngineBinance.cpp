#include "TDEngineBinance.h"
#include "longfist/ctp.h"
#include "longfist/LFUtils.h"
#include "TypeConvert.hpp"
#include <boost/algorithm/string.hpp>

USING_WC_NAMESPACE

TDEngineBinance::TDEngineBinance(): ITDEngine(SOURCE_BINANCE)
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
    account_units.resize(account_num);
    KF_LOG_INFO(logger, "resize_accounts");
}

TradeAccount TDEngineBinance::load_account(int idx, const json& j_config)
{
    KF_LOG_INFO(logger, "load_account");
    // internal load
    string api_key = j_config["APIKey"].get<string>();
    string secret_key = j_config["SecretKey"].get<string>();

    KF_LOG_INFO(logger, "api_key:" + api_key);
    KF_LOG_INFO(logger, "secret_key:" + secret_key);
    AccountUnitBinance& unit = account_units[idx];
    unit.api_key = api_key;
    unit.secret_key = secret_key;

    stringstream ss;
    string str;
    ss<<idx;
    ss>>str;
    KF_LOG_INFO(logger, "account_units idx:" + str);
    // set up
    TradeAccount account = {};
    //partly copy this fields
    strncpy(account.UserID, api_key.c_str(), 16);
    strncpy(account.Password, secret_key.c_str(), 21);
    return account;
}

void TDEngineBinance::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "connect");
    for (int idx = 0; idx < account_units.size(); idx ++)
    {
        AccountUnitBinance& unit = account_units[idx];
        BinaCPP::init( unit.api_key , unit.secret_key );

        Json::Value result;
            long recvWindow = 10000;
        KF_LOG_INFO(logger, "try connect unit.api_key:" + unit.api_key);
        if (!unit.logged_in)
        {
            // User Balance
            BinaCPP::get_account( recvWindow , result );
            for ( int i  = 0 ; i < result["balances"].size() ; i++ ) {
                string symbol = result["balances"][i]["asset"].asString();
                KF_LOG_INFO(logger,  symbol);
                KF_LOG_INFO(logger,  result["balances"][i]["free"].asString().c_str());
                KF_LOG_INFO(logger,  result["balances"][i]["locked"].asString().c_str());
            }
            unit.logged_in = true;
        }
    }

}

void TDEngineBinance::login(long timeout_nsec)
{
    KF_LOG_INFO(logger, "login");
    connect(timeout_nsec);
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
    for (auto& unit: account_units)
    {
        if (!unit.logged_in)
            return false;
    }
    return true;
}

bool TDEngineBinance::is_connected() const
{
    KF_LOG_INFO(logger, "is_connected");
    return is_logged_in();
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


#define GBK2UTF8(msg) kungfu::yijinjing::gbk2utf8(string(msg))

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



