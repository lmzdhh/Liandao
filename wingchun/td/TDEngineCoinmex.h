
#ifndef PROJECT_TDENGINECOINMEX_H
#define PROJECT_TDENGINECOINMEX_H

#include "ITDEngine.h"
#include "longfist/LFConstants.h"
#include <vector>
#include <sstream>
#include <map>

#include "Timer.h"

WC_NAMESPACE_START

/**
 * account information unit extra for CTP is here.
 */
struct AccountUnitCoinmex
{
    string api_key;
	string secret_key;
    // internal flags
    bool    logged_in;
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

private:
    // journal writers
    yijinjing::JournalWriterPtr raw_writer;
    vector<AccountUnitCoinmex> account_units;

    std::string GetSide(const LfDirectionType& input);
    LfDirectionType GetDirection(std::string input);
    std::string GetType(const LfOrderPriceTypeType& input);
    LfOrderPriceTypeType GetPriceType(std::string input);
    std::string GetTimeInForce(const LfTimeConditionType& input);
    LfTimeConditionType GetTimeCondition(std::string input);
    LfOrderStatusType GetOrderStatus(std::string input);
    std::string GetInputOrderData(const LFInputOrderField* order, int recvWindow);

    void loop();
    std::vector<std::string> split(std::string str, std::string token);
    void GetAndHandleOrderResponse();

    static constexpr int scale_offset = 1e8;
    std::map<std::string, std::vector<std::string>*> symbols_pending_orderref;
    ThreadPtr rest_thread;
    uint64_t last_rest_get_ts = 0;
    int rest_get_interval_ms = 500;
    
    //for CMake to load JSON
    void GetAndHandleTradeResponse(const std::string& symbol, int limit);
    
};

WC_NAMESPACE_END

#endif //PROJECT_TDENGINECOINMEX_H



