
#ifndef PROJECT_TDENGINEBINANCE_H
#define PROJECT_TDENGINEBINANCE_H

#include "ITDEngine.h"
#include "longfist/LFConstants.h"
#include <vector>
#include <sstream>
#include <map>
#include <mutex>
#include "binacpp.h"
#include "binacpp_websocket.h"
#include <json/json.h>
#include "Timer.h"

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
    uint64_t last_trade_id; //for myTrade
};

struct AccountUnitBinance
{
    std::string api_key;
    std::string secret_key;
    // internal flags
    bool    logged_in;
    //std::mutex mutex_order;
    std::vector<PendingBinanceOrderStatus> pendingOrderStatus;
    //std::mutex mutex_trade;
    std::vector<PendingBinanceTradeStatus> pendingTradeStatus;
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
    void GetAndHandleOrderTradeResponse();
    void addPendingQueryOrdersAndTrades(AccountUnitBinance& unit, const char_31 InstrumentID,
                                        const char_21 OrderRef, const LfOrderStatusType OrderStatus, const uint64_t VolumeTraded);

    inline void onRspNewOrderACK(const LFInputOrderField* data, AccountUnitBinance& unit, Json::Value& result, int requestId);
    inline void onRspNewOrderRESULT(const LFInputOrderField* data, AccountUnitBinance& unit, Json::Value& result, int requestId);
    inline void onRspNewOrderFULL(const LFInputOrderField* data, AccountUnitBinance& unit, Json::Value& result, int requestId);

    void retrieveOrderStatus(AccountUnitBinance& unit);
    void retrieveTradeStatus(AccountUnitBinance& unit);
    static constexpr int scale_offset = 1e8;

    ThreadPtr rest_thread;
    uint64_t last_rest_get_ts = 0;
    int rest_get_interval_ms = 500;
};

WC_NAMESPACE_END

#endif //PROJECT_TDENGINEBINANCE_H



