#include <string>
#include <cstring>
#include <openssl/hmac.h>
#include <openssl/sha.h>

namespace utils { namespace crypto {

inline std::string b2a_hex(char *byte_arr, int n) {
    const static std::string HexCodes = "0123456789abcdef";
    std::string HexString;
    for(int i = 0; i < n; ++i) {
        unsigned char BinValue = byte_arr[i];
        HexString += HexCodes[(BinValue >> 4) & 0x0F];
        HexString += HexCodes[BinValue & 0x0F];
    }
    return HexString;
}

inline std::string hmac_sha256( const char *key, const char *data) {
    unsigned char* digest;
    digest = HMAC(EVP_sha256(), key, strlen(key), (unsigned char*)(data), strlen(data), NULL, NULL);
    return b2a_hex((char *)digest, 32);
}

inline std::string hmac_sha512( const char *key, const char *data) {
    unsigned char* digest;
    digest = HMAC(EVP_sha256(), key, strlen(key), (unsigned char*)(data), strlen(data), NULL, NULL);
    return b2a_hex((char *)digest, 64);
}


}}
