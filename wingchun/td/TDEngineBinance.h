
#ifndef PROJECT_TDENGINEBINANCE_H
#define PROJECT_TDENGINEBINANCE_H

#include "ITDEngine.h"
#include "longfist/LFConstants.h"
#include "ThostFtdcTraderApi.h"

#include <sstream>
#include "binacpp.h"
#include "binacpp_websocket.h"
#include <json/json.h>
#include "Timer.h"

WC_NAMESPACE_START

/**
 * account information unit extra for CTP is here.
 */
struct AccountUnitBinance
{
    string api_key;
	string secret_key;
    // internal flags
    bool    logged_in;
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
    static constexpr int scale_offset = 1e8;
    // journal writers
    yijinjing::JournalWriterPtr raw_writer;
    vector<AccountUnitBinance> account_units;

    std::string GetSide(const LfDirectionType& input);
    std::string GetType(const LfOrderPriceTypeType& input);
    std::string GetTimeInForce(const LfTimeConditionType& input);
    std::string GetInputOrderData(const LFInputOrderField* order, int recvWindow);
    // rsp functions
    void OnRspOrderInsert(CThostFtdcInputOrderField *pInputOrder, CThostFtdcRspInfoField *pRspInfo,
                                    int nRequestID, bool bIsLast);
    void OnRtnOrder(CThostFtdcOrderField *pOrder);
    void OnRspQryInvestorPosition(CThostFtdcInvestorPositionField *pInvestorPosition, CThostFtdcRspInfoField *pRspInfo,
                                     int nRequestID, bool bIsLast);
public:
    
    
};

WC_NAMESPACE_END

#endif //PROJECT_TDENGINEBINANCE_H



