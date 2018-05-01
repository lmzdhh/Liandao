#ifndef WINGCHUN_MDENGINEBINANCE_H
#define WINGCHUN_MDENGINEBINANCE_H

#include "IMDEngine.h"
#include "longfist/LFConstants.h"

WC_NAMESPACE_START

class MDEngineBinance: public IMDEngine
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
    virtual string name() const { return "MDEngineBinance"; };

public:
    MDEngineBinance();

private:
    void GetAndHandleDepthResponse(const std::string& symbol, int limit);

    void GetAndHandleTradeResponse(const std::string& symbol, int limit);
    
    void loop();

private:
    ThreadPtr rest_thread;
    bool connected = false;
    bool logged_in = false;

    std::vector<std::string> symbols;
    int book_depth_count = 5;
    int trade_count = 10;
    int rest_get_interval_ms = 500;

    uint64_t last_rest_get_ts = 0;
    uint64_t last_trade_id = 0;
    static constexpr int scale_offset = 1e8;
};

DECLARE_PTR(MDEngineBinance);

WC_NAMESPACE_END

#endif
