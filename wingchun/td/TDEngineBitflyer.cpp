#include "TDEngineBitflyer.h"
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
USING_WC_NAMESPACE


TDEngineBitflyer::TDEngineBitflyer() : ITDEngine(SOURCE_BITFLYER)
{
    logger = yijinjing::KfLog::getLogger("TradeEngine.Bitflyer");
    KF_LOG_INFO(logger, "[TDEngineBitflyer]");

    mutex_order_and_trade = new std::mutex();
    //mutex_response_order_status = new std::mutex();
    //mutex_orderaction_waiting_response = new std::mutex();
}

TDEngineBitflyer::~TDEngineBitflyer()
{
    if (mutex_order_and_trade != nullptr) delete mutex_order_and_trade;
    if (mutex_response_order_status != nullptr) delete mutex_response_order_status;
    if (mutex_orderaction_waiting_response != nullptr) delete mutex_orderaction_waiting_response;
}


inline int64_t TDEngineBitflyer::get_timestamp()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}


/*
queryString格式 “/v1/”+query
/v1/getchats
/v1/getchats/usa
/v1/getchats/eu
*/
cpr::Response TDEngineBitflyer::rest_withoutAuth(string& method, string& path, string& body)
{
    string Timestamp = to_string(get_timestamp());
    //string method = "GET";
    //string path = "/v1/getchats/usa"; please append the version /v1/
    //string body="";
    /*baseUrl:https://api.bitflyer.com*/
    string url = "https://api.bitflyer.com";
    url += path;
    cpr::Response response;
    if (!strcmp(method.c_str(), "GET"))
    {
        response = cpr::Get(
            Url{ url },
            Body{ body },
            Timeout{ 10000 }
        );
    }
    else if (!strcmp(method.c_str(), "POST"))
    {
        response = cpr::Post(
            Url{ url },
            Body{ body },
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
        " (body) " << body <<
        " (timestamp) " << Timestamp <<
        " (response.status_code) " << response.status_code <<
        " (response.error.message) " << response.error.message <<
        " (response.text) " << response.text.c_str());
    return response;
}

cpr::Response TDEngineBitflyer::rest_withAuth(AccountUnitBitflyer& unit, string& method, string& path, string& body)
{
    string Timestamp = to_string(get_timestamp());
    //string method = "GET";
    //string path = "/v1/getchats/usa"; please append the version /v1/
    //string body="";
    /*baseUrl:https://api.bitflyer.com*/
    string key=unit.api_key;
    string secret=unit.secret_key;
    string url = unit.baseUrl + path;
    string text = Timestamp + method + path + body;
    string sign=hmac_sha256(secret.c_str(), text.c_str());
    cpr::Response response;
    if (!strcmp(method.c_str(), "GET"))
    {
        response = cpr::Get(
            Url{ url },
            Body{ body },
            Header{
            {"ACCESS-KEY", key.c_str()},
            {"ACCESS-TIMESTAMP",Timestamp},
            {"ACCESS-SIGN",sign.c_str()},
            {"Content-Type", "application/json"}
            },
            Timeout{ 10000 }
        );
    }
    else if (!strcmp(method.c_str(), "POST"))
    {
        response = cpr::Post(
            Url{ url },
            Body{ body },
            Header{{"ACCESS-KEY", key},
            {"ACCESS-TIMESTAMP",Timestamp},
            {"ACCESS-SIGN",sign},
            {"Content-Type", "application/json"}
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
        " (body) " << body <<
        " (timestamp) " << Timestamp <<
        " (text) "<<text<<
        //" (key) "<<key<<
        //" (secret) "<<secret<<
        " (sign) "<<sign<<
        " (response.status_code) " << response.status_code <<
        " (response.error.message) " << response.error.message <<
        " (response.text) " << response.text.c_str());
    return response;
}

cpr::Response TDEngineBitflyer::chat()
{
    cpr::Response r;
    string method = "GET";
    string path = "/v1/getchats/usa";
    string body = "";

    r = rest_withoutAuth(method, path, body);
    return r;
}

cpr::Response TDEngineBitflyer::get_order(int requestId, int type)
{
    string method = "GET";
    string path = "/v1/me/getchildorders";
    string body = "";
    cpr::Response r;
    AccountUnitBitflyer& unit = account_units[0];
    if (type == 0) //get order about child order
    {
        map<int, OrderInfo>::iterator it;
        it = unit.map_new_order.find(requestId);
        if (it == unit.map_new_order.end())
        {
            //KF_LOG_ERROR(logger, "we do not find this order's child order id by requestId");
            r.status_code = 200;//need edit,,,,,
            r.error.message = "we do not find this order's child order id by requestId";
            KF_LOG_ERROR(logger, "[get_order]: " << r.error.message);
            return r;
        }
        OrderInfo& stOrderInfo = it->second;
        body = R"({"product_code":")" + stOrderInfo.product_code + R"(",)"
            + R"("count":")" + "100" + R"(",)"
            + R"("child_order_state":")" + "ACTIVE" + R"(",)"
            + R"("child_order_id":")" + stOrderInfo.child_order_acceptance_id
            + R"("})";
        r = rest_withAuth(account_units[0], method, path, body);
    }
    return r;
}

