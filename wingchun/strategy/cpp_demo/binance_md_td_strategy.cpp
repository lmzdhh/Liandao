/**
 * Strategy demo, same logic as band_demo_strategy.py
 * @Author cjiang (changhao.jiang@taurus.ai)
 * @since   Nov, 2017
 */

#include "IWCStrategy.h"
#include <deque>

USING_WC_NAMESPACE

#define SOURCE_INDEX SOURCE_BINANCE
#define M_TICKER "btcusdt"
#define M_EXCHANGE EXCHANGE_SHFE
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

class Strategy: public IWCStrategy
{
protected:
    bool td_connected;
    bool trade_completed;
    int rid;
    int cancel_id;
    int md_num;
    int traded_volume;
    Signal signal;
public:
    virtual void init();
    virtual void on_market_data(const LFMarketDataField* data, short source, long rcv_time);
    virtual void on_rsp_position(const PosHandlerPtr posMap, int request_id, short source, long rcv_time);
    virtual void on_rtn_trade(const LFRtnTradeField* data, int request_id, short source, long rcv_time);
    virtual void on_rsp_order(const LFInputOrderField* data, int request_id, short source, long rcv_time, short errorId=0, const char* errorMsg=nullptr);
    virtual void on_price_book_update(const LFPriceBook20Field* data, short source, long rcv_time);

public:
    Strategy(const string& name);
};

Strategy::Strategy(const string& name): IWCStrategy(name)
{
    std::cout << "[Strategy] (Strategy)" <<std::endl;
    rid = -1;
    cancel_id = -1;

}

void Strategy::init()
{
    std::cout << "[Strategy] (init)" <<std::endl;
    KF_LOG_DEBUG(logger, "[init] SOURCE_INDEX:" << SOURCE_INDEX);
    data->add_market_data(SOURCE_INDEX);
    data->add_register_td(SOURCE_INDEX);
    vector<string> tickers;
    tickers.push_back(M_TICKER);
    util->subscribeMarketData(tickers, SOURCE_INDEX);
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
    int my_order_id = 0;
    std::cout << "[Strategy] (init) end" <<std::endl;
}

void Strategy::on_rsp_position(const PosHandlerPtr posMap, int request_id, short source, long rcv_time)
{
    KF_LOG_INFO(logger, " [on_rsp_position] (source)" << source << " (request_id)" << request_id << " (PosHandlerPtr)" << posMap->to_string());
    if (request_id == -1 && source == SOURCE_INDEX)
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

void Strategy::on_market_data(const LFMarketDataField* md, short source, long rcv_time)
{
    KF_LOG_INFO(logger, "[on_market_data] (source)"<< source << " (InstrumentID)" << md->InstrumentID <<  "(AskPrice1)" << md->AskPrice1);
}

void Strategy::on_price_book_update(const LFPriceBook20Field* data, short source, long rcv_time)
{
    KF_LOG_INFO(logger, "[on_price_book_update] (source)" << source << " (ticker)" << data->InstrumentID
                                                                         << " (bidcount)" << data->BidLevelCount
                                                                         << " (askcount)" << data->AskLevelCount);

    if(rid == -1)
    {
        //new insert
        rid = util->insert_limit_order(SOURCE_INDEX, M_TICKER, M_EXCHANGE,
                                       289, 51200000000,
                                       LF_CHAR_Buy, LF_CHAR_Open);

        KF_LOG_DEBUG(logger, "[insert_limit_order] (rid)" << rid);
    } else {
        //cancel it
        if(cancel_id == -1) {
            cancel_id = util->cancel_order(SOURCE_INDEX, rid);
            KF_LOG_DEBUG(logger, "[cancel_order] (cancel_id)" << cancel_id);
        }
    }
}


void Strategy::on_rtn_trade(const LFRtnTradeField* rtn_trade, int request_id, short source, long rcv_time)
{
    KF_LOG_DEBUG(logger, "[on_rtn_trade]" << " (InstrumentID)" << rtn_trade->InstrumentID <<
                                          " (Price)" << rtn_trade->Price <<
                                          " (Volume)" << rtn_trade->Volume);

}

void Strategy::on_rsp_order(const LFInputOrderField* order, int request_id, short source, long rcv_time, short errorId, const char* errorMsg)
{
    KF_LOG_ERROR(logger, "[on_rsp_order] (err_id)" << errorId << " (err_msg)" << errorMsg <<
                                                   " (order_id)" << request_id <<
                                                   " (source)" << source <<
                                                   " (LFInputOrderField.InstrumentID)" << order->InstrumentID <<
                                                   " (LFInputOrderField.OrderRef)" << order->OrderRef <<
                                                   " (LFInputOrderField.LimitPrice)" << order->LimitPrice <<
                                                   "(LFInputOrderField.Volume)" << order->Volume);
}


int main(int argc, const char* argv[])
{
    Strategy str(string("YOUR_STRATEGY2"));
    str.init();
    str.start();
    str.block();
    return 0;
}