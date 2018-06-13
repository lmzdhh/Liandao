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
    KF_LOG_INFO(logger, "[pre_load]");
}

void TDEngineBinance::resize_accounts(int account_num)
{
    account_units.resize(account_num);
    KF_LOG_INFO(logger, "[resize_accounts]");
}

TradeAccount TDEngineBinance::load_account(int idx, const json& j_config)
{
    KF_LOG_INFO(logger, "[load_account]");
    // internal load
    string api_key = j_config["APIKey"].get<string>();
    string secret_key = j_config["SecretKey"].get<string>();
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();
    /*
    //init all of the possible symbol?
    std::string define_symbols = j_config["symbols"].get<string>();
    KF_LOG_INFO(logger, "[load_account] (api_key)" << api_key << " (rest_get_interval_ms)" << rest_get_interval_ms << " (symbols)" << define_symbols);
    std::vector<std::string> fields = split(define_symbols, " ");
    for (size_t n = 0; n < fields.size(); n++)
    {
        
        symbols_pending_orderref.insert(std::pair<std::string, std::vector<std::string>*>(fields[n], new std::vector<std::string>()));
    }
    */
    AccountUnitBinance& unit = account_units[idx];
    unit.api_key = api_key;
    unit.secret_key = secret_key;

    // set up
    TradeAccount account = {};
    //partly copy this fields
    strncpy(account.UserID, api_key.c_str(), 16);
    strncpy(account.Password, secret_key.c_str(), 21);
    return account;
}

void TDEngineBinance::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[connect]");
    for (int idx = 0; idx < account_units.size(); idx ++)
    {
        AccountUnitBinance& unit = account_units[idx];
        BinaCPP::init( unit.api_key , unit.secret_key );

        Json::Value result;
        long recvWindow = 10000;
        KF_LOG_INFO(logger, "[connect] (api_key)" << unit.api_key);
        if (!unit.logged_in)
        {
            // User Balance
            BinaCPP::get_account( recvWindow , result );
            for ( int i  = 0 ; i < result["balances"].size() ; i++ ) {
                string symbol = result["balances"][i]["asset"].asString();
                KF_LOG_INFO(logger,  "[connect] (symbol)" << symbol << " free:" <<  result["balances"][i]["free"].asString().c_str()
                << " locked: " << result["balances"][i]["locked"].asString().c_str());
            }
            unit.logged_in = true;
            BinaCPP::get_openOrders( "TRXBTC", recvWindow, result ) ;
	        KF_LOG_INFO(logger, "[connect] (get_openOrders TRXBTC)" << result);
        }
    }

    KF_LOG_INFO(logger, "[connect] rest_thread start on TDEngineBinance::loop");
    rest_thread = ThreadPtr(new std::thread(boost::bind(&TDEngineBinance::loop, this)));
}

void TDEngineBinance::login(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[login]");
    connect(timeout_nsec);
}

void TDEngineBinance::logout()
{
    KF_LOG_INFO(logger, "[logout]");
}

void TDEngineBinance::release_api()
{
    KF_LOG_INFO(logger, "[release_api]");
}

bool TDEngineBinance::is_logged_in() const
{
    KF_LOG_INFO(logger, "[is_logged_in]");
    for (auto& unit: account_units)
    {
        if (!unit.logged_in)
            return false;
    }
    return true;
}

