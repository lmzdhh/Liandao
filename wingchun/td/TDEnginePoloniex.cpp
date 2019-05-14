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
	KF_LOG_INFO(logger, "[load_account] (baseUrl_private_point)" << url_public_point);
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
	{
		retry_interval_milliseconds = 1000;
	}

	if (j_config.find("max_retry_times") != j_config.end()) {
		max_retry_times = j_config["max_retry_times"].get<int>();
	}
	else
	{
		max_retry_times = 5;
	}
    KF_LOG_INFO(logger, "[load_account] (max_retry_times)" << max_retry_times);

	if (j_config.find("url_public_point") != j_config.end()) {
		url_public_point = j_config["url_public_point"].get<int>();
	}
	else
	{
		url_public_point = "https://poloniex.com/public";
	}
	KF_LOG_INFO(logger, "[load_account] (url_public_point)" << url_public_point);

    //账户设置
    unit.api_key = api_key;
    unit.secret_key = secret_key;
    unit.baseUrl = baseUrl;
    unit.positionHolder.clear();

    //系统账户信息
    TradeAccount account = {};
    strncpy(account.UserID, api_key.c_str(), 16);
    strncpy(account.Password, secret_key.c_str(), 21);
    //simply for rest api test
    
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
    AccountUnitPoloniex& unit = account_units[account_index];
    KF_LOG_INFO(logger, "[req_investor_position] (InstrumentID) " << data->InstrumentID);
    send_writer->write_frame(data, sizeof(LFQryPositionField), source_id, MSG_TYPE_LF_QRY_POS_POLONIEX, 1, requestId);
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
    string timestamp = to_string(get_timestamp());
    string command = "command=returnBalances&nonce="+timestamp;
    string method = "POST";
	int count = 1;
	KF_LOG_DEBUG(logger, "[getbalance]" );
    cpr::Response r = rest_withAuth(unit, method, command);//获得账户余额消息
    json js;
    while (true)
    {
        if (r.status_code == 200)//获得余额信息的操作成功了
        {
			int ret = get_response_parsed_position(r);
            if (ret==0)
            {
                /*解析response*/
                /*若是解析成功则返回1*/
                break;
            }
            else if(ret==1)
            {
				errorId = 100;
				errorMsg = "get balance response parsed error,it's not an object quit";
				KF_LOG_ERROR(logger, errorId);
				raw_writer->write_error_frame(&pos, sizeof(LFRspPositionField), 
                        source_id, MSG_TYPE_LF_RSP_POS_BITFINEX, 1, requestId, errorId, errorMsg.c_str());
				return;
            }
			else if(ret==2)
			{
				errorId = 200;
				errorMsg = r.text;
				KF_LOG_ERROR(logger, "get balance "<<r.text<<" quit");
				raw_writer->write_error_frame(&pos, sizeof(LFRspPositionField), 
                        source_id, MSG_TYPE_LF_RSP_POS_BITFINEX, 1, requestId, errorId, errorMsg.c_str());
				return;
			}
        }
        else
        {
			if (count > max_retry_times)
			{
				errorMsg = "after several retry,get balance still failed";
				KF_LOG_ERROR(logger, errorMsg);
				errorId = 300;
				raw_writer->write_error_frame(&pos, sizeof(LFRspPositionField), 
                        source_id, MSG_TYPE_LF_RSP_POS_BITFINEX, 1, requestId, errorId, errorMsg.c_str());
			}
            KF_LOG_ERROR(logger, "get balance failed,retry after retry_interval_milliseconds");
			std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_milliseconds));
			KF_LOG_DEBUG(logger, "[getbalance]");
            r = rest_withAuth(unit, method, command);
        }
    }
    bool findSymbolInResult = false;
    //send the filtered position
    int position_count = unit.positionHolder.size();
    for (int i = 0; i < position_count; i++)//逐个将获得的ticker存入pos
    {
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
		errorId = 400;
		errorMsg = "!findSymbolInResult";
        on_rsp_position(&pos, 1, requestId, errorId, errorMsg.c_str());
    }
}

