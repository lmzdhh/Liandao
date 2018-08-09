#ifndef KUNGFU_COINPAIRWHITELIST_H
#define KUNGFU_COINPAIRWHITELIST_H

#include <iostream>
#include <unordered_map>
#include <document.h>
#include <json.hpp>
#include <KfLog.h>

WC_NAMESPACE_START

using rapidjson::Document;
using nlohmann::json;


struct CoinBaseQuote
{
    std::string base;
    std::string quote;
};



//key是策略使用的币值对,定义格式是 "小写base_小写quote",例如: ltc_btc
//value是交易所使用的币值对,定义的格式根据交易所不同而不同，比如binance，使用 LTCBTC, 而coinmex 使用LTC_BTC, 而bitmex使用 LTC/BTC
//可以参考kungfu.json.simple

class CoinPairWhiteList {


public:

    void ReadWhiteLists(const json& j_config);
    int Size();

    std::string GetKeyByValue(std::string exchange_coinpair);
    std::string GetValueByKey(std::string strategy_coinpair);
    void Debug_print();
    //void foreach( callback());  // TODO
    std::unordered_map<std::string, std::string>& GetKeyIsStrategyCoinpairWhiteList();
    std::vector<CoinBaseQuote>& GetCoinBaseQuotes();
    //从白名单的策略定义中提取出币种的名称，可以用于在查询position的时候，过滤那些定义在白名单中的币种
    void GetBaseQuoteFromWhiteListStrategyCoinPair();
    //可以用于在查询position的时候，过滤那些定义在白名单中的币种
    bool HasSymbolInWhiteList(std::string symbol);

private:
    void split(std::string str, std::string token, CoinBaseQuote& sub);
    void debug_print(std::unordered_map<std::string, std::string> &keyIsStrategyCoinpairWhiteList);
    void debug_print(std::vector<CoinBaseQuote> &sub);

public:


private:
    /** kungfu logger */
//    KfLogPtr logger;

    //因为白名单只是只启动初期加载一次，也即启动以后keyIsStrategyCoinpairWhiteList 是只用来读的，是不是就没有了多线程并发访问冲突？
    //"strategy_coinpair(base_quote)":"exchange_coinpair",
    std::unordered_map<std::string, std::string> keyIsStrategyCoinpairWhiteList;

    //订阅的币种的base和quote, 全是大写字母
    std::vector<CoinBaseQuote> coinBaseQuotes;

};

WC_NAMESPACE_END
#endif //KUNGFU_COINPAIRWHITELIST_H