void TDEngineBitflyer::init()
{
    ITDEngine::init();
    JournalPair tdRawPair = getTdRawJournalPair(source_id);
    raw_writer = yijinjing::JournalSafeWriter::create(tdRawPair.first, tdRawPair.second, "RAW_" + name());
    KF_LOG_INFO(logger, "[init]");
}

void TDEngineBitflyer::pre_load(const json& j_config)
{
    //占位
    KF_LOG_INFO(logger, "[pre_load]");
}

void TDEngineBitflyer::resize_accounts(int account_num)
{
    //占位
    account_units.resize(account_num);
    KF_LOG_INFO(logger, "[resize_accounts]");
}

TradeAccount TDEngineBitflyer::load_account(int idx, const json & j_config)
{
    KF_LOG_INFO(logger, "[load_account]");
    AccountUnitBitflyer& unit = account_units[idx];
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
    chat();
    string body = "";
    string path = "/v1/me/getbalance";
    string method = "GET";
    cpr::Response r = rest_withAuth(account_units[0], method, path, body);//获得账户余额消息
    KF_LOG_DEBUG(logger, "[getbalance](status_code)" << r.status_code <<
            "(response.text)" << r.text <<
            "(response.error.text)" << r.error.message);
    //test ends here
    return account;
}

void TDEngineBitflyer::connect(long timeout_nesc)
{
    //占位
    KF_LOG_INFO(logger, "[connect]");
    for(auto& unit:account_units)
    {
        account_units[0].logged_in=true;//maybe we need to add some "if" here
    }
}

void TDEngineBitflyer::login(long timeout_nesc)
{
    //占位
    KF_LOG_INFO(logger, "[login]");
    connect(timeout_nesc);
}

void TDEngineBitflyer::logout()
{
    //占位
    KF_LOG_INFO(logger, "[logout]");
}

void TDEngineBitflyer::release_api()
{
    //占位
    KF_LOG_INFO(logger, "[release_api]");
}

bool TDEngineBitflyer::is_connected() const
{
    KF_LOG_INFO(logger, "[is_connected]");
    return is_logged_in();
}

bool TDEngineBitflyer::is_logged_in() const
{
    //占位
    KF_LOG_INFO(logger, "[is_logged_in]");
    /*for(auto &unit:account_units)
    {
        if(!unit.logged_in)
            return false;
    }*/
    return true;
}

