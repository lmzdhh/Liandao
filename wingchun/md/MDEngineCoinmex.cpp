#include "MDEngineCoinmex.h"
#include "TypeConvert.hpp"
#include "Timer.h"
#include "longfist/LFUtils.h"
#include "longfist/LFDataStruct.h"

#include <document.h>
#include <iostream>
#include <string>
#include <sstream>
#include <stdio.h>
#include <assert.h>
#include <string>
#include <cpr/cpr.h>
#include <chrono>


using cpr::Get;
using cpr::Url;
using cpr::Parameters;
using cpr::Payload;
using cpr::Post;

using rapidjson::Document;
using rapidjson::SizeType;
using rapidjson::Value;
using std::string;
using std::to_string;
using std::stod;
using std::stoi;


USING_WC_NAMESPACE

MDEngineCoinmex::MDEngineCoinmex(): IMDEngine(SOURCE_COINMEX)
{
    logger = yijinjing::KfLog::getLogger("MdEngine.COINMEX");
}

void MDEngineCoinmex::load(const json& j_config)
{
    symbols.push_back(j_config["symbol"].get<string>());
    book_depth_count = j_config["book_depth_count"].get<int>();
    trade_count = j_config["trade_count"].get<int>();
    rest_get_interval_ms = j_config["rest_get_interval_ms"].get<int>();

    KF_LOG_INFO(logger, "MDEngineCoinmex::load: symbol: " << symbols[0] << " book_depth_count: "
		<< book_depth_count << " trade_count: " << trade_count << " rest_get_interval_ms: " << rest_get_interval_ms); 	
}

void MDEngineCoinmex::connect(long timeout_nsec)
{
   KF_LOG_INFO(logger, "MDEngineCoinmex::connect:");
	
   connected = true;

   rest_thread = ThreadPtr(new std::thread(boost::bind(&MDEngineCoinmex::loop, this)));
}

void MDEngineCoinmex::login(long timeout_nsec)
{
   KF_LOG_INFO(logger, "MDEngineCoinmex::login:");
   logged_in = true;
}

void MDEngineCoinmex::logout()
{
   KF_LOG_INFO(logger, "MDEngineCoinmex::logout:");
}

void MDEngineCoinmex::release_api()
{
   KF_LOG_INFO(logger, "MDEngineCoinmex::release_api:");
}

void MDEngineCoinmex::subscribeMarketData(const vector<string>& instruments, const vector<string>& markets)
{
   KF_LOG_INFO(logger, "MDEngineCoinmex::subscribeMarketData:");
}

void MDEngineCoinmex::GetAndHandleDepthResponse(const std::string& symbol, int limit)
{
    const auto static url = "https://api.binance.com/api/v1/depth";
    const auto response = Get(Url{url}, Parameters{{"symbol", symbol},
                                                        {"limit",  to_string(limit)}});
    Document d;
    d.Parse(response.text.c_str());

	LFMarketDataField md;
	memset(&md, 0, sizeof(md));

    bool has_update = false;	    	
	if(d.HasMember("bids") && d["bids"].IsArray() && d["bids"].Size() >= limit)
	{
		md.BidPrice1 = stod(d["bids"].GetArray()[0][0].GetString()) * scale_offset;
		md.BidVolume1 = stod(d["bids"].GetArray()[0][1].GetString()) * scale_offset;
		md.BidPrice2 = stod(d["bids"].GetArray()[1][0].GetString()) * scale_offset;
		md.BidVolume2 = stod(d["bids"].GetArray()[1][1].GetString()) * scale_offset;
		md.BidPrice3 = stod(d["bids"].GetArray()[2][0].GetString()) * scale_offset;
		md.BidVolume3 = stod(d["bids"].GetArray()[2][1].GetString()) * scale_offset;
		md.BidPrice4 = stod(d["bids"].GetArray()[3][0].GetString()) * scale_offset;
		md.BidVolume4 = stod(d["bids"].GetArray()[3][1].GetString()) * scale_offset;
		md.BidPrice5 = stod(d["bids"].GetArray()[4][0].GetString()) * scale_offset;
		md.BidVolume5 = stod(d["bids"].GetArray()[4][1].GetString()) * scale_offset;
		
		has_update = true;
	}

	if(d.HasMember("asks") && d["asks"].IsArray() && d["asks"].Size() >= limit)
	{
		md.AskPrice1 = stod(d["asks"].GetArray()[0][0].GetString()) * scale_offset;
		md.AskVolume1 = stod(d["asks"].GetArray()[0][1].GetString()) * scale_offset;
		md.AskPrice2 = stod(d["asks"].GetArray()[1][0].GetString()) * scale_offset;
		md.AskVolume2 = stod(d["asks"].GetArray()[1][1].GetString()) * scale_offset;
		md.AskPrice3 = stod(d["asks"].GetArray()[2][0].GetString()) * scale_offset;
		md.AskVolume3 = stod(d["asks"].GetArray()[2][1].GetString()) * scale_offset;
		md.AskPrice4 = stod(d["asks"].GetArray()[3][0].GetString()) * scale_offset;
		md.AskVolume4 = stod(d["asks"].GetArray()[3][1].GetString()) * scale_offset;
		md.AskPrice5 = stod(d["asks"].GetArray()[4][0].GetString()) * scale_offset;
		md.AskVolume5 = stod(d["asks"].GetArray()[4][1].GetString()) * scale_offset;
		
		has_update = true;
	}
    
    if(has_update)
    {
        strcpy(md.InstrumentID, symbol.c_str());
	    strcpy(md.ExchangeID, "binance");
	    md.UpdateMillisec = last_rest_get_ts;

	    on_market_data(&md);
	} 
}

