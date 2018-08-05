

#include <WC_DECLARE.h>
#include <KfLog.h>
#include "CoinPairWhiteList.h"



WC_NAMESPACE_START

void CoinPairWhiteList::ReadWhiteLists(const json& j_config)
{
    //KF_LOG_INFO(logger, "[readWhiteLists]");

    if(j_config.find("whiteLists") != j_config.end()) {
        //KF_LOG_INFO(logger, "[readWhiteLists] found whiteLists");
        //has whiteLists
        json whiteLists = j_config["whiteLists"].get<json>();
        if(whiteLists.is_object())
        {
            for (json::iterator it = whiteLists.begin(); it != whiteLists.end(); ++it) {
                std::string strategy_coinpair = it.key();
                std::string exchange_coinpair = it.value();
                //KF_LOG_INFO(logger, "[readWhiteLists] (strategy_coinpair) " << strategy_coinpair << " (exchange_coinpair) " << exchange_coinpair);
                keyIsStrategyCoinpairWhiteList.insert(std::pair<std::string, std::string>(strategy_coinpair, exchange_coinpair));
            }
        }
    }
}

int CoinPairWhiteList::Size()
{
    return keyIsStrategyCoinpairWhiteList.size();
}

void CoinPairWhiteList::Debug_print()
{
    debug_print(keyIsStrategyCoinpairWhiteList);
}

void CoinPairWhiteList::debug_print(std::unordered_map<std::string, std::string> &keyIsStrategyCoinpairWhiteList)
{
    std::unordered_map<std::string, std::string>::iterator map_itr;
    map_itr = keyIsStrategyCoinpairWhiteList.begin();
    while(map_itr != keyIsStrategyCoinpairWhiteList.end()) {
        //KF_LOG_INFO(logger, "[debug_print] keyIsExchangeSideWhiteList (strategy_coinpair) " << map_itr->first << " (md_coinpair) "<< map_itr->second);
        std::cout << "[debug_print] keyIsExchangeSideWhiteList (strategy_coinpair) " << map_itr->first << " (md_coinpair) "<< map_itr->second << std::endl;
        map_itr++;
    }
}

std::string CoinPairWhiteList::GetWhiteListCoinpairByExchangeCoinPair(std::string md_coinpair)
{
    //TODO 交易所来的币值对 都是大写的，需要转成大写进行比较，如果保存的数据已经是大写的了，就可以省去这个转换过程
    std::string ticker = md_coinpair;
    std::transform(ticker.begin(), ticker.end(), ticker.begin(), ::toupper);

    //KF_LOG_INFO(logger, "[getWhiteListCoinpairFrom] find md_coinpair (md_coinpair) " << md_coinpair << " (toupper(ticker)) " << ticker);
    std::unordered_map<std::string, std::string>::iterator map_itr;
    map_itr = keyIsStrategyCoinpairWhiteList.begin();
    while(map_itr != keyIsStrategyCoinpairWhiteList.end()) {
        //TODO  std::string == std::string 与 strcmp(str.c_str, s.c_str()) == 0 谁更快
        if(ticker == map_itr->second)
        {
            //KF_LOG_INFO(logger, "[getWhiteListCoinpairFrom] found md_coinpair (strategy_coinpair) " << map_itr->first << " (exchange_coinpair) " << map_itr->second);
            return map_itr->first;
        }
        map_itr++;
    }
    //KF_LOG_INFO(logger, "[getWhiteListCoinpairFrom] not found md_coinpair (md_coinpair) " << md_coinpair);
    return "";
}

std::unordered_map<std::string, std::string>& CoinPairWhiteList::GetKeyIsStrategyCoinpairWhiteList()
{
    return keyIsStrategyCoinpairWhiteList;
}


WC_NAMESPACE_END