
#ifndef PROJECT_TDENGINEOCEANEX2_H
#define PROJECT_TDENGINEOCEANEX2_H
#include "TDEngineOceanEx.h"
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

        class TDEngineOceanEx2: public ITDEngine
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
            virtual string name() const { return "TDEngineOceanEx"; };

            // req functions
            virtual void req_investor_position(const LFQryPositionField* data, int account_index, int requestId);
            virtual void req_qry_account(const LFQryAccountField* data, int account_index, int requestId);
            virtual void req_order_insert(const LFInputOrderField* data, int account_index, int requestId, long rcv_time);
            virtual void req_order_action(const LFOrderActionField* data, int account_index, int requestId, long rcv_time);


        public:
            TDEngineOceanEx2();
            ~TDEngineOceanEx2();

        private:
            // journal writers
            yijinjing::JournalWriterPtr raw_writer;
            vector<AccountUnitOceanEx> account_units;

            std::string GetSide(const LfDirectionType& input);
            LfDirectionType GetDirection(std::string input);
            std::string GetType(const LfOrderPriceTypeType& input);
            LfOrderPriceTypeType GetPriceType(std::string input);
            LfOrderStatusType GetOrderStatus(std::string input);
            inline int64_t getTimestamp();


            virtual void set_reader_thread() override;
            void loop();
            std::vector<std::string> split(std::string str, std::string token);
            void GetAndHandleOrderTradeResponse();
            void addNewQueryOrdersAndTrades(AccountUnitOceanEx& unit, const char_31 InstrumentID,
                                            const char_21 OrderRef, const LfOrderStatusType OrderStatus,
                                            const uint64_t VolumeTraded, int64_t remoteOrderId);
            void retrieveOrderStatus(AccountUnitOceanEx& unit);
            void moveNewOrderStatusToPending(AccountUnitOceanEx& unit);

            void handlerResponseOrderStatus(AccountUnitOceanEx& unit, std::vector<PendingOrderStatus>::iterator orderStatusIterator, ResponsedOrderStatus& responsedOrderStatus);
            void addResponsedOrderStatusNoOrderRef(ResponsedOrderStatus &responsedOrderStatus, Document& json);



            std::string parseJsonToString(Document &d);

            void handlerResponsedOrderStatus(AccountUnitOceanEx& unit);

            void addRemoteOrderIdOrderActionSentTime(const LFOrderActionField* data, int requestId, int64_t remoteOrderId);

            void loopOrderActionNoResponseTimeOut();
            void orderActionNoResponseTimeOut();
        private:
            void get_account(AccountUnitOceanEx& unit, Document& json);
            void send_order(AccountUnitOceanEx& unit, const char *code,
                            const char *side, const char *type, double size, double price, double funds, Document& json);

            void cancel_all_orders(AccountUnitOceanEx& unit, std::string code, Document& json);
            void cancel_order(AccountUnitOceanEx& unit, std::string code, std::string orderId, Document& json);
            void query_order(AccountUnitOceanEx& unit, std::string code, std::string orderId, Document& json);
            void getResponse(int http_status_code, std::string responseText, std::string errorMsg, Document& json);
            void printResponse(const Document& d);

            bool shouldRetry(Document& d);

            std::string construct_request_body(const AccountUnitOceanEx& unit,const  std::string& data,bool isget = true);
            std::string createInsertOrdertring(const char *code,
                                               const char *side, const char *type, double size, double price);

            cpr::Response Get(const std::string& url,const std::string& body, AccountUnitOceanEx& unit);
            cpr::Response Post(const std::string& url,const std::string& body, AccountUnitOceanEx& unit);
        private:

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

            std::map<std::string, int64_t> localOrderRefRemoteOrderId;

            //对于每个撤单指令发出后30秒（可配置）内，如果没有收到回报，就给策略报错（撤单被拒绝，pls retry)
            std::map<int64_t, OrderActionSentTime> remoteOrderIdOrderActionSentTime;


            std::vector<ResponsedOrderStatus> responsedOrderStatusNoOrderRef;

            int max_rest_retry_times = 3;
            int retry_interval_milliseconds = 1000;
            int orderaction_max_waiting_seconds = 30;
        };

WC_NAMESPACE_END

#endif //PROJECT_TDENGINEOCEANEX2_H



