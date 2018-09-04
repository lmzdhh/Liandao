//
// Created by bingchen on 8/17/18.
//

#include "PriceBook20Assembler.h"


void PriceBook20Assembler::EraseAskPrice(std::string ticker, int64_t price)
{
    auto iter = tickerPriceMap.find(ticker);
    if(iter != tickerPriceMap.end()) {
        std::vector<PriceAndVolume>::iterator itr;
        std::vector<PriceAndVolume>* priceBooks = iter->second->asksPriceAndVolumes;
        int idx = -1;
        for(itr = priceBooks->begin(); itr != priceBooks->end(); itr++)
        {
            idx++;
            if(price == itr.base()->price) {
                break;
            }
        }

        if(itr != priceBooks->end()) {
            //find the price
            if (idx < 20) {
                iter->second->hasLevel20AskChanged = true;
            }
            priceBooks->erase(itr);
        }
    }
}

void PriceBook20Assembler::EraseBidPrice(std::string ticker, int64_t price)
{
    auto iter = tickerPriceMap.find(ticker);
    if(iter != tickerPriceMap.end()) {
        std::vector<PriceAndVolume>::iterator itr;
        std::vector<PriceAndVolume>* priceBooks = iter->second->bidsPriceAndVolumes;
        int idx = -1;
        for(itr = priceBooks->begin(); itr != priceBooks->end(); itr++)
        {
            idx++;
            if(price == itr.base()->price) {
                break;
            }
        }

        if(itr != priceBooks->end()) {
            //find the price
            if (idx < 20) {
                iter->second->hasLevel20BidChanged = true;
            }
            priceBooks->erase(itr);
        }
    }
}

void PriceBook20Assembler::UpdateAskPrice(std::string ticker, int64_t price, uint64_t volume)
{
    std::vector<PriceAndVolume>* asksPriceAndVolume = nullptr;
    PriceLevelBooks* priceLevelBook = nullptr;
    auto iter = tickerPriceMap.find(ticker);
    if(iter != tickerPriceMap.end()) {
        priceLevelBook = iter->second;
        asksPriceAndVolume = iter->second->asksPriceAndVolumes;
    } else {
        priceLevelBook = new PriceLevelBooks();
        priceLevelBook->bidsPriceAndVolumes = new std::vector<PriceAndVolume>();
        priceLevelBook->asksPriceAndVolumes = new std::vector<PriceAndVolume>();
        tickerPriceMap.insert(std::pair<std::string, PriceLevelBooks*>(ticker, priceLevelBook));
        asksPriceAndVolume = priceLevelBook->asksPriceAndVolumes;
    }

    std::vector<PriceAndVolume>::iterator itr;
    bool existPrice = false;
    int idx = -1;
    for(itr = asksPriceAndVolume->begin(); itr != asksPriceAndVolume->end(); itr++)
    {
        idx++;
        if(price < itr.base()->price) {
            break;
        }
        //exist price replace volume
        if(price == itr.base()->price) {
            existPrice = true;
            itr.base()->volume = volume;
            break;
        }

    }

    if(existPrice) {
        if (idx < 20) {
            priceLevelBook->hasLevel20AskChanged = true;
        }
        return;
    }

    PriceAndVolume pv;
    pv.price = price;
    pv.volume = volume;
    asksPriceAndVolume->insert(itr, pv);
    if (idx < 20) {
        priceLevelBook->hasLevel20AskChanged = true;
    }
}


