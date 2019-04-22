
#ifndef PROJECT_TDENGINEHUOBI_H
#define PROJECT_TDENGINEHUOBI_H

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
#include <cpr/cpr.h>
#include <stringbuffer.h>
using rapidjson::Document;
using rapidjson::StringBuffer;
WC_NAMESPACE_START

/**
 * account information unit extra is here.
 */

struct PendingOrderStatus
{
    char_31 InstrumentID = {0};   //合约代码
    char_21 OrderRef = {0};       //报单引用
    LfOrderStatusType OrderStatus = LF_CHAR_NotTouched;  //报单状态
    uint64_t VolumeTraded = 0;  //今成交数量
    int64_t averagePrice = 0;// given averagePrice on response of query_order
    std::string remoteOrderId;// sender_order response order id://{"orderId":19319936159776,"result":true}
};

struct OrderActionSentTime
{
    LFOrderActionField data;
    int requestId;
    int64_t sentNameTime;
};

struct ResponsedOrderStatus
{
    int64_t averagePrice = 0;
    std::string ticker;
    int64_t createdDate = 0;


    //今成交数量
    uint64_t VolumeTraded;
    int id = 0;
    uint64_t openVolume = 0;
    std::string orderId;
    std::string orderType;
    //报单价格条件
    LfOrderPriceTypeType OrderPriceType;
    int64_t price = 0;
    //买卖方向
    LfDirectionType Direction;

    //报单状态
    LfOrderStatusType OrderStatus;
    uint64_t trunoverVolume = 0;
    uint64_t volume = 0;
};
//价格和数量精度
/*
字段名称	数据类型	描述
base-currency	string	交易对中的基础币种
quote-currency	string	交易对中的报价币种
price-precision	integer	交易对报价的精度（小数点后位数）
amount-precision	integer	交易对基础币种计数精度（小数点后位数）
symbol-partition	string	交易区，可能值: [main，innovation，bifurcation]
*/
struct PriceVolumePrecision
{
    std::string baseCurrency;
    std::string quoteCurrency;
    int pricePrecision=0;
    int amountPrecision=0;
    std::string symbolPartition;
    std::string symbol;
};
enum HuobiWsStatus{
    nothing,
    accounts_topic,
    orders_topic
};
struct AccountUnitHuobi
{
    string api_key;//uid
    string secret_key;
    string passphrase;

    string baseUrl;
    // internal flags
    bool    logged_in;
    std::vector<PendingOrderStatus> newOrderStatus;
    std::vector<PendingOrderStatus> pendingOrderStatus;
    std::map<std::string,PriceVolumePrecision> mapPriceVolumePrecision;
    CoinPairWhiteList coinPairWhiteList;
    CoinPairWhiteList positionWhiteList;
    std::string accountId;
    struct lws* webSocketConn;
};
/**
 * CTP trade engine
 */
class TDEngineHuobi: public ITDEngine
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
    virtual string name() const { return "TDEngineHuobi"; };

    // req functions
    virtual void req_investor_position(const LFQryPositionField* data, int account_index, int requestId);
    virtual void req_qry_account(const LFQryAccountField* data, int account_index, int requestId);
    virtual void req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time);
    virtual void req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time);


public:
    TDEngineHuobi();
    ~TDEngineHuobi();

private:
    // journal writers
    yijinjing::JournalWriterPtr raw_writer;
    vector<AccountUnitHuobi> account_units;

    std::string GetSide(const LfDirectionType& input);
    LfDirectionType GetDirection(std::string input);
    std::string GetType(const LfOrderPriceTypeType& input);
    LfOrderPriceTypeType GetPriceType(std::string input);
    LfOrderStatusType GetOrderStatus(std::string state);
    inline int64_t getTimestamp();


    virtual void set_reader_thread() override;
    void loop();
    std::vector<std::string> split(std::string str, std::string token);
    void GetAndHandleOrderTradeResponse();
    void addNewQueryOrdersAndTrades(AccountUnitHuobi& unit, const char_31 InstrumentID,
                                    const char_21 OrderRef, const LfOrderStatusType OrderStatus,
                                    const uint64_t VolumeTraded, const std::string& remoteOrderId);
    void retrieveOrderStatus(AccountUnitHuobi& unit);
    void moveNewOrderStatusToPending(AccountUnitHuobi& unit);

    void handlerResponseOrderStatus(AccountUnitHuobi& unit, std::vector<PendingOrderStatus>::iterator orderStatusIterator, ResponsedOrderStatus& responsedOrderStatus);
    void addResponsedOrderStatusNoOrderRef(ResponsedOrderStatus &responsedOrderStatus, Document& json);
    void getPriceVolumePrecision(AccountUnitHuobi& unit);
    void dealPriceVolume(AccountUnitHuobi& unit,const std::string& symbol,int64_t nPrice,int64_t nVolume,std::string& nDealPrice,std::string& nDealVome);

    std::string parseJsonToString(Document &d);

    void addRemoteOrderIdOrderActionSentTime(const LFOrderActionField* data, int requestId, const std::string& remoteOrderId);

    void loopOrderActionNoResponseTimeOut();
    void orderActionNoResponseTimeOut();
