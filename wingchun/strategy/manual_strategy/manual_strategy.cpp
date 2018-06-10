#include "IWCStrategy.h"
#include <queue>
#include <string>
#include <readline/readline.h>
#include <readline/history.h>
#include <stdio.h>

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
		if(event_ptr)
		{
		    event_queue.push(event_ptr);
		}
		else
		{
		    //failed to create an event	
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
    virtual void on_rtn_trade(const LFRtnTradeField* data, int request_id, short source, long rcv_time);
    virtual void on_rsp_order(const LFInputOrderField* data, int request_id, 
			short source, long rcv_time, short errorId=0, const char* errorMsg=nullptr);

public:
    ManualStrategy(const string& name, exchange_source_index _exch_src_idx, const string& _exch_name, const string& _symbol);

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


    while(true)
    { 
	char *line = readline (">> ");
	if(line)
	{
	    add_history(line);
	    int et = 0;
	    sscanf(line, "%d", &et);
	    fprintf(stdout, "%d\n", et);
	    int first_arg, second_arg;
            sscanf(line, "%d%d%d", &et, &first_arg, &second_arg);
	    fprintf(stdout, "%d %d %d\n", et, first_arg, second_arg);
	    
	    free(line);
	}
    }

    /*
    ManualStrategy str(string("manual_strategy"), exch_src_idx, exchange_name, symbol_str);
    str.init();
    str.start();
    str.block();
    */
    return 0;
}