int TDEngineBitflyer::get_response_parsed_position(cpr::Response r)
{
    auto js = json::parse(r.text);
    AccountUnitBitflyer& unit = account_units[0];
    PositionSetting ps;
    /*
    [
        {
            "currency_code": "JPY",
            "amount" : 1024078,
            "available" : 508000
        },
        {
            "currency_code": "BTC",
            "amount" : 10.24,
            "available" : 4.12
        },
        {
            "currency_code": "ETH",
            "amount" : 20.48,
            "available" : 16.38
        }
    ]
    */
    if (js.is_array())//用于判断能否解析
    {
        for (int i = 0; i < js.size(); i++)
        {
            auto object = js[i];
            ps.ticker = object["currency_code"].get<string>();
            ps.amount = object["amount"].get<double>();
            if (ps.amount > 0)
                ps.isLong = true;
            else ps.isLong = false;
            unit.positionHolder.push_back(ps);
        }
        //TODO: maybe we need a debug print
        return 1;
    }
    return 0;
}

void TDEngineBitflyer::req_investor_position(const LFQryPositionField * data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_investor_position] (requestId)" << requestId);

    AccountUnitBitflyer& unit = account_units[account_index];
    KF_LOG_INFO(logger, "[req_investor_position]" << "(InstrumentID) " << data->InstrumentID);
    send_writer->write_frame(data, sizeof(LFQryPositionField), source_id, MSG_TYPE_LF_QRY_POS_BITFINEX, 1, requestId);
    int errorId = 0;
    std::string errorMsg = "";

    LFRspPositionField pos;
    memset(&pos, 0, sizeof(LFRspPositionField));
    strncpy(pos.BrokerID, data->BrokerID, 11);
    strncpy(pos.InvestorID, data->InvestorID, 19);
    strncpy(pos.InstrumentID, data->InstrumentID, 31);
    pos.PosiDirection = LF_CHAR_Long;
    pos.HedgeFlag = LF_CHAR_Speculation;
    pos.Position = 0;
    pos.YdPosition = 0;
    pos.PositionCost = 0;

    /*实现一个函数获得balance信息，一个函数解析并存储到positionHolder里面去*/
    string body = "";
    string path = "/v1/me/getbalance";
    string method = "GET";
    cpr::Response r = rest_withAuth(unit, method, path, body);//获得账户余额消息
    KF_LOG_DEBUG(logger, "[getbalance](status_code)" << r.status_code <<
        "(response.text)" << r.text <<
        "(response.error.text)" << r.error.message);
    json js;
    while (true)
    {
        if (r.status_code == 200)//获得余额信息成功
        {
            if (get_response_parsed_position(r))
            {
                /*解析response*/
                /*若是解析成功则退出*/
                break;
            }
            else
            {
                KF_LOG_ERROR(logger, "get balance response parsed error,quit");
                return ;
            }
        }
        else
        {
            KF_LOG_ERROR(logger, "get balance failed,retry");
            r = rest_withAuth(unit, method, path, body);
            KF_LOG_DEBUG(logger, "[getbalance](status_code)" << r.status_code <<
                "(response.text)" << r.text <<
                "(response.error.text)" << r.error.message);
        }
    }
    bool findSymbolInResult = false;
    //send the filtered position
    int position_count = unit.positionHolder.size();
    for (int i = 0; i < position_count; i++)
    {
        pos.PosiDirection = LF_CHAR_Long;
        strncpy(pos.InstrumentID, unit.positionHolder[i].ticker.c_str(), 31);
        if (unit.positionHolder[i].isLong) {
            pos.PosiDirection = LF_CHAR_Long;
        }
        else {
            pos.PosiDirection = LF_CHAR_Short;
        }
        pos.Position = unit.positionHolder[i].amount;
        on_rsp_position(&pos, i == (position_count - 1), requestId, errorId, errorMsg.c_str());
        findSymbolInResult = true;
    }

    if (!findSymbolInResult)
    {
        KF_LOG_INFO(logger, "[req_investor_position] (!findSymbolInResult) (requestId)" << requestId);
        on_rsp_position(&pos, 1, requestId, errorId, errorMsg.c_str());
    }

    if (errorId != 0)
    {
        raw_writer->write_error_frame(&pos, sizeof(LFRspPositionField), source_id, MSG_TYPE_LF_RSP_POS_BITFINEX, 1, requestId, errorId, errorMsg.c_str());
    }

}

void TDEngineBitflyer::req_qry_account(const LFQryAccountField * data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_qry_account]");
}

