
#ifndef PROJECT_TDENGINEBINANCE_H
#define PROJECT_TDENGINEBINANCE_H

#include "ITDEngine.h"
#include "longfist/LFConstants.h"
#include "CoinPairWhiteList.h"
#include "InterfaceMgr.h"
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
    int64_t LimitPrice;
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

    // the trade id that has been called on_rtn_trade. Do not send it again.
    std::vector<int64_t> newSentTradeIds;
    std::vector<int64_t> sentTradeIds;

    CoinPairWhiteList coinPairWhiteList;
    CoinPairWhiteList positionWhiteList;
};

struct OrderActionInfo
{
    int64_t rcv_time;
    LFOrderActionField data;
    int request_id;
}

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
	InterfaceMgr m_interfaceMgr;

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
    bool loadExchangeOrderFilters(AccountUnitBinance& unit, Document &doc);
    void GetAndHandleOrderTradeResponse();
    void addNewSentTradeIds(AccountUnitBinance& unit, int64_t newSentTradeIds);
    void addNewQueryOrdersAndTrades(AccountUnitBinance& unit, const char_31 InstrumentID,int64_t limitPrice,
                                        const char_21 OrderRef, const LfOrderStatusType OrderStatus, const uint64_t VolumeTraded, LfDirectionType Direction, int64_t binanceOrderId);

    inline void onRspNewOrderACK(const LFInputOrderField* data, AccountUnitBinance& unit, Document& result, int requestId);
    inline void onRspNewOrderRESULT(const LFInputOrderField* data, AccountUnitBinance& unit, Document& result, int requestId);
    inline void onRspNewOrderFULL(const LFInputOrderField* data, AccountUnitBinance& unit, Document& result, int requestId);
    void onRtnNewOrder(const LFInputOrderField* data, AccountUnitBinance& unit, int requestId);
    void retrieveOrderStatus(AccountUnitBinance& unit);
    void retrieveTradeStatus(AccountUnitBinance& unit);
    void moveNewtoPending(AccountUnitBinance& unit);
    bool isExistSymbolInPendingTradeStatus(AccountUnitBinance& unit, const char_31 InstrumentID);
    bool isExistSymbolInPendingBinanceOrderStatus(AccountUnitBinance& unit, const char_31 InstrumentID, const char_21 OrderRef);
    bool removeBinanceOrderIdFromPendingOnRtnTrades(AccountUnitBinance& unit, int64_t binanceOrderId);

    int64_t fixPriceTickSize(int keepPrecision, int64_t price, bool isBuy);

    inline int64_t getTimestamp();
    int64_t getTimeDiffOfExchange(AccountUnitBinance& unit);

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

    bool shouldRetry(int http_status_code, std::string errorMsg, std::string text);
    bool order_count_over_limit();

    enum RequestWeightType
    {
        Unkonw = 0,
        GetOpenOrder_Type,
        SendOrder_Type,
        CancelOrder_Type,
        GetOrder_Type,
        TradeList_Type
    };
    struct weight_data
    {
        int weight;
        uint64_t time;

        weight_data()
        {
            memset(this, 0, sizeof(weight_data));
        }

        void addWeight(RequestWeightType type)
        {
            switch(type)
            {
                case GetOpenOrder_Type:
                    weight = 1;
                    break;
                case SendOrder_Type:
                    weight = 1;
                    break;
                case CancelOrder_Type:
                    weight = 1;
                    break;
                case GetOrder_Type:
                    weight = 1;
                    break;
                case TradeList_Type:
                    weight = 5;
                    break;
                default:
                    weight = 0;
                    break;
            }
        }
    };
    void handle_request_weight(RequestWeightType type);
    void meet_429();
    bool isHandling();
    
private:
    static constexpr int scale_offset = 1e8;
    ThreadPtr rest_thread;
    uint64_t last_rest_get_ts = 0;
    uint64_t rest_get_interval_ms = 500;

    uint64_t order_insert_recvwindow_ms = 5000;
    uint64_t order_action_recvwindow_ms = 5000;

    /////////////// order_count_over_limit ////////////////
    //code=-1429,msg:order count over 10000 limit.
    int order_count_per_second = 5;
    uint64_t order_total_count = 0;

    /////////////// request weight ////////////////
    //<=0，do nothing even meet 429
    //>0，limit weight per minute；
    int request_weight_per_minute = 1000;
    uint64_t weight_count = 0;
    std::mutex* mutex_weight = nullptr;

    //handle 429,prohibit send/cencel order time,ms
    //code=-1429,msg:order count over 10000 limit.
    int prohibit_order_ms = 10000;      //default 10s
    int default_429_rest_interval_ms = 1000;      //default 10s
    bool bHandle_429 = false;
    std::mutex* mutex_handle_429 = nullptr;
    uint64_t startTime_429 = 0;

    std::mutex* mutex_order_and_trade = nullptr;
    int SYNC_TIME_DEFAULT_INTERVAL = 10000;
    int sync_time_interval;
    int64_t timeDiffOfExchange = 0;
    int exchange_shift_ms = 0;

    int max_rest_retry_times = 3;
    int retry_interval_milliseconds = 1000;
	int m_interface_switch = 0;
    int cancel_timeout_milliseconds = 5000;
    std::map<std::string,OrderActionInfo> mapCancelOrder;
};

WC_NAMESPACE_END

#endif //PROJECT_TDENGINEBINANCE_H



