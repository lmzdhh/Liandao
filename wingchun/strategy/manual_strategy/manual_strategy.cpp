#include "IWCStrategy.h"
#include <deque>
#include <string>

USING_WC_NAMESPACE

#define TRADED_VOLUME_LIMIT 500

struct Signal
{
    string name;
    int look_back;
    int param1;
    int param2;
    int trade_size;
    std::deque<double> TickPrice;
    bool has_open_position;
    bool has_open_long_position;
    bool has_open_short_position;
};

class ManualStrategy: public IWCStrategy
{
protected:
    bool td_connected;
    bool trade_completed;
    int rid;
    int md_num;
    int traded_volume;
    Signal signal;
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
    // ========= bind and initialize a signal ========
    signal.name = "sample_signal";
    signal.look_back = 1000;
    signal.param1 = 200;
    signal.param2 = 50;
    signal.TickPrice.clear();
    signal.has_open_position = false;
    signal.has_open_long_position = false;
    signal.has_open_short_position = false;
    signal.trade_size = 1;
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

    ManualStrategy str(string("manual_strategy"), exch_src_idx, exchange_name, symbol_str);
    str.init();
    str.start();
    str.block();
    return 0;
}
