
#ifndef PROJECT_TDENGINEBINANCE_H
#define PROJECT_TDENGINEBINANCE_H

#include "ITDEngine.h"
#include "longfist/LFConstants.h"
#include <vector>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <map>
#include <mutex>
#include "Timer.h"
#include <document.h>

using rapidjson::Document;

WC_NAMESPACE_START

/**
 * account information unit extra is here.
 */

struct PendingBinanceOrderStatus
{
    char_31 InstrumentID;   //合约代码
    char_21 OrderRef;       //报单引用
    LfOrderStatusType OrderStatus;  //报单状态
    uint64_t VolumeTraded;  //今成交数量
};

struct PendingBinanceTradeStatus
{
    char_31 InstrumentID;   //合约代码
    int64_t last_trade_id; //for myTrade
};

//当Order已经全部成交完成之后，到get_myTrades拿到这个OrderRef记录的信息以后， 删除记录，不再get_myTrades
struct OnRtnOrderDoneAndWaitingOnRtnTrade
{
    char_21 OrderRef;       //报单引用
    int64_t binanceOrderId;       //binance 报单引用
    LfDirectionType Direction;  //买卖方向
};

struct SendOrderFilter
{
    char_31 InstrumentID;   //合约代码
    int ticksize; //for price round.
    //...other
};

struct SubscribeCoinBaseQuote
{
    std::string base;
    std::string quote;
};

struct AccountUnitBinance
{
    std::string api_key;
    std::string secret_key;
    // internal flags
    bool    logged_in;
    std::vector<PendingBinanceOrderStatus> newOrderStatus;
    std::vector<PendingBinanceOrderStatus> pendingOrderStatus;
    std::vector<PendingBinanceTradeStatus> newTradeStatus;
    std::vector<PendingBinanceTradeStatus> pendingTradeStatus;

    std::vector<OnRtnOrderDoneAndWaitingOnRtnTrade> newOnRtnTrades;
    std::vector<OnRtnOrderDoneAndWaitingOnRtnTrade> pendingOnRtnTrades;
    std::vector<std::string> whiteListInstrumentIDs;
    std::map<std::string, SendOrderFilter> sendOrderFilters;

    //in TD, lookup direction is:
    // our strategy recognized coinpair ---> outcoming exchange coinpair
    //if strategy's coinpair is not in this map ,ignore it
    //"strategy_coinpair(base_quote)":"exchange_coinpair",
    std::map<std::string, std::string> keyIsStrategyCoinpairWhiteList;

    std::vector<SubscribeCoinBaseQuote> subscribeCoinBaseQuote;
};

/**
 * CTP trade engine
 */
class TDEngineBinance: public ITDEngine
{
public:
    /** init internal journal writer (both raw and send) */
    virtual void init();
    /** for settleconfirm and authenticate setting */
    virtual void pre_load(const json& j_config);
    virtual TradeAccount load_account(int idx, const json& j_account);
    virtual void resize_accounts(int account_num);
    /** connect && login related */
    virtual void connect(long timeout_nsec);
    virtual void login(long timeout_nsec);
    virtual void logout();
    virtual void release_api();
    virtual bool is_connected() const;
    virtual bool is_logged_in() const;
    virtual string name() const { return "TDEngineBinance"; };

    // req functions
    virtual void req_investor_position(const LFQryPositionField* data, int account_index, int requestId);
    virtual void req_qry_account(const LFQryAccountField* data, int account_index, int requestId);
    virtual void req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time);
    virtual void req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time);

public:
    TDEngineBinance();
    ~TDEngineBinance();
