#include <document.h>
#include <iostream>
#include <ctime>
#include <string>
#include <sstream>
#include <stdio.h>
#include <assert.h>
#include <string>
#include <cpr/cpr.h>

#include "../../utils/crypto/openssl_util.h"
#include "../../longfist/longfist/LFDataStruct.h"
#include "../../longfist/longfist/LFConstants.h"
#include "../../longfist/longfist/LFPrintUtils.h"


using cpr::Get;
using cpr::Url;
using cpr::Parameters;
using cpr::Payload;
using cpr::Post;
using cpr::Delete;

using rapidjson::Document;
using rapidjson::SizeType;
using rapidjson::Value;
using std::string;
using std::to_string;
using std::stod;
using std::stoi;
using utils::crypto::hmac_sha256;

namespace {
  
  std::string GetResponse(const std::string& symbol, int limit) 
  {
    //const auto static url = "https://www.bitmex.com/api/v1/orderBook/L2";
    const auto static url = "https://testnet.bitmex.com/api/v1/orderBook/L2";
    const auto response = Get(Url{url}, Parameters{{"symbol", symbol},
                                                        {"limit",  to_string(limit)}});

    std::cout << " response status: " << response.status_code << std::endl;
    return response.text;
     
  }
  
  std::string GetConnectionResponse() 
  {
    //const auto static url = "https://www.bitmex.com/api/v1";
    const auto static url = "https://testnet.bitmex.com/api/v1";
    const auto response = Get(Url{url});
    return response.text;
  }

  std::string GetTradeResponse(const std::string& symbol, int limit) 
  {
    //const auto static url = "https://www.bitmex.com/api/v1/trade";
    const auto static url = "https://testnet.bitmex.com/api/v1/trade";
    const auto response = Get(Url{url}, Parameters{{"symbol", symbol},
                                                        {"limit",  to_string(limit)}});
    return response.text;
  }

  void ParseResponse(const std::string& response) {
    Document d;
    d.Parse(response.c_str());

    LFMarketDataField* lf_md = new LFMarketDataField;

    const Value& v = d.GetArray();
    assert(v.IsArray());

    for (Value::ConstValueIterator iter=v.Begin(); iter != v.End(); iter++)
    {
        std::cout << (*iter)["side"].GetString() << " ";
        std::cout << (*iter)["size"].GetInt() << " ";
        std::cout << (*iter)["price"].GetDouble() << std::endl;
        //std::cout << (*iter)["id"].GetInt() << std::endl;

    }

    //std::cout << PRINT_MD(lf_md);
  }
} /* TestMarketData */

void TestMarketData(const std::string& symbol, int limit) {
  std::cout << "---------------------------------------------------\n";
  std::cout << "------  Order Book  -------------------------------\n";
  std::cout << "Test get order book, endpoint: api/v1/orderBook/L2, symbol: " << symbol
    << ", limit: " << limit << std::endl;

  const auto response = GetResponse(symbol, limit); 


  std::cout << "raw response: " << response << std::endl;

  ParseResponse(response);
}

void TestInstruments()
{
  std::cout << "---------------------------------------------------\n";
  std::cout << "------  Instruments -------------------------------\n";
  std::cout << "Test get instruments, endpoint: api/v1/instrument  . \n";

  const auto static url = "https://testnet.bitmex.com/api/v1/instrument/indices";
  const auto response = Get(Url{url});

  std::cout << "status: " << response.status_code << std::endl;
  if (response.status_code == 200) {
      Document json;
      json.Parse(response.text.c_str());
      std::cout << json.Size() << std::endl;
  }

  //std::cout << "msg: " << response.text << std::endl;
}

void TestTrade(const std::string& symbol, int limit) {
  std::cout << "---------------------------------------------------\n";
  std::cout << "-------------------  Test Trade  ------------------\n";
  std::cout << "Test trade , endpoint: api/v1/trade, symbol: " << symbol
    << ", limit: " << limit << std::endl;

  const auto response = GetTradeResponse(symbol, limit); 
  Document d;
  d.Parse(response.c_str());
  
  //std::cout << "raw trade response: " << response << std::endl;

  if(d.IsArray())
  {
    for(int i = 0; i < d.Size(); ++i)
    {
    	const auto& ele = d[i];
	if(ele.HasMember("trdMatchID") && ele.HasMember("price") && ele.HasMember("size"))
        {
	    std::cout << "found a trade: ";
	    if(ele["trdMatchID"].IsString())
            {
                std::cout << ele["trdMatchID"].GetString();
            }
 	    
	    if(ele["symbol"].IsString())
            {
                std::cout << " " << ele["symbol"].GetString();
            }
 	    
	    if(ele["price"].IsDouble())
		std::cout << " " << ele["price"].GetDouble();
		
	    if(ele["size"].IsInt())
		std::cout << " " << ele["size"].GetInt(); 

            std::cout << std::endl;

        }
    }
  }
}

