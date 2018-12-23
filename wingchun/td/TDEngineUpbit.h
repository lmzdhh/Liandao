
#ifndef PROJECT_TDENGINEUpbit_H
#define PROJECT_TDENGINEUpbit_H

#include "ITDEngine.h"
#include "longfist/LFConstants.h"
#include "CoinPairWhiteList.h"
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

struct PendingUpbitOrderStatus
{
    char_31 InstrumentID;   //合约代码
    char_21 OrderRef;       //报单引用
    LfOrderStatusType OrderStatus;  //报单状态
    uint64_t VolumeTraded;  //今成交数量
};

struct PendingUpbitTradeStatus
{
    char_31 InstrumentID;   //合约代码
    int64_t last_trade_id; //for myTrade
};

//当Order已经全部成交完成之后，到get_myTrades拿到这个OrderRef记录的信息以后， 删除记录，不再get_myTrades
struct OnRtnOrderDoneAndWaitingOnRtnTrade
{
    char_21 OrderRef;       //报单引用
    std::string OrderRef;       //Upbit 报单引用
    LfDirectionType Direction;  //买卖方向
};

struct SendOrderFilter
{
    char_31 InstrumentID;   //合约代码
    std::string strBidCurrency; 
    long long nBidMinTotal;
    std::string strAskCurrency;
    long long nAskMinTotal;
    long long nMaxTotal;
    std::string strState;
    //...other
};

struct AccountUnitUpbit
{
    std::string api_key;
    std::string secret_key;
    // internal flags
    bool    logged_in;
    std::vector<PendingUpbitOrderStatus> newOrderStatus;
    std::vector<PendingUpbitOrderStatus> pendingOrderStatus;
    std::vector<PendingUpbitTradeStatus> newTradeStatus;
    std::vector<PendingUpbitTradeStatus> pendingTradeStatus;

    std::vector<OnRtnOrderDoneAndWaitingOnRtnTrade> newOnRtnTrades;
    std::vector<OnRtnOrderDoneAndWaitingOnRtnTrade> pendingOnRtnTrades;
    std::vector<std::string> whiteListInstrumentIDs;
    std::map<std::string, SendOrderFilter> sendOrderFilters;
    
    // the trade id that has been called on_rtn_trade. Do not send it again.
    std::vector<int64_t> newSentTradeIds;
    std::vector<int64_t> sentTradeIds;

    CoinPairWhiteList coinPairWhiteList;
    CoinPairWhiteList positionWhiteList;
};

/**
 * CTP trade engine
 */
class TDEngineUpbit: public ITDEngine
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
    virtual string name() const { return "TDEngineUpbit"; };

    // req functions
    virtual void req_investor_position(const LFQryPositionField* data, int account_index, int requestId);
    virtual void req_qry_account(const LFQryAccountField* data, int account_index, int requestId);
    virtual void req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time);
    virtual void req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time);

public:
    TDEngineUpbit();
    ~TDEngineUpbit();