void PriceBook20Assembler::UpdateBidPrice(std::string ticker, int64_t price, uint64_t volume)
{
    std::vector<PriceAndVolume>* bidsPriceAndVolume = nullptr;
    PriceLevelBooks* priceLevelBook = nullptr;
    auto iter = tickerPriceMap.find(ticker);
    if(iter != tickerPriceMap.end()) {
        priceLevelBook = iter->second;
        bidsPriceAndVolume = iter->second->bidsPriceAndVolumes;
    } else {
        priceLevelBook = new PriceLevelBooks();
        priceLevelBook->bidsPriceAndVolumes = new std::vector<PriceAndVolume>();
        priceLevelBook->asksPriceAndVolumes = new std::vector<PriceAndVolume>();
        tickerPriceMap.insert(std::pair<std::string, PriceLevelBooks*>(ticker, priceLevelBook));
        bidsPriceAndVolume = priceLevelBook->bidsPriceAndVolumes;
    }

    std::vector<PriceAndVolume>::iterator itr;
    bool existPrice = false;
    int idx = -1;
    for(itr = bidsPriceAndVolume->begin(); itr != bidsPriceAndVolume->end(); itr++)
    {
        idx++;
        if(price > itr.base()->price) {
            break;
        }
        //exist price replace volume
        if(price == itr.base()->price) {
            existPrice = true;
            itr.base()->volume = volume;
            break;
        }
    }

    if(existPrice) {
        if (idx < 20) {
            priceLevelBook->hasLevel20BidChanged = true;
        }
        return;
    }

    PriceAndVolume pv;
    pv.price = price;
    pv.volume = volume;
    bidsPriceAndVolume->insert(itr, pv);
    if (idx < 20) {
        priceLevelBook->hasLevel20BidChanged = true;
    }
}



bool PriceBook20Assembler::Assembler(std::string ticker, LFPriceBook20Field &md)
{
    std::vector<PriceAndVolume>* asksPriceAndVolume = nullptr;
    std::vector<PriceAndVolume>* bidsPriceAndVolume = nullptr;
    auto iter = tickerPriceMap.find(ticker);
    if(iter != tickerPriceMap.end()) {
        asksPriceAndVolume = iter->second->asksPriceAndVolumes;
        bidsPriceAndVolume = iter->second->bidsPriceAndVolumes;
        if(!iter->second->hasLevel20AskChanged && !iter->second->hasLevel20BidChanged) {
            return false;
        }
    } else {
        return false;
    }

    int askTotalSize = asksPriceAndVolume->size();
    auto size = std::min(askTotalSize, 20);

    for(int i = 0; i < size; ++i)
    {
        md.AskLevels[i].price = asksPriceAndVolume->at(i).price;
        md.AskLevels[i].volume = asksPriceAndVolume->at(i).volume;
        std::cout << "LFPriceBook20Field AskLevels: (i)" << i << "(price)" << md.AskLevels[i].price<<  "  (volume)"<< md.AskLevels[i].volume << std::endl;
    }
    md.AskLevelCount = size;

    int bidTotalSize = bidsPriceAndVolume->size();
    size = std::min(bidTotalSize, 20);

    for(int i = 0; i < size; ++i)
    {
        md.BidLevels[i].price = bidsPriceAndVolume->at(i).price;
        md.BidLevels[i].volume = bidsPriceAndVolume->at(i).volume;
        std::cout << "LFPriceBook20Field BidLevels: (i) " << i << "(price)" << md.BidLevels[i].price<<  "  (volume)"<< md.BidLevels[i].volume << std::endl;
    }
    md.BidLevelCount = size;

    strcpy(md.InstrumentID, ticker.c_str());
    return true;
}

void PriceBook20Assembler::clearPriceBook(std::string ticker)
{
    auto iter = tickerPriceMap.find(ticker);
    if(iter != tickerPriceMap.end()) {
        iter->second->asksPriceAndVolumes->clear();
        iter->second->bidsPriceAndVolumes->clear();
    }
    tickerPriceMap.erase(ticker);
}

void PriceBook20Assembler::clearPriceBook()
{
    auto map_itr = tickerPriceMap.begin();
    while(map_itr != tickerPriceMap.end()){
        map_itr->second->asksPriceAndVolumes->clear();
        map_itr->second->bidsPriceAndVolumes->clear();
        map_itr++;
    }
}
