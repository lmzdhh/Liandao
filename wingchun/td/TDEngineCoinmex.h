
#ifndef PROJECT_TDENGINECOINMEX_H
#define PROJECT_TDENGINECOINMEX_H

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

struct PendingCoinmexOrderStatus
{
    char_31 InstrumentID;   //合约代码
    char_21 OrderRef;       //报单引用
    LfOrderStatusType OrderStatus;  //报单状态
    uint64_t VolumeTraded;  //今成交数量
    uint64_t averagePrice;//coinmex given averagePrice on response of query_order
    std::string remoteOrderId;//coinmex sender_order response order id://{"orderId":19319936159776,"result":true}
};


struct SendOrderFilter
{
    char_31 InstrumentID;   //合约代码
    int ticksize; //for price round.
    //...other
};

struct SubscribeCoinmexBaseQuote
{
    std::string base;
    std::string quote;
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

    //in TD, lookup direction is:
    // our strategy recognized coinpair ---> outcoming exchange coinpair
    //if strategy's coinpair is not in this map ,ignore it
    //"strategy_coinpair(base_quote)":"exchange_coinpair",
    std::map<std::string, std::string> keyIsStrategyCoinpairWhiteList;

    std::vector<SubscribeCoinmexBaseQuote> subscribeCoinmexBaseQuote;
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
private:
    // journal writers
    yijinjing::JournalWriterPtr raw_writer;
    vector<AccountUnitCoinmex> account_units;

    std::string GetSide(const LfDirectionType& input);
    LfDirectionType GetDirection(std::string input);
    std::string GetType(const LfOrderPriceTypeType& input);
    LfOrderPriceTypeType GetPriceType(std::string input);
    LfOrderStatusType GetOrderStatus(std::string input);

    virtual void set_reader_thread() override;
    void loop();
    std::vector<std::string> split(std::string str, std::string token);
    void GetAndHandleOrderTradeResponse();
    void addNewQueryOrdersAndTrades(AccountUnitCoinmex& unit, const char_31 InstrumentID,
                                    const char_21 OrderRef, const LfOrderStatusType OrderStatus,
                                    const uint64_t VolumeTraded, std::string remoteOrderId);

    void retrieveOrderStatus(AccountUnitCoinmex& unit);
    void moveNewtoPending(AccountUnitCoinmex& unit);
    static constexpr int scale_offset = 1e8;

    ThreadPtr rest_thread;
    uint64_t last_rest_get_ts = 0;
    uint64_t rest_get_interval_ms = 500;

    std::mutex* mutex_order_and_trade = nullptr;

    std::map<std::string, std::string> localOrderRefRemoteOrderId;

    int SYNC_TIME_DEFAULT_INTERVAL = 10000;
    int sync_time_interval;
    int64_t timeDiffOfExchange = 0;
    int exchange_shift_ms = 0;
private:
    int HTTP_RESPONSE_OK = 200;
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

    int Round(std::string tickSizeStr);
    int64_t fixPriceTickSize(int keepPrecision, int64_t price, bool isBuy);
    bool loadExchangeOrderFilters(AccountUnitCoinmex& unit, Document &doc);
    void debug_print(std::map<std::string, SendOrderFilter> &sendOrderFilters);
    SendOrderFilter getSendOrderFilter(AccountUnitCoinmex& unit, const char *symbol);
private:
    inline int64_t getTimestamp();
    int64_t getTimeDiffOfExchange(AccountUnitCoinmex& unit);
    void readWhiteLists(AccountUnitCoinmex& unit, const json& j_config);
    std::string getWhiteListCoinpairFrom(AccountUnitCoinmex& unit, const char_31 strategy_coinpair);
    bool hasSymbolInWhiteList(std::vector<SubscribeCoinmexBaseQuote> &sub, std::string symbol);
    void split(std::string str, std::string token, SubscribeCoinmexBaseQuote& sub);
    void debug_print(std::vector<SubscribeCoinmexBaseQuote> &sub);
    void debug_print(std::map<std::string, std::string> &keyIsStrategyCoinpairWhiteList);
};

WC_NAMESPACE_END

#endif //PROJECT_TDENGINECOINMEX_H



