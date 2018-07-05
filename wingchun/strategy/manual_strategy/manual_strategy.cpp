#include "IWCStrategy.h"
#include "longfist/LFUtils.h"

#include <queue>
#include <string>
#include <readline/readline.h>
#include <readline/history.h>
#include <stdio.h>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <mutex>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <fstream>
#include <memory>

USING_WC_NAMESPACE

static bool g_should_stop = false;

using exchange_map_t = std::unordered_map<std::string, short>;

enum event_type
{
	status_update = 0,
	insert_order,
	cancel_order,
	market_data,
	req_pos,
	help,
	unknown
};	

struct order_info
{
	std::unordered_set<int> request_id_set;
	
	short exch_source = SOURCE_UNKNOWN;		
	LfOrderStatusType status = LF_CHAR_Unknown;	
	LfDirectionType side = LF_CHAR_Buy;
	LfTimeConditionType tif = LF_CHAR_IOC;
	LfOrderPriceTypeType type;
	
	int64_t price = 0;	
	uint64_t total_qty = 0;
	uint64_t trade_qty = 0;
};

struct trade_info
{
	order_info* order;
	
	int64_t price;
	uint64_t volume;
};

struct manual_strategy_controller_context
{
	static const char* help_msg()
	{
		return  "usage: \n"
			"0 - print strategy status e.g. information of order, pos and etc\n"
			"1 <exchange> <ticker> <price> <qty> <side>(0->buy, 1->sell) <tif>(1->IOC, 3->GTD) - insert order\n"
			"2 <exchange> <order_id>(-1 -> cancel all) - cancel order\n"
			"3 <exchange> <ticker> - print latest market data e.g. bbo, last price and etc\n"
			"4 <exchange> - request pos from exchange\n"
			"5 - print this help message\n";
	}

	manual_strategy_controller_context(WCStrategyUtilPtr util, const exchange_map_t& mes, const exchange_map_t& tes) 
																: strategy_util(util), md_exchanges(mes), td_exchanges(tes)
	{
	}

	struct market_data_info
	{
		int64_t best_bid_px;
		int64_t best_ask_px;
		uint64_t best_bid_qty;
		uint64_t best_ask_qty;
		int64_t last_px;
  		uint64_t last_volume;
	};

	using ticker_market_data_t = std::unordered_map<std::string, market_data_info>;
	std::unordered_map<short, ticker_market_data_t> all_ticker_market_data;

	std::mutex market_data_mutex;
	
	struct pos_info
	{
		uint64_t long_pos;
		uint64_t short_pos;	
	};
	
	std::unordered_map<std::string, pos_info> ticker_pos_info;
	
	std::mutex order_info_mutex;	
	std::unordered_map<int, std::shared_ptr<order_info>> all_order_info_map;
	std::unordered_map<short, std::unordered_set<int>> exchange_order_info;
	
	const exchange_map_t& md_exchanges;
	const exchange_map_t& td_exchanges;

	WCStrategyUtilPtr strategy_util;
};

struct event_base
{
	event_base(event_type et) : type(et)
	{
	}

	virtual bool parse_line(const char* line_str) = 0;
	
	virtual void to_str(char*, size_t) const = 0;	
	
	virtual void process() const = 0;

	void set_strategy_context(manual_strategy_controller_context& mscc)
	{
		strategy_context = &mscc;
	}

	event_type type = unknown;	
	
	short exch_source = SOURCE_UNKNOWN;
	
	manual_strategy_controller_context* strategy_context = nullptr;
};

struct status_update_event : event_base
{
	status_update_event() : event_base(status_update)
	{
	}

	bool parse_line(const char* line_str) override
	{ 
		int et;
		int ret = sscanf(line_str, "%d", &et);
		
		return ret == 1 && et == static_cast<int>(status_update);
	}
	
	void process() const override
	{
		std::lock_guard<std::mutex> guard(strategy_context->order_info_mutex);
        for(const auto& item : strategy_context->exchange_order_info)
        {
            std::stringstream ss;
            for(auto i : item.second)
            {
                auto iter = strategy_context->all_order_info_map.find(i);
                if(iter == strategy_context->all_order_info_map.end())
                {
                    continue;
                }

                const auto status = iter->second->status;
                if (status != LF_CHAR_Unknown && status != LF_CHAR_Error && status != LF_CHAR_AllTraded
                        && status != LF_CHAR_Canceled && status != LF_CHAR_PartTradedNotQueueing)
                {	
                    ss << i << ", ";
                }
            }

            fprintf(stdout, "all active order on exchange %u request id: %s\n", item.first, ss.str().c_str());
        }
	}

