#include "MDEngineOceanEx.h"
#include "TypeConvert.hpp"
#include "Timer.h"
#include "longfist/LFUtils.h"
#include "longfist/LFDataStruct.h"

#include <writer.h>
#include <stringbuffer.h>
#include <document.h>
#include <iostream>
#include <string>
#include <sstream>
#include <stdio.h>
#include <assert.h>
#include <string>
#include <cpr/cpr.h>
#include <chrono>


using cpr::Delete;
using cpr::Get;
using cpr::Url;
using cpr::Body;
using cpr::Header;
using cpr::Parameters;
using cpr::Payload;
using cpr::Post;
using cpr::Timeout;

using rapidjson::Document;
using rapidjson::SizeType;
using rapidjson::Value;
using rapidjson::Writer;
using rapidjson::StringBuffer;
using std::string;
using std::to_string;
using std::stod;
using std::stoi;


USING_WC_NAMESPACE

MDEngineOceanEx::MDEngineOceanEx(): IMDEngine(SOURCE_OCEANEX)
{
    logger = yijinjing::KfLog::getLogger("MdEngine.OceanEx");
}

void MDEngineOceanEx::load(const json& j_config)
{
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();
    KF_LOG_INFO(logger, "MDEngineOceanEx:: rest_get_interval_ms: " << rest_get_interval_ms);


    coinPairWhiteList.ReadWhiteLists(j_config, "whiteLists");
    coinPairWhiteList.Debug_print();

    getBaseQuoteFromWhiteListStrategyCoinPair();

    debug_print(coinpairs_used_in_rest);

    //display usage:
    if(coinPairWhiteList.Size() == 0) {
        KF_LOG_ERROR(logger, "MDEngineOceanEx:: please add whiteLists in kungfu.json like this :");
        KF_LOG_ERROR(logger, "\"whiteLists\":{");
        KF_LOG_ERROR(logger, "    \"strategy_coinpair(base_quote)\": \"exchange_coinpair\",");
        KF_LOG_ERROR(logger, "    \"btc_usdt\": \"btcusdt\",");
        KF_LOG_ERROR(logger, "     \"etc_eth\": \"etceth\"");
        KF_LOG_ERROR(logger, "},");
    }
}

void MDEngineOceanEx::getBaseQuoteFromWhiteListStrategyCoinPair()
{
    std::unordered_map<std::string, std::string>::iterator map_itr;
    map_itr = coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().begin();
    while(map_itr != coinPairWhiteList.GetKeyIsStrategyCoinpairWhiteList().end())
    {
        std::cout << "[getBaseQuoteFromWhiteListStrategyCoinPair] keyIsExchangeSideWhiteList (strategy_coinpair) "
                  << map_itr->first << " (exchange_coinpair) "<< map_itr->second << std::endl;

        coinpairs_used_in_rest.push_back(map_itr->second);
        map_itr++;
    }
}


void MDEngineOceanEx::debug_print(std::vector<std::string> &coinpairs_used_in_rest)
{
    int count = coinpairs_used_in_rest.size();
    KF_LOG_INFO(logger, "[debug_print] coinpairs_used_in_rest (count) " << count);

    for (int i = 0; i < count;i++)
    {
        KF_LOG_INFO(logger, "[debug_print] coinpairs_used_in_rest: " << coinpairs_used_in_rest[i]);
    }
}

void MDEngineOceanEx::connect(long timeout_nsec)
{
    KF_LOG_INFO(logger, "MDEngineOceanEx::connect:");
}


void MDEngineOceanEx::set_reader_thread()
{
	IMDEngine::set_reader_thread();

    rest_order_book_thread = ThreadPtr(new std::thread(boost::bind(&MDEngineOceanEx::loopOrderBook, this)));

    rest_trade_thread = ThreadPtr(new std::thread(boost::bind(&MDEngineOceanEx::loopTrade, this)));

}

void MDEngineOceanEx::login(long timeout_nsec)
{
    KF_LOG_INFO(logger, "[login]");
    connect(timeout_nsec);
}

void MDEngineOceanEx::logout()
{
   KF_LOG_INFO(logger, "MDEngineOceanEx::logout:");
}

void MDEngineOceanEx::release_api()
{
   KF_LOG_INFO(logger, "MDEngineOceanEx::release_api:");
}

