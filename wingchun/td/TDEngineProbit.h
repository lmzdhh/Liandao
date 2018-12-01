
#ifndef PROJECT_TDENGINEPRBIT_H
#define PROJECT_TDENGINEPRBIT_H

#include "ITDEngine.h"
#include "longfist/LFConstants.h"
#include "CoinPairWhiteList.h"
#include <vector>
#include <sstream>
#include <map>
#include <atomic>
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
enum class AccountStatus
{
    AS_AUTH,
    AS_WAITING,
    AS_OPEN_ORDER,
    AS_TRADE_HISTORY,
    AS_OVER
};
struct AccountUnitProbit
{
    string api_key;
    string secret_key;
    string baseUrl;
	string authUrl;
	string wsUrl;
    // internal flags
    bool    logged_in;
    std::vector<PendingOrderStatus> newOrderStatus;
    std::vector<PendingOrderStatus> pendingOrderStatus;
    std::map<std::string, SendOrderFilter> sendOrderFilters;

    CoinPairWhiteList coinPairWhiteList;
    CoinPairWhiteList positionWhiteList;

    std::vector<std::string> newPendingSendMsg;
    std::vector<std::string> pendingSendMsg;
    struct lws * websocketConn;
    int wsStatus=0;
    volatile  AccountStatus status;
    std::map<std::string/*client_order_id*/, LFRtnOrderField> ordersMap;
    int gpTimes = 24 * 60 * 60*1000;
    int64_t preFilledCost = 0;
};

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
    virtual string name() const { return "TDEngineProbit"; };
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
    void lws_write_subscribe(struct lws* conn);
    void lws_login(AccountUnitProbit& unit, long timeout_nsec);
    int Round(std::string tickSizeStr);
		//2018-01-01T00:00:00.000Z
    std::string TimeToFormatISO8601(int64_t timestamp);
	void MyPost(const std::string& url,const std::string& auth, const std::string& body,Document& json);
private:
    void sendMessage(std::string&& msg,struct lws * conn);
private:
    // journal writers
    yijinjing::JournalWriterPtr raw_writer;
    vector<AccountUnitProbit> account_units;
    virtual void set_reader_thread() override;
    std::string GetSide(const LfDirectionType& input);
    LfDirectionType GetDirection(const std::string&);
    std::string GetType(const LfOrderPriceTypeType&);
    LfOrderPriceTypeType GetPriceType(const std::string& );
    LfOrderStatusType GetOrderStatus(const std::string&);
	LfTimeConditionType GetTimeCondition(const std::string&);
    void addNewQueryOrdersAndTrades(AccountUnitProbit& unit, const char_31 InstrumentID, const char_21 OrderRef, const LfOrderStatusType OrderStatus, const uint64_t VolumeTraded, int reqID);
    void moveNewtoPending(AccountUnitProbit& unit);
    static constexpr int scale_offset = 1e8;
    int rest_get_interval_ms = 500;
    std::map<std::string, std::string> localOrderRefRemoteOrderId;
	//websocket
    AccountUnitProbit& findAccountUnitByWebsocketConn(struct lws * websocketConn);
    void onOrder(struct lws * websocketConn, Document& json);
    void onTrade(struct lws * conn, const char* orderRef, const char* api_key, const char* instrumentID, LfDirectionType direction, uint64_t volume, int64_t price);
    void wsloop();
    struct lws_context *context = nullptr;
    ThreadPtr ws_thread;
    std::string getAuthToken(const AccountUnitProbit& unit );
    int64_t m_tokenExpireTime = 0;
    std::string m_authToken;
private:
    int HTTP_RESPONSE_OK = 200;
    void get_account(const AccountUnitProbit& unit,  Document& json);
    void get_products(const AccountUnitProbit& unit, Document& json);
    void send_order(const AccountUnitProbit& unit, const char *code,const char *side, const char *type, double size, double price,double cost, const std::string& orderRef, Document& json);
    void cancel_all_orders(AccountUnitProbit& unit);
    void cancel_order(const AccountUnitProbit& unit, const std::string& orderId, const std::string& marketID,double quantity, Document& json);
    void getResponse(int http_status_code, const std::string& responseText, const std::string& errorMsg, Document& json);
    void printResponse(const Document& d);
    inline int64_t getTimestamp();
    int64_t fixPriceTickSize(int keepPrecision, int64_t price, bool isBuy);
    bool loadExchangeOrderFilters(AccountUnitProbit& unit);
    void debug_print(const std::map<std::string, SendOrderFilter>&);
    SendOrderFilter getSendOrderFilter(const AccountUnitProbit& unit, const char *symbol);
	bool OpenOrderToLFOrder(AccountUnitProbit& unit,  rapidjson::Value& json, LFRtnOrderField& order);
};

WC_NAMESPACE_END

#endif //PROJECT_TDENGINEPRBIT_H



