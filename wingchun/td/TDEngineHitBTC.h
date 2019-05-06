
#ifndef PROJECT_TDENGINEHitBTC_H
#define PROJECT_TDENGINEHitBTC_H

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

//struct PendingHitBTCOrderStatus
//{
//    char_31 InstrumentID;   //合约代码
//    char_21 OrderRef;       //报单引用
//    LfOrderStatusType OrderStatus;  //报单状态
//    uint64_t VolumeTraded;  //今成交数量
//    uint64_t averagePrice;//HitBTC given averagePrice on response of query_order
//};
//
//struct PendingHitBTCTradeStatus
//{
//    char_31 InstrumentID;   //合约代码
//    uint64_t last_trade_id; //for myTrade
//};
//
//struct SendOrderFilter
//{
//    char_31 InstrumentID;   //合约代码
//    int ticksize; //for price round.
//    //...other
//};
//
//struct SubscribeHitBTCBaseQuote
//{
//    std::string base;
//    std::string quote;
//};

struct PositionSetting
{

            string ticker;
            bool isLong;
            uint64_t amount;

};

struct AccountUnitHitBTC
{
    string api_key;
    string secret_key;
    string baseUrl;
    // internal flags
    bool    logged_in;

    CoinPairWhiteList coinPairWhiteList;
    CoinPairWhiteList positionWhiteList;
    std::vector<std::string> newPendingSendMsg;
    std::vector<std::string> pendingSendMsg;
    struct lws * websocketConn;
};

        struct OrderInsertData
        {
            LFInputOrderField data;
            int requestId;
            //交易所反馈的orderId,在通知消息on_req能得到这个数据, findOrderRefByOrderid() will use this, for the trade info only has orderId
            int64_t remoteOrderId;

            //    撤单有两种，一种是用orderid 另一种是用clientid + date ,所以在发单时候记录一下当前日期，留待撤单时使用
            //            如果撤单时发现有orderid  会优先使用orderid ，没有orderid 的时候，才会使用clientid + date
            //            这种情况可能发生在 我们发单以后， on-req还没有来得及返回orderid，我们就发送撤单指令了
            std::string dateStr;

            LFRtnOrderField rtnOrder;
        };

        struct OrderActionData
        {
            LFOrderActionField data;
            int requestId;
        };

/**
 * CTP trade engine
 */
class TDEngineHitBTC: public ITDEngine
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
    virtual string name() const { return "TDEngineHitBTC"; };

    // req functions
    virtual void req_investor_position(const LFQryPositionField* data, int account_index, int requestId);
    virtual void req_qry_account(const LFQryAccountField* data, int account_index, int requestId);//not needed
    virtual void req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time);
    virtual void req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time);

public:
    TDEngineHitBTC();
    ~TDEngineHitBTC();


    void on_lws_data(struct lws* conn, const char* data, size_t len);
    void on_lws_connection_error(struct lws* conn);
    int lws_write_subscribe(struct lws* conn);
private:
    // journal writers
    yijinjing::JournalWriterPtr raw_writer;
    vector<AccountUnitHitBTC> account_units;

    virtual void set_reader_thread() override;

    std::string GetSide(const LfDirectionType& input);
    //LfDirectionType GetDirection(std::string input);
    std::string GetType(const LfOrderPriceTypeType& input);
    LfOrderPriceTypeType GetPriceType(std::string input);
    LfOrderStatusType GetOrderStatus(std::string input);

    //void loop();

    inline int64_t getTimestamp();

    inline std::string getTimestampString();

    std::string createAuthJsonString(AccountUnitHitBTC& unit );
    std::string parseJsonToString(Document &d);
    std::string createInsertOrderJsonString(int gid, std::string clientOrderId, std::string type, std::string symbol, std::string amountStr,
            std::string priceStr, std::string sideStr);
    std::string createCancelOrderIdJsonString(std::string orderId);
    std::string createCancelOrderCIdJsonString(int cid, std::string dateStr);
    std::string getDateStr();
    std::string createSubReportJsonString();

    void lws_login(AccountUnitHitBTC& unit, long timeout_nsec);
    void onInfo(Document& json);
    void onAuth(struct lws * websocketConn, Document& json);
    void onPing(Document& json);
    void onPosition(struct lws * websocketConn, Document& json);
    void onTradeExecuted(struct lws * websocketConn, Document& json);
    void onTradeExecutionUpdate(struct lws * websocketConn, Document& json);
    void onOrderSnapshot(struct lws * websocketConn, Document& json);
    void onOrderNewUpdateCancel(struct lws * websocketConn, Document& json);
    void onNotification(struct lws * websocketConn, Document& json);


    void onOrder(struct lws * websocketConn, rapidjson::Value& json);
    void wsloop();


    AccountUnitHitBTC& findAccountUnitHitBTCByWebsocketConn(struct lws * websocketConn);

    void addPendingSendMsg(AccountUnitHitBTC& unit, std::string msg);
    void moveNewtoPending(AccountUnitHitBTC& unit);

    OrderInsertData findOrderInsertDataByOrderId(int64_t orderId);
    OrderInsertData findOrderInsertDataByOrderRef(const char_21 orderRef);

    static constexpr int scale_offset = 1e8;
    struct lws_context *context = nullptr;
    ThreadPtr ws_thread;


    ThreadPtr rest_thread;
    uint64_t last_rest_get_ts = 0;
    int rest_get_interval_ms = 500;
    std::vector<PositionSetting> positionHolder;


    std::mutex* mutex_order_and_trade = nullptr;

    std::map<std::string, std::string> localOrderRefRemoteOrderId;

    int SYNC_TIME_DEFAULT_INTERVAL = 10000;
    int sync_time_interval;
    int64_t timeDiffOfExchange = 0;

private:


    std::unordered_map<int, OrderInsertData> CIDorderInsertData;
    std::unordered_map<int, OrderActionData> CIDorderActionData;
    std::unordered_map<int, OrderActionData> pendingOrderActionData;
    std::unordered_map<int64_t, OrderActionData> RemoteOrderIDorderActionData;

};

WC_NAMESPACE_END

#endif //PROJECT_TDEngineHitBTC_H



