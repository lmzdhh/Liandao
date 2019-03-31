
#ifndef PROJECT_TDENGINEKUCOIN_H
#define PROJECT_TDENGINEKUCOIN_H

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
using rapidjson::Document;

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


        struct AccountUnitKuCoin
        {
            string api_key;//uid
            string secret_key;
            string passphrase;

            string baseUrl;
            // internal flags
            bool    logged_in;
            std::vector<PendingOrderStatus> newOrderStatus;
            std::vector<PendingOrderStatus> pendingOrderStatus;

            CoinPairWhiteList coinPairWhiteList;
            CoinPairWhiteList positionWhiteList;

            std::vector<std::string> newPendingSendMsg;
            std::vector<std::string> pendingSendMsg;
        };


/**
 * CTP trade engine
 */
        class TDEngineKuCoin: public ITDEngine
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
            virtual string name() const { return "TDEngineKuCoin"; };

            // req functions
            virtual void req_investor_position(const LFQryPositionField* data, int account_index, int requestId);
            virtual void req_qry_account(const LFQryAccountField* data, int account_index, int requestId);
            virtual void req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time);
            virtual void req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time);


        public:
            TDEngineKuCoin();
            ~TDEngineKuCoin();

        private:
            // journal writers
            yijinjing::JournalWriterPtr raw_writer;
            vector<AccountUnitKuCoin> account_units;

            std::string GetSide(const LfDirectionType& input);
            LfDirectionType GetDirection(std::string input);
            std::string GetType(const LfOrderPriceTypeType& input);
            LfOrderPriceTypeType GetPriceType(std::string input);
            LfOrderStatusType GetOrderStatus(bool isCancel,int64_t nSize,int64_t nDealSize);
            inline int64_t getTimestamp();


            virtual void set_reader_thread() override;
            void loop();
            std::vector<std::string> split(std::string str, std::string token);
            void GetAndHandleOrderTradeResponse();
            void addNewQueryOrdersAndTrades(AccountUnitKuCoin& unit, const char_31 InstrumentID,
                                            const char_21 OrderRef, const LfOrderStatusType OrderStatus,
                                            const uint64_t VolumeTraded, const std::string& remoteOrderId);
            void retrieveOrderStatus(AccountUnitKuCoin& unit);
            void moveNewOrderStatusToPending(AccountUnitKuCoin& unit);

            void handlerResponseOrderStatus(AccountUnitKuCoin& unit, std::vector<PendingOrderStatus>::iterator orderStatusIterator, ResponsedOrderStatus& responsedOrderStatus);
            void addResponsedOrderStatusNoOrderRef(ResponsedOrderStatus &responsedOrderStatus, Document& json);



            std::string parseJsonToString(Document &d);

            void addRemoteOrderIdOrderActionSentTime(const LFOrderActionField* data, int requestId, const std::string& remoteOrderId);

            void loopOrderActionNoResponseTimeOut();
            void orderActionNoResponseTimeOut();
        private:
            void get_account(AccountUnitKuCoin& unit, Document& json);
            void send_order(AccountUnitKuCoin& unit, const char *code,
                            const char *side, const char *type, double size, double price, double funds, const string& strOrderRef,Document& json);

            void cancel_all_orders(AccountUnitKuCoin& unit, std::string code, Document& json);
            void cancel_order(AccountUnitKuCoin& unit, std::string code, std::string orderId, Document& json);
            void query_order(AccountUnitKuCoin& unit, std::string code, std::string orderId, Document& json);
            void getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& json);
            void printResponse(const Document& d);

            bool shouldRetry(Document& d);

            std::string construct_request_body(const AccountUnitKuCoin& unit,const  std::string& data,bool isget = true);
            cpr::Header construct_request_header(AccountUnitKuCoin& unit,const std::string& strSign,const std::string& strContentType);
            std::string createInsertOrdertring(const char *code,
                                               const char *side, const char *type, double size, double price,const string& strOrderRef);

            cpr::Response Get(const std::string& url,const std::string& body, AccountUnitKuCoin& unit);
            cpr::Response Post(const std::string& url,const std::string& body, AccountUnitKuCoin& unit);
            cpr::Response Delete(const std::string& url,const std::string& body, AccountUnitKuCoin& unit);

            void genUniqueKey();
            std::string genClinetid(const std::string& orderRef);
        private:
             std::string m_uniqueKey;
            struct lws_context *context = nullptr;

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

#endif //PROJECT_TDENGINEKUCOIN_H



