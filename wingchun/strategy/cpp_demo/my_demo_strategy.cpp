/**
 * Strategy demo, same logic as band_demo_strategy.py
 * @Author cjiang (changhao.jiang@taurus.ai)
 * @since   Nov, 2017
 */

#include "IWCStrategy.h"
#include <deque>

USING_WC_NAMESPACE

#define SOURCE_INDEX SOURCE_CTP
#define M_TICKER "rb1801"
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
    virtual void on_market_data(const LFMarketDataField* data, short source, long rcv_time);
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

void Strategy::on_market_data(const LFMarketDataField* md, short source, long rcv_time)
{
    if (strcmp(M_TICKER, md->InstrumentID) == 0 && td_connected)
    {
        signal.TickPrice.push_back(md->LastPrice);
        if (signal.TickPrice.size() > signal.look_back)
            signal.TickPrice.pop_front();
        md_num += 1;
        if (md_num < signal.look_back + 2)
            return;
        // ============ prepare data ============
        double rolling_min = 9999999;
        double rolling_max = 0;
        
        for (int i = 0; i < signal.param1; i++)
        {
            int idx = signal.look_back - 1 - signal.param2 - i; // delay
            double curPrice = signal.TickPrice[idx];
            rolling_max = (curPrice > rolling_max) ? curPrice: rolling_max;
            rolling_min = (curPrice < rolling_min) ? curPrice: rolling_min;
        }
        bool long_entry_condition = rolling_max <= md->LastPrice;
        bool short_entry_condition = rolling_min >= md->LastPrice;
        bool exit_condition = rolling_max > md->LastPrice && rolling_min < md->LastPrice;
        rid = util->insert_limit_order(SOURCE_INDEX, M_TICKER, M_EXCHANGE,
                                               curPrice*0.98, signal.trade_size,
                                               LF_CHAR_Buy, LF_CHAR_Open);
        sleep(0.1)

        util->cancel_order(SOURCE_INDEX,rid)



    }
}


int main(int argc, const char* argv[])
{
    Strategy str(string("cpp_test"));
    str.init();
    str.start();
    str.block();
    return 0;
}