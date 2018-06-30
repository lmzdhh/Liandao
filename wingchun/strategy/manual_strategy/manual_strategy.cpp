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

USING_WC_NAMESPACE

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
	int order_id;
	
	LfOrderStatusType state = LF_CHAR_Unknown;	
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
			"2 <exchange> <order_id> - cancel order\n"
			"3 <exchange> <ticker> - print latest market data e.g. bbo, last price and etc\n"
			"4 <exchange> - request pos from exchange\n"
			"5 - print this help message\n";
	}

	manual_strategy_controller_context(WCStrategyUtilPtr util) : strategy_util(util)
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

	std::unordered_map<std::string, market_data_info> ticker_market_data;
	std::mutex market_data_mutex;
	
	struct pos_info
	{
		uint64_t long_pos;
		uint64_t short_pos;	
	};
	
	std::unordered_map<std::string, pos_info> ticker_pos_info;
	
	struct pending_order_info
	{
		uint64_t total_buy_qty;
		uint64_t total_sell_qty;	
	};

	std::unordered_map<std::string, pending_order_info> ticker_pending_order_info;
	
	std::unordered_map<int, order_info*> all_order_map;
	
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
	
	exchange_source_index exch_source = SOURCE_UNKNOWN;
 	
	const char* exch_name = "";
	
	manual_strategy_controller_context* strategy_context = nullptr;
};

struct status_update_event : event_base
{
	status_update_event() : event_base(status_update)
	{
	}

	bool parse_line(const char* line_str) override
	{
		return true;
	}
	
	void process() const override
	{
		char buf[128] = {};
		to_str(buf, 128);

		fprintf(stdout, "%s\n", buf);
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
		if(ret != 7)
		{
			return false;
		}

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
			
			if(tif == LF_CHAR_IOC)
			{
				request_id = strategy_context->strategy_util->insert_fok_order(
						exch_source, std::string(ticker), std::string(exch_name), price, qty, side, LF_CHAR_Open);
			}
			else if(tif == LF_CHAR_GFD)
			{
				request_id = strategy_context->strategy_util->insert_limit_order(
						exch_source, std::string(ticker), std::string(exch_name), price, qty, side, LF_CHAR_Open);
			}
			
			if(request_id != -1)
			{	
				fprintf(stdout, "inserted a limit order with returned request_id [%d]\n", request_id);
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
						exch_source, std::string(ticker), std::string(exch_name),  qty, side, LF_CHAR_Open);
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
		if(ret != 3)
		{
			return false;
		}

		return true;
	}
	
