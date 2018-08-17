//
// Created by bingchen on 8/17/18.
//

#include "PriceBook20Assembler.h"


void PriceBook20Assembler::EraseAskPrice(std::string ticker, int64_t price)
{
    auto iter = tickerAskPriceMap.find(ticker);
    if(iter != tickerAskPriceMap.end()) {
        iter->second->erase(price);
    }
}

void PriceBook20Assembler::UpdateAskPrice(std::string ticker, int64_t price, uint64_t volume)
{
    std::map<int64_t, uint64_t>*  asksPriceAndVolume;
    auto iter = tickerAskPriceMap.find(ticker);
    if(iter != tickerAskPriceMap.end()) {
        asksPriceAndVolume = iter->second;
    } else {
        asksPriceAndVolume = new std::map<int64_t, uint64_t>();
        tickerAskPriceMap.insert(std::pair<std::string, std::map<int64_t, uint64_t>*>(ticker, asksPriceAndVolume));
    }

    asksPriceAndVolume->erase(price);
    asksPriceAndVolume->insert(std::pair<int64_t, uint64_t>(price, volume));
}


void PriceBook20Assembler::EraseBidPrice(std::string ticker, int64_t price)
{
    auto iter = tickerBidPriceMap.find(ticker);
    if(iter != tickerBidPriceMap.end()) {
        iter->second->erase(price);
    }
}

void PriceBook20Assembler::UpdateBidPrice(std::string ticker, int64_t price, uint64_t volume)
{
    std::map<int64_t, uint64_t>*  bidsPriceAndVolume;
    auto iter = tickerBidPriceMap.find(ticker);
    if(iter != tickerBidPriceMap.end()) {
        bidsPriceAndVolume = iter->second;
    } else {
        bidsPriceAndVolume = new std::map<int64_t, uint64_t>();
        tickerBidPriceMap.insert(std::pair<std::string, std::map<int64_t, uint64_t>*>(ticker, bidsPriceAndVolume));
    }
    bidsPriceAndVolume->erase(price);
    bidsPriceAndVolume->insert(std::pair<int64_t, uint64_t>(price, volume));
}



bool PriceBook20Assembler::Assembler(std::string ticker, LFPriceBook20Field &md)
{
//create book update
    std::vector<PriceAndVolume> sort_result;

    std::map<int64_t, uint64_t>*  asksPriceAndVolume;
    auto iter = tickerAskPriceMap.find(ticker);
    if(iter != tickerAskPriceMap.end()) {
        asksPriceAndVolume = iter->second;
    } else {
        asksPriceAndVolume = new std::map<int64_t, uint64_t>();
        tickerAskPriceMap.insert(std::pair<std::string, std::map<int64_t, uint64_t>*>(ticker, asksPriceAndVolume));
    }
    sortMapByKey(*asksPriceAndVolume, sort_result, sort_price_desc);
//        std::cout<<"asksPriceAndVolume sorted desc:"<< std::endl;
//        for(int i=0; i<sort_result.size(); i++)
//        {
//            std::cout << i << "    " << sort_result[i].price << "," << sort_result[i].volume << std::endl;
//        }
    //asks 	卖方深度 from big to little
    int askTotalSize = (int)sort_result.size();
    auto size = std::min(askTotalSize, 20);

    for(int i = 0; i < size; ++i)
    {
        md.AskLevels[i].price = sort_result[askTotalSize - i - 1].price;
        md.AskLevels[i].volume = sort_result[askTotalSize - i - 1].volume;
//            KF_LOG_INFO(logger, "MDEngineCoinmex::onDepth:  LFPriceBook20Field AskLevels: (i)" << i << "(price)" << md.AskLevels[i].price<<  "  (volume)"<< md.AskLevels[i].volume);
    }
    md.AskLevelCount = size;


    sort_result.clear();

    std::map<int64_t, uint64_t>*  bidsPriceAndVolume;
    iter = tickerBidPriceMap.find(ticker);
    if(iter != tickerBidPriceMap.end()) {
        bidsPriceAndVolume = iter->second;
    } else {
        bidsPriceAndVolume = new std::map<int64_t, uint64_t>();
        tickerBidPriceMap.insert(std::pair<std::string, std::map<int64_t, uint64_t>*>(ticker, bidsPriceAndVolume));
    }
    sortMapByKey(*bidsPriceAndVolume, sort_result, sort_price_asc);
//        std::cout<<"bidsPriceAndVolume sorted asc:"<< std::endl;
//        for(int i=0; i<sort_result.size(); i++)
//        {
//            std::cout << i << "    " << sort_result[i].price << "," << sort_result[i].volume << std::endl;
//        }
    //bids 	买方深度 from big to little
    int bidTotalSize = (int)sort_result.size();
    size = std::min(bidTotalSize, 20);

    for(int i = 0; i < size; ++i)
    {
        md.BidLevels[i].price = sort_result[bidTotalSize - i - 1].price;
        md.BidLevels[i].volume = sort_result[bidTotalSize - i - 1].volume;
//            KF_LOG_INFO(logger, "MDEngineCoinmex::onDepth:  LFPriceBook20Field BidLevels: (i) " << i << "(price)" << md.BidLevels[i].price<<  "  (volume)"<< md.BidLevels[i].volume);
    }
    md.BidLevelCount = size;
    sort_result.clear();

    strcpy(md.InstrumentID, ticker.c_str());
}

void PriceBook20Assembler::clearPriceBook(std::string ticker)
{
    std::map<int64_t, uint64_t>*  asksPriceAndVolume;
    auto iter = tickerAskPriceMap.find(ticker);
    if(iter != tickerAskPriceMap.end()) {
        iter->second->clear();
    }
    tickerAskPriceMap.erase(ticker);


    std::map<int64_t, uint64_t>*  bidsPriceAndVolume;
    iter = tickerBidPriceMap.find(ticker);
    if(iter != tickerBidPriceMap.end()) {
        iter->second->clear();
    }
    tickerBidPriceMap.erase(ticker);

}

void PriceBook20Assembler::clearPriceBook()
{
    //clear price and volumes of tickers
    std::map<std::string, std::map<int64_t, uint64_t>*> ::iterator map_itr;

    map_itr = tickerAskPriceMap.begin();
    while(map_itr != tickerAskPriceMap.end()){
        map_itr->second->clear();
        map_itr++;
    }

    map_itr = tickerBidPriceMap.begin();
    while(map_itr != tickerBidPriceMap.end()){
        map_itr->second->clear();
        map_itr++;
    }
}