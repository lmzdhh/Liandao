#ifndef WINGCHUN_MDENGINEBITMEX_H
#define WINGCHUN_MDENGINEBITMEX_H

#include "IMDEngine.h"
#include "longfist/LFConstants.h"
#include "CoinPairWhiteList.h"
#include "PriceBook20Assembler.h"

#include <document.h>
#include <libwebsockets.h>

#include <unordered_map>
#include <vector>
#include <set>

using rapidjson::Document;

WC_NAMESPACE_START

class MDEngineBitmex : public IMDEngine {
public:
    MDEngineBitmex();

    // load json config file, which includes white list
    virtual void load(const json& config);
    virtual void connect(long timeout_nsec);
    // take care of initialization and creating websockets
    virtual void login(long timeout_nsec);
    virtual void logout();
    virtual void release_api();
    virtual void subscribeMarketData(const vector<string>& instruments, const vector<string>& markets);
    virtual bool is_connected() const { return connected; };
    virtual bool is_logged_in() const { return logged_in; };
    virtual std::string name() const { return "MDEngineBitmex"; };

    // subscribe to a channel
    void subscribeChannel(struct lws* conn);
    // main callback function
    void processData(struct lws* conn, const char* data, size_t len);
    // reconnect when previous websocket connection was unable to complete a handshake with server
    void handleConnectionError(struct lws* conn);
  
private:
    ThreadPtr read_thread;
    struct lws_context* context = nullptr;

    bool connected = false;
    bool logged_in = false;
    // upon subscription an image of the existing data will be received through as a
    // partial action. other messages may be received before the partial comes through
    // in that case drop any messages received until a partial has been received
    std::set<std::string> received_partial;

    static constexpr int scale_offset = 1e8;
    int rest_get_interval_ms = 500;

    // read in whitelist from config
    CoinPairWhiteList whiteList;

    // each string is a serialized json used to subscribe to a channel
    std::vector<std::string> subscribeJsonStrings;
    // number of channels have been subscribed to
    int num_subscribed = 0;
  
    PriceBook20Assembler priceBook;
  
    // the id on an orderbook entry is a composite of price and symbol, and is
    // always unique for any given price level. for update and delete actions
    // received message has id but not price. so id/price pair is saved for lookup
    std::unordered_map<uint64_t, int64_t> id_to_price;

    virtual void set_reader_thread() override;

    // serialize json to string to append to websocket request
    void createSubscribeJsonStrings();
    std::string createOrderbookJsonString(std::string symbol);
    std::string createTradeJsonString(std::string symbol);
    void debugPrint(std::vector<std::string> &jsons);
  
    // enter event loop and close any active websocket connections after done
    void enterEventLoop();
    
    void processOrderbookData(Document& json);
    void processTradingData(Document& json);
};

DECLARE_PTR(MDEngineBitmex);

WC_NAMESPACE_END

#endif
