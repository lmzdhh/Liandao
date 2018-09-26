
#ifndef PROJECT_TDENGINECOINMEX_H
#define PROJECT_TDENGINECOINMEX_H

#include "ITDEngine.h"
#include "longfist/LFConstants.h"
#include "CoinPairWhiteList.h"
#include <vector>
#include <sstream>
#include <map>
#include <atomic>
#include <mutex>
#include "Timer.h"
#include <document.h>
#include <libwebsockets.h>

using rapidjson::Document;

WC_NAMESPACE_START

/**
 * account information unit extra is here.
 */

struct PendingCoinmexOrderStatus
{
    char_31 InstrumentID;   //合约代码
    char_21 OrderRef;       //报单引用
    LfOrderStatusType OrderStatus;  //报单状态
    uint64_t VolumeTraded;  //今成交数量
    int64_t averagePrice;//coinmex given averagePrice on response of query_order
    int64_t remoteOrderId;//coinmex sender_order response order id://{"orderId":19319936159776,"result":true}
};

struct ResponsedOrderStatus
{
    int64_t averagePrice = 0;
    std::string ticker;
    int64_t createdDate = 0;


    //今成交数量
    uint64_t VolumeTraded;
    int id = 0;
    uint64_t openVolume = 0;
    int64_t orderId = 0;
    std::string orderType;
    //报单价格条件
    LfOrderPriceTypeType OrderPriceType;
    int64_t price = 0;
    //买卖方向
    LfDirectionType Direction;

    //报单状态
    LfOrderStatusType OrderStatus;
    uint64_t trunoverVolume = 0;
    uint64_t volume = 0;
};

struct SendOrderFilter
{
    char_31 InstrumentID;   //合约代码
    int ticksize; //for price round.
    //...other
};

struct AccountUnitCoinmex
{
    string api_key;
    string secret_key;
    string passphrase;
    //coinmex and bitmore use the same api, use this parameter for them
    string baseUrl;
    // internal flags
    bool    logged_in;
    std::vector<PendingCoinmexOrderStatus> newOrderStatus;
    std::vector<PendingCoinmexOrderStatus> pendingOrderStatus;
    std::map<std::string, SendOrderFilter> sendOrderFilters;

    CoinPairWhiteList coinPairWhiteList;
    CoinPairWhiteList positionWhiteList;

    std::vector<std::string> newPendingSendMsg;
    std::vector<std::string> pendingSendMsg;
    struct lws * websocketConn;
};


/**
 * CTP trade engine
 */
class TDEngineCoinmex: public ITDEngine
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
    virtual string name() const { return "TDEngineCoinmex"; };

    // req functions
    virtual void req_investor_position(const LFQryPositionField* data, int account_index, int requestId);
    virtual void req_qry_account(const LFQryAccountField* data, int account_index, int requestId);
    virtual void req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time);
    virtual void req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time);

public:
    TDEngineCoinmex();
    ~TDEngineCoinmex();



    //websocket
    void on_lws_data(struct lws* conn, const char* data, size_t len);
    void on_lws_connection_error(struct lws* conn);
    int lws_write_subscribe(struct lws* conn);
    void lws_login(AccountUnitCoinmex& unit, long timeout_nsec);