	void process() const override
	{
		if(strategy_context && strategy_context->strategy_util)
		{
			char buf[128] = {};
			to_str(buf, 128);
			fprintf(stdout, "%s\n", buf);

			int request_id = strategy_context->strategy_util->cancel_order(exch_source, order_id);
			
			fprintf(stdout, "cancel an order with returned request_id [%d]\n", request_id);
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
	int order_id = -1;
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
		if(ret != 2)
		{
			return false;
		}

		return true;
	}
	
	void process() const override
	{
		int request_id = strategy_context->strategy_util->req_position(exch_source);
		fprintf(stdout, "req position with returned request id [%d]\n", request_id);
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
		if(ret != 3)
		{
			return false;
		}

		return true;
	}

	void process() const override
	{
		if(strategy_context)
		{
			std::string symbol(ticker);
			std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::toupper);

		    std::lock_guard<std::mutex> guard(strategy_context->market_data_mutex);

			auto iter = strategy_context->ticker_market_data.find(symbol);
			if(iter != strategy_context->ticker_market_data.end())
			{
				auto& md = iter->second;
				fprintf(stdout, "%lu %ld X %lu %ld, %lu @ %ld\n", 
					md.best_bid_qty, md.best_bid_px, md.best_ask_px, md.best_ask_qty, md.last_volume, md.last_px);
			}
			else
			{
				fprintf(stdout, "no market data for ticker %s\n", ticker);
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
		return true;
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
	
	manual_strategy_controller(WCStrategyUtilPtr util, exchange_source_index exch_source, std::string ex_name) 
																: mscc(util), exch_src(exch_source), exch_name(ex_name)
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
		if(event_ptr && event_ptr->parse_line(event_line))
		{
		    event_ptr->set_strategy_context(mscc);
			event_ptr->exch_source = exch_src;
			event_ptr->exch_name = exch_name.c_str();
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
		auto& md = mscc.ticker_market_data[ticker];
		md.best_bid_px = data->BidPrice1;
		md.best_bid_qty = data->BidVolume1;
		md.best_ask_px = data->AskPrice1;
		md.best_ask_qty = data->AskVolume1;	
	}
	
	void on_l2_trade(const LFL2TradeField* data, short source, long rcv_time)
	{
		std::lock_guard<std::mutex> guard(mscc.market_data_mutex);

		std::string ticker = data->InstrumentID;
		auto& md = mscc.ticker_market_data[ticker];
		md.last_px = data->Price;
		md.last_volume = data->Volume;
	}

private:
	
	std::queue<event_base*> event_queue;

	manual_strategy_controller_context mscc;
	
	exchange_source_index exch_src;
	
	std::string exch_name;
};

class ManualStrategy: public IWCStrategy
{
protected:
    bool td_connected;
    bool trade_completed;
    int rid;
    int md_num;
    int traded_volume;
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
    ManualStrategy(const string& name, exchange_source_index _exch_src_idx, const string& _exch_name, const string& _symbol);

    void on_command_line(const char*);

private:
    exchange_source_index exch_src_index = SOURCE_UNKNOWN;
    string exch_name;
    string symbol;
	
    manual_strategy_controller msc;
};

ManualStrategy::ManualStrategy(const string& name, exchange_source_index _exch_src_idx, 
			const string& _exch_name, const string& _symbol): IWCStrategy(name), 
						exch_src_index(_exch_src_idx), exch_name(_exch_name), symbol(_symbol), msc(util, exch_src_index, exch_name)
{
    rid = -1;
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
    data->add_market_data(exch_src_index);
    data->add_register_td(exch_src_index);
    vector<string> tickers;
    tickers.push_back(symbol);
    util->subscribeMarketData(tickers, exch_src_index);
    // necessary initialization of internal fields.
    td_connected = false;
    trade_completed = true;
    md_num = 0;
    traded_volume = 0;

    fprintf(stdout, msc.help_msg());
}

void ManualStrategy::on_rsp_position(const PosHandlerPtr posMap, int request_id, short source, long rcv_time)
{
    if (request_id == -1 && source == exch_src_index)
    {
        td_connected = true;
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

	fprintf(stdout, "on_rtn_trade: instrument_id [%s], price [%ld], volume [%lu], position [%s]\n", 
					rtn_trade->InstrumentID, rtn_trade->Price, rtn_trade->Volume, data->get_pos(source)->to_string().c_str());
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
    if(argc != 3)
    {
        fprintf(stderr, "usage: %s <exchange> <symbol>\n", argv[0]);
        exit(1);
    }
    
    const std::string exchange_name = argv[1];
    exchange_source_index exch_src_idx = get_source_index_from_str(exchange_name);
   
    if(exch_src_idx == SOURCE_UNKNOWN)
	{
			fprintf(stderr, "invalid exchange name [%s]", argv[1]);
			exit(1);
	}

    const std::string symbol_str = argv[2];

    fprintf(stdout, "going to initialize strategy with exchange name [%s],  exchange source id [%d], symbol [%s]\n", 
						exchange_name.c_str(), static_cast<int>(exch_src_idx), symbol_str.c_str());

    ManualStrategy str(string("manual_strategy"), exch_src_idx, exchange_name, symbol_str);
    str.init();
    str.start();

    fprintf(stdout, "going to interactive session, print <quit> to end the session\n");
	
	while(IWCDataProcessor::signal_received < 0)
	{ 
			line = readline (">> ");
			if(line && strlen(line) > 0)
			{
					if(strcmp(line, "quit") == 0)
					{
							str.stop();
							break;
					}
					add_history(line);

					str.on_command_line(line);	

					free(line);
			}
	}

    str.block();
}