string TDEngineBitflyer::get_order_type(LfOrderPriceTypeType type)
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
string TDEngineBitflyer::get_order_side(LfDirectionType type)
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
void TDEngineBitflyer::req_order_insert(const LFInputOrderField * data, int account_index, int requestId, long rcv_time)
{
    KF_LOG_INFO(logger, "[req_order_insert]");
    AccountUnitBitflyer& unit = account_units[account_index];
    /*
    Request

    POST /v1/me/sendchildorder

    Body parameters
    {
        "product_code": "BTC_JPY",
        "child_order_type": "LIMIT",
        "side": "BUY",
        "price": 30000,
        "size": 0.1,
        "minute_to_expire": 10000,
        "time_in_force": "GTC"
    }
    */
    int errorId = 0;
    string errorMsg;
    string method = "POST";
    string path = "/v1/me/sendchildorder";
    string body;
    string product_code = unit.coinPairWhiteList.GetValueByKey(string(data->InstrumentID));//就是ticker
    if (product_code.length() == 0)
    {
        errorId = 200;
        errorMsg = string(data->InstrumentID) + " not in WhiteList, ignored";
        KF_LOG_ERROR(logger, "[req_order_insert]: not in WhiteList, ignore it  " <<
            "(rid)" << requestId <<
            " (errorId)" << errorId <<
            " (errorMsg) " << errorMsg);
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BITFLYER, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    std::stringstream ss;
    string child_order_type = get_order_type(data->OrderPriceType);
    string side = get_order_side(data->Direction);
    double price = data->LimitPrice*1.0 / scale_offset;
    ss.flush();
    ss << price;
    string price_str = ss.str();
    double size = data->Volume*1.0 / scale_offset;
    ss.flush();
    ss << size;
    string size_str = ss.str();
    int minute_to_expire = 10000;
    ss.flush();
    ss << minute_to_expire;
    string minute_to_expire_str = ss.str();
    string time_in_force = "GTC";

    body = R"({"product_code":")" + product_code + R"(",)"
        + R"("child_order_type":")" + child_order_type + R"(",)"
        + R"("side":")" + side + R"(",)"
        + R"("price":")" + price_str + R"(",)"
        + R"("size":")" + size_str + R"(",)"
        + R"("minute_to_expire":")" + minute_to_expire_str + R"(",)"
        + R"("time_in_force":")" + time_in_force + R"("})";
    cpr::Response r;
    r = rest_withAuth(unit, method, path, body);//下单

    if (r.status_code == 200)//成功
    {
        json js;
        try
        {
            js = json::parse(r.text);
        }
        catch (std::exception &e)
        {
            errorId = 100;
            errorMsg = "json has parse_error , response text might not be json style";
            KF_LOG_ERROR(logger, "json parse response.text error:" << e.what() << "(errorMsg)" << errorMsg);
        }
        string child_order_acceptance_id = js["child_order_acceptance_id"].get<string>();

        OrderInfo stOrderInfo;//构建一个map方便日后查订单
        stOrderInfo.requestId = requestId;
        stOrderInfo.child_order_acceptance_id = child_order_acceptance_id;
        stOrderInfo.timestamp = get_timestamp();
        stOrderInfo.product_code = product_code;
        unit.map_new_order.insert(std::make_pair(requestId, stOrderInfo));//插入orderinfo，方便日后寻找订单创建时的相关信息

        LFRtnOrderField rtn_order;//返回order信息
        memset(&rtn_order, 0, sizeof(LFRtnOrderField));
        rtn_order.OrderStatus = LF_CHAR_NotTouched;
        strcpy(rtn_order.ExchangeID, "bitflyer");
        strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
        rtn_order.Direction = data->Direction;
        rtn_order.TimeCondition = LF_CHAR_GTC;
        rtn_order.OrderPriceType = data->OrderPriceType;
        strcpy(rtn_order.InstrumentID, data->InstrumentID);
        rtn_order.VolumeTotalOriginal = data->Volume;
        rtn_order.VolumeTotal = data->Volume;
        rtn_order.RequestID = requestId;
        strncpy(rtn_order.BusinessUnit, child_order_acceptance_id.c_str(), 25);
        on_rtn_order(&rtn_order);
        raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
            source_id, MSG_TYPE_LF_RTN_ORDER_BITFLYER,
            1/*islast*/, (rtn_order.RequestID > 0) ? rtn_order.RequestID : -1);
    }
    else
    {
        errorId = r.status_code;
        errorMsg = r.error.message;
        KF_LOG_ERROR(logger, "req_order_insert error");
    }
    on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());

}