	void to_str(char* buf, size_t size) const override
	{
		snprintf(buf, size, "status_update");
	}
};

struct insert_order_event : event_base
{
	insert_order_event() : event_base(insert_order)
	{
	}

	bool parse_line(const char* line_str) override
	{
		int et;
		int ret = sscanf(line_str, "%d %s %s %ld %lu %c %c ", &et, exchange, ticker, &price, &qty, &side, &tif);
		if(ret != 7 || et != static_cast<int>(insert_order))
		{
			return false;
		}

		auto iter = strategy_context->td_exchanges.find(std::string(exchange));
		if(iter == strategy_context->td_exchanges.end())
		{
			fprintf(stdout, "insert_order_event: invalid td exchange %s\n", exchange);
			return false;
		}

		exch_source = iter->second;

		return true;
	}

	void process() const override
	{
		if(strategy_context && strategy_context->strategy_util)
		{	
			char buf[128] = {};
			to_str(buf, 128);
			fprintf(stdout, "%s\n", buf);
			
			int request_id = -1;	
			
			std::lock_guard<std::mutex> guard(strategy_context->order_info_mutex);

			if(tif == LF_CHAR_IOC)
			{
				request_id = strategy_context->strategy_util->insert_fok_order(
						exch_source, std::string(ticker), std::string(exchange), price, qty, side, LF_CHAR_Open);
			}
			else if(tif == LF_CHAR_GFD)
			{
				request_id = strategy_context->strategy_util->insert_limit_order(
						exch_source, std::string(ticker), std::string(exchange), price, qty, side, LF_CHAR_Open);
			}
			
			if(request_id != -1)
			{	
				auto ret = strategy_context->all_order_info_map.emplace(std::make_pair(request_id, std::make_shared<order_info>()));
				if(!ret.second)
				{
					fprintf(stdout, "duplicate request id [%d] in order map, exit...\n", request_id);
					g_should_stop = true;
					return;
				}
				
				ret.first->second->request_id_set.insert(request_id);
				ret.first->second->exch_source = exch_source;
				
				strategy_context->exchange_order_info[exch_source].insert(request_id);
	
				fprintf(stdout, "inserted a limit order with returned request_id [%d] and source %u\n", request_id, exch_source);
			}
			else
			{
				char buf[128] = {};
				
				to_str(buf, 128);
				fprintf(stdout, "failed to insert order with [%s]\n", buf);
			}

			/*
			else if (type == LF_CHAR_AnyPrice)
			{
				request_id = strategy_context->strategy_util->insert_market_order(
						exch_source, std::string(ticker), std::string(exchange),  qty, side, LF_CHAR_Open);
			}
			*/
		}
		else
		{
			fprintf(stdout, "no strategy context, failed to insert order\n");
		}
	}

	void to_str(char* buf, size_t size) const override
	{
		snprintf(buf, size, "insert_order %s %s %ld %lu %c %c %c", exchange, ticker, price, qty, side, type, tif);
	}

	char exchange[16] = {};
	char ticker[16] = {};
	int64_t price = 0;
	uint64_t qty = 0;
	LfDirectionType side = 0;
	LfOrderPriceTypeType type = LF_CHAR_LimitPrice;
	LfTimeConditionType tif = 0;
};

struct cancel_order_event : event_base
{
	cancel_order_event() : event_base(cancel_order)
	{
	}

	bool parse_line(const char* line_str) override
	{
		int et;
		int ret = sscanf(line_str, "%d %s %d", &et, exchange, &order_id);
		if(ret != 3 || et != static_cast<int>(cancel_order))
		{
			return false;
		}
		
		auto iter = strategy_context->td_exchanges.find(std::string(exchange));
		if(iter == strategy_context->td_exchanges.end())
		{
			fprintf(stdout, "cancel_order_event: invalid td exchange %s\n", exchange);
			return false;
		}
		
		exch_source = iter->second;

		if(order_id == -1)
		{
			return true;
		}
	
		std::lock_guard<std::mutex> guard(strategy_context->order_info_mutex);
		auto oiter = strategy_context->all_order_info_map.find(order_id);
		if(oiter == strategy_context->all_order_info_map.end())
		{
			fprintf(stdout, "cancel order: unknown request id [%d] in order map\n", order_id);
			return false;
		}
		
		orig_order = oiter->second;

		return true;
	}
	