private:
    void get_account(AccountUnitHuobi& unit, Document& json);
    void send_order(AccountUnitHuobi& unit, const char *code,
                            const char *side, const char *type, std::string volume, std::string price, Document& json);
    void cancel_all_orders(AccountUnitHuobi& unit, std::string code, Document& json);
    void cancel_order(AccountUnitHuobi& unit, std::string code, std::string orderId, Document& json);
    void query_order(AccountUnitHuobi& unit, std::string code, std::string orderId, Document& json);
    void getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& json);
    void printResponse(const Document& d);

    bool shouldRetry(Document& d);
    
    std::string createInsertOrdertring(const char *accountId,
                    const char *amount, const char *price, const char *source, const char *symbol,const char *type);

    cpr::Response Get(const std::string& url,const std::string& body, AccountUnitHuobi& unit);
    cpr::Response Post(const std::string& url,const std::string& body, AccountUnitHuobi& unit);
    void genUniqueKey();
    std::string genClinetid(const std::string& orderRef);

public:
    //zip 压缩和解压
    int gzCompress(const char *src, int srcLen, char *dest, int destLen);
    int gzDecompress(const char *src, int srcLen, const char *dst, int dstLen);
    //cys add
    char dec2hexChar(short int n);
    std::string escapeURL(const string &URL);
    std::string getAccountId(AccountUnitHuobi& unit);
    std::string getHuobiTime();
    std::string getHuobiSignatrue(std::string parameters[],int psize,std::string timestamp,std::string method_url,std::string reqType,AccountUnitHuobi& unit);
public:
    void lws_login(AccountUnitHuobi& unit, long timeout_nsec);
    void writeErrorLog(std::string strError);
    void on_lws_data(struct lws* conn, const char* data, size_t len);
    int lws_write_subscribe(struct lws* conn);
    void on_lws_connection_error(struct lws* conn);
private:
    void onPong(struct lws* conn);
    void Ping(struct lws* conn);
    AccountUnitHuobi& findAccountUnitHuobiByWebsocketConn(struct lws * websocketConn);
    std::string makeSubscribeAccountsUpdate(AccountUnitHuobi& unit);
    std::string getId();
    int64_t getMSTime();
    void loopwebsocket();
private:
    bool m_shouldPing = true;
    bool m_isPong = false;
    bool m_isSubL3 = false;
    HuobiWsStatus wsStatus = nothing;
    struct lws_context *context = nullptr;
    std::string m_strToken;
    struct lws* m_conn;
private:
    std::string m_uniqueKey;
    int HTTP_RESPONSE_OK = 200;
    static constexpr int scale_offset = 1e8;

    ThreadPtr rest_thread;
    ThreadPtr orderaction_timeout_thread;

    uint64_t last_rest_get_ts = 0;
    uint64_t rest_get_interval_ms = 500;

    std::mutex* mutex_order_and_trade = nullptr;
    std::mutex* mutex_response_order_status = nullptr;
    std::mutex* mutex_orderaction_waiting_response = nullptr;

    std::map<std::string, std::string> localOrderRefRemoteOrderId;

    //对于每个撤单指令发出后30秒（可配置）内，如果没有收到回报，就给策略报错（撤单被拒绝，pls retry)
    std::map<std::string, OrderActionSentTime> remoteOrderIdOrderActionSentTime;


    std::vector<ResponsedOrderStatus> responsedOrderStatusNoOrderRef;
    int max_rest_retry_times = 3;
    int retry_interval_milliseconds = 1000;
    int orderaction_max_waiting_seconds = 30;

};

WC_NAMESPACE_END

#endif //PROJECT_TDENGINEHuoTDEngineHuoBi_H



