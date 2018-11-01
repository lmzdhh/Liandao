#ifndef WINGCHUN_MDENGINEOceanEx_H
#define WINGCHUN_MDENGINEOceanEx_H

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


class MDEngineOceanEx: public IMDEngine
{
public:
    /** load internal information from config json */
    virtual void load(const json& j_config);
    virtual void connect(long timeout_nsec);
    virtual void login(long timeout_nsec);
    virtual void logout();
    virtual void release_api();
    virtual void subscribeMarketData(const vector<string>& instruments, const vector<string>& markets);
    virtual bool is_connected() const { return true; };
    virtual bool is_logged_in() const { return true; };
    virtual string name() const { return "MDEngineOceanEx"; };

public:
    MDEngineOceanEx();

private:
    void loopOrderBook();
    void loopTrade();
    void loopMarketData();
    virtual void set_reader_thread() override;

    //从白名单的策略定义中提取出币种的名称
    void getBaseQuoteFromWhiteListStrategyCoinPair();
    void debug_print(std::vector<std::string> &coinpairs_used_in_rest);
private:
    ThreadPtr rest_order_book_thread;
    ThreadPtr rest_trade_thread;

    int rest_get_interval_ms = 500;

    static constexpr int scale_offset = 1e8;

    PriceBook20Assembler priceBook20Assembler;

    std::vector<std::string> coinpairs_used_in_rest;

    CoinPairWhiteList coinPairWhiteList;

    int fromTradeId = 0;
};

DECLARE_PTR(MDEngineOceanEx);

WC_NAMESPACE_END

#endif
