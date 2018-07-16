#include "TDEngineBinance.h"
#include "longfist/ctp.h"
#include "longfist/LFUtils.h"
#include "TypeConvert.hpp"
#include <boost/algorithm/string.hpp>


#include "../../utils/crypto/openssl_util.h"

#include "TypeConvert.hpp"
#include <boost/algorithm/string.hpp>
#include <cpr/cpr.h>
#include <writer.h>
#include <stringbuffer.h>
using cpr::Delete;
using cpr::Get;
using cpr::Url;
using cpr::Body;
using cpr::Header;
using cpr::Parameters;
using cpr::Payload;
using cpr::Post;
using cpr::Timeout;

using rapidjson::StringRef;
using rapidjson::Writer;
using rapidjson::StringBuffer;
using rapidjson::Document;
using rapidjson::SizeType;
using rapidjson::Value;
using std::string;
using std::to_string;
using std::stod;
using std::stoi;
using utils::crypto::hmac_sha256;
using utils::crypto::hmac_sha256_byte;
using utils::crypto::base64_encode;

USING_WC_NAMESPACE

TDEngineBinance::TDEngineBinance(): ITDEngine(SOURCE_BINANCE)
{
    logger = yijinjing::KfLog::getLogger("TradeEngine.Binance");
    KF_LOG_INFO(logger, "[ATTENTION] default to confirm settlement and no authentication!");

    mutex_order_and_trade = new std::mutex();
}