bool TDEngineBinance::is_connected() const
{
    KF_LOG_INFO(logger, "[is_connected]");
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

 LfDirectionType TDEngineBinance::GetDirection(std::string input) {
    if ("BUY" == input) {
        return LF_CHAR_Buy;
    } else if ("SELL" == input) {
        return LF_CHAR_Sell;
    } else {
        return LF_CHAR_Buy;
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

LfOrderPriceTypeType TDEngineBinance::GetPriceType(std::string input) {
    if ("LIMIT" == input) {
        return LF_CHAR_LimitPrice;
    } else if ("MARKET" == input) {
        return LF_CHAR_AnyPrice;
    } else {
        return '0';
    }
}

std::string TDEngineBinance::GetTimeInForce(const LfTimeConditionType& input) {
    if (LF_CHAR_IOC == input) {
      return "IOC";
    } else if (LF_CHAR_GFD == input) {
      return "GTC";
    } else if (LF_CHAR_FOK == input) {
      return "FOK";
    } else {
      return "UNKNOWN";
    }
}

LfTimeConditionType TDEngineBinance::GetTimeCondition(std::string input) {
    if ("IOC" == input) {
      return LF_CHAR_IOC;
    } else if ("GTC" == input) {
      return LF_CHAR_GFD;
    } else if ("FOK" == input) {
      return LF_CHAR_FOK;
    } else {
      return '0';
    }
}

LfOrderStatusType TDEngineBinance::GetOrderStatus(std::string input) {
    if ("NEW" == input) {
      return LF_CHAR_NotTouched;
    } else if ("PARTIALLY_FILLED" == input) {
      return LF_CHAR_PartTradedNotQueueing;
    } else if ("FILLED" == input) {
      return LF_CHAR_AllTraded;
    } else if ("CANCELED" == input) {
      return LF_CHAR_Canceled;
    } else if ("PENDING_CANCEL" == input) {
      return LF_CHAR_NotTouched;
    } else if ("REJECTED" == input) {
      return LF_CHAR_Error;
    } else if ("EXPIRED" == input) {
      return LF_CHAR_Error;
    } else {
      return LF_CHAR_AllTraded;
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
    KF_LOG_INFO(logger, "[req_investor_position]");
    /*-------------------------------------------------------------
	// Example: Get all account orders; active, canceled, or filled.
	BinaCPP::get_allOrders( "BNBETH", 0,0, recvWindow, result ) 
	cout << result << endl;
	*/

    KF_LOG_DEBUG(logger, "[req_investor_position] (Mock EmptyData)" << " (Bid)" << data->BrokerID
                                     << " (Iid)" << data->InvestorID
                                     << " (Tid)" << data->InstrumentID);
    send_writer->write_frame(data, sizeof(LFQryPositionField), source_id, MSG_TYPE_LF_QRY_POS_BINANCE, 1, requestId);
    std::string msg = "";
    LFRspPositionField pos;
    strncpy(pos.BrokerID, data->BrokerID, 11);
    strncpy(pos.InvestorID, data->InvestorID, 19);
    strncpy(pos.InstrumentID, data->InstrumentID, 31);
    on_rsp_position(&pos, 1, requestId, 0, msg.c_str());
}

void TDEngineBinance::req_qry_account(const LFQryAccountField *data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_qry_account]");
}

void TDEngineBinance::req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitBinance& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_insert]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Tid)" << data->InstrumentID
                                              << " (OrderRef)" << data->OrderRef);
    long recvWindow = 10000;
    double stopPrice = 0;
    double icebergQty = 0;
    Json::Value result;
    //KF_LOG_INFO(logger, "[req_order_insert]" << " (Volume)" << data->Volume);
    //KF_LOG_INFO(logger, "[req_order_insert]" << " (data->Volume*1.0/scale_offset)" << data->Volume*1.0/scale_offset);
    //KF_LOG_INFO(logger, "[req_order_insert]" << " (LimitPrice)" << data->LimitPrice);
    //KF_LOG_INFO(logger, "[req_order_insert]" << " (LimitPrice*1.0/scale_offset)" << data->LimitPrice*1.0/scale_offset);
    //BinaCPP::send_order( "BNBETH", "BUY", "LIMIT", "GTC", 20 , 0.00380000, "",0,0, recvWindow, result );
    //BinaCPP::send_order( "BNBETH", "BUY", "MARKET", "GTC", 20 , 0,   "",0,0, recvWindow, result );
    BinaCPP::init( unit.api_key, unit.secret_key );
    BinaCPP::send_order( data->InstrumentID, GetSide(data->Direction).c_str(), GetType(data->OrderPriceType).c_str(),
        GetTimeInForce(data->TimeCondition).c_str(), data->Volume*1.0/scale_offset, data->LimitPrice*1.0/scale_offset, data->OrderRef, 
        stopPrice, icebergQty, recvWindow, result);

    send_writer->write_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BINANCE, 1/*ISLAST*/, requestId);
    KF_LOG_INFO(logger, "[req_order_insert]" << " (result)" << result);

    int errorId = 0;
    std::string errorMsg = "";
    if(result["code"] != Json::nullValue)
    {
        errorId = result["code"].asInt();
        KF_LOG_ERROR(logger, "[req_order_insert] order insert failed!" << " (rid)" << requestId << " (code)" << errorId);
        errorMsg = (result["msg"] == Json::nullValue) ? "" : result["msg"].asString();
    }
    on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());

    //paser the trade info in the response result
    if(result["code"] == Json::nullValue)
    {
        if(result["fills"] == Json::nullValue)
        {
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
            LFRtnOrderField rtn_order;
            strcpy(rtn_order.ExchangeID, "binance");
            strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
            strncpy(rtn_order.InstrumentID, result["symbol"].asString().c_str(), 31);
            rtn_order.Direction = data->Direction;
            rtn_order.TimeCondition = data->TimeCondition;
            rtn_order.OrderPriceType = data->OrderPriceType;
            strncpy(rtn_order.OrderRef, result["clientOrderId"].asString().c_str(), 13);
            rtn_order.VolumeTraded = stod(result["executedQty"].asString().c_str()) * scale_offset;
            rtn_order.VolumeTotalOriginal = stod(result["origQty"].asString().c_str()) * scale_offset;
            rtn_order.LimitPrice = stod(result["price"].asString().c_str()) * scale_offset;
            rtn_order.RequestID = requestId;
            rtn_order.OrderStatus = GetOrderStatus(result["status"].asString().c_str());

            on_rtn_order(&rtn_order);
            raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                    source_id, MSG_TYPE_LF_RTN_ORDER_BINANCE,
                                    1/*islast*/, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);
        } else {
            LFRtnOrderField rtn_order;
            strcpy(rtn_order.ExchangeID, "binance");
            strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
            strncpy(rtn_order.InstrumentID, result["symbol"].asString().c_str(), 31);
            rtn_order.Direction = data->Direction;
            rtn_order.TimeCondition = data->TimeCondition;
            rtn_order.OrderPriceType = data->OrderPriceType;
            strncpy(rtn_order.OrderRef, result["clientOrderId"].asString().c_str(), 13);
            rtn_order.VolumeTotalOriginal = stod(result["origQty"].asString().c_str()) * scale_offset;
            rtn_order.RequestID = requestId;
            rtn_order.OrderStatus = GetOrderStatus(result["status"].asString().c_str());

            int fills_size = result["fills"].size();
            KF_LOG_INFO(logger, "[req_order_insert]" << " result[fills] exist. (result)" << result);
            for(int i = 0; i < fills_size; ++i)
            {
                rtn_order.VolumeTraded = stod(result["fills"][i]["qty"].asString().c_str()) * scale_offset;
                rtn_order.LimitPrice = stod(result["fills"][i]["price"].asString().c_str()) * scale_offset;

                on_rtn_order(&rtn_order);
                raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                        source_id, MSG_TYPE_LF_RTN_ORDER_BINANCE,
                                        1/*islast*/, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);
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
        //add pending orderref
        std::map<std::string, std::vector<std::string>*>::iterator symbols_pending_orderref_itr;
        symbols_pending_orderref_itr = symbols_pending_orderref.find(data->InstrumentID);
        if(symbols_pending_orderref_itr == symbols_pending_orderref.end())
        {
            symbols_pending_orderref.insert(std::pair<std::string, std::vector<std::string>*>(data->InstrumentID, new std::vector<std::string>()));
        }   
        symbols_pending_orderref[data->InstrumentID]->push_back(data->OrderRef);
    }
}

