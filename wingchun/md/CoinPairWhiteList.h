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

class CoinPairWhiteList {


public:


    void ReadWhiteLists(const json& j_config);
    int Size();
    std::string GetWhiteListCoinpairByExchangeCoinPair(std::string md_coinpair);
    void Debug_print();
    //void foreach( callback());  // TODO
    std::unordered_map<std::string, std::string>& GetKeyIsStrategyCoinpairWhiteList();
private:
    void debug_print(std::unordered_map<std::string, std::string> &keyIsStrategyCoinpairWhiteList);


public:
    //因为白名单只是只启动初期加载一次，也即启动以后keyIsStrategyCoinpairWhiteList 是只用来读的，是不是就没有了多线程并发访问冲突？
    //"strategy_coinpair(base_quote)":"exchange_coinpair",
    std::unordered_map<std::string, std::string> keyIsStrategyCoinpairWhiteList;


private:
    /** kungfu logger */
//    KfLogPtr logger;



};

WC_NAMESPACE_END
#endif //KUNGFU_COINPAIRWHITELIST_H
