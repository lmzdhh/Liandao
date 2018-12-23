
#ifndef PROJECT_TDENGINEDaybit_H
#define PROJECT_TDENGINEDaybit_H

#include "ITDEngine.h"
#include "longfist/LFConstants.h"
#include "CoinPairWhiteList.h"
#include <vector>
#include <queue>
#include <sstream>
#include <map>
#include <atomic>
#include <mutex>
#include "Timer.h"
#include <document.h>
#include <libwebsockets.h>
#include <cpr/cpr.h>
using rapidjson::Document;

/*
- endpoint: wss://api.staging.daybit.com/v1/user_api_socket/websocket/
- api key: 6nIg0pkRWfb0JhFtUsO9bM1GTpTYfgJs
- api secret: kQ4eMfse0TKsCe838iP_6AAw7gZNMpUh
*/
WC_NAMESPACE_START

/**
 * account information unit extra is here.
 */

struct SendOrderFilter
{
    std::string InstrumentID;   //合约代码
    double ticksize; //for price round.
};


struct AccountUnitDaybit
{
    string api_key;
    string secret_key; 
    string baseUrl;
    string path;
    // internal flags
    bool    logged_in;
    std::map<std::string, SendOrderFilter> sendOrderFilters;
	std::map<int64_t, LFRtnOrderField> ordersMap;
    std::map<int64_t, std::pair<LFRtnOrderField,LFInputOrderField>> ordersLocalMap;
    CoinPairWhiteList coinPairWhiteList;
    CoinPairWhiteList positionWhiteList;
    std::queue<std::string> listMessageToSend;
    std::map<std::string, int64_t> mapSubscribeRef;
    std::vector<std::string> pendingSendMsg;
    struct lws * websocketConn;
    int maxRetryCount=3;
};


/**
 * CTP trade engine
 */
class TDEngineDaybit: public ITDEngine
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
    virtual string name() const { return "TDEngineDaybit"; };

    // req functions
    virtual void req_investor_position(const LFQryPositionField* data, int account_index, int requestId);
    virtual void req_qry_account(const LFQryAccountField* data, int account_index, int requestId);
    virtual void req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time);
    virtual void req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time);

public:
    TDEngineDaybit();
    ~TDEngineDaybit();


   
    //websocket
    void on_lws_data(struct lws* conn, const char* data, size_t len);
    void on_lws_connection_error(struct lws* conn);
    int on_lws_write(struct lws* conn);
    void lws_login(AccountUnitDaybit& unit, long timeout_nsec);
    
    int Round(std::string tickSizeStr);
private:
    // journal writers
    yijinjing::JournalWriterPtr raw_writer;
    vector<AccountUnitDaybit> account_units;

    virtual void set_reader_thread() override;
    LfDirectionType GetDirection(bool isSell);
    std::string GetType(const LfOrderPriceTypeType& input);
    LfOrderPriceTypeType GetPriceType(std::string input);
    LfOrderStatusType GetOrderStatus(std::string input);
    void addNewOrder(AccountUnitDaybit& unit, const char_31 InstrumentID,
                                    const char_21 OrderRef, LfDirectionType direction,const LfOrderStatusType OrderStatus, const uint64_t VolumeTraded, int reqID,int64_t ref,LFInputOrderField input);
    static constexpr int scale_offset = 1e8;

    int64_t base_interval_ms=500;
    std::map<std::string, int64_t> localOrderRefRemoteOrderId;
    int m_limitRate_Remain = 0;
    int64_t m_TimeStamp_Reset;
    int64_t m_time_diff_with_server=0;
    bool isSyncServerTime = false;
//websocket
    AccountUnitDaybit& findAccountUnitByWebsocketConn(struct lws * websocketConn);
    void onRtnOrder(struct lws * websocketConn, rapidjson::Value& json);
    void onRspOrder(struct lws * websocketConn, rapidjson::Value& json,int64_t ref);
    void onRspError(struct lws * websocketConn, std::string errorMsg,int64_t ref);
    void onRtnTrade(struct lws * websocketConn, rapidjson::Value& json);
    void onRtnMarket(struct lws * websocketConn, rapidjson::Value& json);
    void wsloop();
    

    struct lws_context *context = nullptr;
    ThreadPtr ws_thread;
    ThreadPtr heartbeat_thread;
private:
    int64_t						m_joinRef = 0;
	int64_t						m_ref = 0;
	int64_t makeRef();
    int64_t getRef();
	int64_t makeJoinRef();
    int64_t getJoinRef();
    void get_account(AccountUnitDaybit& unit, Document& json);

    void cancel_all_orders(AccountUnitDaybit& unit);
    void cancel_order(AccountUnitDaybit& unit, int64_t orderId);

    //void query_order(AccountUnitDaybit& unit, std::string code, std::string orderId, Document& json);
    std::string  getResponse(rapidjson::Value& payload, rapidjson::Value& response);
    void printResponse(const rapidjson::Value& d);

    int64_t getTimestamp();

    int64_t fixPriceTickSize(double keepPrecision, int64_t price, bool isBuy);
    bool loadExchangeOrderFilters(AccountUnitDaybit& unit, rapidjson::Value &doc);
    void debug_print(std::map<std::string, SendOrderFilter> &sendOrderFilters);
    SendOrderFilter getSendOrderFilter(AccountUnitDaybit& unit, const std::string& symbol);
	bool ShouldRetry(const Document& json);
    std::string createJoinReq(int64_t joinref,const std::string& topic);
    std::string createLeaveReq(int64_t joinref,const std::string& topic);
    std::string createNewOrderReq(int64_t joinref,double amount,double price,const std::string& symbol,bool isSell );
    std::string createCancelOrderReq(int64_t joinref,int64_t orderID);
    std::string createCancelAllOrdersReq(int64_t joinref);
    std::string createSubscribeOrderReq(int64_t joinref);
    std::string createSubscribeTradeReq(int64_t joinref);
    std::string createPhoenixMsg(int64_t joinref,const std::string& topic,const std::string& event,rapidjson::Value& payload);
    std::string createSubscribeMarketReq(int64_t joinref);
    std::string createGetServerTimeReq(int64_t joinref);
    std::string createHeartBeatReq();
    void InitSubscribeMsg(AccountUnitDaybit& unit,bool only_api_topic = true);
    void heartbeat_loop();
};

WC_NAMESPACE_END

#endif //PROJECT_TDENGINEDaybit_H



