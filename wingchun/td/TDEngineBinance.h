
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
#include <queue>
#include <libwebsockets.h>
#include <atomic>
#include <string>

using rapidjson::Document;

WC_NAMESPACE_START

/**
 * account information unit extra is here.
 */

enum RequestWeightType
{
    Unkonw = 0,
    GetOpenOrder_Type,
    SendOrder_Type,
    CancelOrder_Type,
    GetOrder_Type,
    TradeList_Type,
    GetListenKey_Type,
    PutListenKey_Type
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
            case GetListenKey_Type:
                weight = 1;
                break;
            case PutListenKey_Type:
                weight = 1;
                break;
            default:
                weight = 0;
                break;
        }
    }
};

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
    int stepsize;
    //...other
};
//----rate_limit_data_map-----
struct RateLimitUnit
{
    //UFR
    std::atomic<uint64_t> order_total; //委托总量
    std::atomic<uint64_t> trade_total; //成交总量
    //GCR
    std::atomic<uint64_t> gtc_canceled_order_total;//已撤单GTC委托总量
    std::map<std::string,int64_t> mapOrderTime;//GTC发单时间
    RateLimitUnit(const RateLimitUnit& src){
         order_total = src.order_total.load();
         trade_total = src.trade_total.load();
         gtc_canceled_order_total = src.gtc_canceled_order_total.load();
         mapOrderTime = src.mapOrderTime;
    };
    RateLimitUnit(){
         order_total = 0;
         trade_total = 0;
         gtc_canceled_order_total=0;
    };
    void Reset()
    {
        order_total = 0;
        trade_total = 0;
        gtc_canceled_order_total=0;
        mapOrderTime.clear();
    };
};

struct AccountUnitBinance
{
    std::string api_key;
    std::string secret_key;
    std::string listenKey;
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
    std::map<std::string, LFRtnOrderField> ordersMap;
    std::vector<std::string> wsOrderStatus;
    // the trade id that has been called on_rtn_trade. Do not send it again.
    std::vector<int64_t> newSentTradeIds;
    std::vector<int64_t> sentTradeIds;

    CoinPairWhiteList coinPairWhiteList;
    CoinPairWhiteList positionWhiteList;

    
    uint64_t order_total_count = 0;

    uint64_t weight_count = 0;
    std::mutex* mutex_weight = nullptr;
    std::queue<weight_data> weight_data_queue;

    std::queue<long long> time_queue;
    bool bHandle_429 = false;
    std::mutex* mutex_handle_429 = nullptr;
    uint64_t startTime_429 = 0;
    std::mutex* mutex_order_and_trade = nullptr;
    struct lws_context *context = nullptr;
    struct lws * websocketConn;

    std::map<std::string, RateLimitUnit> rate_limit_data_map;
    int64_t last_rate_limit_timestamp = 0;    //
    AccountUnitBinance(const AccountUnitBinance& source);
    AccountUnitBinance();
    ~AccountUnitBinance();
};

struct OrderActionInfo
{
    int64_t rcv_time;
    LFOrderActionField data;
    int request_id;
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

        //websocket
    void on_lws_data(struct lws* conn, const char* data, size_t len);
    void on_lws_connection_error(struct lws* conn);
    int lws_write_subscribe(struct lws* conn);
    void lws_login(AccountUnitBinance& unit, long timeout_nsec);

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
    void testUTC();
    std::vector<std::string> split(std::string str, std::string token);
    bool loadExchangeOrderFilters(AccountUnitBinance& unit, Document &doc);
    inline void onRspNewOrderRESULT(const LFInputOrderField* data, AccountUnitBinance& unit, Document& result, int requestId);
    inline void onRspNewOrderFULL(const LFInputOrderField* data, AccountUnitBinance& unit, Document& result, int requestId);
    void onRtnNewOrder(const LFInputOrderField* data, AccountUnitBinance& unit, int requestId,string remoteOrderId,int64_t fixedVolume,int64_t fixedPrice);
    void moveNewtoPending(AccountUnitBinance& unit);