void TestConnection()
{
  std::cout << "---------------------------------------------------\n";
  std::cout << "------  Test Connection   -------------------------\n";
  std::cout << "Test Connection endpoint: api/v1 " << std::endl; 

  const auto response = GetConnectionResponse(); 
  std::cout << "raw response: " << response << std::endl;

  //ParseResponse(response);
}

void TestCancelAllOrders(const std::string apiKey, const std::string apiSecret, const std::string expires)
{
  std::cout << "---------------------------------------------------\n";
  std::cout << "------  Test CancelAllOrders   -------------------------\n";
  std::cout << "Test CancelAllOrders endpoint: api/v1/order/all " << std::endl; 

  std::string path = "DELETE/api/v1/order/all";
  std::string alldata = path;
  alldata += expires;

  std::string apiSignature = hmac_sha256(apiSecret.c_str(),alldata.c_str());

  std::cout << "apiSignature = " << apiSignature << std::endl;

  const auto static url = "https://www.bitmex.com/api/v1/order/all";
  const auto response = Delete(cpr::Url{url},
                               cpr::Header{{"Content-Type","application/json"},{"api-key",apiKey},{"api-signature", apiSignature},{"api-expires",expires}});

  std::cout << "raw response: " << response.status_code << std::endl;
  std::cout << "raw response: " << response.text << std::endl;

  //ParseResponse(response);
}

void TestCancelOrderWithId(const std::string apiKey, const std::string apiSecret, const std::string expires, std::string orderId)
{
  std::cout << "---------------------------------------------------\n";
  std::cout << "------  Test CancelOrderWithId   ------------------\n";
  std::cout << "Test CancelAllOrders endpoint: api/v1/order " << std::endl; 

  std::string order_data = "{\"orderID\":";
  order_data += "\"" + orderId + "\"" ;
  order_data += "}";

  std::cout << order_data << std::endl;

  std::string path = "DELETE/api/v1/order";
  std::string alldata = path;
  alldata += expires;
  alldata += order_data;

  std::string apiSignature = hmac_sha256(apiSecret.c_str(),alldata.c_str());

  std::cout << "apiSignature = " << apiSignature << std::endl;

  const auto static url = "https://www.bitmex.com/api/v1/order";
  const auto response = Delete(cpr::Url{url},
                               cpr::Header{{"Content-Type","application/json"},{"api-key",apiKey},{"api-signature", apiSignature},{"api-expires",expires}},
			       cpr::Body{order_data});

  std::cout << "raw response: " << response.status_code << std::endl;
  std::cout << "raw response: " << response.text << std::endl;

  //ParseResponse(response);
}

/*
std::string GetApiKey(std::string id, std::string secret)
{
  std::cout << "---------------------------------------------------\n";
  std::cout << "------  Get Key -------------------------\n";
  std::cout << "Get Api Key endpoint: /apiKey " << std::endl; 

  const auto static url = "https://www.bitmex.com/api/v1/apiKey";
  const auto response = Get(Url{url}, Parameters{{"id",id},{"secret", secret},{"nonce",0},{"enabled",true}});

  std::cout << response.status_code << std::endl;
  std::cout << "raw response: " << response.text << std::endl;
}
*/

namespace {

  std::string GetSide(const LfDirectionType& input) {
    if (LF_CHAR_Buy == input) {
      return "Buy";
    } else if (LF_CHAR_Sell == input) {
      return "Sell";
    } else {
      return "UNKNOWN";
    }
  }

  std::string GetType(const LfOrderPriceTypeType& input) {
    if (LF_CHAR_LimitPrice == input) {
      return "Limit";
    } else if (LF_CHAR_AnyPrice == input) {
      return "Market";
    } else {
      return "UNKNOWN";
    }
  }

  std::string GetTimeInForce(const LfTimeConditionType& input) {
    if (LF_CHAR_IOC == input) {
      return "IOC";
    } else if (LF_CHAR_GTC == input) {
      return "GTC";
    } else if (LF_CHAR_FOK == input) {
      return "FOK";
    } else {
      return "UNKNOWN";
    }
  }

