

#include <WC_DECLARE.h>
#include <KfLog.h>
#include "CoinPairWhiteList.h"



WC_NAMESPACE_START

        void CoinPairWhiteList::ReadWhiteLists(const json& j_config)
        {
            std::cout << "[readWhiteLists]"<< std::endl;

            if(j_config.find("whiteLists") != j_config.end()) {
                std::cout << "[readWhiteLists] found whiteLists"<< std::endl;
                //has whiteLists
                json whiteLists = j_config["whiteLists"].get<json>();
                if(whiteLists.is_object())
                {
                    for (json::iterator it = whiteLists.begin(); it != whiteLists.end(); ++it)
                    {
                        std::string strategy_coinpair = it.key();
                        std::string exchange_coinpair = it.value();
                        std::cout <<  "[readWhiteLists] (strategy_coinpair) " << strategy_coinpair << " (exchange_coinpair) " << exchange_coinpair<< std::endl;
                        keyIsStrategyCoinpairWhiteList.insert(std::pair<std::string, std::string>(strategy_coinpair, exchange_coinpair));
                    }
                }
            }

            getBaseQuoteFromWhiteListStrategyCoinPair();
        }

        int CoinPairWhiteList::Size()
        {
            return keyIsStrategyCoinpairWhiteList.size();
        }

        void CoinPairWhiteList::Debug_print()
        {
            debug_print(keyIsStrategyCoinpairWhiteList);
            debug_print(coinBaseQuotes);
        }

        void CoinPairWhiteList::debug_print(std::unordered_map<std::string, std::string> &keyIsStrategyCoinpairWhiteList)
        {
            std::unordered_map<std::string, std::string>::iterator map_itr;
            map_itr = keyIsStrategyCoinpairWhiteList.begin();
            std::cout << "[debug_print] keyIsExchangeSideWhiteList (count) " << keyIsStrategyCoinpairWhiteList.size() << std::endl;
            while(map_itr != keyIsStrategyCoinpairWhiteList.end())
            {
                std::cout << "[debug_print] keyIsExchangeSideWhiteList (strategy_coinpair) " << map_itr->first << " (md_coinpair) "<< map_itr->second << std::endl;
                map_itr++;
            }
        }

        std::string CoinPairWhiteList::GetKeyByValue(std::string exchange_coinpair)
        {
            std::unordered_map<std::string, std::string>::iterator map_itr;
            map_itr = keyIsStrategyCoinpairWhiteList.begin();
            while(map_itr != keyIsStrategyCoinpairWhiteList.end())
            {
                //TODO  std::string == std::string 与 strcmp(str.c_str, s.c_str()) == 0 谁更快
                if(exchange_coinpair == map_itr->second)
                {
                    std::cout << "[GetKeyByValue] found (strategy_coinpair) " <<
                              map_itr->first << " (exchange_coinpair) " << map_itr->second << std::endl;

                    return map_itr->first;
                }
                map_itr++;
            }
            std::cout << "[getWhiteListCoinpairFrom] not found (exchange_coinpair) " << exchange_coinpair << std::endl;
            return "";
        }


        std::string CoinPairWhiteList::GetValueByKey(std::string strategy_coinpair)
        {
            std::unordered_map<std::string, std::string>::iterator map_itr;
            map_itr = keyIsStrategyCoinpairWhiteList.begin();
            while(map_itr != keyIsStrategyCoinpairWhiteList.end())
            {
                if(strategy_coinpair == map_itr->first)
                {
                    std::cout << "[GetValueByKey] found (strategy_coinpair) " <<
                              map_itr->first << " (exchange_coinpair) " << map_itr->second << std::endl;

                    return map_itr->second;
                }
                map_itr++;
            }
            std::cout <<  "[GetValueByKey] not found (strategy_coinpair) " << strategy_coinpair << std::endl;
            return "";
        }


        std::unordered_map<std::string, std::string>& CoinPairWhiteList::GetKeyIsStrategyCoinpairWhiteList()
        {
            return keyIsStrategyCoinpairWhiteList;
        }

        std::vector<CoinBaseQuote>& CoinPairWhiteList::GetCoinBaseQuotes()
        {
            return coinBaseQuotes;
        }


        void CoinPairWhiteList::getBaseQuoteFromWhiteListStrategyCoinPair()
        {
            std::unordered_map<std::string, std::string>::iterator map_itr;
            map_itr = keyIsStrategyCoinpairWhiteList.begin();
            while(map_itr != keyIsStrategyCoinpairWhiteList.end())
            {
                std::cout << "[getBaseQuoteFromWhiteListStrategyCoinPair] keyIsExchangeSideWhiteList (strategy_coinpair) "
                             << map_itr->first << " (exchange_coinpair) "<< map_itr->second << std::endl;

                // strategy_coinpair 转换成大写字母, 因为比较时区分大小写: HasSymbolInWhiteList
                std::string coinpair = map_itr->first;
                std::transform(coinpair.begin(), coinpair.end(), coinpair.begin(), ::toupper);

                CoinBaseQuote baseQuote;
                split(coinpair, "_", baseQuote);
                std::cout << "[readWhiteLists] getBaseQuoteFromWhiteListStrategyCoinPair (base) " << baseQuote.base << " (quote) " << baseQuote.quote << std::endl;

                if(baseQuote.base.length() > 0)
                {
                    //get correct base_quote config
                    coinBaseQuotes.push_back(baseQuote);
                }
                map_itr++;
            }
        }

        bool CoinPairWhiteList::HasSymbolInWhiteList(std::string oneCoinName)
        {
            int count = coinBaseQuotes.size();
            //转换成大写字母
            std::string upperSymbol = oneCoinName;
            std::transform(upperSymbol.begin(), upperSymbol.end(), upperSymbol.begin(), ::toupper);
            for (int i = 0; i < count; i++)
            {
                if(coinBaseQuotes[i].base == upperSymbol || coinBaseQuotes[i].quote == upperSymbol)
                {
                    std::cout << "[hasSymbolInWhiteList] (found) (symbol) " << oneCoinName << std::endl;
                    return true;
                }
            }
            std::cout << "[hasSymbolInWhiteList] (not found) (symbol) " << oneCoinName << std::endl;
            return false;
        }

        //example: btc_usdt
        void CoinPairWhiteList::split(std::string str, std::string token, CoinBaseQuote& sub)
        {
            if (str.size() > 0)
            {
                size_t index = str.find(token);
                if (index != std::string::npos)
                {
                    sub.base = str.substr(0, index);
                    sub.quote = str.substr(index + token.size());
                }
                else {
                    //not found, do nothing
                }
            }
        }

        void CoinPairWhiteList::debug_print(std::vector<CoinBaseQuote> &sub)
        {
            int count = sub.size();
            std::cout << "[debug_print] CoinBaseQuote (count) " << count << std::endl;
            for (int i = 0; i < count;i++)
            {
                std::cout << "[debug_print] CoinBaseQuote (base) " << sub[i].base <<  " (quote) " << sub[i].quote << std::endl;
            }
        }



WC_NAMESPACE_END