#ifndef WINGCHUN_MDENGINECOINMEX_H
#define WINGCHUN_MDENGINECOINMEX_H

#include "IMDEngine.h"
#include "longfist/LFConstants.h"
#include "CoinPairWhiteList.h"
#include "PriceBook20Assembler.h"
#include <libwebsockets.h>
#include <document.h>
#include <map>
#include <vector>

WC_NAMESPACE_START

using rapidjson::Document;


struct CoinBaseQuote
{
    std::string base;
    std::string quote;
};


class MDEngineCoinmex: public IMDEngine
{
public:
    /** load internal information from config json */
    virtual void load(const json& j_config);
    virtual void connect(long timeout_nsec);
    virtual void login(long timeout_nsec);
    virtual void logout();
    virtual void release_api();
    virtual void subscribeMarketData(const vector<string>& instruments, const vector<string>& markets);
    virtual bool is_connected() const { return connected; };
    virtual bool is_logged_in() const { return logged_in; };
    virtual string name() const { return "MDEngineCoinmex"; };

public:
    MDEngineCoinmex();

    void on_lws_data(struct lws* conn, const char* data, size_t len);
    void on_lws_connection_error(struct lws* conn);
    int lws_write_subscribe(struct lws* conn);

private:
    void onDepth(Document& json);
    void onTickers(Document& json);
    void onFills(Document& json);

    std::string parseJsonToString(const char* in);
    std::string createDepthJsonString(std::string base, std::string quote);
    std::string createTickersJsonString();
    std::string createFillsJsonString(std::string base, std::string quote);
    void loop();


    virtual void set_reader_thread() override;
    void debug_print(std::vector<std::string> &subJsonString);

    void split(std::string str, std::string token, CoinBaseQuote& sub);
    //从白名单的策略定义中提取出币种的名称
    void getBaseQuoteFromWhiteListStrategyCoinPair();

    void makeWebsocketSubscribeJsonString();
private:
    ThreadPtr rest_thread;
    bool connected = false;
    bool logged_in = false;

    int rest_get_interval_ms = 500;

    static constexpr int scale_offset = 1e8;

    struct lws_context *context = nullptr;

    size_t subscribe_index = 0;

    PriceBook20Assembler priceBook20Assembler;

    std::vector<std::string> websocketSubscribeJsonString;

    CoinPairWhiteList coinPairWhiteList;

    //订阅的币种的base和quote, 全是大写字母
    std::vector<CoinBaseQuote> coinBaseQuotes;
};



DECLARE_PTR(MDEngineCoinmex);

WC_NAMESPACE_END

#endif