void TDEngineBitflyer::req_order_action(const LFOrderActionField * data, int account_index, int requestId, long rcv_time)
{
    AccountUnitBitflyer& unit=account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_action]" << " (rid)" << requestId
        << " (APIKey)" << unit.api_key
        << " (Iid)" << data->InvestorID
        << " (OrderRef)" << data->OrderRef
        << " (KfOrderID)" << data->KfOrderID);
    send_writer->write_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BITFLYER, 1, requestId);
    int errorId = 0;
    std::string errorMsg = "";
    string method = "POST";
    string path = "/v1/me/cancelchildorder";
    string body;
    std::string product_code = unit.coinPairWhiteList.GetValueByKey(string(data->InstrumentID));
    if (product_code.length() == 0)
    {
        errorId = 200;
        errorMsg = string(data->InstrumentID) + "not in WhiteList, ignore it";
        KF_LOG_ERROR(logger, "[req_order_action]: not in WhiteList , ignore it: (rid)" << requestId << " (errorId)" <<
            errorId << " (errorMsg) " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_OCEANEX, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    KF_LOG_DEBUG(logger, "[req_order_action] (exchange_ticker)" << product_code);
    map<int, OrderInfo>::iterator it;
    it = unit.map_new_order.find(requestId);
    if (it == unit.map_new_order.end())
    {
        KF_LOG_ERROR(logger, "we do not find this order's child order id by requestId");
        errorId = 200;//need edit,,,,,
        errorMsg = "we do not find this order's child order id by requestId";
        KF_LOG_ERROR(logger, "[req_order_action]: " << errorMsg);
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_OCEANEX, 1, requestId, errorId, errorMsg.c_str());
        return;
    }
    string child_order_id = it->second.child_order_acceptance_id;
    /*
    {
        "product_code": "BTC_JPY",
            "child_order_id" : "JOR20150707-055555-022222"
    }

    {
        "product_code": "BTC_JPY",
            "child_order_acceptance_id" : "JRF20150707-033333-099999"
    }
    */
    body = R"({"product_code":")" + product_code + R"(",)"
        + R"("child_order_id":")" + child_order_id + R"("})";
    cpr::Response r = rest_withAuth(unit, method, path, body);
    if (r.status_code == 200)//成功
    {
        errorId = 0;
        errorMsg = "";
        KF_LOG_ERROR(logger, "[req_order_action] cancel_order succeeded (requestId)" << requestId );
    }
    else
    {
        errorId = r.status_code;
        errorMsg = r.error.message;
        KF_LOG_ERROR(logger, "[req_order_action] cancel_order failed (requestId)" << requestId << "(errorId)" << errorId << "(errorMsg)" << errorMsg);  
    }
    on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
    //对于一个发单和撤单，我们需要调用哪些函数返回结果
    //是否是只要实现基本的三个req 函数即可
    //在返回结果时有必要调用锁吗？
}
BOOST_PYTHON_MODULE(libbitflyertd)
{
    using namespace boost::python;
    class_<TDEngineBitflyer, boost::shared_ptr<TDEngineBitflyer> >("Engine")
        .def(init<>())
        .def("init", &TDEngineBitflyer::initialize)
        .def("start", &TDEngineBitflyer::start)
        .def("stop", &TDEngineBitflyer::stop)
        .def("logout", &TDEngineBitflyer::logout)
        .def("wait_for_stop", &TDEngineBitflyer::wait_for_stop);
}