	void process() const override
	{
		if(strategy_context && strategy_context->strategy_util)
		{
			std::lock_guard<std::mutex> guard(strategy_context->order_info_mutex);

			if(order_id == -1)
			{
				auto& exch_request_id_set = strategy_context->exchange_order_info[exch_source];
				for(auto i : exch_request_id_set)
				{
					auto oiter = strategy_context->all_order_info_map.find(i);
					if(oiter == strategy_context->all_order_info_map.end())
					{
						fprintf(stdout, "cancel all orders: unknown request id [%d] in order map\n", i);
						continue;
					}
					
					int request_id = strategy_context->strategy_util->cancel_order(exch_source, i);
					if(request_id != -1)
					{	
							oiter->second->request_id_set.insert(request_id);
							strategy_context->all_order_info_map[request_id] = oiter->second;

							fprintf(stdout, "cancel an order [%d] with returned request_id [%d]\n", i, request_id);
					}
					else
					{
							fprintf(stdout, "failed to cancel an order [%d], exit\n", i);
							g_should_stop = true;
							continue;
					}	
				}
			}
			else if(orig_order)
			{
					int request_id = strategy_context->strategy_util->cancel_order(exch_source, order_id);
					if(request_id != -1)
					{	
							orig_order->request_id_set.insert(request_id);
							strategy_context->all_order_info_map[request_id] = orig_order;

							fprintf(stdout, "cancel an order [%d] with returned request_id [%d]\n", order_id, request_id);
					}
					else
					{
							fprintf(stdout, "failed to cancel an order [%d], exit\n", order_id);
							g_should_stop = true;
							return;
					}
			}
		}
		else
		{
			fprintf(stdout, "no strategy context, failed to cancel order\n");
		}
	}

	void to_str(char* buf, size_t size) const override
	{
		snprintf(buf, size, "cancel_order %s %d", exchange, order_id);
	}
	
	char exchange[16] = {};
	int order_id = 0;
	std::shared_ptr<order_info> orig_order;
};

struct req_pos_event : event_base
{
	req_pos_event() : event_base(req_pos)
	{
	}

	bool parse_line(const char* line_str) override
	{
		int et;
		int ret = sscanf(line_str, "%d %s", &et, exchange);
		if(ret != 2 || et != static_cast<int>(req_pos))
		{
			return false;
		}
		
		auto iter = strategy_context->td_exchanges.find(std::string(exchange));
		if(iter == strategy_context->td_exchanges.end())
		{
			fprintf(stdout, "req_pos_event: invalid td exchange %s\n", exchange);
			return false;
		}

		exch_source = iter->second;

		return true;
	}
	
	void process() const override
	{
		int request_id = strategy_context->strategy_util->req_position(exch_source);
		fprintf(stdout, "req position with returned request id [%d] and td source [%u]\n", request_id, exch_source);
	}

	void to_str(char* buf, size_t size) const override
	{
		snprintf(buf, size, "req_pos %s", exchange);
	}
	
	char exchange[16] = {};
};


struct market_data_event : event_base
{
	market_data_event() : event_base(market_data)
	{
	}

	bool parse_line(const char* line_str) override
	{
		int et;
		int ret = sscanf(line_str, "%d %s %s", &et, exchange, ticker);
		if(ret != 3 || et != static_cast<int>(market_data))
		{
			return false;
		}
		
		std::string exch_str(exchange);	
		auto iter = strategy_context->md_exchanges.find(exch_str);
		if(iter == strategy_context->md_exchanges.end())
		{
			fprintf(stdout, "market_data_event: invalid md exchange %s\n", exchange);
			return false;
		}

		exch_source = iter->second;

		return true;
	}

