#include <document.h>
#include <iostream>
#include <string>
#include <sstream>
#include <stdio.h>
#include <assert.h>
#include <string>

#include "../../longfist/longfist/LFDataStruct.h"
#include "../../longfist/longfist/LFConstants.h"
#include "../../utils/cpr/cpr.h"
#include "binance_order_translation_utils.h"

using rapidjson::Document;
using rapidjson::SizeType;
using rapidjson::Value;
using utils::crypto::hmac_sha256;
using cpr::Get;
using cpr::Url;
using cpr::Parameters;
using std::string;
using std::to_string;

std::string TranslateNewOrder(const LFInputOrderField& order, const std::string& secret) {  
  std::stringstream ss;
  ss << "symbol=" << order.InstrumentID << "&";
  ss << "side=" << GetSide(order.Direction) << "&";
  ss << "type=" << GetType(order.OrderPriceType) << "&";
  ss << "timeInForce=" << GetTimeInForce(order.TimeCondition) << "&";
  ss << "quantity=" << order.Volume << "&";
  ss << "price=" << order.LimitPrice << "&";
  ss << "recvWindow=5000&timestamp=1499827319559";

  std::string data = ss.str();
  std::string signature = hmac_sha256(secret.c_str(), data.c_str());

  ss << "&" << "signature=" << signature;

  return ss.str();
}

int main() {
  static const char* kTypeNames[] = { "Null", "False", "True", "Object", "Array", "String", "Number" };

  {
    std::cout << "---------------------------------------------------" << std::endl;
    std::cout << "Test get order book, endpoint: api/v1/depth" << std::endl;

    const char* response = "{\"lastUpdateId\":67625893,\"bids\":[[\"0.01700000\",\"1.96000000\",[]],[\"0.01697200\",\"9.61000000\",[]],[\"0.01697100\",\"193.73000000\",[]],[\"0.01697000\",\"40.79000000\",[]],[\"0.01696700\",\"2.93000000\",[]]],\"asks\":[[\"0.01700100\",\"2.00000000\",[]],[\"0.01700400\",\"0.80000000\",[]],[\"0.01700500\",\"0.30000000\",[]],[\"0.01700900\",\"2.61000000\",[]],[\"0.01701100\",\"2.06000000\",[]]]}";
    Document d;
    d.Parse(response);

    std::cout << "get order book, api/v1/depth" << std::endl;
    std::cout << "asks: " << std::endl;
    for (const auto& v : d["asks"].GetArray()) {
      std::cout << "\tprice: " << v[0].GetString() << ", qty: " << v[1].GetString() << std::endl;
    }
    std::cout << "bids: " << std::endl;
    for (const auto& v : d["bids"].GetArray()) {
      std::cout << "\tprice: " << v[0].GetString() << ", qty: " << v[1].GetString() << std::endl;
    }
  }

  {
    std::cout << "---------------------------------------------------" << std::endl;
    std::cout << "Test get recent trade, endpoint: api/v1/trades" << std::endl;

    const char* response = "[{\"id\":10722449,\"price\":\"0.01700000\",\"qty\":\"2.01000000\",\"time\":1524014034090,\"isBuyerMaker\":false,\"isBestMatch\":true},{\"id\":10722450,\"price\":\"0.01700000\",\"qty\":\"1.00000000\",\"time\":1524014034090,\"isBuyerMaker\":false,\"isBestMatch\":true},{\"id\":10722451,\"price\":\"0.01700000\",\"qty\":\"0.38000000\",\"time\":1524014034263,\"isBuyerMaker\":true,\"isBestMatch\":true},{\"id\":10722452,\"price\":\"0.01700000\",\"qty\":\"1.00000000\",\"time\":1524014038502,\"isBuyerMaker\":true,\"isBestMatch\":true},{\"id\":10722453,\"price\":\"0.01700000\",\"qty\":\"0.60000000\",\"time\":1524014039785,\"isBuyerMaker\":true,\"isBestMatch\":true}]";
    Document d;
    d.Parse(response);

    std::cout << std::endl << "get recent trades, api/v1/trades" << std::endl;
    for (const auto& v : d.GetArray()) {
      std::cout << "\tid: " << v["id"].GetInt() << ", price: " << v["price"].GetString() << ", qty: " << v["qty"].GetString() << std::endl;
    }
  }

  {
    std::cout << "---------------------------------------------------" << std::endl;
    std::cout << "Test new order translation from binance rest api example" << std::endl;

    std::string symbol = "LTCBTC";
    LFInputOrderField new_order;
    memcpy(new_order.InstrumentID, symbol.c_str(), symbol.size());
    new_order.Direction = LF_CHAR_Buy;
    new_order.OrderPriceType = LF_CHAR_LimitPrice;
    new_order.TimeCondition = LF_CHAR_GTC;
    new_order.Volume = 1;
    new_order.LimitPrice = 0.1;

    std::string secret = "NhqPtmdSJYdKjVHjA7PZj4Mge3R5YNiP1e3UZjInClVN65XAbvqqM6A7H5fATj0j";
    std::string message = TranslateNewOrder(new_order, secret);

    std::cout << "message: " << signature << std::endl;
  }

  return 0;
}
