#include "TDEnginePoloniex.h"
#include "longfist/ctp.h"
#include "longfist/LFUtils.h"
#include "TypeConvert.hpp"
#include <boost/algorithm/string.hpp>

#include <writer.h>
#include <stringbuffer.h>
#include <document.h>
#include <iostream>
#include <string>
#include <sstream>
#include <map>
#include <stdio.h>
#include <assert.h>
#include <mutex>
#include <chrono>
#include "../../utils/crypto/openssl_util.h"

using cpr::Delete;
using cpr::Url;
using cpr::Body;
using cpr::Header;
using cpr::Parameters;
using cpr::Payload;
using cpr::Timeout;

/*
using rapidjson::StringRef;
using rapidjson::Writer;
using rapidjson::StringBuffer;
using rapidjson::Document;
using rapidjson::SizeType;
using rapidjson::Value;
*/
using std::string;
using std::to_string;
using std::stod;
using std::stoi;
using utils::crypto::hmac_sha256;
using utils::crypto::hmac_sha256_byte;
using utils::crypto::base64_encode;
using utils::crypto::hmac_sha512;
USING_WC_NAMESPACE
void TDEnginePoloniex::init()
{
    ITDEngine::init();
    JournalPair tdRawPair = getTdRawJournalPair(source_id);
    raw_writer = yijinjing::JournalSafeWriter::create(tdRawPair.first, tdRawPair.second, "RAW_" + name());
    KF_LOG_INFO(logger, "[init]");
}

void TDEnginePoloniex::pre_load(const json& j_config)
{
    //占位
    KF_LOG_INFO(logger, "[pre_load]");
}

TradeAccount TDEnginePoloniex::load_account(int idx, const json& j_config)
{
    KF_LOG_INFO(logger, "[load_account]");
    AccountUnitPoloniex& unit = account_units[idx];
    //加载必要参数
    string api_key = j_config["APIKey"].get<string>();
    string secret_key = j_config["SecretKey"].get<string>();
    string baseUrl = j_config["baseUrl"].get<string>();
    //币对白名单设置
    unit.coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
    unit.coinPairWhiteList.Debug_print();
    //持仓白名单设置
    unit.positionWhiteList.ReadWhiteLists(j_config, "positionWhiteLists");
    unit.positionWhiteList.Debug_print();

    //可选配置参数如下设立
    if (j_config.find("retry_interval_milliseconds") != j_config.end()) {
        retry_interval_milliseconds = j_config["retry_interval_milliseconds"].get<int>();
    }
    else
        retry_interval_milliseconds = 1000;//////////////////////////////
    KF_LOG_INFO(logger, "[load_account] (retry_interval_milliseconds)" << retry_interval_milliseconds);

    //账户设置
    unit.api_key = api_key;
    unit.secret_key = secret_key;
    unit.baseUrl = baseUrl;
    unit.positionHolder.clear();
    KF_LOG_INFO(logger, "[load_account] (baseUrl)" << unit.baseUrl);

    //系统账户信息
    TradeAccount account = {};
    strncpy(account.UserID, api_key.c_str(), 16);
    strncpy(account.Password, secret_key.c_str(), 21);
    //simply for rest api test
   /* 
    KF_LOG_DEBUG(logger, "[getbalance](status_code)" << r.status_code <<
        "(response.text)" << r.text <<
        "(response.error.text)" << r.error.message);
        */

    //test ends here
    return account;
}

void TDEnginePoloniex::resize_accounts(int account_num)
{
    //占位
    account_units.resize(account_num);
    KF_LOG_INFO(logger, "[resize_accounts]");
}

void TDEnginePoloniex::connect(long timeout_nsec)
{
    //占位
    KF_LOG_INFO(logger, "[connect]");
    for (auto& unit : account_units)
    {
        //以后可以通过一个需认证的接口访问是否成功来判断是否有正确的密钥对，成功则登入，目前没有需要
        unit.logged_in = true;//maybe we need to add some "if" here
    }
}

void TDEnginePoloniex::login(long timeout_nsec)
{
    //占位
    KF_LOG_INFO(logger, "[login]");
    connect(timeout_nsec);
}

void TDEnginePoloniex::logout()
{
    //占位
    KF_LOG_INFO(logger, "[logout]");
}

void TDEnginePoloniex::release_api()
{
    //占位
    KF_LOG_INFO(logger, "[release_api]");
}

bool TDEnginePoloniex::is_connected() const
{
    KF_LOG_INFO(logger, "[is_connected]");
    return is_logged_in();
}

bool TDEnginePoloniex::is_logged_in() const
{
    //占位
    KF_LOG_INFO(logger, "[is_logged_in]");
    /*for(auto &unit:account_units)  //启用多用户后，此处需修改
    {
        if(!unit.logged_in)
            return false;
    }*/
    return true;
}

void TDEnginePoloniex::req_investor_position(const LFQryPositionField* data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId);
    //TODO;
}

void TDEnginePoloniex::req_qry_account(const LFQryAccountField* data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_qry_account]");
}

