#include <document.h>
#include <iostream>

using rapidjson::Document;

int main() {
  // 1. Prase get order book, api/v1/depth
{
  const char* response = "{"lastUpdateId":67625893,"bids":[["0.01700000","1.96000000",[]],["0.01697200","9.61000000",[]],["0.01697100","193.73000000",[]],["0.01697000","40.79000000",[]],["0.01696700","2.93000000",[]]],"asks":[["0.01700100","2.00000000",[]],["0.01700400","0.80000000",[]],["0.01700500","0.30000000",[]],["0.01700900","2.61000000",[]],["0.01701100","2.06000000",[]]]}";
  Document d;
  d.Parse(response);
  std::cout << d["asks"] << std::endl;
  std::cout << d["bids"] << std::endl;
}

  return 0;
}