	void process() const override
	{
		if(strategy_context)
		{
			std::string symbol(ticker);
			std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::toupper);

		    std::lock_guard<std::mutex> guard(strategy_context->market_data_mutex);
			
			auto exch_iter = strategy_context->all_ticker_market_data.find(exch_source);
			if(exch_iter == strategy_context->all_ticker_market_data.end())
			{
				fprintf(stdout, "no market data for exchange %s", exchange);
				return;
			}
			
			const auto& ticker_market_data = exch_iter->second;

			auto iter = ticker_market_data.find(symbol);
			if(iter != ticker_market_data.end())
			{
				auto& md = iter->second;
				fprintf(stdout, "%lu %ld X %lu %ld, %lu @ %ld\n", 
					md.best_bid_qty, md.best_bid_px, md.best_ask_px, md.best_ask_qty, md.last_volume, md.last_px);
			}
			else
			{
				fprintf(stdout, "no market data for ticker %s on exchange %s\n", ticker, exchange);
			}
		}
		else
		{
			fprintf(stdout, "invalid strategy context\n");
		}
	}

	void to_str(char* buf, size_t size) const override
	{
		snprintf(buf, size, "market_data");
	}

	char exchange[16] = {};
	char ticker[16] = {};
};

struct help_event : event_base
{
	help_event() : event_base(help)
	{
	}

	bool parse_line(const char* line_str) override
	{
		int et;
		int ret = sscanf(line_str, "%d", &et);
		
		return ret == 1 && et == static_cast<int>(help);
	}

	void process() const override
	{
		fprintf(stdout, strategy_context->help_msg());
	}

	void to_str(char* buf, size_t size) const override
	{
		snprintf(buf, size, "help");
	}
};


struct event_factory
{
	static event_base* create_event(event_type et)
	{
		switch(et)
		{
			case status_update:
				return new status_update_event;
			case insert_order:
				return new insert_order_event;
			case cancel_order:
				return new cancel_order_event;
			case market_data:
				return new market_data_event;
			case req_pos:
				return new req_pos_event;
			case help:
				return new help_event;
			default:
				return nullptr;
		}
	}
	
	static void claim_event(event_base* e)
	{
		delete e;
	}
};

class manual_strategy_controller
{
public:
	
	manual_strategy_controller(WCStrategyUtilPtr util, const exchange_map_t& mes, const exchange_map_t& tes) 
                                                                    : mscc(util, mes, tes), md_exchanges(mes), td_exchanges(tes)
	{
	}

	const char* help_msg()
	{
		return mscc.help_msg();
	}	
	
	void add_event(int v, const char* event_line)
	{
		event_type event = event_type::unknown;

		switch(v)
		{
			case 0:
				event = status_update;
				break;
			case 1:
				event = insert_order;
				break;
			case 2:
				event = cancel_order;
				break;
			case 3:
				event = market_data;
				break;
			case 4:
				event = req_pos;
				break;
			case 5:
				event = help;
				break;
			default:
				event = unknown;
				break;
		}
		
		auto event_ptr = event_factory::create_event(event);
		event_ptr->set_strategy_context(mscc);
		if(event_ptr->parse_line(event_line))
		{
		    event_queue.push(event_ptr);
		}
		else
		{
		    //failed to create an event	
		    fprintf(stderr, "invalid event type %d or arguments\n", v);
		}
	}

	event_base* get_next_event() const
	{
		if(!event_queue.empty())
		{
			return event_queue.front();
		}

		return nullptr;
	}
	
	void remove_next_event()
	{
		if(!event_queue.empty())
		{
			event_queue.pop();
		}
	}

public:
	
    void on_market_data(const LFMarketDataField* data, short source, long rcv_time)
	{
		std::lock_guard<std::mutex> guard(mscc.market_data_mutex);

		std::string ticker = data->InstrumentID;
		auto& md = mscc.all_ticker_market_data[source][ticker];
		md.best_bid_px = data->BidPrice1;
		md.best_bid_qty = data->BidVolume1;
		md.best_ask_px = data->AskPrice1;
		md.best_ask_qty = data->AskVolume1;	
	}
	
	void on_l2_trade(const LFL2TradeField* data, short source, long rcv_time)
	{
		std::lock_guard<std::mutex> guard(mscc.market_data_mutex);

		std::string ticker = data->InstrumentID;
		auto& md = mscc.all_ticker_market_data[source][ticker];
		md.last_px = data->Price;
		md.last_volume = data->Volume;
	}
	