private:
    bool use_restful_to_receive_status = false;
    // journal writers
    yijinjing::JournalWriterPtr raw_writer;
    vector<AccountUnitCoinmex> account_units;

    std::string GetSide(const LfDirectionType& input);
    LfDirectionType GetDirection(std::string input);
    std::string GetType(const LfOrderPriceTypeType& input);
    LfOrderPriceTypeType GetPriceType(std::string input);
    LfOrderStatusType GetOrderStatus(std::string input);
    int Round(std::string tickSizeStr);

    virtual void set_reader_thread() override;
    void loop();
    std::vector<std::string> split(std::string str, std::string token);
    void GetAndHandleOrderTradeResponse();
    void addNewQueryOrdersAndTrades(AccountUnitCoinmex& unit, const char_31 InstrumentID,
                                    const char_21 OrderRef, const LfOrderStatusType OrderStatus,
                                    const uint64_t VolumeTraded, int64_t remoteOrderId);
    void retrieveOrderStatus(AccountUnitCoinmex& unit);
    void moveNewOrderStatusToPending(AccountUnitCoinmex& unit);

    void handlerResponseOrderStatus(AccountUnitCoinmex& unit, std::vector<PendingCoinmexOrderStatus>::iterator orderStatusIterator, ResponsedOrderStatus& responsedOrderStatus);
    void addResponsedOrderStatusNoOrderRef(ResponsedOrderStatus &responsedOrderStatus, Document& json);

    inline int64_t getTimestamp();
    int64_t getTimeDiffOfExchange(AccountUnitCoinmex& unit);

    //websocket
    AccountUnitCoinmex& findAccountUnitByWebsocketConn(struct lws * websocketConn);
    void onOrder(struct lws * websocketConn, Document& json);
    void wsloop();
    void addWebsocketPendingSendMsg(AccountUnitCoinmex& unit, std::string msg);
    void moveNewWebsocketMsgToPending(AccountUnitCoinmex& unit);
    std::string createAuthJsonString(AccountUnitCoinmex& unit );
    std::string createOrderJsonString();

    std::string parseJsonToString(Document &d);

    void handlerResponsedOrderStatus(AccountUnitCoinmex& unit);
private:
    void get_exchange_time(AccountUnitCoinmex& unit, Document& json);
    void get_account(AccountUnitCoinmex& unit, Document& json);
    void get_depth(AccountUnitCoinmex& unit, std::string code, Document& json);
    void get_products(AccountUnitCoinmex& unit, Document& json);
    void send_order(AccountUnitCoinmex& unit, const char *code,
                        const char *side, const char *type, double size, double price, double funds, Document& json);

    void cancel_all_orders(AccountUnitCoinmex& unit, std::string code, Document& json);
    void cancel_order(AccountUnitCoinmex& unit, std::string code, std::string orderId, Document& json);
    void query_orders(AccountUnitCoinmex& unit, std::string code, std::string status, Document& json);
    void query_order(AccountUnitCoinmex& unit, std::string code, std::string orderId, Document& json);
    void getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& json);
    void printResponse(const Document& d);
    inline std::string getTimestampString();

    bool shouldRetry(int http_status_code, std::string errorMsg);


    int64_t fixPriceTickSize(int keepPrecision, int64_t price, bool isBuy);
    bool loadExchangeOrderFilters(AccountUnitCoinmex& unit, Document &doc);
    void debug_print(std::map<std::string, SendOrderFilter> &sendOrderFilters);
    SendOrderFilter getSendOrderFilter(AccountUnitCoinmex& unit, const char *symbol);

private:

    struct lws_context *context = nullptr;

    int HTTP_RESPONSE_OK = 200;
    static constexpr int scale_offset = 1e8;

    ThreadPtr rest_thread;
    ThreadPtr ws_thread;

    uint64_t last_rest_get_ts = 0;
    uint64_t rest_get_interval_ms = 500;
    uint64_t ws_get_interval_ms = 500;

    std::mutex* mutex_order_and_trade = nullptr;

    std::map<std::string, int64_t> localOrderRefRemoteOrderId;

    std::vector<ResponsedOrderStatus> responsedOrderStatusNoOrderRef;

    int SYNC_TIME_DEFAULT_INTERVAL = 10000;
    int sync_time_interval;
    int64_t timeDiffOfExchange = 0;
    int exchange_shift_ms = 0;

    int MAX_REST_RETRY_TIMES = 3;
    int RETRY_INTERVAL_MILLISECONDS = 1000;
};

WC_NAMESPACE_END

#endif //PROJECT_TDENGINECOINMEX_H