void TDEngineBinance::req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time)
{
    AccountUnitBinance& unit = account_units[account_index];
    KF_LOG_DEBUG(logger, "[req_order_action]" << " (rid)" << requestId
                                              << " (APIKey)" << unit.api_key
                                              << " (Iid)" << data->InvestorID
                                              << " (OrderRef)" << data->OrderRef);

    Json::Value result;
    long recvWindow = 10000;

    //strcpy(data->ExchangeID, "binance");
    //strncpy(data->UserID, unit.api_key.c_str(), 16);
    BinaCPP::init( unit.api_key, unit.secret_key );
	BinaCPP::cancel_order(data->InstrumentID, 0, data->OrderRef,"", recvWindow, result);
    KF_LOG_INFO(logger, "[req_order_action]" << " (result)" << result);
    send_writer->write_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BINANCE, 1, requestId);

    int errorId = 0;
    std::string errorMsg = "";
    if(result["code"] != Json::nullValue)
    {
        errorId = result["code"].asInt();
        KF_LOG_ERROR(logger, "[req_order_action] order action failed!" << " (rid)" << requestId << " (code)" << errorId);
        errorMsg = (result["msg"] == Json::nullValue) ? "" : result["msg"].asString();
    }
    on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
    raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BINANCE, 1, requestId, errorId, errorMsg.c_str());
}

