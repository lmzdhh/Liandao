#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <string.h>
#include <sys/time.h>
#include <sstream>
#include <vector>
#include <iostream>
#include <string>

// using example in https://github.com/binance-exchange/binance-official-api-docs/blob/master/rest-api.md

std::string b2a_hex(char *byte_arr, int n) {
    const static std::string HexCodes = "0123456789abcdef";
    std::string HexString;
    for(int i = 0; i < n; ++i) {
        unsigned char BinValue = byte_arr[i];
        HexString += HexCodes[(BinValue >> 4) & 0x0F];
        HexString += HexCodes[BinValue & 0x0F];
    }
    return HexString;
}

int main() {
    char secret[] = "NhqPtmdSJYdKjVHjA7PZj4Mge3R5YNiP1e3UZjInClVN65XAbvqqM6A7H5fATj0j";
    char message[] = "symbol=LTCBTC&side=BUY&type=LIMIT&timeInForce=GTC&quantity=1&price=0.1&recvWindow=5000&timestamp=1499827319559";

    unsigned char* digest;
    digest = HMAC(EVP_sha256(), secret, strlen(secret), (unsigned char*)(message), strlen(message), NULL, NULL);
    std::string signature = b2a_hex((char *)digest, 32);
    std::cout << "signature is " << signature << std::endl;

    return 0;
}
