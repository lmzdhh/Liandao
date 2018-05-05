#include <string>
#include <openssl/hmac.h>
#include <openssl/sha.h>

std::string b2a_hex( char *byte_arr, int n );
std::string hmac_sha256( const char *key, const char *data);
std::string hmac_sha512( const char *key, const char *data);

