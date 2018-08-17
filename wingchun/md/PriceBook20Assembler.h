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




struct PriceAndVolume
{
    int64_t price;
    uint64_t volume;
    bool operator < (const PriceAndVolume &other) const
    {
        if (price<other.price)
        {
            return true;
        }
        return false;
    }
};


static int sort_price_asc(const PriceAndVolume &p1,const PriceAndVolume &p2)
{
    return p1.price < p2.price;
};

static int sort_price_desc(const PriceAndVolume &p1,const PriceAndVolume &p2)
{
    return p1.price > p2.price;
};

template<typename T>
static void sortMapByKey(std::map<int64_t, uint64_t> &t_map, std::vector<PriceAndVolume> &t_vec, T& sort_by)
{
    for(std::map<int64_t, uint64_t>::iterator iter = t_map.begin();iter != t_map.end(); iter ++)
    {
        PriceAndVolume pv;
        pv.price = iter->first;
        pv.volume = iter->second;
        t_vec.push_back(pv);
    }
    sort(t_vec.begin(), t_vec.end(), sort_by);
};


class PriceBook20Assembler {


public:

    void EraseAskPrice(std::string ticker, int64_t price);
    void UpdateAskPrice(std::string ticker, int64_t price, uint64_t volume);
    void EraseBidPrice(std::string ticker, int64_t price);
    void UpdateBidPrice(std::string ticker, int64_t price, uint64_t volume);

    bool Assembler(std::string ticker, LFPriceBook20Field &md);

    void clearPriceBook(std::string ticker);
    void clearPriceBook();
private:
    //<ticker, <price, volume>>
    std::map<std::string, std::map<int64_t, uint64_t>*> tickerAskPriceMap;
    std::map<std::string, std::map<int64_t, uint64_t>*> tickerBidPriceMap;


};


#endif //KUNGFU_PRICEBOOK20ASSEMBLER_H
