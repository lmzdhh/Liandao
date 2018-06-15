#include "IWCStrategy.h"
#include <queue>
#include <string>
#include <readline/readline.h>
#include <readline/history.h>
#include <stdio.h>
#include <unordered_map>
#include <vector>

USING_WC_NAMESPACE

enum event_type
{
	status_update = 0,
	insert_order,
	cancel_order,
	market_data,
	help,
	unknown
};	

struct event_base
{
	event_base(event_type et) : type(et)
	{
	}

	virtual bool parse_line(const char* line_str) = 0;
	
	virtual const char* to_str() const = 0;	
	event_type type = unknown;	
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

	const char* to_str() const override
	{
		return "status update";
	}
};

struct insert_order_event : event_base
{
	insert_order_event() : event_base(insert_order)
	{
	}

	bool parse_line(const char* line_str) override
	{
		return true;
	}
	
	const char* to_str() const override
	{
		return "insert order";
	}

	std::string exchange;
	std::string ticker;
	int64_t price = 0;
	uint64_t qty = 0;
	char side = 0;
	char type = 0;
	char tif = 0;
};

struct cancel_order_event : event_base
{
	cancel_order_event() : event_base(cancel_order)
	{
	}

	bool parse_line(const char* line_str) override
	{
		return true;
	}

	const char* to_str() const override
	{
		return "cancel order";
	}

	int order_id = -1;
};

struct market_data_event : event_base
{
	market_data_event() : event_base(market_data)
	{
	}

	bool parse_line(const char* line_str) override
	{
		return true;
	}
	
	const char* to_str() const override
	{
		return "market data";
	}
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
	
	const char* to_str() const override
	{
		return "help";
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

struct manual_strategy_controller_context
{
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
	
	struct pos_info
	{
		uint64_t long_pos;
		uint64_t short_pos;	
	};
	
	std::unordered_map<std::string, pos_info> ticker_pos_info;
	
	struct order_info
	{
		uint64_t total_buy_qty;
		uint64_t total_sell_qty;	
	};

	std::unordered_map<std::string, order_info> ticker_order_info;

	WCStrategyUtilPtr strategy_util;
};


class manual_strategy_controller
{
public:

	static const char* help_msg()
	{
		return  "usage: \n"
			    "0 - print strategy status e.g. information of order, pos and etc\n"
				"1 <exchange> <ticker> <price> <qty> <side> <type> <tif> - insert order\n"
				"2 <exchange> <order_id> - cancel order\n"
				"3 - print latest market data e.g. bbo, last price and etc\n"
				"4 - print this help message\n";
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
				event = help;
				break;
			default:
				event = unknown;
				break;
		}
		
		auto event_ptr = event_factory::create_event(event);
		if(event_ptr && event_ptr->parse_line(event_line))
		{
		    event_queue.push(event_ptr);
		}
		else
		{
		    //failed to create an event	
		    fprintf(stderr, "invalid event type %d\n", v);
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

private:
	
	std::queue<event_base*> event_queue;
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
						exch_src_index(_exch_src_idx), exch_name(_exch_name), symbol(_symbol)
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
	fprintf(stdout, "%s\n", e->to_str());

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
    }
}

void ManualStrategy::on_market_data(const LFMarketDataField* md, short source, long rcv_time)
{
    KF_LOG_DEBUG(logger, "[BOOK]" << " (t)" << md->InstrumentID << " (bid px)" << md->BidPrice1
                                   << " (bid qty)" << md->BidVolume1 << " (ask px):" << md->AskPrice1 << " (ask qty)" << md->AskVolume1);
}

void ManualStrategy::on_rtn_order(const LFRtnOrderField* data, int request_id, short source, long rcv_time)
{

}

void ManualStrategy::on_rsp_order_action(const LFOrderActionField* data, int request_id, short source, long rcv_time, short errorId, const char* errorMsg)
{
}

void ManualStrategy::on_rtn_trade(const LFRtnTradeField* rtn_trade, int request_id, short source, long rcv_time)
{
    KF_LOG_DEBUG(logger, "[TRADE]" << " (t)" << rtn_trade->InstrumentID << " (p)" << rtn_trade->Price
                                   << " (v)" << rtn_trade->Volume << " POS:" << data->get_pos(source)->to_string());
}

void ManualStrategy::on_rsp_order(const LFInputOrderField* order, int request_id, short source, long rcv_time, short errorId, const char* errorMsg)
{
    if (errorId != 0)
        KF_LOG_ERROR(logger, " (err_id)" << errorId << " (err_msg)" << errorMsg << "(order_id)" << request_id << " (source)" << source);
}

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
    }

    const std::string symbol_str = argv[2];

    fprintf(stderr, "going to initialize strategy with exchange name [%s],  exchange source id [%d], symbol [%s]\n", 
						exchange_name.c_str(), static_cast<int>(exch_src_idx), symbol_str.c_str());

    ManualStrategy str(string("manual_strategy"), exch_src_idx, exchange_name, symbol_str);
    str.init();
    str.start();

    fprintf(stderr, "going to interactive session, print <quit> to end the session\n");
	
    while(true)
    { 
	char *line = readline (">> ");
	if(line)
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
