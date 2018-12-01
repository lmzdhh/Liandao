/**
 * Strategy demo, same logic as band_demo_strategy.py
 * @Author cjiang (changhao.jiang@taurus.ai)
 * @since   Nov, 2017
 */

#include "IWCStrategy.h"
#include <deque>
USING_WC_NAMESPACE

#define SOURCE_INDEX SOURCE_PROBIT
#define M_TICKER "xrp_usdt"
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
    int md_num;
    int traded_volume;
    Signal signal;
public:
    virtual void init();
    //virtual void on_market_data(const LFMarketDataField* data, short source, long rcv_time);
    virtual void on_rsp_position(const PosHandlerPtr posMap, int request_id, short source, long rcv_time);
    virtual void on_rtn_trade(const LFRtnTradeField* data, int request_id, short source, long rcv_time);
    virtual void on_rsp_order(const LFInputOrderField* data, int request_id, short source, long rcv_time, short errorId=0, const char* errorMsg=nullptr);

public:
    Strategy(const string& name);
};

Strategy::Strategy(const string& name): IWCStrategy(name)
{
    rid = -1;
}

void Strategy::init()
{
    //data->add_market_data(SOURCE_INDEX);
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
}

void Strategy::on_rsp_position(const PosHandlerPtr posMap, int request_id, short source, long rcv_time)
{
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

void Strategy::on_rtn_trade(const LFRtnTradeField* rtn_trade, int request_id, short source, long rcv_time)
{
    KF_LOG_DEBUG(logger, "[TRADE]" << " (t)" << rtn_trade->InstrumentID << " (p)" << rtn_trade->Price
                                   << " (v)" << rtn_trade->Volume << " POS:" << data->get_pos(source)->to_string());
    traded_volume += rtn_trade->Volume;
    if (rid == request_id)
    {
        trade_completed = true;
        if (rtn_trade->OffsetFlag == LF_CHAR_Open)
        {
            signal.has_open_position = true;
            if (rtn_trade->Direction == LF_CHAR_Buy)
            {
                signal.has_open_long_position = true;
                signal.has_open_short_position = false;
            }
            else if (rtn_trade->Direction == LF_CHAR_Sell)
            {
                signal.has_open_short_position = true;
                signal.has_open_long_position = false;
            }
        }
        else if (rtn_trade->OffsetFlag == LF_CHAR_CloseToday)
        {
            signal.has_open_position = false;
            signal.has_open_short_position = false;
            signal.has_open_long_position = false;
        }
    }
    if (traded_volume >= TRADED_VOLUME_LIMIT)
    {
        KF_LOG_INFO(logger, "[FINISH] traded volume limit: " << TRADED_VOLUME_LIMIT);
        data->stop();
    }
}

void Strategy::on_rsp_order(const LFInputOrderField* order, int request_id, short source, long rcv_time, short errorId, const char* errorMsg)
{
    if (errorId != 0)
        KF_LOG_ERROR(logger, " (err_id)" << errorId << " (err_msg)" << errorMsg << "(order_id)" << request_id << " (source)" << source);
}

int main(int argc, const char* argv[])
{
    Strategy str(string("YOUR_STRATEGY1"));
    str.init();
    str.start();
    str.block();
    return 0;
}
