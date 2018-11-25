
#ifndef PROJECT_TDENGINEPRBIT_H
#define PROJECT_TDENGINEPRBIT_H

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

struct PendingOrderStatus
{
    char_31 InstrumentID;   //合约代码
    char_21 OrderRef;       //报单引用
    LfOrderStatusType OrderStatus;  //报单状态
    uint64_t VolumeTraded;  //今成交数量
    int64_t averagePrice;
    std::string remoteOrderId;
	int requestID;
};

struct SendOrderFilter
{
    char_31 InstrumentID;   //合约代码
    int ticksize; //for price round.
};
struct AccountUnit
{
    string api_key;
    string secret_key;
    string baseUrl;
    // internal flags
    bool    logged_in;
    std::vector<PendingOrderStatus> newOrderStatus;
    std::vector<PendingOrderStatus> pendingOrderStatus;
    std::map<std::string, SendOrderFilter> sendOrderFilters;
	std::map<std::string, LFRtnOrderField> ordersMap;
    CoinPairWhiteList coinPairWhiteList;
    CoinPairWhiteList positionWhiteList;

    std::vector<std::string> newPendingSendMsg;
    std::vector<std::string> pendingSendMsg;
    struct lws * websocketConn;
    int wsStatus=0;
};


/**
 * CTP trade engine
 */
class TDEngineProbit: public ITDEngine
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
    TDEngineProbit();
    ~TDEngineProbit();

    //websocket
    void on_lws_data(struct lws* conn, const char* data, size_t len);
    void on_lws_connection_error(struct lws* conn);
    int lws_write_subscribe(struct lws* conn);
    void lws_login(AccountUnit& unit, long timeout_nsec);
    
    int Round(std::string tickSizeStr);
private:
    // journal writers
    yijinjing::JournalWriterPtr raw_writer;
    vector<AccountUnit> account_units;

    virtual void set_reader_thread() override;

    std::string GetSide(const LfDirectionType& input);
    LfDirectionType GetDirection(std::string input);
    std::string GetType(const LfOrderPriceTypeType& input);
    LfOrderPriceTypeType GetPriceType(std::string input);
    LfOrderStatusType GetOrderStatus(std::string input);

    std::vector<std::string> split(std::string str, std::string token);
    void addNewQueryOrdersAndTrades(AccountUnit& unit, const char_31 InstrumentID,
                                    const char_21 OrderRef, const LfOrderStatusType OrderStatus, const uint64_t VolumeTraded, int reqID);

    void moveNewtoPending(AccountUnit& unit);
    static constexpr int scale_offset = 1e8;

    int rest_get_interval_ms = 500;

    std::mutex  m_orderMutex;

    std::map<std::string, std::string> localOrderRefRemoteOrderId;


//websocket
    AccountUnit& findAccountUnitByWebsocketConn(struct lws * websocketConn);
    void onOrder(struct lws * websocketConn, Document& json);
    void onTrade(struct lws * websocketConn, Document& json);
    void wsloop();
    //void addWebsocketPendingSendMsg(AccountUnitBitmex& unit, std::string msg);
    std::string createAuthJsonString(AccountUnit& unit );
    std::string createOrderJsonString();

    struct lws_context *context = nullptr;
    ThreadPtr ws_thread;

private:
    int HTTP_RESPONSE_OK = 200;

    void get_account(const AccountUnit& unit, const Document& json);

    void get_products(const AccountUnit& unit, const Document& json);
    void send_order(const AccountUnit& unit, const char *code,
                        const char *side, const char *type, double size, double price, const std::string& orderRef,const Document& json);

    void cancel_all_orders(const AccountUnit& unit,const Document& json);
    void cancel_order(const AccountUnit& unit, const std::string& orderId,const Document& json);

    void query_order(AccountUnit& unit, std::string code, std::string orderId, Document& json);
    void getResponse(int http_status_code, const std::string& responseText, const std::string& errorMsg, Document& json);
    void printResponse(const Document& d);

    std::string getLwsAuthReq(const AccountUnit& unit);
    std::string getLwsSubscribe();

    inline int64_t getTimestamp();

    int64_t fixPriceTickSize(int keepPrecision, int64_t price, bool isBuy);
    bool loadExchangeOrderFilters(AccountUnit& unit);
    void debug_print(const std::map<std::string, SendOrderFilter>&);
    SendOrderFilter getSendOrderFilter(const AccountUnit& unit, const char *symbol);

};

WC_NAMESPACE_END

#endif //PROJECT_TDENGINEPRBIT_H



