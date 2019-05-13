#ifndef WINGCHUN_MDENGINEPOLONIEX_H
#define WINGCHUN_MDENGINEPOLONIEX_H

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


struct SubscribeChannel
{
    int channelId;
    string exchange_coinpair;
    strint inner_coinpair;
    //book or trade or ...
};


class MDEnginePoloniex: public IMDEngine
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
    virtual string name() const { return "MDEnginePoloniex"; };

public:
    MDEnginePoloniex();

    void on_lws_data(struct lws* conn, const char* data, size_t len);
    void on_lws_connection_error(struct lws* conn);
    int lws_write_subscribe(struct lws* conn);

private:
    inline int64_t getTimestamp();

    void GetINitializationInfomation(Document& json,int channelId,bool isInistial);

    void onInfo(Document& json);

    SubscribeChannel findByChannelID(int channelId);
    SubscribeChannel findByExchangePair(int exchange_coinpair);

    std::string parseJsonToString(Document &d);
    std::string createBookJsonString(std::string exchange_coinpair);

    void loop();

    virtual void set_reader_thread() override;
    void debug_print(std::vector<std::string> &subJsonString);
    void debug_print(std::vector<SubscribeChannel> &websocketSubscribeChannel);

    void makeWebsocketSubscribeJsonString();
private:
    ThreadPtr rest_thread;
    bool connected = false;
    bool logged_in = false;

    int book_depth_count = 25;
    int trade_count = 10;
    int rest_get_interval_ms = 500;
    std::string baseUrl = "api2.poloniex.com";
    std::string path = "/";

    static constexpr int scale_offset = 1e8;

    struct lws_context *context = nullptr;

    size_t subscribe_index = 0;

    //subscribe_channel
    std::vector<SubscribeChannel> websocketSubscribeChannel;
    SubscribeChannel EMPTY_CHANNEL = {0};


    PriceBook20Assembler priceBook20Assembler;

    std::vector<std::string> websocketSubscribeJsonString;

    std::vector<std::string> websocketPendingSendMsg;


    CoinPairWhiteList coinPairWhiteList;
};

DECLARE_PTR(MDEnginePoloniex);

WC_NAMESPACE_END

#endif