void TDEnginePoloniex::req_qry_account(const LFQryAccountField* data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_qry_account]");
}

void TDEnginePoloniex::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time)
{
    KF_LOG_INFO(logger, "[req_order_insert]");
	send_writer->write_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_POLONIEX, 1, requestId);
	on_rsp_order_insert(data, requestId, 0, "");

	AccountUnitPoloniex& unit = account_units[0];
	int errorId = 0;
	string errorMsg="";
	string currency_pair = unit.coinPairWhiteList.GetValueByKey(string(data->InstrumentID));
	string order_type = get_order_type(data->OrderPriceType);//市价单或限价单
	string order_side = get_order_side(data->Direction);//买或者卖
	std::stringstream ss;
	double rate = data->LimitPrice * 1.0 / scale_offset;
	ss.flush();
	ss << rate;
	string rate_str = ss.str();
	double amount = data->Volume * 1.0 / scale_offset;
	ss.flush();
	ss << amount;
	string amount_str = ss.str();
	string method = "POST";
	string timestamp = to_string(get_timestamp());
	//"command=buy&currencyPair=BTC_ETH&rate=0.01&amount=1&nonce=154264078495300"
	string command = "command=";
	if (data->OrderPriceType == LF_CHAR_LimitPrice)//说明是限价单
	{
		command += order_side +
			"&currencyPair=" + currency_pair +
			"&rate=" + rate_str +
			"&amount=" + amount_str +
			"&nonce=" + timestamp;
	}
	else
	{
		//若是市价单，，暂时还没有办法处理，先出错处理吧
		KF_LOG_ERROR(logger, "[req_order_insert](market order error)");
		errorId = 100;
		errorMsg = "market order error";
		on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
		return;
	}
	OrderInfo order_info;
	order_info.requestId = requestId;
	order_info.timestamp = timestamp;
	order_info.currency_pair = currency_pair;
	cpr::Response r;
	json js;
	r=rest_withAuth(unit, method, command);
	//TODO:发单错误或者异常状况处理

	//
	js = json::parse(r.text);
	if (js.is_object())
	{
		if (js.find("error") != js.end()||js.find("orderNumber")==js.end())//错误回报，或者回报中没有orderNumber（可省略）
		{
			//TODO:出错处理
            KF_LOG_ERROR(logger, "[req_order_insert](insert order error)");
            return;
		}
		order_info.order_number = stod(js["orderNumber"].get<string>());
	}
	//获得订单信息，处理 order_info、rtn_order
	unit.map_new_order.insert(std::make_pair(requestId, order_info));
	LFRtnOrderField rtn_order;//返回order信息
	memset(&rtn_order, 0, sizeof(LFRtnOrderField));
	//以下为必填项
	ss.flush();
	ss << order_info.order_number;
	string order_number_str = ss.str();
	strncpy(rtn_order.BusinessUnit, order_number_str.c_str(), order_number_str.length());
	rtn_order.OrderStatus = LF_CHAR_NotTouched;
	rtn_order.LimitPrice = data->LimitPrice;
	rtn_order.VolumeTotalOriginal = data->Volume;
	rtn_order.VolumeTraded = 0;//刚刚发单，看作没有成交，等后面更新订单状态时再逐渐写入
	rtn_order.VolumeTotal = rtn_order.VolumeTotalOriginal - rtn_order.VolumeTraded;
	//以下为可填项，尽量补全
	strncpy(rtn_order.UserID, data->UserID, 16);
	strncpy(rtn_order.InstrumentID, data->InstrumentID, 31);
	strncpy(rtn_order.ExchangeID, "poloniex", 8);
	rtn_order.RequestID = requestId;
	rtn_order.Direction = data->Direction;
	rtn_order.TimeCondition = data->TimeCondition;
	rtn_order.OrderPriceType = data->OrderPriceType;
	on_rtn_order(&rtn_order);
	//TODO:后续跟踪处理trade
}

