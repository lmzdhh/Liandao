#ifndef WINGCHUN_MDENGINEOceanEx_H
#define WINGCHUN_MDENGINEOceanEx_H

#include "IMDEngine.h"
#include "longfist/LFConstants.h"
#include <libwebsockets.h>
#include <document.h>
#include <map>
#include <vector>

WC_NAMESPACE_START

using rapidjson::Document;

struct PriceAndVolume
{
    int64_t price;
    uint64_t volume;
    bool operator < (const PriceAndVolume &other) const
    {
        if (price<other.price)
        {
            return true;
        }
        return false;
    }
};

std::map<std::string,int> mapPrecision{
    {"btcusdt",2},
    {"ethusdt",2},
    {"ethbtc",6},
    {"shavet",4},
    {"vetusdt",5},
    {"vetbtc",8},
    {"veteth",8},
    {"vthovet",4}
};
//coinmex use base and quote to sub depth data
struct SubscribeCoinBaseQuote
{
    std::string base;
    std::string quote;
};

static int sort_price_asc(const PriceAndVolume &p1,const PriceAndVolume &p2)
{
    return p1.price < p2.price;
};

static int sort_price_desc(const PriceAndVolume &p1,const PriceAndVolume &p2)
{
    return p1.price > p2.price;
};

template<typename T>
static void sortMapByKey(std::map<int64_t, uint64_t> &t_map, std::vector<PriceAndVolume> &t_vec, T& sort_by)
{
    for(std::map<int64_t, uint64_t>::iterator iter = t_map.begin();iter != t_map.end(); iter ++)
    {
        PriceAndVolume pv;
        pv.price = iter->first;
        pv.volume = iter->second;
        t_vec.push_back(pv);
    }
    sort(t_vec.begin(), t_vec.end(), sort_by);
};

class MDEngineOceanEx: public IMDEngine
{
public:
    /** load internal information from config json */
    virtual void load(const json& j_config);
    virtual void connect(long timeout_nsec);
    virtual void login(long timeout_nsec);
    virtual void logout();
    virtual void release_api();
    virtual void subscribeMarketData(const vector<string>& instruments, const vector<string>& markets);
    virtual bool is_connected() const { return connected; };
    virtual bool is_logged_in() const { return logged_in; };
    virtual string name() const { return "MDEngineOceanEx"; };

public:
    MDEngineOceanEx();

    void on_lws_data(struct lws* conn, const char* data, size_t len);
    void on_lws_connection_error(struct lws* conn);
    int lws_write_subscribe(struct lws* conn);
    void writeErrorLog(std::string strError);
private:
    void onDepth(Document& json);
    void onTickers(Document& json);
    void onFills(Document& json);

    std::string parseJsonToString(const char* in);
    std::string createDepthJsonString(std::string base, std::string quote);
    std::string createTickersJsonString();
    std::string createFillsJsonString(std::string base, std::string quote);
    void clearPriceBook();
    void loop();
    std::string dealDataSprit(const char* src);

    virtual void set_reader_thread() override;
private:
    ThreadPtr rest_thread;
    bool connected = false;
    bool logged_in = false;

    int rest_get_interval_ms = 500;
    int book_depth_count = 5;
    static constexpr int scale_offset = 1e8;

    struct lws_context *context = nullptr;

    int subscribe_index = 0;

    //<ticker, <price, volume>>
    std::map<std::string, std::map<int64_t, uint64_t>*> tickerAskPriceMap;
    std::map<std::string, std::map<int64_t, uint64_t>*> tickerBidPriceMap;

private:
    void readWhiteLists(const json& j_config);
    std::string getWhiteListCoinpairFrom(std::string md_coinpair);
    bool shouldUpdateData(const LFPriceBook20Field& md);
    void split(std::string str, std::string token, SubscribeCoinBaseQuote& sub);
    std::string getLiandaoCoin(const std::string& strExchangeCoin);
    void debug_print(std::vector<SubscribeCoinBaseQuote> &sub);
    void debug_print(std::map<std::string, std::string> &keyIsStrategyCoinpairWhiteList);
    void debug_print(std::vector<std::string> &subJsonString);
    //coinmex use base and quote to sub depth data, so make this vector for it
    std::vector<SubscribeCoinBaseQuote> subscribeCoinBaseQuote;

    std::vector<std::string> websocketSubscribeJsonString;
    std::map<std::string,LFPriceBook20Field> mapLastData;
    //in MD, lookup direction is:
    // incoming exchange coinpair ---> our strategy recognized coinpair
    //if coming data 's coinpair is not in this map ,ignore it
    //"strategy_coinpair(base_quote)":"exchange_coinpair",
    std::map<std::string, std::string> keyIsStrategyCoinpairWhiteList;
};

DECLARE_PTR(MDEngineOceanEx);

WC_NAMESPACE_END

#endif