void TDEnginePoloniex::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time)
{
    KF_LOG_INFO(logger, "[req_order_insert]");
}

void TDEnginePoloniex::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitPoloniex& unit = account_units[0];
    KF_LOG_DEBUG(logger, "[req_order_action]" << " (rid)" << requestId
        << " (APIKey)" << unit.api_key
        << " (Iid)" << data->InvestorID
        << " (OrderRef)" << data->OrderRef
        << " (KfOrderID)" << data->KfOrderID);
}

TDEnginePoloniex::TDEnginePoloniex():ITDEngine(SOURCE_POLONIEX)
{
    logger = yijinjing::KfLog::getLogger("TradeEngine.Poloniex");
    KF_LOG_INFO(logger, "[TDEnginePoloniex]");

    mutex_order_and_trade = new std::mutex();
    //mutex_response_order_status = new std::mutex();
    //mutex_orderaction_waiting_response = new std::mutex();
}

TDEnginePoloniex::~TDEnginePoloniex()
{
    if (mutex_order_and_trade != nullptr) delete mutex_order_and_trade;
    if (mutex_response_order_status != nullptr) delete mutex_response_order_status;
    if (mutex_orderaction_waiting_response != nullptr) delete mutex_orderaction_waiting_response;
}

inline int64_t TDEnginePoloniex::get_timestamp()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

string TDEnginePoloniex::get_order_type(LfOrderPriceTypeType type)
{
    if (type == LF_CHAR_LimitPrice)
    {
        return "LIMIT";
    }
    else if (type == LF_CHAR_AnyPrice)//need check 哪一个order price type对应market类型
    {
        return "MARKET";
    }
    return "false";
}

string TDEnginePoloniex::get_order_side(LfDirectionType type)
{
    if (type == LF_CHAR_Buy)
    {
        return "BUY";
    }
    else if (type == LF_CHAR_Sell)
    {
        return "SELL";
    }
    return "false";
}

cpr::Response TDEnginePoloniex::rest_withoutAuth(string& method, string& command)
{
    string Timestamp = to_string(get_timestamp());
    string url = "https://poloniex.com/public";
    url += "?"+command;
    // command= "command = returnOrderBook & currencyPair = BTC_ETH & depth = 10";
    cpr::Response response;
    if (!strcmp(method.c_str(), "GET"))
    {
        response = cpr::Get(
            Url{ url },
            Timeout{ 10000 }
        );
    }
    else if (!strcmp(method.c_str(), "POST"))
    {
        response = cpr::Post(
            Url{ url },
            Timeout{ 10000 }
        );
    }
    else
    {
        KF_LOG_ERROR(logger, "request method error");
        response.error.message = "request method error";
        response.status_code = 404;
        return response;
    }
    KF_LOG_INFO(logger, "[" << method << "] (url) " << url <<
        " (timestamp) " << Timestamp <<
        " (response.status_code) " << response.status_code <<
        " (response.error.message) " << response.error.message <<
        " (response.text) " << response.text.c_str());
    return response;
}

cpr::Response TDEnginePoloniex::rest_withAuth(AccountUnitPoloniex& unit, string& method, string& command)
{
    string key = unit.api_key;
    string secret = unit.secret_key;
    //url="https://poloniex.com/tradingApi";
    string url = unit.baseUrl;
    //command="command=returnBalances&nonce=154264078495300";
    string body = command;
    string sign=hmac_sha512(secret.c_str(),body.c_str());
    cpr::Response response;
    if (!strcmp(method.c_str(), "GET"))
    {
        response = cpr::Get(
            Url{ url },
            Body{ body },
            Header{
            {"Key", key.c_str()},
            {"Sign",sign.c_str()}
            },
            Timeout{ 10000 }
        );
    }
    else if (!strcmp(method.c_str(), "POST"))
    {
        response = cpr::Post(
            Url{ url },
            Body{ body },
            Header{
            {"Key", key.c_str()},
            {"Sign",sign.c_str()}
            },
            Timeout{ 10000 }
        );
    }
    else
    {
        KF_LOG_ERROR(logger, "request method error");
        response.error.message = "request method error";
        response.status_code = 404;
        return response;
    }
    KF_LOG_INFO(logger, "[" << method << "] (url) " << url <<
        " (command) " << command <<
        //" (key) "<<key<<
        //" (secret) "<<secret<<
        " (sign) " << sign <<
        " (response.status_code) " << response.status_code <<
        " (response.error.message) " << response.error.message <<
        " (response.text) " << response.text.c_str());
    return response;
}

BOOST_PYTHON_MODULE(libpoloniextd)
{
    using namespace boost::python;
    class_<TDEnginePoloniex, boost::shared_ptr<TDEnginePoloniex> >("Engine")
        .def(init<>())
        .def("init", &TDEnginePoloniex::initialize)
        .def("start", &TDEnginePoloniex::start)
        .def("stop", &TDEnginePoloniex::stop)
        .def("logout", &TDEnginePoloniex::logout)
        .def("wait_for_stop", &TDEnginePoloniex::wait_for_stop);
}