private:
    // journal writers
    yijinjing::JournalWriterPtr raw_writer;
    vector<AccountUnitBinance> account_units;

    std::string GetSide(const LfDirectionType& input);
    LfDirectionType GetDirection(std::string input);
    std::string GetType(const LfOrderPriceTypeType& input);
    LfOrderPriceTypeType GetPriceType(std::string input);
    std::string GetTimeInForce(const LfTimeConditionType& input);
    LfTimeConditionType GetTimeCondition(std::string input);
    LfOrderStatusType GetOrderStatus(std::string input);

    void loop();
    std::vector<std::string> split(std::string str, std::string token);
    bool loadExchangeOrderFilters(AccountUnitBinance& unit, Document &doc);
    void GetAndHandleOrderTradeResponse();
    void addNewQueryOrdersAndTrades(AccountUnitBinance& unit, const char_31 InstrumentID,
                                        const char_21 OrderRef, const LfOrderStatusType OrderStatus, const uint64_t VolumeTraded, LfDirectionType Direction, int64_t binanceOrderId);

    inline void onRspNewOrderACK(const LFInputOrderField* data, AccountUnitBinance& unit, Document& result, int requestId);
    inline void onRspNewOrderRESULT(const LFInputOrderField* data, AccountUnitBinance& unit, Document& result, int requestId);
    inline void onRspNewOrderFULL(const LFInputOrderField* data, AccountUnitBinance& unit, Document& result, int requestId);

    void retrieveOrderStatus(AccountUnitBinance& unit);
    void retrieveTradeStatus(AccountUnitBinance& unit);
    void moveNewtoPending(AccountUnitBinance& unit);
    bool isExistSymbolInPendingTradeStatus(AccountUnitBinance& unit, const char_31 InstrumentID);
    bool isExistSymbolInPendingBinanceOrderStatus(AccountUnitBinance& unit, const char_31 InstrumentID);
    bool removeBinanceOrderIdFromPendingOnRtnTrades(AccountUnitBinance& unit, int64_t binanceOrderId);
    static constexpr int scale_offset = 1e8;
    int64_t fixPriceTickSize(int keepPrecision, int64_t price, bool isBuy);

    ThreadPtr rest_thread;
    uint64_t last_rest_get_ts = 0;
    int rest_get_interval_ms = 500;

    std::mutex* mutex_order_and_trade = nullptr;

    int SYNC_TIME_DEFAULT_INTERVAL = 10000;
    int sync_time_interval;
    int64_t timeDiffOfExchange = 0;
private:
    int HTTP_RESPONSE_OK = 200;
    void send_order(AccountUnitBinance& unit, const char *symbol,
                    const char *side,
                    const char *type,
                    const char *timeInForce,
                    double quantity,
                    double price,
                    const char *newClientOrderId,
                    double stopPrice,
                    double icebergQty,
                    Document& json);

    void get_order(AccountUnitBinance& unit, const char *symbol, long orderId, const char *origClientOrderId, Document& json);
    void cancel_order(AccountUnitBinance& unit, const char *symbol,
                      long orderId, const char *origClientOrderId, const char *newClientOrderId, Document &doc);
    void get_my_trades(AccountUnitBinance& unit, const char *symbol, int limit, int64_t fromId, Document &doc);
    void get_open_orders(AccountUnitBinance& unit, const char *symbol, Document &doc);
    void get_exchange_infos(AccountUnitBinance& unit, Document &doc);
    void get_exchange_time(AccountUnitBinance& unit, Document &doc);
    void get_account(AccountUnitBinance& unit, Document &doc);
    void getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& doc);
    void printResponse(const Document& d);
    inline std::string getTimestampString();

    void debug_print(std::map<std::string, SendOrderFilter> &sendOrderFilters);

    SendOrderFilter getSendOrderFilter(AccountUnitBinance& unit, const char *symbol);

private:
    inline int64_t getTimestamp();
    int64_t getTimeDiffOfExchange(AccountUnitBinance& unit);
    void readWhiteLists(AccountUnitBinance& unit, const json& j_config);
    std::string getWhiteListCoinpairFrom(AccountUnitBinance& unit, const char_31 strategy_coinpair);
    bool hasSymbolInWhiteList(std::vector<SubscribeCoinBaseQuote> &sub, std::string symbol);
    void split(std::string str, std::string token, SubscribeCoinBaseQuote& sub);
    void debug_print(std::vector<SubscribeCoinBaseQuote> &sub);
    void debug_print(std::map<std::string, std::string> &keyIsStrategyCoinpairWhiteList);

};

WC_NAMESPACE_END

#endif //PROJECT_TDENGINEBINANCE_H



