#ifndef WINGCHUN_MDENGINEBITHUMB_H
#define WINGCHUN_MDENGINEBITHUMB_H

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


class MDEngineBithumb: public IMDEngine
{
public:
    struct CoinBaseQuote
    {
        std::string base;
        std::string quote;
    };

    /** load internal information from config json */
    virtual void load(const json& j_config){return;}
    virtual void connect(long timeout_nsec){return;}
    virtual void login(long timeout_nsec){return;}
    virtual void logout(){return;}
    virtual void release_api(){return;}
    virtual void subscribeMarketData(const vector<string>& instruments, const vector<string>& markets){return;}
    virtual bool is_connected() const { return true; }
    virtual bool is_logged_in() const { return true; };
    virtual string name() const { return "MDEngineBithumb"; };

public:
    MDEngineBithumb();

    void on_lws_data(struct lws* conn, const char* data, size_t len){return;}
    void on_lws_connection_error(struct lws* conn){return ;}
    int lws_write_subscribe(struct lws* conn){return 0;}
};



DECLARE_PTR(MDEngineBithumb);

WC_NAMESPACE_END

#endif
