
#ifndef PROJECT_TDENGINEMOCK_H
#define PROJECT_TDENGINEMOCK_H

#include "ITDEngine.h"
#include "longfist/LFConstants.h"
#include <vector>
#include <sstream>
#include <map>
#include <atomic>
#include <mutex>
#include "Timer.h"
#include <document.h>
#include "CoinPairWhiteList.h"
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

    CoinPairWhiteList coinPairWhiteList;
    CoinPairWhiteList positionWhiteList;
};


/**
 * CTP trade engine
 */
class TDEngineMock: public ITDEngine
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
    virtual string name() const { return "TDEngineMock"; };

    // req functions
    virtual void req_investor_position(const LFQryPositionField* data, int account_index, int requestId);
    virtual void req_qry_account(const LFQryAccountField* data, int account_index, int requestId);
    virtual void req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time);
    virtual void req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time);

public:
    TDEngineMock();
    ~TDEngineMock();
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
    void getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& json);
    void printResponse(const Document& d);
    inline std::string getTimestampString();

};

WC_NAMESPACE_END

#endif //PROJECT_TDEngineMock_H