private:
    // journal writers
    yijinjing::JournalWriterPtr raw_writer;
    vector<AccountUnitUpbit> account_units;

    std::string GetSide(const LfDirectionType& input);
    LfDirectionType GetDirection(std::string input);
    std::string GetType(const LfOrderPriceTypeType& input);
    LfOrderPriceTypeType GetPriceType(std::string input);
    std::string GetTimeInForce(const LfTimeConditionType& input);
    LfTimeConditionType GetTimeCondition(std::string input);
    LfOrderStatusType GetOrderStatus(std::string input);

    virtual void set_reader_thread() override;
    void loop();
    std::vector<std::string> split(std::string str, std::string token);
    bool loadExchangeOrderFilters(AccountUnitUpbit& unit, Document &doc);
    void GetAndHandleOrderTradeResponse();
    void addNewSentTradeIds(AccountUnitUpbit& unit, int64_t newSentTradeIds);
    void addNewQueryOrdersAndTrades(AccountUnitUpbit& unit, const char_31 InstrumentID,
                                        const char_21 OrderRef, const LfOrderStatusType OrderStatus, const uint64_t VolumeTraded, LfDirectionType Direction, int64_t UpbitOrderId);

    inline void onRspNewOrderACK(const LFInputOrderField* data, AccountUnitUpbit& unit, Document& result, int requestId);
    inline void onRspNewOrderRESULT(const LFInputOrderField* data, AccountUnitUpbit& unit, Document& result, int requestId);
    inline void onRspNewOrderFULL(const LFInputOrderField* data, AccountUnitUpbit& unit, Document& result, int requestId);

    void retrieveOrderStatus(AccountUnitUpbit& unit);
    void retrieveTradeStatus(AccountUnitUpbit& unit);
    void moveNewtoPending(AccountUnitUpbit& unit);
    bool isExistSymbolInPendingTradeStatus(AccountUnitUpbit& unit, const char_31 InstrumentID);
    bool isExistSymbolInPendingUpbitOrderStatus(AccountUnitUpbit& unit, const char_31 InstrumentID, const char_21 OrderRef);
    bool removeUpbitOrderIdFromPendingOnRtnTrades(AccountUnitUpbit& unit, int64_t UpbitOrderId);

    int64_t fixPriceTickSize(int keepPrecision, int64_t price, bool isBuy);

    inline int64_t getTimestamp();
    int64_t getTimeDiffOfExchange(AccountUnitUpbit& unit);

private:
    int HTTP_RESPONSE_OK = 200;
    void send_order(AccountUnitUpbit& unit, const char *symbol,
                    const char *side,
                    const char *type,
                    const char *timeInForce,
                    double quantity,
                    double price,
                    const char *newClientOrderId,
                    double stopPrice,
                    double icebergQty,
                    Document& json);

    void get_order(AccountUnitUpbit& unit, const char *origClientOrderId, Document& json);
    void cancel_order(AccountUnitUpbit& unit, const char *symbol,
                      long orderId, const char *origClientOrderId, const char *newClientOrderId, Document &doc);
    void get_my_trades(AccountUnitUpbit& unit, const char *symbol, int limit, int64_t fromId, Document &doc);
    void get_open_orders(AccountUnitUpbit& unit, const char *symbol, Document &doc);
    void get_exchange_infos(AccountUnitUpbit& unit, Document &doc);
    void getChanceResponce(const AccountUnitUpbit& unit, const std::string& strMarket,Document& d);
    void getAccountResponce(const AccountUnitUpbit& unit,Document& d);
    void getAllMarkets(std::vector<std::string>& vstrMarkets);
    bool loadMarketsInfo(const AccountUnitUpbit& unit, const std::vector<std::string>& vstrMarkets);
    std::string getEncode(const std::string& str);
    std::string getAuthorization(const AccountUnitUpbit& unit,const std::string& strQuery = std::string());
    void get_exchange_time(AccountUnitUpbit& unit, Document &doc);
    void get_account(AccountUnitUpbit& unit, Document &doc);
    void getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& doc);
    void printResponse(const Document& d);
    inline std::string getTimestampString();
    LfOrderStatusType convertOrderStatus(const std::string& strStatus,,int64_t nTrades);

    void debug_print(std::map<std::string, SendOrderFilter> &sendOrderFilters);

    SendOrderFilter getSendOrderFilter(AccountUnitUpbit& unit, const char *symbol);

    bool shouldRetry(int http_status_code, std::string errorMsg, std::string text);
private:
    static constexpr int scale_offset = 1e8;
    ThreadPtr rest_thread;
    uint64_t last_rest_get_ts = 0;
    uint64_t rest_get_interval_ms = 500;

    uint64_t order_insert_recvwindow_ms = 5000;
    uint64_t order_action_recvwindow_ms = 5000;
    std::mutex* mutex_order_and_trade = nullptr;

    int SYNC_TIME_DEFAULT_INTERVAL = 10000;
    int sync_time_interval;
    int64_t timeDiffOfExchange = 0;
    int exchange_shift_ms = 0;

    int max_rest_retry_times = 3;
    int retry_interval_milliseconds = 1000;
};

WC_NAMESPACE_END

#endif //PROJECT_TDENGINEUpbit_H