    int64_t fixPriceTickSize(int keepPrecision, int64_t price, bool isBuy);
    int64_t fixVolumeStepSize(int keepPrecision, int64_t volume, bool isBuy);


    inline int64_t getTimestamp();
    int64_t getTimeDiffOfExchange(AccountUnitBinance& unit);
    std::string parseJsonToString(Document &d);
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
    void get_listen_key(AccountUnitBinance& unit, Document &doc);
    void put_listen_key(AccountUnitBinance& unit, Document &doc);
    void getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& doc);
    void printResponse(const Document& d);
    inline std::string getTimestampString();

    void debug_print(std::map<std::string, SendOrderFilter> &sendOrderFilters);

    SendOrderFilter getSendOrderFilter(AccountUnitBinance& unit, const char *symbol);

    bool shouldRetry(int http_status_code, std::string errorMsg, std::string text);
    bool order_count_over_limit(AccountUnitBinance& unit);

   

    void handle_request_weight(AccountUnitBinance& unit,RequestWeightType type);
    void meet_429(AccountUnitBinance& unit);
    bool isHandling(AccountUnitBinance& unit);
    
  

    AccountUnitBinance& findAccountUnitByWebsocketConn(struct lws * websocketConn);
    void onOrder(AccountUnitBinance& unit, Document& json);
    void wsloop();

    ThreadPtr ws_thread;
private:
    static constexpr int scale_offset = 1e8;
    ThreadPtr rest_thread;
    ThreadPtr test_thread;
    uint64_t last_rest_get_ts = 0;
    uint64_t rest_get_interval_ms = 500;
    std::string restBaseUrl = "https://api.binance.com";
    std::string wsBaseUrl = "stream.binance.com";
    uint64_t order_insert_recvwindow_ms = 5000;
    uint64_t order_action_recvwindow_ms = 5000;

    /////////////// order_count_over_limit ////////////////
    //code=-1429,msg:order count over 10000 limit.
    int order_count_per_second = 5;
    

    ////////////// last UTC time  /////////////////////
    int64_t last_UTC_timestamp = 0;
  //  uint64_t last_test_timestamp = 0;

    ////////////// UFR /////////////////////
    double UFR_limit = 0.998;    //触发条件·未成交率上限
    int UFR_order_lower_limit = 300;  //触发条件·委托单数量下限
    //GCR
    double GCR_limit = 0.98;    //触发条件·GTC撤单率上限
    int GCR_order_lower_limit = 150;  //触发条件·GTC委托单数量下限
    int64_t Rate_Limit_Reset_Interval = 600000;


    /////////////// request weight ////////////////
    //<=0，do nothing even meet 429
    //>0，limit weight per minute；
    int request_weight_per_minute = 1000;
    

    //handle 429,prohibit send/cencel order time,ms
    //code=-1429,msg:order count over 10000 limit.
    int prohibit_order_ms = 10000;      //default 10s
    int default_429_rest_interval_ms = 1000;      //default 1s
    

    
    int SYNC_TIME_DEFAULT_INTERVAL = 30000;
    //int sync_time_interval = 10000;
    int64_t timeDiffOfExchange = 0;
    int exchange_shift_ms = 0;

    int max_rest_retry_times = 3;
    int retry_interval_milliseconds = 1000;
	int m_interface_switch = 0;
    int cancel_timeout_milliseconds = 5000;
    std::map<std::string,OrderActionInfo> mapCancelOrder;
    std::map<std::string,AccountUnitBinance*> mapInsertOrders;
    AccountUnitBinance& get_current_account();
    AccountUnitBinance& get_account_from_orderref(std::string& ref);
};

WC_NAMESPACE_END

#endif //PROJECT_TDENGINEBINANCE_H