void TDEnginePoloniex::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time)
{
	KF_LOG_DEBUG(logger, "[req_order_action]" << " (rid)" << requestId
		<< " (account_index)" << account_index);
	send_writer->write_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_POLONIEX, 1, requestId);
	AccountUnitPoloniex& unit = account_units[0];
	//get order status
	cpr::Response r;
	r=return_order_status(requestId);
	//TODO:解析这个response

	//
	//cancel order
	OrderInfo order_info;
	if (unit.map_new_order.count(requestId))
	{
		order_info = unit.map_new_order[requestId];
	}
	else
	{
		KF_LOG_ERROR(logger, "could not find order by requestId");
		//TODO:出错处理
		return ;
	}
	string method = "POST";
	string timestamp = to_string(get_timestamp());
	string command = "command=cancelOrder&orderNumber=" + to_string(order_info.order_number) +
		"&nonce=" + timestamp;
	r=rest_withAuth(unit, method, command);
	//TODO:出错及异常处理

	//
	//on_rsp_order_action
}

TDEnginePoloniex::TDEnginePoloniex():ITDEngine(SOURCE_POLONIEX)
{
    logger = yijinjing::KfLog::getLogger("TradeEngine.Poloniex");
    KF_LOG_INFO(logger, "[TDEnginePoloniex]");

    mutex_order_and_trade = new std::mutex();
    mutex_response_order_status = new std::mutex();
    mutex_orderaction_waiting_response = new std::mutex();
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
        return "buy";
    }
    else if (type == LF_CHAR_Sell)
    {
        return "sell";
    }
    return "false";
}

int TDEnginePoloniex::get_response_parsed_position(cpr::Response r)
{
    auto js = json::parse(r.text);
    AccountUnitPoloniex& unit = account_units[0];
    PositionSetting ps;
    if (js.is_object())
    {
		if (js.find("error") != js.end())
		{
			return 2;
		}
        std::unordered_map<std::string, std::string>& whitelist=unit.positionWhiteList.GetKeyIsStrategyCoinpairWhiteList();
        std::unordered_map<std::string, std::string>::iterator it;
        for (it = whitelist.begin(); it != whitelist.end(); ++it)
        {
            string exchange_coinpair = it->first;
            ps.ticker = exchange_coinpair;
            ps.amount = stod(js[exchange_coinpair].get<string>());
            if (ps.amount > 0)
                ps.isLong = true;
            else ps.isLong = false;
            unit.positionHolder.push_back(ps);
        }
        return 0;
    }

    return 1;
}

cpr::Response TDEnginePoloniex::rest_withoutAuth(string& method, string& command)
{
    string Timestamp = to_string(get_timestamp());
    string url = url_public_point;
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

cpr::Response TDEnginePoloniex::return_orderbook()
{
	return cpr::Response();
}

cpr::Response TDEnginePoloniex::return_order_status(int requestId)
{
	KF_LOG_INFO(logger, "[return_order_status](rid)"<<requestId);
	cpr::Response r;
	AccountUnitPoloniex& unit = account_units[0];
	OrderInfo order_info;
	if (unit.map_new_order.count(requestId))
	{
		order_info = unit.map_new_order[requestId];
	}
	else
	{
		r.status_code = 404;
		KF_LOG_ERROR(logger, "could not find order by requestId");
		return r;
	}
	string method = "POST";
	string timestamp = to_string(get_timestamp());
	string command = "command=returnOrderStatus&orderNumber=";
	std::stringstream ss;
	ss.flush();
	ss << order_info.order_number;
	string order_number_str = ss.str();
	command += order_number_str +
		"&nonce=" + timestamp;
	r=rest_withAuth(unit, method, command);
	//TODO:出错处理

	//
	return r;
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