TDEngineBinance::~TDEngineBinance()
{
    if(mutex_order_and_trade != nullptr) delete mutex_order_and_trade;
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

    AccountUnitBinance& unit = account_units[idx];
    unit.api_key = api_key;
    unit.secret_key = secret_key;

    KF_LOG_INFO(logger, "[load_account] (api_key)" << api_key << " (is whiteListInstrumentIDs exist?)" << (j_config.find("whiteListInstrumentIDs") != j_config.end()));
    BinaCPP::init( unit.api_key , unit.secret_key );

    Json::Value result;
    long recvWindow = 10000;

    if(j_config.find("whiteListInstrumentIDs") != j_config.end()) {
        string whiteListInstrumentIDs = j_config["whiteListInstrumentIDs"].get<string>();
        if(whiteListInstrumentIDs.length() > 0)
        {
            KF_LOG_INFO(logger, "[load_account] (api_key)" << api_key << " (whiteListInstrumentIDs)" << whiteListInstrumentIDs);
            unit.whiteListInstrumentIDs = split(whiteListInstrumentIDs, ",");
            if(unit.whiteListInstrumentIDs.size() > 0)
            {
                for(int i=0; i < unit.whiteListInstrumentIDs.size(); i++)
                {
                    KF_LOG_INFO(logger, "[load_account] (api_key)" << api_key << " (cancel_all_orders of instrumentID)" << unit.whiteListInstrumentIDs[i]);
                    Document d;
                    get_open_orders(unit, unit.whiteListInstrumentIDs[i].c_str(), d);
                    KF_LOG_INFO(logger, "[load_account] print get_open_orders");
                    printResponse(d);

                    if(d.IsArray()) { // expected success response is array
                        size_t len = d.Size();
                        KF_LOG_INFO(logger, "[get_open_orders] (length)" << len);
                        for (int i = 0; i < len; i++) {
                            if(d[i].IsObject() && d[i].HasMember("symbol") && d[i].HasMember("clientOrderId"))
                            {
                                if(d[i]["symbol"].IsString() && d[i]["clientOrderId"].IsString())
                                {
                                    std::string symbol = d[i]["symbol"].GetString();
                                    std::string orderRef = d[i]["clientOrderId"].GetString();

                                    BinaCPP::cancel_order(symbol.c_str(), 0, orderRef.c_str(),"", recvWindow, result);

                                    KF_LOG_INFO(logger, "[load_account] cancel_order" << " (result)" << result);
                                    int errorId = 0;
                                    std::string errorMsg = "";
                                    if(result["code"] != Json::nullValue)
                                    {
                                        errorId = result["code"].asInt();
                                        errorMsg = (result["msg"] == Json::nullValue) ? "" : result["msg"].asString();
                                        KF_LOG_ERROR(logger, "[load_account] cancel_order failed! (rid)  -1 (errorId)" << errorId << " (errorMsg) " << errorMsg);
                                    }
//                                    if(errorId != 0)
//                                    {
//                                        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
//                                        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BINANCE, 1, requestId, errorId, errorMsg.c_str());
//                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
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
            KF_LOG_INFO(logger, "[connect] get_account " << " (result)" << result);
            for ( int i  = 0 ; i < result["balances"].size() ; i++ ) {
                string symbol = result["balances"][i]["asset"].asString();
                KF_LOG_INFO(logger,  "[connect] (symbol)" << symbol << " free:" <<  result["balances"][i]["free"].asString().c_str()
                << " locked: " << result["balances"][i]["locked"].asString().c_str());
            }
            unit.logged_in = true;
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
      return LF_CHAR_NotTouched;
    }
}

/**
 * req functions
 */
void TDEngineBinance::req_investor_position(const LFQryPositionField* data, int account_index, int requestId)
{
    KF_LOG_INFO(logger, "[req_investor_position]");

    AccountUnitBinance& unit = account_units[account_index];
    BinaCPP::init( unit.api_key , unit.secret_key );
    Json::Value result;
    long recvWindow = 10000;
    KF_LOG_INFO(logger, "[req_investor_position] (api_key)" << unit.api_key);

    // User Balance
    BinaCPP::get_account( recvWindow , result );
    int errorId = 0;
    std::string errorMsg = "";
    if(result["code"] != Json::nullValue)
    {
        errorId = result["code"].asInt();
        KF_LOG_ERROR(logger, "[req_investor_position] failed!" << " (rid)" << requestId << " (code)" << errorId);
        errorMsg = (result["msg"] == Json::nullValue) ? "" : result["msg"].asString();
    }
    send_writer->write_frame(data, sizeof(LFQryPositionField), source_id, MSG_TYPE_LF_QRY_POS_BINANCE, 1, requestId);

    LFRspPositionField pos;
    memset(&pos, 0, sizeof(LFRspPositionField));
    strncpy(pos.BrokerID, data->BrokerID, 11);
    strncpy(pos.InvestorID, data->InvestorID, 19);
    strncpy(pos.InstrumentID, data->InstrumentID, 31);
    pos.PosiDirection = LF_CHAR_Long;
    pos.Position = 0;

    bool findSymbolInResult = false;

    if(result["code"] == Json::nullValue) {
        int balancesSize = result["balances"].size();
        for (int i = 0; i < balancesSize; i++) {
            string symbol = result["balances"][i]["asset"].asString();
            strncpy(pos.InstrumentID, symbol.c_str(), 31);
            KF_LOG_INFO(logger, "[req_investor_position] (symbol)" << symbol << " free:"
                                                                   << result["balances"][i]["free"].asString().c_str()
                                                                   << " locked: "
                                                                   << result["balances"][i]["locked"].asString().c_str());
            pos.Position = std::round(stod(result["balances"][i]["free"].asString().c_str()) * scale_offset);
            on_rsp_position(&pos, i == (balancesSize - 1), requestId);
            findSymbolInResult = true;
        }
    }
    if(!findSymbolInResult)
    {
        on_rsp_position(&pos, 1, requestId, errorId, errorMsg.c_str());
    }
    raw_writer->write_error_frame(&pos, sizeof(LFRspPositionField), source_id, MSG_TYPE_LF_RSP_POS_BINANCE, 1, requestId, errorId, errorMsg.c_str());
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
        KF_LOG_ERROR(logger, "[req_order_insert] failed!" << " (rid)" << requestId << " (code)" << errorId);
        errorMsg = (result["msg"] == Json::nullValue) ? "" : result["msg"].asString();
    }
    if(errorId != 0)
    {
        on_rsp_order_insert(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFInputOrderField), source_id, MSG_TYPE_LF_ORDER_BINANCE, 1, requestId, errorId, errorMsg.c_str());
    }

    //paser the order/trade info in the response result
    if(result["code"] == Json::nullValue)
    {
        if(result["status"] == Json::nullValue)
        {//no status, it is ACK
            onRspNewOrderACK(data, unit, result, requestId);
        } else {
            if(result["fills"] == Json::nullValue)
            {
                // it is RESULT
                onRspNewOrderRESULT(data, unit, result, requestId);
            } else {
                // it is FULL
                onRspNewOrderFULL(data, unit, result, requestId);
            }
        }
    }
}


void TDEngineBinance::onRspNewOrderACK(const LFInputOrderField* data, AccountUnitBinance& unit, Json::Value& result, int requestId)
{
    /*Response ACK:
                    {
                      "symbol": "BTCUSDT",
                      "orderId": 28,
                      "clientOrderId": "6gCrw2kRUAF9CvJDGP16IP",
                      "transactTime": 1507725176595
                    }
    */

    //if not Traded, add pendingOrderStatus for GetAndHandleOrderTradeResponse
    char noneStatus = '\0';//none
    uint64_t binanceOrderId =  result["orderId"].asInt64();
    addNewQueryOrdersAndTrades(unit, data->InstrumentID, data->OrderRef, noneStatus, 0, data->Direction, binanceOrderId);
}


void TDEngineBinance::onRspNewOrderRESULT(const LFInputOrderField* data, AccountUnitBinance& unit, Json::Value& result, int requestId)
{
    /*Response RESULT:
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
                      "side": "SELL"
                    }

    */
    // no strike price, dont emit OnRtnTrade
    LFRtnOrderField rtn_order;
    memset(&rtn_order, 0, sizeof(LFRtnOrderField));
    strcpy(rtn_order.ExchangeID, "binance");
    strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
    strncpy(rtn_order.InstrumentID, result["symbol"].asString().c_str(), 31);
    rtn_order.Direction = data->Direction;
    rtn_order.TimeCondition = data->TimeCondition;
    rtn_order.OrderPriceType = data->OrderPriceType;
    strncpy(rtn_order.OrderRef, result["clientOrderId"].asString().c_str(), 13);
    rtn_order.VolumeTraded = std::round(stod(result["executedQty"].asString().c_str()) * scale_offset);
    rtn_order.VolumeTotalOriginal = std::round(stod(result["origQty"].asString().c_str()) * scale_offset);
    rtn_order.VolumeTotal = rtn_order.VolumeTotalOriginal - rtn_order.VolumeTraded;
    rtn_order.LimitPrice = std::round(stod(result["price"].asString().c_str()) * scale_offset);
    rtn_order.RequestID = requestId;
    rtn_order.OrderStatus = GetOrderStatus(result["status"].asString().c_str());
    on_rtn_order(&rtn_order);
    raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                            source_id, MSG_TYPE_LF_RTN_ORDER_BINANCE,
                            1/*islast*/, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);

    //if All Traded, emit OnRtnTrade
    if(rtn_order.OrderStatus == LF_CHAR_AllTraded)
    {
        LFRtnTradeField rtn_trade;
        memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
        strcpy(rtn_trade.ExchangeID, "binance");
        strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
        strncpy(rtn_trade.InstrumentID, result["symbol"].asString().c_str(), 31);
        strncpy(rtn_trade.OrderRef, result["clientOrderId"].asString().c_str(), 13);
        rtn_trade.Direction = data->Direction;
        rtn_trade.Volume = std::round(stod(result["executedQty"].asString().c_str()) * scale_offset);
        rtn_trade.Price = std::round(stod(result["price"].asString().c_str()) * scale_offset);

        on_rtn_trade(&rtn_trade);
        raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
                                source_id, MSG_TYPE_LF_RTN_TRADE_BINANCE, 1/*islast*/, -1/*invalidRid*/);
    }

    //if not All Traded, add pendingOrderStatus for GetAndHandleOrderTradeResponse
    if(rtn_order.VolumeTraded  < rtn_order.VolumeTotalOriginal )
    {
        uint64_t binanceOrderId =  result["orderId"].asInt64();
        addNewQueryOrdersAndTrades(unit, rtn_order.InstrumentID,
                                       rtn_order.OrderRef, rtn_order.OrderStatus, rtn_order.VolumeTraded, data->Direction, binanceOrderId);
    }
}

void TDEngineBinance::onRspNewOrderFULL(const LFInputOrderField* data, AccountUnitBinance& unit, Json::Value& result, int requestId)
{
    /*Response FULL:
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

    LFRtnOrderField rtn_order;
    memset(&rtn_order, 0, sizeof(LFRtnOrderField));
    strcpy(rtn_order.ExchangeID, "binance");
    strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
    strncpy(rtn_order.InstrumentID, result["symbol"].asString().c_str(), 31);
    rtn_order.Direction = data->Direction;
    rtn_order.TimeCondition = data->TimeCondition;
    rtn_order.OrderPriceType = data->OrderPriceType;
    strncpy(rtn_order.OrderRef, result["clientOrderId"].asString().c_str(), 13);
    rtn_order.VolumeTraded = std::round(stod(result["executedQty"].asString().c_str()) * scale_offset);
    rtn_order.VolumeTotalOriginal = std::round(stod(result["origQty"].asString().c_str()) * scale_offset);
    rtn_order.VolumeTotal = rtn_order.VolumeTotalOriginal - rtn_order.VolumeTraded;
    rtn_order.LimitPrice = std::round(stod(result["price"].asString().c_str()) * scale_offset);
    rtn_order.RequestID = requestId;
    rtn_order.OrderStatus = GetOrderStatus(result["status"].asString().c_str());
    on_rtn_order(&rtn_order);
    raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                            source_id, MSG_TYPE_LF_RTN_ORDER_BINANCE,
                            1/*islast*/, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);

    //we have strike price, emit OnRtnTrade
    int fills_size = result["fills"].size();
    KF_LOG_INFO(logger, "[req_order_insert]" << " result[fills] exist. (result)" << result);

    LFRtnTradeField rtn_trade;
    memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
    strcpy(rtn_trade.ExchangeID, "binance");
    strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
    strncpy(rtn_trade.InstrumentID, result["symbol"].asString().c_str(), 31);
    strncpy(rtn_trade.OrderRef, result["clientOrderId"].asString().c_str(), 13);
    rtn_trade.Direction = data->Direction;

    for(int i = 0; i < fills_size; ++i)
    {
        rtn_trade.Volume = std::round(stod(result["executedQty"].asString().c_str()) * scale_offset);
        rtn_trade.Price = std::round(stod(result["price"].asString().c_str()) * scale_offset);
        on_rtn_trade(&rtn_trade);
        raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
                                source_id, MSG_TYPE_LF_RTN_TRADE_BINANCE, 1/*islast*/, -1/*invalidRid*/);
    }

    //if not All Traded, add pendingOrderStatus for GetAndHandleOrderTradeResponse
    if(rtn_order.VolumeTraded  < rtn_order.VolumeTotalOriginal )
    {
        uint64_t binanceOrderId =  result["orderId"].asInt64();
        addNewQueryOrdersAndTrades(unit, rtn_order.InstrumentID,
                                       rtn_order.OrderRef, rtn_order.OrderStatus, rtn_order.VolumeTraded, data->Direction, binanceOrderId);
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
    BinaCPP::init( unit.api_key, unit.secret_key );
	BinaCPP::cancel_order(data->InstrumentID, 0, data->OrderRef,"", recvWindow, result);
    send_writer->write_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BINANCE, 1, requestId);

    KF_LOG_INFO(logger, "[req_order_action]" << " (result)" << result);
    int errorId = 0;
    std::string errorMsg = "";
    if(result["code"] != Json::nullValue)
    {
        errorId = result["code"].asInt();
        KF_LOG_ERROR(logger, "[req_order_action] failed!" << " (rid)" << requestId << " (code)" << errorId);
        errorMsg = (result["msg"] == Json::nullValue) ? "" : result["msg"].asString();
    }
    if(errorId != 0)
    {
        on_rsp_order_action(data, requestId, errorId, errorMsg.c_str());
        raw_writer->write_error_frame(data, sizeof(LFOrderActionField), source_id, MSG_TYPE_LF_ORDER_ACTION_BINANCE, 1, requestId, errorId, errorMsg.c_str());
    }
}

void TDEngineBinance::GetAndHandleOrderTradeResponse()
{
    //every account
    for (int idx = 0; idx < account_units.size(); idx ++)
    {
        AccountUnitBinance& unit = account_units[idx];
        KF_LOG_INFO(logger, "[GetAndHandleOrderTradeResponse] (api_key)" << unit.api_key);
        if (!unit.logged_in)
        {
            continue;
        }
        //BinaCPP::init( unit.api_key , unit.secret_key );
        moveNewtoPending(unit);
        retrieveOrderStatus(unit);
        retrieveTradeStatus(unit);
    }//end every account
}


void TDEngineBinance::moveNewtoPending(AccountUnitBinance& unit)
{
    std::lock_guard<std::mutex> guard_mutex(*mutex_order_and_trade);

    std::vector<PendingBinanceOrderStatus>::iterator newOrderStatusIterator;
    for(newOrderStatusIterator = unit.newOrderStatus.begin(); newOrderStatusIterator != unit.newOrderStatus.end();)
    {
        unit.pendingOrderStatus.push_back(*newOrderStatusIterator);
        newOrderStatusIterator = unit.newOrderStatus.erase(newOrderStatusIterator);
    }

    std::vector<OnRtnOrderDoneAndWaitingOnRtnTrade>::iterator tradeIterator;
    for(tradeIterator = unit.newOnRtnTrades.begin(); tradeIterator != unit.newOnRtnTrades.end();)
    {
        unit.pendingOnRtnTrades.push_back(*tradeIterator);
        tradeIterator = unit.newOnRtnTrades.erase(tradeIterator);
    }

    std::vector<PendingBinanceTradeStatus>::iterator newTradeStatusIterator;
    for(newTradeStatusIterator = unit.newTradeStatus.begin(); newTradeStatusIterator != unit.newTradeStatus.end();) {
        unit.pendingTradeStatus.push_back(*newTradeStatusIterator);
        newTradeStatusIterator = unit.newTradeStatus.erase(newTradeStatusIterator);
    }
}

void TDEngineBinance::retrieveOrderStatus(AccountUnitBinance& unit)
{
    KF_LOG_INFO(logger, "[retrieveOrderStatus] ");
    std::vector<PendingBinanceOrderStatus>::iterator orderStatusIterator;
//    int indexNum = 0;
//    for(orderStatusIterator = unit.pendingOrderStatus.begin(); orderStatusIterator != unit.pendingOrderStatus.end(); orderStatusIterator++)
//    {
//        indexNum++;
//        KF_LOG_INFO(logger, "[retrieveOrderStatus] get_order [" << indexNum <<"]    account.api_key:"<< unit.api_key
//                                                                          << "  account.pendingOrderStatus.InstrumentID: "<< orderStatusIterator->InstrumentID
//                                                                          <<"  account.pendingOrderStatus.OrderRef: " << orderStatusIterator->OrderRef
//                                                                          <<"  account.pendingOrderStatus.OrderStatus: " << orderStatusIterator->OrderStatus
//        );
//    }

    for(orderStatusIterator = unit.pendingOrderStatus.begin(); orderStatusIterator != unit.pendingOrderStatus.end();)
    {
        KF_LOG_INFO(logger, "[retrieveOrderStatus] get_order " << "account.api_key:"<< unit.api_key
                                                                          << "  account.pendingOrderStatus.InstrumentID: "<< orderStatusIterator->InstrumentID
                                                                          <<"  account.pendingOrderStatus.OrderRef: " << orderStatusIterator->OrderRef
                                                                          <<"  account.pendingOrderStatus.OrderStatus: " << orderStatusIterator->OrderStatus
        );

        Json::Value orderResult;
        long recvWindow = 10000;
        BinaCPP::init( unit.api_key , unit.secret_key );
        BinaCPP::get_order( orderStatusIterator->InstrumentID, 0, orderStatusIterator->OrderRef, recvWindow, orderResult );
        KF_LOG_INFO(logger, "[retrieveOrderStatus] get_order " << " (symbol)" << orderStatusIterator->InstrumentID
                                                                          << " (orderId)" << orderStatusIterator->OrderRef
                                                                          << " (result)" << orderResult);
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
        //parse order status
        if(orderResult["code"] == Json::nullValue)
        {
            LFRtnOrderField rtn_order;
            memset(&rtn_order, 0, sizeof(LFRtnOrderField));
            rtn_order.OrderStatus = GetOrderStatus(orderResult["status"].asString().c_str());
            rtn_order.VolumeTraded = std::round(stod(orderResult["executedQty"].asString().c_str()) * scale_offset);

            //if status changed or LF_CHAR_PartTradedNotQueueing but traded valume changes, emit onRtnOrder
            if(orderStatusIterator->OrderStatus != rtn_order.OrderStatus ||
               (LF_CHAR_PartTradedNotQueueing == rtn_order.OrderStatus
                && rtn_order.VolumeTraded != orderStatusIterator->VolumeTraded))
            {
                strcpy(rtn_order.ExchangeID, "binance");
                strncpy(rtn_order.UserID, unit.api_key.c_str(), 16);
                strncpy(rtn_order.InstrumentID, orderResult["symbol"].asString().c_str(), 31);
                rtn_order.Direction = GetDirection(orderResult["side"].asString());
                rtn_order.TimeCondition = GetTimeCondition(orderResult["timeInForce"].asString());
                rtn_order.OrderPriceType = GetPriceType(orderResult["type"].asString());
                strncpy(rtn_order.OrderRef, orderResult["clientOrderId"].asString().c_str(), 13);
                rtn_order.VolumeTotalOriginal = std::round(stod(orderResult["origQty"].asString().c_str()) * scale_offset);
                rtn_order.LimitPrice = std::round(stod(orderResult["price"].asString().c_str()) * scale_offset);
                rtn_order.VolumeTotal = rtn_order.VolumeTotalOriginal - rtn_order.VolumeTraded;
                on_rtn_order(&rtn_order);
                raw_writer->write_frame(&rtn_order, sizeof(LFRtnOrderField),
                                        source_id, MSG_TYPE_LF_RTN_ORDER_BINANCE,
                                        1/*islast*/, (rtn_order.RequestID > 0) ? rtn_order.RequestID: -1);
                //update last status
                orderStatusIterator->OrderStatus = rtn_order.OrderStatus;
                orderStatusIterator->VolumeTraded = rtn_order.VolumeTraded;

                //is status is canceled, dont need get the orderref's trade info
                if(orderStatusIterator->OrderStatus == LF_CHAR_Canceled) {
                    uint64_t binanceOrderId =  orderResult["orderId"].asInt64();
                    if(removeBinanceOrderIdFromPendingOnRtnTrades(unit, binanceOrderId))
                    {
                        KF_LOG_INFO(logger, "[retrieveTradeStatus] the (OrderRef) "
                                            << orderStatusIterator->OrderRef
                                            << " is LF_CHAR_Canceled. done need get this (binanceOrderId)"
                                               << binanceOrderId << " trade info");
                    }
                }
            }
        } else {
            KF_LOG_ERROR(logger, "[retrieveOrderStatus] get_order fail." << " (symbol)" << orderStatusIterator->InstrumentID
                                                                                    << " (orderId)" << orderStatusIterator->OrderRef
                                                                                    << " (result)" << orderResult);
        }

        //remove order when finish
        if(orderStatusIterator->OrderStatus == LF_CHAR_AllTraded  || orderStatusIterator->OrderStatus == LF_CHAR_Canceled
           || orderStatusIterator->OrderStatus == LF_CHAR_Error)
        {
            KF_LOG_INFO(logger, "[retrieveOrderStatus] remove a pendingOrderStatus.");
            orderStatusIterator = unit.pendingOrderStatus.erase(orderStatusIterator);
        } else {
            ++orderStatusIterator;
        }
        //KF_LOG_INFO(logger, "[retrieveOrderStatus] move to next pendingOrderStatus.");
    }
}

void TDEngineBinance::retrieveTradeStatus(AccountUnitBinance& unit)
{
    KF_LOG_INFO(logger, "[retrieveTradeStatus] (unit.pendingOrderStatus.size())" << unit.pendingOrderStatus.size() << " (unit.pendingOnRtnTrades.size()) " << unit.pendingOnRtnTrades.size());
    //if 'ours' order is finished, and ours trade is finished too , dont get trade info anymore.
    if(unit.pendingOrderStatus.size() == 0 && unit.pendingOnRtnTrades.size() == 0) return;
    Json::Value resultTrade;
    long recvWindow = 900000;
    std::vector<PendingBinanceTradeStatus>::iterator tradeStatusIterator;
    for(tradeStatusIterator = unit.pendingTradeStatus.begin(); tradeStatusIterator != unit.pendingTradeStatus.end(); ++tradeStatusIterator)
    {
        BinaCPP::init( unit.api_key , unit.secret_key );
        BinaCPP::get_myTrades( tradeStatusIterator->InstrumentID, 500, tradeStatusIterator->last_trade_id, recvWindow , resultTrade );
        /*
         [
          {
            "id": 28457,
            "orderId": 100234,
            "price": "4.00000100",
            "qty": "12.00000000",
            "commission": "10.10000000",
            "commissionAsset": "BNB",
            "time": 1499865549590,
            "isBuyer": true,
            "isMaker": false,
            "isBestMatch": true
          }
        ]

         example2, isBuyer=isMaker=true:
         [{
                "commission" : "0.00137879",
                "commissionAsset" : "BNB",
                "id" : 30801527,
                "isBestMatch" : true,
                "isBuyer" : true,
                "isMaker" : true,
                "orderId" : 61311309,
                "price" : "0.00000603",
                "qty" : "1000.00000000",
                "time" : 1530541407016
        }]
        */
        if(resultTrade.isObject()) {
            //expected is array,but get object, it must be the error response json:
            /*
             {
            "code" : -1021,
            "msg" : "Timestamp for this request is outside of the recvWindow."
            }
             * */
            int errorId = 0;
            std::string errorMsg = "";
            if(resultTrade["code"] != Json::nullValue)
            {
                errorId = resultTrade["code"].asInt();
                errorMsg = (resultTrade["msg"] == Json::nullValue) ? "" : resultTrade["msg"].asString();
                KF_LOG_ERROR(logger, "[retrieveTradeStatus] get_myTrades failed!" << " (errorId)" << errorId << " (errorMsg)" << errorMsg);

            }
            continue;
        }
        KF_LOG_INFO(logger, "[retrieveTradeStatus] get_myTrades (last_trade_id)" << tradeStatusIterator->last_trade_id << " (result)"<< resultTrade);
        for(int i = 0 ; i < resultTrade.size(); i++)
        {
            LFRtnTradeField rtn_trade;
            memset(&rtn_trade, 0, sizeof(LFRtnTradeField));
            strcpy(rtn_trade.ExchangeID, "binance");
            strncpy(rtn_trade.UserID, unit.api_key.c_str(), 16);
            strncpy(rtn_trade.InstrumentID, tradeStatusIterator->InstrumentID, 31);
            rtn_trade.Volume = std::round(stod(resultTrade[i]["qty"].asString().c_str()) * scale_offset);
            rtn_trade.Price = std::round(stod(resultTrade[i]["price"].asString().c_str()) * scale_offset);

            //apply the direction of the OrderRef
            uint64_t binanceOrderId =  resultTrade[i]["orderId"].asInt64();
            std::vector<OnRtnOrderDoneAndWaitingOnRtnTrade>::iterator tradeIterator;

            bool match_one = false;
            for(tradeIterator = unit.pendingOnRtnTrades.begin(); tradeIterator != unit.pendingOnRtnTrades.end(); ++tradeIterator)
            {
                if(tradeIterator->binanceOrderId == binanceOrderId)
                {
                    rtn_trade.Direction = tradeIterator->Direction;
                    strncpy(rtn_trade.OrderRef, tradeIterator->OrderRef, 13);
                    match_one = true;
                }
            }
            if(match_one) {
                on_rtn_trade(&rtn_trade);
                raw_writer->write_frame(&rtn_trade, sizeof(LFRtnTradeField),
                                        source_id, MSG_TYPE_LF_RTN_TRADE_BINANCE, 1/*islast*/, -1/*invalidRid*/);
            }

            uint64_t newtradeId = resultTrade[i]["id"].asInt64();
            KF_LOG_INFO(logger, "[retrieveTradeStatus] get_myTrades (newtradeId)" << newtradeId);
            if(newtradeId >= tradeStatusIterator->last_trade_id) {
                tradeStatusIterator->last_trade_id = newtradeId + 1;// for new trade
            }
            KF_LOG_INFO(logger, "[retrieveTradeStatus] get_myTrades (last_trade_id)" << tradeStatusIterator->last_trade_id);
        }

        //here, use another for-loop is for there maybe more than one trades on the same orderRef:
        //if the first one remove pendingOnRtnTrades, the second one could not get the Direction.
        for(int i = 0 ; i < resultTrade.size(); i++)
        {
            if(! isExistSymbolInPendingBinanceOrderStatus(unit, tradeStatusIterator->InstrumentID)) {
                //all the OnRtnOrder is finished.
                uint64_t binanceOrderId =  resultTrade[i]["orderId"].asInt64();
                if(removeBinanceOrderIdFromPendingOnRtnTrades(unit, binanceOrderId))
                {
                    KF_LOG_INFO(logger, "[retrieveTradeStatus] there is no pendingOrderStatus(LF_CHAR_AllTraded/LF_CHAR_Canceled/LF_CHAR_Error occur). this is the last turn of get_myTrades on (symbol)" << tradeStatusIterator->InstrumentID);
                }
            }
        }
    }
}

bool TDEngineBinance::removeBinanceOrderIdFromPendingOnRtnTrades(AccountUnitBinance& unit, uint64_t binanceOrderId)
{
    std::vector<OnRtnOrderDoneAndWaitingOnRtnTrade>::iterator tradeIterator;
    for(tradeIterator = unit.pendingOnRtnTrades.begin(); tradeIterator != unit.pendingOnRtnTrades.end(); )
    {
        if(tradeIterator->binanceOrderId == binanceOrderId)
        {
            tradeIterator = unit.pendingOnRtnTrades.erase(tradeIterator);
        } else {
            ++tradeIterator;
        }
    }
}

void TDEngineBinance::addNewQueryOrdersAndTrades(AccountUnitBinance& unit, const char_31 InstrumentID,
                                                     const char_21 OrderRef, const LfOrderStatusType OrderStatus,
                                                 const uint64_t VolumeTraded, LfDirectionType Direction, uint64_t binanceOrderId)
{
    //add new orderId for GetAndHandleOrderTradeResponse
    std::lock_guard<std::mutex> guard_mutex(*mutex_order_and_trade);

    PendingBinanceOrderStatus status;
    memset(&status, 0, sizeof(PendingBinanceOrderStatus));
    strncpy(status.InstrumentID, InstrumentID, 31);
    strncpy(status.OrderRef, OrderRef, 21);
    status.OrderStatus = OrderStatus;
    status.VolumeTraded = VolumeTraded;
    unit.newOrderStatus.push_back(status);

    OnRtnOrderDoneAndWaitingOnRtnTrade waitingTrade;
    strncpy(waitingTrade.OrderRef, OrderRef, 21);
    waitingTrade.binanceOrderId = binanceOrderId;
    waitingTrade.Direction = Direction;
    unit.newOnRtnTrades.push_back(waitingTrade);

    //add new symbol for GetAndHandleOrderTradeResponse if had no this symbol before
    if(!isExistSymbolInPendingTradeStatus(unit, InstrumentID))
    {
        PendingBinanceTradeStatus tradeStatus;
        memset(&tradeStatus, 0, sizeof(PendingBinanceTradeStatus));
        strncpy(tradeStatus.InstrumentID, InstrumentID, 31);
        tradeStatus.last_trade_id = 0;
        unit.newTradeStatus.push_back(tradeStatus);
    }
}

bool TDEngineBinance::isExistSymbolInPendingTradeStatus(AccountUnitBinance& unit, const char_31 InstrumentID)
{
    std::vector<PendingBinanceTradeStatus>::iterator tradeStatusIterator;

    for(tradeStatusIterator = unit.pendingTradeStatus.begin(); tradeStatusIterator != unit.pendingTradeStatus.end(); ++tradeStatusIterator)
    {
        if(strcmp(tradeStatusIterator->InstrumentID, InstrumentID) == 0)
        {
            return true;
        }
    }
    return false;
}


bool TDEngineBinance::isExistSymbolInPendingBinanceOrderStatus(AccountUnitBinance& unit, const char_31 InstrumentID)
{
    std::vector<PendingBinanceOrderStatus>::iterator orderStatusIterator;

    for(orderStatusIterator = unit.pendingOrderStatus.begin(); orderStatusIterator != unit.pendingOrderStatus.end(); orderStatusIterator++) {
        if (strcmp(orderStatusIterator->InstrumentID, InstrumentID) == 0) {
            return true;
        }
    }
    return false;
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
        GetAndHandleOrderTradeResponse();
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

void TDEngineBinance::get_open_orders(AccountUnitBinance& unit, const char *symbol, Document &json)
{
    KF_LOG_INFO(logger, "[get_open_orders]");
    long recvWindow = 10000;
    std::string Timestamp = getTimestampString();
    std::string Method = "GET";
    std::string requestPath = "https://api.binance.com/api/v3/openOrders?";
    std::string queryString("");
    std::string body = "";

    bool hasSetParameter = false;

    if(strlen(symbol) > 0) {
        queryString.append( "symbol=" );
        queryString.append( symbol );
        hasSetParameter = true;
    }

    if ( recvWindow > 0 ) {
        if(hasSetParameter)
        {
            queryString.append("&recvWindow=");
            queryString.append( to_string( recvWindow) );
        } else {
            queryString.append("recvWindow=");
            queryString.append( to_string( recvWindow) );
        }
        hasSetParameter = true;
    }

    if(hasSetParameter)
    {
        queryString.append("&timestamp=");
        queryString.append( Timestamp );
    } else {
        queryString.append("timestamp=");
        queryString.append( Timestamp );
    }

    std::string signature =  hmac_sha256( unit.secret_key.c_str(), queryString.c_str() );
    queryString.append( "&signature=");
    queryString.append( signature );

    string url = requestPath + queryString;

    const auto response = Get(Url{url},
                                 Header{{"X-MBX-APIKEY", unit.api_key}},
                                 Body{body}, Timeout{100000});

    KF_LOG_INFO(logger, "[get_open_orders] (url) " << url << " (response) " << response.text.c_str());
    /*If the symbol is not sent, orders for all symbols will be returned in an array.
    [
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
        "isWorking": trueO
      }
    ]
     * */
    return getResponse(response.status_code, response.text, response.error.message, json);
}

void TDEngineBinance::printResponse(const Document& d)
{
    if(d.IsObject() && d.HasMember("code")) {
        KF_LOG_INFO(logger, "[printResponse] error (code) " << d["code"].GetInt() << " (message) " << d["message"].GetString());
    } else {
        StringBuffer buffer;
        Writer<StringBuffer> writer(buffer);
        d.Accept(writer);
        KF_LOG_INFO(logger, "[printResponse] ok (text) " << buffer.GetString());
    }
}

void TDEngineBinance::getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& json)
{
    if(http_status_code == HTTP_RESPONSE_OK)
    {
        //KF_LOG_INFO(logger, "[getResponse] (http_status_code == 200) (responseText)" << responseText << " (errorMsg) " << errorMsg);
        json.Parse(responseText.c_str());
        //KF_LOG_INFO(logger, "[getResponse] (http_status_code == 200) (HasParseError)" << json.HasParseError());
    } else if(http_status_code == 0 && responseText.length() == 0)
    {
        json.SetObject();
        Document::AllocatorType& allocator = json.GetAllocator();
        int errorId = 1;
        json.AddMember("code", errorId, allocator);
        //KF_LOG_INFO(logger, "[getResponse] (errorMsg)" << errorMsg);
        rapidjson::Value val;
        val.SetString(errorMsg.c_str(), errorMsg.length(), allocator);
        json.AddMember("message", val, allocator);
    } else
    {
        Document d;
        d.Parse(responseText.c_str());
        //KF_LOG_INFO(logger, "[getResponse] (err) (responseText)" << responseText.c_str());

        json.SetObject();
        Document::AllocatorType& allocator = json.GetAllocator();
        json.AddMember("code", http_status_code, allocator);
        if(d.IsObject()) {
            if( d.HasMember("message")) {
                //KF_LOG_INFO(logger, "[getResponse] (err) (errorMsg)" << d["message"].GetString());
                std::string message = d["message"].GetString();
                rapidjson::Value val;
                val.SetString(message.c_str(), message.length(), allocator);
                json.AddMember("message", val, allocator);
            }
            if( d.HasMember("msg")) {
                //KF_LOG_INFO(logger, "[getResponse] (err) (errorMsg)" << d["msg"].GetString());
                std::string message = d["msg"].GetString();
                rapidjson::Value val;
                val.SetString(message.c_str(), message.length(), allocator);
                json.AddMember("message", val, allocator);
            }
        }
    }
}

std::string TDEngineBinance::getTimestampString()
{
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::string timestampStr;
    std::stringstream convertStream;
    convertStream << timestamp;
    convertStream >> timestampStr;
    return timestampStr;
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