	void on_rtn_order(const LFRtnOrderField* msg, int request_id, short source, long rcv_time)
	{
		std::lock_guard<std::mutex> guard(mscc.order_info_mutex);
		auto iter = mscc.all_order_info_map.find(request_id);
		if(iter == mscc.all_order_info_map.end())
		{
			fprintf(stdout, "on_rtn_order: unknown request id %d\n", request_id);
			return;
		}

		auto order = iter->second;
		order->status = msg->OrderStatus;
		order->side = msg->Direction;
		order->tif = msg->TimeCondition;
		order->type = msg->OrderPriceType;

		order->price = msg->LimitPrice;
		order->total_qty = msg->VolumeTotalOriginal;
		order->trade_qty = msg->VolumeTraded;		
	
		const auto status = order->status;	
		if (status == LF_CHAR_Unknown || status == LF_CHAR_Error || status == LF_CHAR_AllTraded
                    || status == LF_CHAR_Canceled || status == LF_CHAR_PartTradedNotQueueing)
       	{
			auto& exch_request_id_set = mscc.exchange_order_info[order->exch_source];
        	for(auto i : order->request_id_set)
			{
				mscc.all_order_info_map.erase(i);			
				exch_request_id_set.erase(i);
			}
        }
	}

private:
	
	std::queue<event_base*> event_queue;

    const exchange_map_t& md_exchanges;

    const exchange_map_t& td_exchanges;

	manual_strategy_controller_context mscc;
};

class ManualStrategy: public IWCStrategy
{
public:
    virtual void init();
    virtual void on_market_data(const LFMarketDataField* data, short source, long rcv_time);
	virtual void on_l2_trade(const LFL2TradeField* data, short source, long rcv_time);
    virtual void on_rsp_position(const PosHandlerPtr posMap, int request_id, short source, long rcv_time);
    virtual void on_rtn_order(const LFRtnOrderField* data, int request_id, short source, long rcv_time);
    virtual void on_rtn_trade(const LFRtnTradeField* data, int request_id, short source, long rcv_time);
    virtual void on_rsp_order(const LFInputOrderField* data, int request_id, short source, long rcv_time, short errorId, const char* errorMsg);
    virtual void on_rsp_order_action(const LFOrderActionField* data, int request_id, short source, long rcv_time, short errorId, const char* errorMsg);

public:
    ManualStrategy(const string& name, const exchange_map_t& md_exchanges, const exchange_map_t& td_exchanges);

    void on_command_line(const char*);

private:
    
    exchange_map_t md_exchanges;

    exchange_map_t td_exchanges;

    manual_strategy_controller msc;
};

ManualStrategy::ManualStrategy(const string& name, const exchange_map_t& _md_exchanges, const exchange_map_t& _td_exchanges): 
                            IWCStrategy(name), md_exchanges(_md_exchanges), td_exchanges(_td_exchanges), msc(util, md_exchanges, td_exchanges)
{
}

void ManualStrategy::on_command_line(const char* line)
{
    int et = 0;
    sscanf(line, "%d", &et);

    msc.add_event(et, line);
    
    event_base* e = nullptr;
    while(e = msc.get_next_event())
    {
		e->process();	
		msc.remove_next_event();	
    }
}


void ManualStrategy::init()
{
    for(const auto& i : md_exchanges)
    {
        data->add_market_data(i.second);
    }

    for(const auto& i : td_exchanges)
    {
        data->add_register_td(i.second);
    }

    fprintf(stdout, msc.help_msg());
}

void ManualStrategy::on_rsp_position(const PosHandlerPtr posMap, int request_id, short source, long rcv_time)
{
    if (request_id == -1)
    {
        KF_LOG_INFO(logger, "td connected");
        if (posMap.get() == nullptr)
        {
            data->set_pos(PosHandlerPtr(new PosHandler(source)), source);
        }
    }
    else
    {
        KF_LOG_DEBUG(logger, "[RSP_POS] " << posMap->to_string());

		fprintf(stdout, "on_rsp_position: %s\n", posMap->to_string().c_str());
    }
}

void ManualStrategy::on_market_data(const LFMarketDataField* md, short source, long rcv_time)
{
	msc.on_market_data(md, source, rcv_time);
}

void ManualStrategy::on_l2_trade(const LFL2TradeField* data, short source, long rcv_time)
{
	msc.on_l2_trade(data, source, rcv_time);
}

