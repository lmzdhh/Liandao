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



std::string TDEngineBinance::GetSide(const LfDirectionType& input) {
    if (LF_CHAR_Buy == input) {
        return "BUY";
    } else if (LF_CHAR_Sell == input) {
        return "SELL";
    } else {
        return "UNKNOWN";
    }
}

std::string TDEngineBinance::GetType(const LfOrderPriceTypeType& input) {
    if (LF_CHAR_LimitPrice == input) {
        return "LIMIT";
    } else if (LF_CHAR_AnyPrice == input) {
        return "MARKET";
    } else {
        return "UNKNOWN";
    }
}


std::string TDEngineBinance::GetTimeInForce(const LfTimeConditionType& input) {
    if (LF_CHAR_IOC == input) {
      return "IOC";
    } else if (LF_CHAR_GTC == input) {
      return "GTC";
    } else if (LF_CHAR_FOK == input) {
      return "FOK";
    } else {
      return "UNKNOWN";
    }
}

std::string TDEngineBinance::GetInputOrderData(const LFInputOrderField* order, int recvWindow) {
    std::stringstream ss;
    ss << "symbol=" << order->InstrumentID << "&";
    ss << "side=" << GetSide(order->Direction) << "&";
    ss << "type=" << GetType(order->OrderPriceType) << "&";
    ss << "timeInForce=" << GetTimeInForce(order->TimeCondition) << "&";
    ss << "quantity=" << order->Volume << "&";
    ss << "price=" << order->LimitPrice << "&";
    ss << "recvWindow="<< recvWindow <<"&timestamp=" << yijinjing::getNanoTime();
    return ss.str();
}

/**
 * req functions
 */
void TDEngineBinance::req_investor_position(const LFQryPositionField* data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "req_investor_position");
    /*-------------------------------------------------------------
	// Example: Get all account orders; active, canceled, or filled.
	BinaCPP::get_allOrders( "BNBETH", 0,0, recvWindow, result ) 
	cout << result << endl;
	*/
    
    struct CThostFtdcQryInvestorPositionField req = parseTo(*data);
    KF_LOG_DEBUG(logger, "[req_pos]" << " (Bid)" << req.BrokerID
                                     << " (Iid)" << req.InvestorID
                                     << " (Tid)" << req.InstrumentID);
    send_writer->write_frame(&req, sizeof(CThostFtdcQryInvestorPositionField), source_id, MSG_TYPE_LF_QRY_POS_BINANCE, 1, requestId);
    
    CThostFtdcInvestorPositionField pInvestorPosition;
    strncpy(pInvestorPosition.InstrumentID, data->InstrumentID, 31);

    CThostFtdcRspInfoField pRspInfo;
    pRspInfo.ErrorID = 0;
    std::string msg = "";
    strncpy(pRspInfo.ErrorMsg, msg.c_str(), 1);
    OnRspQryInvestorPosition(&pInvestorPosition, &pRspInfo,
                                     requestId, 1/*ISLAST*/);
}

void TDEngineBinance::req_qry_account(const LFQryAccountField *data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "req_qry_account");
/*
	// -------------------------------------------------------------
	// Example: Get Account 
	BinaCPP::get_account( recvWindow , result );
	cout << result << endl;
	//
    */
   /*
    struct CThostFtdcQryTradingAccountField req = parseTo(*data);
    KF_LOG_DEBUG(logger, "[req_account]" << " (Bid)" << req.BrokerID
                                         << " (Iid)" << req.InvestorID);

    if (account_units[account_index].api->ReqQryTradingAccount(&req, requestId))
    {
        KF_LOG_ERROR(logger, "[request] account info failed!" << " (rid)" << requestId
                                                              << " (idx)" << account_index);
    }
    send_writer->write_frame(&req, sizeof(CThostFtdcQryTradingAccountField), source_id, MSG_TYPE_LF_QRY_ACCOUNT_CTP, 1, requestId);
    */
}