  std::string GetNewOrderData(const LFInputOrderField* order) {
    std::stringstream ss;
    ss << "{";
    ss << "\"symbol\":\"" << order->InstrumentID << "\",";
    ss << "\"side\":\"" << GetSide(order->Direction) << "\",";
    ss << "\"ordType\":\"" << GetType(order->OrderPriceType) << "\",";
    ss << "\"timeInForce\":\"" << GetTimeInForce(order->TimeCondition) << "\",";
    ss << "\"simpleOrderQty\":" << order->Volume << ",";
    ss << "\"price\":" << order->LimitPrice ;
    ss << "}";

    std::cout << "New Order Data: " << ss.str() << std::endl;
    
    return ss.str();
  }

  std::string PostOrder(const LFInputOrderField* sample_new, const std::string apiKey, const std::string order_signature, std::string expires) 
  {
    const auto url = "https://www.bitmex.com/api/v1/order";
    //const auto url = "https://testnet.bitmex.com/api/v1/order";

    std::cout << url << std::endl;

    auto header = cpr::Header{{"Content-type","application/json"},{"api-key",apiKey}, {"api-signature", order_signature}, {"api-expires", expires}};
    //auto header = cpr::Header{{"Content-type","application/plain"}};

    std::cout << header["api-expires"] << std::endl;
    std::cout << header["api-key"] << std::endl;
    std::cout << header["api-signature"] << std::endl;

    std::string order_data = GetNewOrderData(sample_new);
    //std::cout << order_data << std::endl;

    std::cout << "Body: " <<  cpr::Body{order_data} << std::endl;

    auto response = Post(cpr::Url{url}, header, cpr::Body{order_data});
    //auto response = Post(cprurl, header);

    std::cout << response.status_code << std::endl;
    std::cout << response.error.message << std::endl;

    std::cout << "Response: " << response.text << std::endl;
    return response.text;
  }

} /* TestNewOrder */

std::string
generateSignature(const LFInputOrderField* sample_new, const std::string& secret, std::time_t ts) {
  std::cout << "---------------------------------------------------\n";
  std::cout << "---------------  Translate New Order   ------------\n";
  std::cout << "Test translate new order with secret: " << secret << std::endl;
  //std::cout << PRINT_ORDER(order);

  std::string path = "POST/api/v1/order";

  std::cout << ts << " seconds since the Epoch\n";

  std::string order_data = GetNewOrderData(sample_new);

  std::string alldata = path;
  alldata += std::to_string(ts);
  alldata += order_data;
  std::string signature = hmac_sha256(secret.c_str(),alldata.c_str());
  std::string translated = alldata + "&signature=" + signature;

  std::cout << "Translate order data is: " << translated << std::endl;

  return signature;


/*
  return "{\n"
    "  \"symbol\": \"LTCBTC\",\n"
    "  \"orderId\": 28,\n"
    "  \"clientOrderId\": \"6gCrw2kRUAF9CvJDGP16IP\",\n"
    "  \"transactTime\": 1507725176595\n"
    "}";
*/

}

void TranslateOrderAck(const std::string& order_ack,
    LFRtnOrderField* translated_order_ack) {
  Document d;
  d.Parse(order_ack.c_str());

  const std::string symbol = d["symbol"].GetString();
  memcpy(translated_order_ack->InstrumentID, symbol.c_str(), symbol.size());

  const std::string order_id = std::to_string(d["orderId"].GetInt());
  memcpy(translated_order_ack->OrderRef, order_id.c_str(), order_id.size());
}

void TranslateOrderExec(const std::string& order_exec,
    LFRtnOrderField* translated_order_exec) {
  Document d;
  d.Parse(order_exec.c_str());

  const std::string symbol = d["symbol"].GetString();
  memcpy(translated_order_exec->InstrumentID, symbol.c_str(), symbol.size());

  const std::string order_id = std::to_string(d["orderId"].GetInt());
  memcpy(translated_order_exec->OrderRef, order_id.c_str(), order_id.size());

  /* TODO: fill the remaining details. */
}