void MDEngineCoinmex::GetAndHandleTradeResponse(const std::string& symbol, int limit)
{
    const auto static url = "https://api.binance.com/api/v1/trades";
    const auto response = Get(Url{url}, Parameters{{"symbol", symbol},
		    {"limit",  to_string(limit)}});
    Document d;
    d.Parse(response.text.c_str());
    if(d.IsArray())
    {
	    LFL2TradeField trade;
	    memset(&trade, 0, sizeof(trade));
	    strcpy(trade.InstrumentID, symbols[0].c_str());
	    strcpy(trade.ExchangeID, "binance");

	    for(int i = 0; i < d.Size(); ++i)
	    {
		    const auto& ele = d[i];
		    if(!ele.HasMember("id"))
		    {
			continue;
		    }
		    
  		    const auto trade_id = ele["id"].GetUint64();
		    if(trade_id <= last_trade_id)
		    {
		    	continue;
		    }
		    
		    last_trade_id = trade_id;
		    if(ele.HasMember("price") && ele.HasMember("qty") && ele.HasMember("isBuyerMaker") && ele.HasMember("isBestMatch"))
		    {
			    trade.Price = std::stod(ele["price"].GetString()) * scale_offset;
			    trade.Volume = std::stod(ele["qty"].GetString()) * scale_offset;
			    trade.OrderKind[0] = ele["isBestMatch"].GetBool() ? 'B' : 'N';
			    trade.OrderBSFlag[0] = ele["isBuyerMaker"].GetBool() ? 'B' : 'S';
			    on_trade(&trade);
		    }
	    }
    }   
}


void MDEngineCoinmex::loop()
{
		while(isRunning)
		{
				using namespace std::chrono;
				auto current_ms = duration_cast< milliseconds>(system_clock::now().time_since_epoch()).count();
				if(last_rest_get_ts != 0 && (current_ms - last_rest_get_ts) < rest_get_interval_ms)
				{	
						continue;	
				}

				last_rest_get_ts = current_ms;

				for(auto& symbol : symbols)
				{
						GetAndHandleDepthResponse(symbol, book_depth_count);

						GetAndHandleTradeResponse(symbol, trade_count);
				}	
		}
}

BOOST_PYTHON_MODULE(libcoinmexmd)
{
    using namespace boost::python;
    class_<MDEngineCoinmex, boost::shared_ptr<MDEngineCoinmex> >("Engine")
    .def(init<>())
    .def("init", &MDEngineCoinmex::initialize)
    .def("start", &MDEngineCoinmex::start)
    .def("stop", &MDEngineCoinmex::stop)
    .def("logout", &MDEngineCoinmex::logout)
    .def("wait_for_stop", &MDEngineCoinmex::wait_for_stop);
}