void TDEngineBinance::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitBinance& unit = account_units[account_index];
    BinaCPP::init( unit.api_key, unit.secret_key );
    Json::Value result;

    KF_LOG_DEBUG(logger, "[req_order_insert]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Tid)" << data->InstrumentID
                                              << " (OrderRef)" << data->OrderRef);
    long recvWindow = 10000;
    double stopPrice = 0;
    double icebergQty = 0;
    
    KF_LOG_INFO(logger, "[req_order_insert]" << " (Volume)" << data->Volume);
    KF_LOG_INFO(logger, "[req_order_insert]" << " (data->Volume*1.0/scale_offset)" << data->Volume*1.0/scale_offset);
    KF_LOG_INFO(logger, "[req_order_insert]" << " (LimitPrice)" << data->LimitPrice);
    KF_LOG_INFO(logger, "[req_order_insert]" << " (LimitPrice*1.0/scale_offset)" << data->LimitPrice*1.0/scale_offset);
    BinaCPP::send_order( data->InstrumentID, GetSide(data->Direction).c_str(), GetType(data->OrderPriceType).c_str(), 
        GetTimeInForce(LF_CHAR_GTC).c_str(), data->Volume*1.0/scale_offset, data->LimitPrice*1.0/scale_offset, data->OrderRef, 
        stopPrice, icebergQty, recvWindow, result);
    //record 
    struct CThostFtdcInputOrderField req = parseTo(*data);
    //strncpy(req.UserID, unit.api_key.c_str(), 16);
    
    strcpy(req.ExchangeID, "binance");
    req.RequestID = requestId;
    send_writer->write_frame(&req, sizeof(CThostFtdcInputOrderField), source_id, MSG_TYPE_LF_ORDER_BINANCE, 1/*ISLAST*/, requestId);

    //sent, parse the response result
    KF_LOG_INFO(logger, "[req_order_insert]" << " (result)" << result);
    
    CThostFtdcRspInfoField pRspInfo;
    if(result["code"] != Json::nullValue) {
        KF_LOG_ERROR(logger, "[request] order insert failed!" << " (rid)" << requestId << " (code)" << result["code"].asString());
        int errorId = result["code"].asInt();
        pRspInfo.ErrorID = errorId;
        std::string errorMsg = (result["msg"] == Json::nullValue) ? "" : result["msg"].asString();
        strncpy(pRspInfo.ErrorMsg, errorMsg.c_str(), 80);
    } else {
        pRspInfo.ErrorID = 0;
        std::string msg = "";
        strncpy(pRspInfo.ErrorMsg, msg.c_str(), 1);
    }
    OnRspOrderInsert(&req, &pRspInfo, requestId, 1);

    //paser the trade info in the response result
    if(result["code"] == Json::nullValue) {
        if(result["fills"] == Json::nullValue) {
            /*
            //type1
            send_order result:{
                    "clientOrderId" : "8000000",
                    "executedQty" : "0.00000000",
                    "orderId" : 55148160,
                    "origQty" : "10000.00000000",
                    "price" : "0.00000100",
                    "side" : "BUY",
                    "status" : "NEW",
                    "symbol" : "TRXBTC",
                    "timeInForce" : "GTC",
                    "transactTime" : 1527638682688,
                    "type" : "LIMIT"
            }
            //type2
            {
                "symbol": "BTCUSDT",
                "orderId": 28,
                "clientOrderId": "6gCrw2kRUAF9CvJDGP16IP",
                "transactTime": 1507725176595
            }*/
            CThostFtdcOrderField pOrder;
            strcpy(pOrder.ExchangeID, "binance");
            strncpy(pOrder.UserID, unit.api_key.c_str(), 16);
            strncpy(pOrder.InstrumentID, result["symbol"].asString().c_str(), 31);
            pOrder.Direction = data->Direction;
            strncpy(pOrder.OrderRef, result["clientOrderId"].asString().c_str(), 13);
            pOrder.VolumeTraded = atof(result["executedQty"].asString().c_str()) * scale_offset;
            pOrder.VolumeTotalOriginal = atof(result["origQty"].asString().c_str()) * scale_offset;
            pOrder.LimitPrice = atof(result["price"].asString().c_str()) * scale_offset;
            pOrder.RequestID = requestId;
            OnRtnOrder(&pOrder);
        }else{
            CThostFtdcOrderField pOrder;
            strcpy(pOrder.ExchangeID, "binance");
            strncpy(pOrder.UserID, unit.api_key.c_str(), 16);
            strncpy(pOrder.InstrumentID,result["symbol"].asString().c_str(), 31);
            pOrder.Direction = data->Direction;
            strncpy(pOrder.OrderRef, result["clientOrderId"].asString().c_str(), 13);
            pOrder.VolumeTotalOriginal = atof(result["origQty"].asString().c_str()) * scale_offset;
            pOrder.RequestID = requestId;
            int fills_size = result["fills"].size();
            KF_LOG_INFO(logger, "[req_order_insert]" << " result[fills] exist. (result)" << result);
            for(int i = 0; i < fills_size; ++i)
            {
                pOrder.VolumeTraded = atof(result["fills"][i]["qty"].asString().c_str()) * scale_offset;
                pOrder.LimitPrice = atof(result["fills"][i]["price"].asString().c_str()) * scale_offset;
                KF_LOG_INFO(logger, "[req_order_insert]" << " result[fills] exist. (OnRtnOrder_i)" << i);
                OnRtnOrder(&pOrder);
            }
            /*
            //type3
            {
                "symbol": "BTCUSDT",
                "orderId": 28,
                "clientOrderId": "6gCrw2kRUAF9CvJDGP16IP",
                "transactTime": 1507725176595,
                "price": "0.00000000",
                "origQty": "10.00000000",
                "executedQty": "10.00000000",
                "status": "FILLED",
                "timeInForce": "GTC",
                "type": "MARKET",
                "side": "SELL",
                "fills": [
                    {
                    "price": "4000.00000000",
                    "qty": "1.00000000",
                    "commission": "4.00000000",
                    "commissionAsset": "USDT"
                    },
                    {
                    "price": "3999.00000000",
                    "qty": "5.00000000",
                    "commission": "19.99500000",
                    "commissionAsset": "USDT"
                    },
                    {
                    "price": "3998.00000000",
                    "qty": "2.00000000",
                    "commission": "7.99600000",
                    "commissionAsset": "USDT"
                    },
                    {
                    "price": "3997.00000000",
                    "qty": "1.00000000",
                    "commission": "3.99700000",
                    "commissionAsset": "USDT"
                    },
                    {
                    "price": "3995.00000000",
                    "qty": "1.00000000",
                    "commission": "3.99500000",
                    "commissionAsset": "USDT"
                    }
                ]
                }
            */
        }
    } 
    
}