int main() {
  const std::string symbol = "XRPU18";
  const std::string symbol2 = "XBT";
  const std::string symbol3 = "XRPU18";
  const int limit = 5;
  const int depth = 5;

  const std::string api_key = "fHDwOC4W8PZezvNHkibrdKkY";
  const std::string api_secret = "o7ihEtQth_2kEF_fRDT7UkOdEO3PFESWVarwy6S4Ljirz9Z2";

  std::time_t baseNow = std::time(nullptr);
  struct tm* tm = std::localtime(&baseNow);
  tm->tm_sec += 30;
  std::time_t next = std::mktime(tm);

  /* test connection */
  TestConnection();

  /* test market data */
  TestMarketData(symbol3, depth);
  TestInstruments();

  TestTrade(symbol3, depth);


  /* test cancel all orders */
  TestCancelAllOrders(api_key,api_secret,std::to_string(next));

  /* test cancel one order with orderId */
  std::string orderId = "123456789012345678901234567890123456";
  TestCancelOrderWithId(api_key,api_secret,std::to_string(next), orderId);

  
  /* test sample new order */


  /* test */
  //const std::string api_key = "dj6COXW4o1xFIKlVkmbXeF1n";
  //const std::string api_secret = "VDIwt_CYzpAqG6hdrCvU8jqbORK3EVSg0Ccn-VcOCEp5eFk-";

  /* get ApiKey */
  //std::string apikey = GetApiKey(api_key, api_secret);

  //std::cout << "Api Key:" << apikey << std::endl;

  LFInputOrderField* sample_new = new LFInputOrderField();
  memcpy(sample_new->InstrumentID, symbol.c_str(), symbol.size());
  sample_new->Direction = LF_CHAR_Buy;
  sample_new->OrderPriceType = LF_CHAR_LimitPrice;
  sample_new->TimeCondition = LF_CHAR_GTC;
  sample_new->Volume = 1;
  sample_new->LimitPrice = 1;

  std::cout << next << " seconds since the Epoch\n";
  const std::string order_signature = generateSignature(sample_new,api_secret, next);

  std::cout << "order signature = " << order_signature << std::endl;

  /* send order out via rest api */
  std::cout << "api key = " << api_key << std::endl;
  std::string r = PostOrder(sample_new, api_key, order_signature, std::to_string(next));

  std::cout << r << std::endl;

  /* translate order ack */
  LFRtnOrderField* translated_order_ack = new LFRtnOrderField();
  //TranslateOrderAck(order_ack, translated_order_ack);

  /* translate order exec */
  const std::string order_exec = "{\n"
    "  \"symbol\": \"BTCUSDT\",\n"
    "  \"orderId\": 28,\n"
    "  \"clientOrderId\": \"6gCrw2kRUAF9CvJDGP16IP\",\n"
    "  \"transactTime\": 1507725176595,\n"
    "  \"price\": \"0.00000000\",\n"
    "  \"origQty\": \"10.00000000\",\n"
    "  \"executedQty\": \"10.00000000\",\n"
    "  \"status\": \"FILLED\",\n"
    "  \"timeInForce\": \"GTC\",\n"
    "  \"type\": \"MARKET\",\n"
    "  \"side\": \"SELL\",\n"
    "  \"fills\": [\n"
    "    {\n"
    "      \"price\": \"4000.00000000\",\n"
    "      \"qty\": \"1.00000000\",\n"
    "      \"commission\": \"4.00000000\",\n"
    "      \"commissionAsset\": \"USDT\"\n"
    "    },\n"
    "    {\n"
    "      \"price\": \"3999.00000000\",\n"
    "      \"qty\": \"5.00000000\",\n"
    "      \"commission\": \"19.99500000\",\n"
    "      \"commissionAsset\": \"USDT\"\n"
    "    },\n"
    "    {\n"
    "      \"price\": \"3998.00000000\",\n"
    "      \"qty\": \"2.00000000\",\n"
    "      \"commission\": \"7.99600000\",\n"
    "      \"commissionAsset\": \"USDT\"\n"
    "    },\n"
    "    {\n"
    "      \"price\": \"3997.00000000\",\n"
    "      \"qty\": \"1.00000000\",\n"
    "      \"commission\": \"3.99700000\",\n"
    "      \"commissionAsset\": \"USDT\"\n"
    "    },\n"
    "    {\n"
    "      \"price\": \"3995.00000000\",\n"
    "      \"qty\": \"1.00000000\",\n"
    "      \"commission\": \"3.99500000\",\n"
    "      \"commissionAsset\": \"USDT\"\n"
    "    }\n"
    "  ]\n"
    "}";

  LFRtnOrderField* translated_order_exec = new LFRtnOrderField();
  TranslateOrderExec(order_exec, translated_order_exec);

  return 0;
}


