//
// Created by xiaoning on 12/23/18.
//

#ifndef KUNGFU_MDENGINEDAYBIT_H
#define KUNGFU_MDENGINEDAYBIT_H
#include <map>
#include <queue>
#include <unordered_map>
#include <vector>
#include "CoinPairWhiteList.h"
#include "IMDEngine.h"
#include "PriceBook20Assembler.h"

struct lws_context;
struct lws;

WC_NAMESPACE_START
class MDEngineDaybit:public IMDEngine
{
public:
    static MDEngineDaybit*       m_instance;
public:
    MDEngineDaybit();
    virtual ~MDEngineDaybit();

public:
    void load(const json& ) override;
    void connect(long ) override;
    void login(long ) override;
    void logout() override;
    void release_api() override { KF_LOG_INFO(logger, "release_api"); }
    bool is_connected() const override { return m_connected; }
    bool is_logged_in() const override { return m_logged_in; }
    std::string name() const    override { return "MDEngineDaybit"; }

public:
    void onMessage(struct lws*,char* , size_t );
    void onClose(struct lws*);
    void onWrite(struct lws*);
	void phxPush(const std::string &);
	std::string phxPop();
	void phxClear();

protected:
    void set_reader_thread() override;
    void orderbookHandler(const rapidjson::Document&, const std::string&);
	void orderbookInitNotify(const rapidjson::Value&, const std::string&);
	void orderbookInsertNotify(const rapidjson::Value&, const std::string&);
    void tradeHandler(const rapidjson::Document&, const std::string&);
	void serverTimeHandler(const rapidjson::Document&);
	void marketDataHandler(const rapidjson::Document&);	

private:
	void genSubscribeJson();
    std::string genOrderbookJoin(const std::string&, int64_t&);
    std::string genOrderbookReq(const std::string&, int64_t);
	std::string genTradeJoin(const std::string&, int64_t&);
    std::string genTradeReq(const std::string&, int64_t);
	std::string genHeartBeatJson();
	std::string genServerTimeJoin(int64_t&);
	std::string genServerTimeReq(int64_t&);
	std::string genMarketDataJoin(int64_t&);
	std::string genMarketDataReq(int64_t&);

private:
	inline int64_t getTimestamp();
	void reset();
    void createConnection();
    void lwsEventLoop();
	void heartBeatLoop();
    void sendMessage(std::string&& );
	int64_t makeRef();
	int64_t	makeJoinRef();
	std::string getTickPrice(const std::string&);
	void setTickPrice(const std::string&, const std::string&);

private:
    bool                        m_connected = false;
    bool                        m_logged_in = false;
    ThreadPtr                   m_thread;
	ThreadPtr                   m_heartBeatThread;
private:
    CoinPairWhiteList           m_whiteList;
	std::unordered_map<std::string, std::string> m_tickPriceList;
	std::queue<std::string>     m_subscribeQueue;
	PriceBook20Assembler priceBook20Assembler;
	
    int                         m_priceBookNum = 20;
	int 						m_tradeNum = 10;
	int64_t						m_joinRef = 0;
	int64_t						m_ref = 0;
private:
    struct lws_context*         m_lwsContext = nullptr;
    struct lws*                 m_lwsConnection = nullptr;
	std::string					m_url;
    std::string                 m_path;
	int64_t						m_timeDiffWithServer =0;	

};
DECLARE_PTR(MDEngineDaybit);
WC_NAMESPACE_END
#endif //KUNGFU_MDENGINEDAYBIT_H