void MDEngineOceanEx::subscribeMarketData(const vector<string>& instruments, const vector<string>& markets)
{
   KF_LOG_INFO(logger, "MDEngineOceanEx::subscribeMarketData:");
}

void MDEngineOceanEx::loopMarketData()
{
    while(isRunning)
    {
        int count = coinpairs_used_in_rest.size();
        KF_LOG_INFO(logger, "[loopMarketData] coinpairs_used_in_rest (count) " << count);

        std::string  strMarkets="";
        for (int i = 0; i < count;i++) {
            KF_LOG_INFO(logger, "[loopOrderBook] coinpairs_used_in_rest: " << coinpairs_used_in_rest[i]);

            std::string symbol = coinpairs_used_in_rest[i];
            std::string base_quote = symbol;
            std::string ticker = coinPairWhiteList.GetKeyByValue(base_quote);
            if (ticker.length() == 0) {
                KF_LOG_INFO(logger,
                            "[loopMarketData]  not in WhiteList , ignore it:" << base_quote);
                continue;
            }
            KF_LOG_INFO(logger, "[loopMarketData] " << "(ticker) " << ticker);
            strMarkets+=symbol+',';
        }
        if(!strMarkets.empty()) {
            strMarkets.pop_back();
            std::string queryString = "?markets=[" + strMarkets + "]";

            string url = "https://api.oceanex.cc/v1/tickers_multi" + queryString;
            const auto response = Get(Url{url}, cpr::VerifySsl{false},
                                      Header{{"Content-Type", "application/json"}},
                                      Timeout{10000});

            KF_LOG_DEBUG(logger,
                         "[loopMarketData]  (url) " << url << " (response.status_code) " << response.status_code <<
                                                    " (response.error.message) " << response.error.message <<
                                                    " (response.text) " << response.text.c_str());

            Document doc;
            doc.Parse(response.text.c_str());

/*

 {
	"code": 0,
	"message": "Operation is successful",
	"data": {
		"timestamp": 1538985260,
		"asks": [
			["1039.21645069", "1.06722078"],
			["1038.40778933", "0.07441827"],
			["1037.95664878", "0.44592126"],
			["1037.66232086", "0.05652834"],
			["1037.61225818", "0.075671"],
			["1037.46313803", "0.40483944"],
			["1037.4623265", "0.53098531"],
			["1037.37284386", "1.8986311"],
			["1037.0162613", "1.41806928"],
			["1036.81484619", "0.20376681"],
			["1036.67046166", "0.25264251"],
			["1036.41471872", "0.23540677"],
			["1036.21483639", "1.01314363"],
			["1036.19212007", "1.73290909"],
			["1035.66055306", "0.06383132"],
			["1035.54764438", "0.36198474"],
			["1035.40553884", "0.86875877"],
			["1035.2106592", "0.46704188"],
			["1034.93944458", "0.53006703"],
			["1034.78966391", "0.08469086"],
			["1034.59060527", "2.0"],
			["1034.45103684", "0.07637663"],
			["1034.15651499", "0.12563001"],
			["1033.84093451", "0.97796759"],
			["1033.81357133", "0.67432327"],
			["1033.7873936", "0.33818252"],
			["1033.74901591", "0.10168555"],
			["1033.62780622", "0.57090756"],
			["1033.32719203", "1.7228337"],
			["1033.21302309", "0.03511597"],
			["1033.19812427", "0.42062344"],
			["1032.77195358", "0.63407398"],
			["1032.61424164", "0.99695279"],
			["1031.87231685", "0.78713849"],
			["1031.78951854", "0.07440293"],
			["1031.63293789", "0.50278902"],
			["1031.01599648", "0.90575449"],
			["1030.84717524", "0.31659817"],
			["1030.83407423", "0.39996163"],
			["1030.74735858", "0.14122479"],
			["1030.6173438", "1.3640138"],
			["1030.11769821", "0.57926346"],
			["1030.1025338", "0.04923149"],
			["1030.09962212", "0.26459943"],
			["1029.77935501", "0.18670439"],
			["1029.43748501", "2.0"],
			["1029.41531401", "0.09274924"],
			["1029.30796584", "0.13145998"],
			["981.6311429", "0.13013964"]
		],
		"bids": []
	}
}

 * */
            if (doc.HasParseError() || !doc.HasMember("data")) {
                continue;
            } else {
                const auto &d = doc["data"];
                if (d.IsArray() && d.Size() > 0) {
                    auto size = (int) d.Size();
                    for (int i = 0; i < size; ++i) {
                        LFMarketDataField md;
                        memset(&md, 0, sizeof(LFMarketDataField));
                        auto &data = d.GetArray()[i];
                        auto &ticker = data["ticker"];
                        md.LastPrice = stod(ticker["last"].GetString()) * scale_offset;
                        md.Volume = stod(ticker["vol"].GetString()) * scale_offset;
                        md.BidPrice1 = stod(ticker["buy"].GetString()) * scale_offset;
                        md.AskPrice1 = stod(ticker["sell"].GetString()) * scale_offset;
                        md.UpperLimitPrice = stod(ticker["high"].GetString()) * scale_offset;
                        md.LowerLimitPrice = stod(ticker["high"].GetString()) * scale_offset;
                        //md.Ti
                        strcpy(md.InstrumentID, data["market"].GetString());
                        strcpy(md.ExchangeID, "oceanex");
                        //KF_LOG_DEBUG(logger,
                        //             "[loopMarketData]  (on_market_data) " << url << " (response.status_code) " << response.status_code <<
                        //                                        " (response.error.message) " << response.error.message <<
                        //                                       " (response.text) " << response.text.c_str());
                        on_market_data(&md);
                    }
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(rest_get_interval_ms));
    }
}

void MDEngineOceanEx::loopOrderBook()
{
    while(isRunning)
    {
        int count = coinpairs_used_in_rest.size();
        KF_LOG_INFO(logger, "[loopOrderBook] coinpairs_used_in_rest (count) " << count);

        for (int i = 0; i < count;i++)
        {
            KF_LOG_INFO(logger, "[loopOrderBook] coinpairs_used_in_rest: " << coinpairs_used_in_rest[i]);

            std::string symbol = coinpairs_used_in_rest[i];
            std::string base_quote = symbol;
            std::string ticker = coinPairWhiteList.GetKeyByValue(base_quote);
            if (ticker.length() == 0) {
                KF_LOG_INFO(logger,
                            "MDEngineOceanEx::loopOrderBook: not in WhiteList , ignore it:" << base_quote);
                continue;
            }

            KF_LOG_INFO(logger, "MDEngineOceanEx::loopOrderBook:" << "(ticker) " << ticker);


            int limit = 100;
            std::string requestPath = "";
            std::string queryString= "?market=" + symbol + "&limit=" + std::to_string(limit);

            string url = "https://api.oceanex.cc/v1/order_book" + requestPath + queryString;
            const auto response = Get(Url{url}, cpr::VerifySsl{false},
                                      Header{{"Content-Type", "application/json"}},
                                      Timeout{10000});

            KF_LOG_DEBUG(logger, "[loopOrderBook] (url) " << url << " (response.status_code) " << response.status_code <<
                                                         " (response.error.message) " << response.error.message <<
                                                         " (response.text) " << response.text.c_str());

            Document doc;
            doc.Parse(response.text.c_str());

/*

 {
	"code": 0,
	"message": "Operation is successful",
	"data": {
		"timestamp": 1538985260,
		"asks": [
			["1039.21645069", "1.06722078"],
			["1038.40778933", "0.07441827"],
			["1037.95664878", "0.44592126"],
			["1037.66232086", "0.05652834"],
			["1037.61225818", "0.075671"],
			["1037.46313803", "0.40483944"],
			["1037.4623265", "0.53098531"],
			["1037.37284386", "1.8986311"],
			["1037.0162613", "1.41806928"],
			["1036.81484619", "0.20376681"],
			["1036.67046166", "0.25264251"],
			["1036.41471872", "0.23540677"],
			["1036.21483639", "1.01314363"],
			["1036.19212007", "1.73290909"],
			["1035.66055306", "0.06383132"],
			["1035.54764438", "0.36198474"],
			["1035.40553884", "0.86875877"],
			["1035.2106592", "0.46704188"],
			["1034.93944458", "0.53006703"],
			["1034.78966391", "0.08469086"],
			["1034.59060527", "2.0"],
			["1034.45103684", "0.07637663"],
			["1034.15651499", "0.12563001"],
			["1033.84093451", "0.97796759"],
			["1033.81357133", "0.67432327"],
			["1033.7873936", "0.33818252"],
			["1033.74901591", "0.10168555"],
			["1033.62780622", "0.57090756"],
			["1033.32719203", "1.7228337"],
			["1033.21302309", "0.03511597"],
			["1033.19812427", "0.42062344"],
			["1032.77195358", "0.63407398"],
			["1032.61424164", "0.99695279"],
			["1031.87231685", "0.78713849"],
			["1031.78951854", "0.07440293"],
			["1031.63293789", "0.50278902"],
			["1031.01599648", "0.90575449"],
			["1030.84717524", "0.31659817"],
			["1030.83407423", "0.39996163"],
			["1030.74735858", "0.14122479"],
			["1030.6173438", "1.3640138"],
			["1030.11769821", "0.57926346"],
			["1030.1025338", "0.04923149"],
			["1030.09962212", "0.26459943"],
			["1029.77935501", "0.18670439"],
			["1029.43748501", "2.0"],
			["1029.41531401", "0.09274924"],
			["1029.30796584", "0.13145998"],
			["981.6311429", "0.13013964"]
		],
		"bids": []
	}
}

 * */
            if (doc.HasParseError() || !doc.HasMember("data") ) {
                continue;
            } else {
                const auto &d = doc["data"];

                LFPriceBook20Field md;
                memset(&md, 0, sizeof(LFPriceBook20Field));

                bool has_update = false;
                if(d.HasMember("bids"))
                {
                    auto& bids = d["bids"];

                    if(bids.IsArray() && bids.Size() > 0)
                    {
                        auto size = std::min((int)bids.Size(), 20);

                        for(int i = 0; i < size; ++i)
                        {
                            md.BidLevels[i].price = stod(bids.GetArray()[i][0].GetString()) * scale_offset;
                            md.BidLevels[i].volume = stod(bids.GetArray()[i][1].GetString()) * scale_offset;
                        }
                        md.BidLevelCount = size;

                        has_update = true;
                    }
                }

                if(d.HasMember("asks"))
                {
                    auto& asks = d["asks"];

                    if(asks.IsArray() && asks.Size() >0)
                    {
                        auto size = std::min((int)asks.Size(), 20);

                        for(int i = 0; i < size; ++i)
                        {
                            md.AskLevels[i].price = stod(asks.GetArray()[i][0].GetString()) * scale_offset;
                            md.AskLevels[i].volume = stod(asks.GetArray()[i][1].GetString()) * scale_offset;
                        }
                        md.AskLevelCount = size;

                        has_update = true;
                    }
                }

                if (has_update) {

                    strcpy(md.InstrumentID, ticker.c_str());
                    strcpy(md.ExchangeID, "oceanex");

                    on_price_book_update(&md);
                    //
                    LFMarketDataField marketdata;
                    memset(&marketdata, 0, sizeof(LFMarketDataField));
                    marketdata.BidPrice1 = md.BidLevels[0].price;
                    marketdata.BidVolume1 = md.BidLevels[0].volume;
                    marketdata.AskPrice1 = md.AskLevels[md.AskLevelCount-1].price;
                    marketdata.AskVolume1 = md.AskLevels[md.AskLevelCount-1].volume;
                    strcpy(marketdata.InstrumentID, ticker.c_str());
                    strcpy(marketdata.ExchangeID, "oceanex");
                    on_market_data(&marketdata);
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(rest_get_interval_ms));
    }
}

void MDEngineOceanEx::loopTrade()
{
    while(isRunning)
    {
        int count = coinpairs_used_in_rest.size();
        KF_LOG_INFO(logger, "[loopTrade] coinpairs_used_in_rest (count) " << count);

        for (int i = 0; i < count;i++)
        {
            KF_LOG_INFO(logger, "[loopTrade] coinpairs_used_in_rest: " << coinpairs_used_in_rest[i]);

            std::string symbol = coinpairs_used_in_rest[i];
            int limit = 100;
            std::string requestPath = "";
            std::string queryString= "?market=" + symbol + "&limit=" + std::to_string(limit);

            if(fromTradeId > 0)
            {
                queryString += "&from=" + std::to_string(fromTradeId);
            }
            string url = "https://api.oceanex.cc/v1/trades" + requestPath + queryString;
            const auto response = Get(Url{url}, cpr::VerifySsl{false},
                                      Header{{"Content-Type", "application/json"}},
                                      Timeout{10000});

            KF_LOG_DEBUG(logger, "[loopTrade] (url) " << url << " (response.status_code) " << response.status_code <<
                                                         " (response.error.message) " << response.error.message <<
                                                         " (response.text) " << response.text.c_str());

            Document d;
            d.Parse(response.text.c_str());
            /*
        {
        "code": 0,
        "message": "Operation is successful",
        "data": {
            "num_of_resources": 2,
            "resources": [{
                "id": 60947,
                "price": "980.67464115",
                "volume": "0.1560637",
                "funds": "153.047712994041255",
                "market": "ocevet",
                "market_name": "OCE/VET",
                "created_at": "2018-10-08T07:54:34Z",
                "side": "bid",
                "fee": "0.0015"
            }, {
                "id": 60946,
                "price": "981.54026365",
                "volume": "2.0",
                "funds": "1963.0805273",
                "market": "ocevet",
                "market_name": "OCE/VET",
                "created_at": "2018-10-08T07:54:32Z",
                "side": "bid",
                "fee": "0.0015"
            }]
            }
        }
            * */

            if (d.HasParseError() || !d.HasMember("data") || !d["data"].IsObject() || !d["data"].HasMember("resources") || !d["data"]["resources"].IsArray()) {
                continue;
            } else {
                auto& resources = d["data"]["resources"];
                int len = resources.Size();
                KF_LOG_DEBUG(logger, "MDEngineOceanEx::loopTrade:" << "(resources.size) " << len);

                //倒序排列
                for(int i = len - 1; i > 0 ; i--)
                {
                    const auto& ele = resources.GetArray()[i];

                    std::string symbol = ele["market"].GetString();
                    std::string ticker = coinPairWhiteList.GetKeyByValue(symbol);
                    if(ticker.length() == 0) {
                        KF_LOG_INFO(logger, "MDEngineOceanEx::loopTrade: not in WhiteList , ignore it:" << symbol);
                        continue;
                    }

                    KF_LOG_INFO(logger, "MDEngineOceanEx::loopTrade:" << "(ticker) " << ticker);

                    LFL2TradeField trade;
                    memset(&trade, 0, sizeof(LFL2TradeField));
                    strcpy(trade.InstrumentID, ticker.c_str());
                    strcpy(trade.ExchangeID, "oceanex");

                    trade.Price = std::round(std::stod(ele["price"].GetString()) * scale_offset);
                    trade.Volume = std::round(std::stod(ele["volume"].GetString()) * scale_offset);

                    int id = ele["id"].GetInt();
                    if(fromTradeId < id) {
                        fromTradeId = id;
                    }
                    KF_LOG_INFO(logger, "MDEngineOceanEx::loopTrade:" << "(id) " << id);

                    std::string side = ele["side"].GetString();
                    trade.OrderKind[0] = "bid" == side? 'B' : 'N';
                    trade.OrderBSFlag[0] = "bid" == side? 'B' : 'S';
                    on_trade(&trade);

                    KF_LOG_INFO(logger, "MDEngineOceanEx::loopTrade:" << "(ticker) " << ticker
                                                                       << " (Price)" << trade.Price
                                                                       << " (Volume)" << trade.Volume
                                                                       << " (id)" << id
                                                                       << " (side)" << side);
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(rest_get_interval_ms));
    }
}

BOOST_PYTHON_MODULE(liboceanexmd)
{
    using namespace boost::python;
    class_<MDEngineOceanEx, boost::shared_ptr<MDEngineOceanEx> >("Engine")
    .def(init<>())
    .def("init", &MDEngineOceanEx::initialize)
    .def("start", &MDEngineOceanEx::start)
    .def("stop", &MDEngineOceanEx::stop)
    .def("logout", &MDEngineOceanEx::logout)
    .def("wait_for_stop", &MDEngineOceanEx::wait_for_stop);
}
