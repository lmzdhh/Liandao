
#ifndef PROJECT_TDENGINEBITMEX_H
#define PROJECT_TDENGINEBITMEX_H

#include "ITDEngine.h"
#include "longfist/LFConstants.h"
#include <vector>
#include <sstream>
#include <map>
#include <atomic>
#include <mutex>
#include "Timer.h"
#include <document.h>

using rapidjson::Document;

WC_NAMESPACE_START

/**
 * account information unit extra is here.
 */

struct PendingBitmexOrderStatus
{
    char_31 InstrumentID;   //合约代码
    char_21 OrderRef;       //报单引用
    LfOrderStatusType OrderStatus;  //报单状态
    uint64_t VolumeTraded;  //今成交数量
    uint64_t averagePrice;//coinmex given averagePrice on response of query_order
};

struct PendingBitmexTradeStatus
{
    char_31 InstrumentID;   //合约代码
    uint64_t last_trade_id; //for myTrade
};

struct SendOrderFilter
{
    char_31 InstrumentID;   //合约代码
    int ticksize; //for price round.
    //...other
};

struct SubscribeBitmexBaseQuote
{
    std::string base;
    std::string quote;
};

struct AccountUnitBitmex
{
    string api_key;
    string secret_key;
    string passphrase;
    //coinmex and bitmore use the same api, use this parameter for them
    string baseUrl;
    // internal flags
    bool    logged_in;
    std::vector<PendingBitmexOrderStatus> newOrderStatus;
    std::vector<PendingBitmexOrderStatus> pendingOrderStatus;
    std::map<std::string, SendOrderFilter> sendOrderFilters;

    //in TD, lookup direction is:
    // our strategy recognized coinpair ---> outcoming exchange coinpair
    //if strategy's coinpair is not in this map ,ignore it
    //"strategy_coinpair(base_quote)":"exchange_coinpair",
    std::map<std::string, std::string> keyIsStrategyCoinpairWhiteList;

    std::vector<SubscribeBitmexBaseQuote> subscribeBitmexBaseQuote;
};


/**
 * CTP trade engine
 */
class TDEngineBitmex: public ITDEngine
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
    virtual string name() const { return "TDEngineBitmex"; };

    // req functions
    virtual void req_investor_position(const LFQryPositionField* data, int account_index, int requestId);
    virtual void req_qry_account(const LFQryAccountField* data, int account_index, int requestId);
    virtual void req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time);
    virtual void req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time);

public:
    TDEngineBitmex();
    ~TDEngineBitmex();
private:
    // journal writers
    yijinjing::JournalWriterPtr raw_writer;
    vector<AccountUnitBitmex> account_units;

    std::string GetSide(const LfDirectionType& input);
    LfDirectionType GetDirection(std::string input);
    std::string GetType(const LfOrderPriceTypeType& input);
    LfOrderPriceTypeType GetPriceType(std::string input);
    LfOrderStatusType GetOrderStatus(std::string input);

    void loop();
    std::vector<std::string> split(std::string str, std::string token);
    void GetAndHandleOrderTradeResponse();
    void addNewQueryOrdersAndTrades(AccountUnitBitmex& unit, const char_31 InstrumentID,
                                    const char_21 OrderRef, const LfOrderStatusType OrderStatus, const uint64_t VolumeTraded);

    void retrieveOrderStatus(AccountUnitBitmex& unit);
    void moveNewtoPending(AccountUnitBitmex& unit);
    static constexpr int scale_offset = 1e8;

    ThreadPtr rest_thread;
    uint64_t last_rest_get_ts = 0;
    int rest_get_interval_ms = 500;

    std::mutex* mutex_order_and_trade = nullptr;

    std::map<std::string, std::string> localOrderRefRemoteOrderId;

    int SYNC_TIME_DEFAULT_INTERVAL = 10000;
    int sync_time_interval;
    int64_t timeDiffOfExchange = 0;

private:
    int HTTP_RESPONSE_OK = 200;
    void get_exchange_time(AccountUnitBitmex& unit, Document& json);
    void get_account(AccountUnitBitmex& unit, Document& json);
    void get_depth(AccountUnitBitmex& unit, std::string code, Document& json);
    void get_products(AccountUnitBitmex& unit, Document& json);
    void send_order(AccountUnitBitmex& unit, const char *code,
                        const char *side, const char *type, double size, double price, double funds, Document& json);

    //void cancel_all_orders(AccountUnitBitmex& unit, std::string code, Document& json);
    void cancel_all_orders(AccountUnitBitmex& unit, Document& json);
    void cancel_order(AccountUnitBitmex& unit, std::string code, long orderId, Document& json);
    void query_orders(AccountUnitBitmex& unit, std::string code, std::string status, Document& json);
    void query_order(AccountUnitBitmex& unit, std::string code, long orderId, Document& json);
    void getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& json);
    void printResponse(const Document& d);
    inline std::string getTimestampString();

    int Round(std::string tickSizeStr);
    int64_t fixPriceTickSize(int keepPrecision, int64_t price, bool isBuy);
    bool loadExchangeOrderFilters(AccountUnitBitmex& unit, Document &doc);
    void debug_print(std::map<std::string, SendOrderFilter> &sendOrderFilters);
    SendOrderFilter getSendOrderFilter(AccountUnitBitmex& unit, const char *symbol);
private:
    inline int64_t getTimestamp();
    int64_t getTimeDiffOfExchange(AccountUnitBitmex& unit);
    void readWhiteLists(AccountUnitBitmex& unit, const json& j_config);
    std::string getWhiteListCoinpairFrom(AccountUnitBitmex& unit, const char_31 strategy_coinpair);
    bool hasSymbolInWhiteList(std::vector<SubscribeBitmexBaseQuote> &sub, std::string symbol);
    void split(std::string str, std::string token, SubscribeBitmexBaseQuote& sub);
    void debug_print(std::vector<SubscribeBitmexBaseQuote> &sub);
    void debug_print(std::map<std::string, std::string> &keyIsStrategyCoinpairWhiteList);
};

WC_NAMESPACE_END

#endif //PROJECT_TDENGINEBITMEX_H



