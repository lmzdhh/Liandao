#include "../utils/crypto/openssl_util.h"
#include <iostream>


// example in https://github.com/binance-exchange/binance-official-api-docs/blob/master/rest-api.md


int main() {
    const std::string secret = "NhqPtmdSJYdKjVHjA7PZj4Mge3R5YNiP1e3UZjInClVN65XAbvqqM6A7H5fATj0j";
    const std::string message = "symbol=LTCBTC&side=BUY&type=LIMIT&timeInForce=GTC&quantity=1&price=0.1&recvWindow=5000&timestamp=1499827319559";

    const std::string signature = hmac_sha256(secret.c_str(), message.c_str());
    std::cout << "signature is: " << signature << std::endl;
    return 0;
}