void TDEngineBinance::GetAndHandleOrderResponse()
{
    //KF_LOG_INFO(logger, "[GetAndHandleOrderResponse]" << " (symbol)" << symbol);
    /*-------------------------------------------------------------
	// Example: Check an order's status
	BinaCPP::get_order( "BNBETH", 12345678, "", recvWindow, result );
	cout << result << endl;	 
	*/
    //every account
    for (int idx = 0; idx < account_units.size(); idx ++)
    {
        AccountUnitBinance& unit = account_units[idx];
        BinaCPP::init( unit.api_key , unit.secret_key );       
        //KF_LOG_INFO(logger, "[GetAndHandleOrderResponse] connect (api_key)" << unit.api_key);
        if (!unit.logged_in)
        {
            continue;
        }
        //every symbol
        std::map<std::string, std::vector<std::string>*>::iterator symbols_pending_orderref_itr;
        for (symbols_pending_orderref_itr = symbols_pending_orderref.begin();
            symbols_pending_orderref_itr != symbols_pending_orderref.end();
                symbols_pending_orderref_itr++)
        {
            std::string symbol = symbols_pending_orderref_itr->first;
            std::vector<std::string>* pendingOrderIds = symbols_pending_orderref_itr->second;
            //every orderRef
            for (int i = 0; i < pendingOrderIds->size(); i++)
            {
                std::string orderRef = pendingOrderIds->at(i);
                Json::Value result;
                long recvWindow = 10000;
                BinaCPP::get_order( symbol.c_str(), 0, pendingOrderIds->at(i).c_str(), recvWindow, result );
                KF_LOG_INFO(logger, "[GetAndHandleOrderResponse] get_order " << " (symbol)" << symbol << " (orderId)" << pendingOrderIds->at(i) 
                << " (result)" << result);
                //parse order status
                if(result["code"] == Json::nullValue)
                { // no error
                    LFRtnOrderField rtn_order;
                    strcpy(rtn_order.ExchangeID, "binance");
                    strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
                    strncpy(rtn_order.InstrumentID, result["symbol"].asString().c_str(), 31);
                    rtn_order.Direction = GetDirection(result["side"].asString());
                    rtn_order.TimeCondition = GetTimeCondition(result["timeInForce"].asString());
                    rtn_order.OrderPriceType = GetPriceType(result["type"].asString());
                    strncpy(rtn_order.OrderRef, result["clientOrderId"].asString().c_str(), 13);
                    rtn_order.VolumeTraded = stod(result["executedQty"].asString().c_str()) * scale_offset;
                    rtn_order.VolumeTotalOriginal = stod(result["origQty"].asString().c_str()) * scale_offset;
                    rtn_order.LimitPrice = stod(result["price"].asString().c_str()) * scale_offset;
                    rtn_order.OrderStatus = GetOrderStatus(result["status"].asString().c_str());

                    on_rtn_order(&rtn_order);
                    raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                            source_id, MSG_TYPE_LF_RTN_ORDER_BINANCE,
                                            1/*islast*/, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);

                    //when order status is finish, dont get its status anymore
                    if (rtn_order.OrderStatus == LF_CHAR_AllTraded || rtn_order.OrderStatus == LF_CHAR_Canceled  || rtn_order.OrderStatus == LF_CHAR_Error )
                    {
                        if (rtn_order.OrderStatus == LF_CHAR_AllTraded)
                        {
                            LFRtnTradeField rtn_trade;
                            strcpy(rtn_trade.ExchangeID, "binance");
                            strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
                            strncpy(rtn_trade.InstrumentID, result["symbol"].asString().c_str(), 31);
                            strncpy(rtn_trade.OrderRef, result["clientOrderId"].asString().c_str(), 13);
                            rtn_trade.Direction = GetDirection(result["side"].asString());
                            rtn_trade.Volume = stod(result["executedQty"].asString().c_str()) * scale_offset;
                            rtn_trade.Price = stod(result["price"].asString().c_str()) * scale_offset;
                            on_rtn_trade(&rtn_trade);
                            raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
                                                    source_id, MSG_TYPE_LF_RTN_TRADE_BINANCE, 1/*islast*/, -1/*invalidRid*/);

                        }
                        pendingOrderIds->erase(pendingOrderIds->begin() + i);
                        i--;
                    }
                    
                    /*
                    {
                        "symbol": "LTCBTC",
                        "orderId": 1,
                        "clientOrderId": "myOrder1",
                        "price": "0.1",
                        "origQty": "1.0",
                        "executedQty": "0.0",
                        "status": "NEW",
                        "timeInForce": "GTC",
                        "type": "LIMIT",
                        "side": "BUY",
                        "stopPrice": "0.0",
                        "icebergQty": "0.0",
                        "time": 1499827319559,
                        "isWorking": true
                    }
                    */
                }
                //end of every orderRef
            }
            
        }
    }
}

void TDEngineBinance::loop()
{
    while(isRunning)
    {
        using namespace std::chrono;
        auto current_ms = duration_cast< milliseconds>(system_clock::now().time_since_epoch()).count();
        if(last_rest_get_ts != 0 && (current_ms - last_rest_get_ts) < rest_get_interval_ms)
        {	
            continue;	
        }

        last_rest_get_ts = current_ms;
        GetAndHandleOrderResponse();
    }
}



std::vector<std::string> TDEngineBinance::split(std::string str, std::string token)
{
    std::vector<std::string>result;
    while (str.size()) {
        size_t index = str.find(token);
        if (index != std::string::npos) {
            result.push_back(str.substr(0, index));
            str = str.substr(index + token.size());
            if (str.size() == 0)result.push_back(str);
        }
        else {
            result.push_back(str);
            str = "";
        }
    }
    return result;
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