void ManualStrategy::on_rtn_order(const LFRtnOrderField* msg, int request_id, short source, long rcv_time)
{
    KF_LOG_DEBUG(logger, "[ORDER]" << " (t)" << msg->InstrumentID << " (px)" << msg->LimitPrice
                                   << " (volume)" << msg->VolumeTotal << " (pos)" << data->get_pos(source)->to_string());
	
	fprintf(stdout, "on_rtn_order: instrument_id [%s], request_id [%d], source [%u], price [%ld], volume [%lu], volume traded [%lu], status [%s]\n",
						msg->InstrumentID, request_id, source, msg->LimitPrice, msg->VolumeTotalOriginal, msg->VolumeTraded, getLfOrderStatusType(msg->OrderStatus).c_str());
	
	msc.on_rtn_order(msg, request_id, source, rcv_time);
}

void ManualStrategy::on_rsp_order_action(const LFOrderActionField* data, int request_id, short source, long rcv_time, short errorId, const char* errorMsg)
{
    if (errorId != 0)
	{
        KF_LOG_ERROR(logger, " (err_id)" << errorId << " (err_msg)" << errorMsg << "(order_id)" << request_id << " (source)" << source);
	}

	fprintf(stdout, "on_rsp_order_action: request_id [%d], error_id [%u], error_msg [%s]\n", request_id, errorId, errorMsg);
}

void ManualStrategy::on_rtn_trade(const LFRtnTradeField* rtn_trade, int request_id, short source, long rcv_time)
{
    KF_LOG_DEBUG(logger, "[TRADE]" << " (ticker)" << rtn_trade->InstrumentID << " (px)" << rtn_trade->Price
                                   << " (volume)" << rtn_trade->Volume << " (pos)" << data->get_pos(source)->to_string());

	fprintf(stdout, "on_rtn_trade: instrument_id [%s], request_id [%d], price [%ld], volume [%lu], position [%s]\n", 
					rtn_trade->InstrumentID, request_id, rtn_trade->Price, rtn_trade->Volume, data->get_pos(source)->to_string().c_str());
}

void ManualStrategy::on_rsp_order(const LFInputOrderField* order, int request_id, short source, long rcv_time, short errorId, const char* errorMsg)
{
    if (errorId != 0)
	{
        KF_LOG_ERROR(logger, " (err_id)" << errorId << " (err_msg)" << errorMsg << "(order_id)" << request_id << " (source)" << source);
	}
	
	fprintf(stdout, "on_rsp_order: request_id [%d], error_id [%u], error_msg [%s]\n", request_id, errorId, errorMsg);
}

static char* line = nullptr;

int main(int argc, const char* argv[])
{
    if(argc != 2)
    {
        fprintf(stderr, "usage: %s <config_file_path>\n", argv[0]);
        exit(1);
    }
    
    const std::string config_file_path = argv[1];
    std::ifstream inFile;
    inFile.open(config_file_path);
    
    std::stringstream strStream;
    strStream << inFile.rdbuf();
    const string config_str = strStream.str();

	json j_config = json::parse(config_str);
	
	exchange_map_t md_exchanges;
	exchange_map_t td_exchanges;
	
	auto parse_exchange_func = [&j_config](const char* name, exchange_map_t& exchanges){
							for(const auto& t : j_config[name])
							{
								std::string exchange_name = t.get<string>();
								exchange_source_index exch_src_idx = get_source_index_from_str(exchange_name);
								
								if(exch_src_idx == SOURCE_UNKNOWN)
								{
									fprintf(stderr, "invalid md exchange name [%s]", exchange_name.c_str());
									exit(1);
								}
								
								fprintf(stdout, "%s - add %s with source %u\n", name, exchange_name.c_str(), exch_src_idx);	
								exchanges[exchange_name] = exch_src_idx;
							}
						};
	
   	parse_exchange_func("md_exchanges", md_exchanges); 
   	parse_exchange_func("td_exchanges", td_exchanges); 

    ManualStrategy str(string("manual_strategy"), md_exchanges, td_exchanges);
    str.init();
    str.start();

    fprintf(stdout, "going to interactive session, print <quit> to end the session\n");
		
	while(g_should_stop || IWCDataProcessor::signal_received < 0)
	{ 
			line = readline (">> ");
			if(line && strlen(line) > 0)
			{
					if(strcmp(line, "quit") == 0)
					{
							break;
					}
					add_history(line);

					str.on_command_line(line);	

					free(line);
			}
	}

	str.stop();
    str.block();
}
