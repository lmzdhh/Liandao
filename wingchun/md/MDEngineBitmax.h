#ifndef WINGCHUN_MDENGINEBITMAX_H
#define WINGCHUN_MDENGINEBITMAX_H

#include "IMDEngine.h"
#include "longfist/LFConstants.h"
#include "CoinPairWhiteList.h"
#include "PriceBook20Assembler.h"
#include <libwebsockets.h>
#include <map>
#include <unordered_map>

#include <document.h>
#include <map>
#include <vector>

WC_NAMESPACE_START

using rapidjson::Document;


class MDEngineBitmax: public IMDEngine
{
public:

    enum lws_event
    {
        depth20
    };

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
    virtual string name() const { return "MDEngineBitmax"; };

public:
    MDEngineBitmax();

    void on_lws_data(struct lws* conn, const char* data, size_t len);
    int lws_write_subscribe(struct lws* conn);
    void on_lws_connection_error(struct lws* conn);
    void onDepth(Document& json);
    void onMarketTrades(Document& json);

    std::string parseJsonToString(const char* in);
private:
    inline std::string bitmaxSubscribeSymbol(std::string coinpair);
    void connect_lws(std::string t, lws_event e);
    void loop();

    virtual void set_reader_thread() override;

    CoinPairWhiteList coinPairWhiteList;


private:
    ThreadPtr rest_thread;
    bool connected = false;
    bool logged_in = false;

    int book_depth_count = 20;
    int trade_count = 20;
    int rest_get_interval_ms = 500;

    uint64_t last_rest_get_ts = 0;
    static constexpr int scale_offset = 1e8;

    struct lws_context *context = nullptr;
    string api_key;
    string secret_key;

    std::unordered_map<struct lws *,std::pair<std::string, lws_event> > lws_handle_map;

    PriceBook20Assembler priceBook20Assembler;
};

DECLARE_PTR(MDEngineBitmax);

WC_NAMESPACE_END

#endif
