#pragma once
#ifndef PROJECT_TDENGINEPOLONIEX_H
#define PROJECT_TDENGINEPOLONIEX_H

#include "ITDEngine.h"
#include "longfist/LFConstants.h"
#include "CoinPairWhiteList.h"
#include<iostream>
#include <vector>
#include <sstream>
#include <map>
#include <atomic>
#include <mutex>
#include "Timer.h"
#include <libwebsockets.h>
#include <cpr/cpr.h>
//#include <document.h>
/*using rapidjson::Document;*/

WC_NAMESPACE_START

struct PositionSetting
{
    string ticker;
    bool isLong;
    uint64_t amount;
};
struct OrderInfo
{
    string child_order_acceptance_id;
    int64_t timestamp;
    int64_t requestId;
    string product_code;
};
struct AccountUnitPoloniex
{
    string api_key;
    string secret_key;
    string baseUrl;
    // internal flags
    bool    logged_in;

    CoinPairWhiteList coinPairWhiteList;
    CoinPairWhiteList positionWhiteList;

    map<int, OrderInfo> map_new_order;
    std::vector<PositionSetting> positionHolder;//记录每种持仓币种的情况，需要函数来更新
};
class TDEnginePoloniex : public ITDEngine
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
    virtual string name() const { return "TDEnginePoloniex"; };

    // req functions  //this exchanger has no account_index ,just ignore
    virtual void req_investor_position(const LFQryPositionField* data, int account_index, int requestId);
    virtual void req_qry_account(const LFQryAccountField* data, int account_index, int requestId);
    virtual void req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time);
    virtual void req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time);


public:
    TDEnginePoloniex();
    ~TDEnginePoloniex();

private:
    // journal writers
    yijinjing::JournalWriterPtr raw_writer;
    vector<AccountUnitPoloniex> account_units;

    inline int64_t get_timestamp();
    string get_order_type(LfOrderPriceTypeType type);
    string get_order_side(LfDirectionType type);
    int get_response_parsed_position(cpr::Response r);

    cpr::Response rest_withoutAuth(string& method, string& command);
    cpr::Response rest_withAuth(AccountUnitPoloniex& unit, string& method, string& command);

    cpr::Response return_orderbook();//可用来测试接口实现是否有问题
    cpr::Response get_order_status(int requestId);

private:

    static constexpr int scale_offset = 1e8;
    int retry_interval_milliseconds;
    ThreadPtr rest_thread;



    std::mutex* mutex_order_and_trade = nullptr;
    std::mutex* mutex_response_order_status = nullptr;
    std::mutex* mutex_orderaction_waiting_response = nullptr;
};

WC_NAMESPACE_END

#endif //PROJECT_TDENGINEPOLONIEX_H