void TDEngineBinance::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time)
{
    struct CThostFtdcInputOrderActionField req = parseTo(*data);
    strcpy(req.ExchangeID, "binance");
    req.OrderActionRef = local_id ++;
    req.RequestID = requestId;
    KF_LOG_DEBUG(logger, "[req_order_action]" << " (rid)" << requestId
                                              << " (Iid)" << req.InvestorID
                                              << " (OrderRef)" << req.OrderRef
                                              << " (OrderActionRef)" << req.OrderActionRef);


    AccountUnitBinance& unit = account_units[account_index];
    BinaCPP::init( unit.api_key, unit.secret_key );
    Json::Value result;
    long recvWindow = 10000;
    strncpy(req.UserID, unit.api_key.c_str(), 16);
	BinaCPP::cancel_order(data->InstrumentID, 0, req.OrderRef,"", recvWindow, result);
    KF_LOG_INFO(logger, "[req_order_action]" << " (result)" << result);
    send_writer->write_frame(&req, sizeof(CThostFtdcInputOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BINANCE, 1, requestId);
    CThostFtdcRspInfoField pRspInfo;
    if(result["code"] != Json::nullValue) {
        KF_LOG_ERROR(logger, "[req_order_action] order action failed!" << " (rid)" << requestId);
        int errorId = result["code"].asInt();
        pRspInfo.ErrorID = errorId;
        std::string errorMsg = (result["msg"] == Json::nullValue) ? "" : result["msg"].asString();
        strncpy(pRspInfo.ErrorMsg, errorMsg.c_str(), 80);
    } else {
        KF_LOG_INFO(logger, "[req_order_action] order action success!" << " (rid)" << requestId);
        pRspInfo.ErrorID = 0;
        std::string msg = "";
        strncpy(pRspInfo.ErrorMsg, msg.c_str(), 1);
    }
    CThostFtdcInputOrderActionField pInputOrderAction;
    strcpy(pInputOrderAction.ExchangeID, "binance");
    strncpy(pInputOrderAction.UserID, unit.api_key.c_str(), 16);
    strncpy(pInputOrderAction.InstrumentID, data->InstrumentID, 31);
    strncpy(pInputOrderAction.OrderRef, data->OrderRef, 21);
    pInputOrderAction.OrderActionRef = req.OrderActionRef;
    pInputOrderAction.RequestID = data->RequestID;
    pInputOrderAction.LimitPrice = data->LimitPrice;
    pInputOrderAction.VolumeChange = data->VolumeChange;
    OnRspOrderAction(&pInputOrderAction, &pRspInfo, requestId, 1);
}

void TDEngineBinance::OnRspOrderInsert(CThostFtdcInputOrderField *pInputOrder, CThostFtdcRspInfoField *pRspInfo,
                                    int nRequestID, bool bIsLast)
{
    int errorId = (pRspInfo == nullptr) ? 0 : pRspInfo->ErrorID;
    const char* errorMsg = (pRspInfo == nullptr) ? nullptr : EngineUtil::gbkErrorMsg2utf8(pRspInfo->ErrorMsg);
    auto data = parseFrom(*pInputOrder);
    on_rsp_order_insert(&data, nRequestID, errorId, errorMsg);
    raw_writer->write_error_frame(pInputOrder, sizeof(CThostFtdcInputOrderField), source_id, MSG_TYPE_LF_ORDER_BINANCE, bIsLast, nRequestID, errorId, errorMsg);
}

void TDEngineBinance::OnRspOrderAction(CThostFtdcInputOrderActionField *pInputOrderAction,
                                   CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    int errorId = (pRspInfo == nullptr) ? 0 : pRspInfo->ErrorID;
    const char* errorMsg = (pRspInfo == nullptr) ? nullptr : EngineUtil::gbkErrorMsg2utf8(pRspInfo->ErrorMsg);
    auto data = parseFrom(*pInputOrderAction);
    on_rsp_order_action(&data, nRequestID, errorId, errorMsg);
    raw_writer->write_error_frame(pInputOrderAction, sizeof(CThostFtdcInputOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BINANCE, bIsLast, nRequestID, errorId, errorMsg);
}

void TDEngineBinance::OnRtnOrder(CThostFtdcOrderField *pOrder)
{
    KF_LOG_INFO(logger, "[OnRtnOrder]" << " (pOrder)" << pOrder);
    auto rtn_order = parseFrom(*pOrder);
    on_rtn_order(&rtn_order);
    raw_writer->write_frame(pOrder, sizeof(CThostFtdcOrderField),
                            source_id, MSG_TYPE_LF_RTN_ORDER_BINANCE,
                            1/*islast*/, (pOrder->RequestID > 0) ? pOrder->RequestID: -1);
}

void TDEngineBinance::OnRspQryInvestorPosition(CThostFtdcInvestorPositionField *pInvestorPosition, CThostFtdcRspInfoField *pRspInfo,
                                     int nRequestID, bool bIsLast)
{
    int errorId = (pRspInfo == nullptr) ? 0 : pRspInfo->ErrorID;
    const char* errorMsg = (pRspInfo == nullptr) ? nullptr : EngineUtil::gbkErrorMsg2utf8(pRspInfo->ErrorMsg);
    CThostFtdcInvestorPositionField emptyCtp = {};
    if (pInvestorPosition == nullptr)
        pInvestorPosition = &emptyCtp;
    auto pos = parseFrom(*pInvestorPosition);
    on_rsp_position(&pos, bIsLast, nRequestID, errorId, errorMsg);
    raw_writer->write_error_frame(pInvestorPosition, sizeof(CThostFtdcInvestorPositionField), source_id, MSG_TYPE_LF_RSP_POS_BINANCE, bIsLast, nRequestID, errorId, errorMsg);
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





