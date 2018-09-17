//
// Created by bingchen on 8/17/18.
//

#ifndef KUNGFU_PRICEBOOK20ASSEMBLER_H
#define KUNGFU_PRICEBOOK20ASSEMBLER_H

#include <iostream>
#include <algorithm>
#include <map>
#include <vector>
#include <unordered_map>
#include <document.h>
#include "longfist/LFDataStruct.h"


//本类并非多线程安全的，请在单线程环境使用

struct PriceAndVolume
{
    int64_t price;
    uint64_t volume;
};


struct PriceLevelBooks
{
    std::vector<PriceAndVolume>* asksPriceAndVolumes;
    std::vector<PriceAndVolume>* bidsPriceAndVolumes;
    bool hasLevel20AskChanged;
    bool hasLevel20BidChanged;
};


class PriceBook20Assembler {


public:
    ~PriceBook20Assembler();
    void EraseAskPrice(std::string ticker, int64_t price);
    void UpdateAskPrice(std::string ticker, int64_t price, uint64_t volume);
    void EraseBidPrice(std::string ticker, int64_t price);
    void UpdateBidPrice(std::string ticker, int64_t price, uint64_t volume);
    //返回true是有效的更新; false是无效的更新,应该忽略这个LFPriceBook20Field
    bool Assembler(std::string ticker, LFPriceBook20Field &md);

    void clearPriceBook(std::string ticker);
    void clearPriceBook();
private:
    void testPriceBook20Assembler();
    /*Vector follows this order: (from Binance MD)
     {
	"bids": [
		["0.00000702", "17966.00000000", []],
		["0.00000701", "111276.00000000", []],
		["0.00000700", "11730816.00000000", []],
		["0.00000699", "304119.00000000", []],
		["0.00000698", "337397.00000000", []]
	],
	"asks": [
		["0.00000703", "65956.00000000", []],
		["0.00000704", "213919.00000000", []],
		["0.00000705", "463226.00000000", []],
		["0.00000706", "709268.00000000", []],
		["0.00000707", "78529.00000000", []]
	]
}
 * */

    std::unordered_map<std::string, PriceLevelBooks*> tickerPriceMap;
};


#endif //KUNGFU_PRICEBOOK20ASSEMBLER_H
