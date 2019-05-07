
#ifndef PROJECT_TDENGINEBITMAX_H
#define PROJECT_TDENGINEBITMAX_H

#include "ITDEngine.h"
#include "longfist/LFConstants.h"
#include "CoinPairWhiteList.h"
#include <vector>
#include <sstream>
#include <unordered_map>
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
};

struct PendingCoinmexTradeStatus
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

struct AccountUnitCoinmex
{
    string api_key;
    string secret_key;
    //coinmex and bitmore use the same api, use this parameter for them
    string baseUrl;
    // internal flags
    bool    logged_in;
    std::vector<PendingCoinmexOrderStatus> newOrderStatus;
    std::vector<PendingCoinmexOrderStatus> pendingOrderStatus;
    std::map<std::string, SendOrderFilter> sendOrderFilters;

    CoinPairWhiteList coinPairWhiteList;
    CoinPairWhiteList positionWhiteList;
};


/**
 * CTP trade engine
 */
class TDEngineBitmax: public ITDEngine
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
    virtual string name() const { return "TDEngineBitmax"; };

    // req functions
    virtual void req_investor_position(const LFQryPositionField* data, int account_index, int requestId);
    virtual void req_qry_account(const LFQryAccountField* data, int account_index, int requestId);
    virtual void req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time);
    virtual void req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time);

public:
    TDEngineBitmax();
    ~TDEngineBitmax();
private:
    // journal writers
    yijinjing::JournalWriterPtr raw_writer;
    vector<AccountUnitCoinmex> account_units;

    std::string GetSide(const LfDirectionType& input);
    LfDirectionType GetDirection(std::string input);
    std::string GetType(const LfOrderPriceTypeType& input);
    LfOrderPriceTypeType GetPriceType(std::string input);
    LfOrderStatusType GetOrderStatus(std::string input);

    void loop();
    std::vector<std::string> split(std::string str, std::string token);
    static constexpr int scale_offset = 1e8;

    ThreadPtr rest_thread;
    uint64_t last_rest_get_ts = 0;
    int rest_get_interval_ms = 500;

    std::mutex* mutex_order_and_trade = nullptr;


private:
    int HTTP_RESPONSE_OK = 200;

    void get_account(AccountUnitCoinmex& unit, Document& json);
    void get_products(AccountUnitCoinmex& unit, Document& json);
    void send_order(AccountUnitCoinmex& unit, const char *code, const char *coid,
                        const char *side, const char *type, double size, double price, Document& json);

    void cancel_order(AccountUnitCoinmex& unit, const char *code, const char *coid, const char *origCoid, Document& json);
    void query_orders(AccountUnitCoinmex& unit, Document& json);

    void getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& json);
    void printResponse(const Document& d);
    inline std::string getTimestampString();

    int64_t fixPriceTickSize(int keepPrecision, int64_t price, bool isBuy);
    bool loadExchangeOrderFilters(AccountUnitCoinmex& unit, Document &doc);
    void debug_print(std::map<std::string, SendOrderFilter> &sendOrderFilters);
    SendOrderFilter getSendOrderFilter(AccountUnitCoinmex& unit, const char *symbol);

    inline int64_t getTimestamp();
};

WC_NAMESPACE_END

#endif //PROJECT_TDEngineBitmax_H



