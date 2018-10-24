#include <string>
#include <iostream>
#include <cstring>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <sstream>
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

inline unsigned char* hmac_sha256_byte( const char *key, const char *data) {
    return HMAC(EVP_sha256(), key, strlen(key), (unsigned char*)(data), strlen(data), NULL, NULL);
}

inline std::string hmac_sha384( const char *key, const char *data) {
    unsigned char* digest;
    digest = HMAC(EVP_sha384(), key, strlen(key), (unsigned char*)(data), strlen(data), NULL, NULL);
    return b2a_hex((char *)digest, 48);
}

inline unsigned char* hmac_sha384_byte( const char *key, const char *data) {
    return HMAC(EVP_sha384(), key, strlen(key), (unsigned char*)(data), strlen(data), NULL, NULL);
}

inline std::string hmac_sha512( const char *key, const char *data) {
    unsigned char* digest;
    digest = HMAC(EVP_sha256(), key, strlen(key), (unsigned char*)(data), strlen(data), NULL, NULL);
    return b2a_hex((char *)digest, 64);
}

static const std::string base64_chars ="ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                       "abcdefghijklmnopqrstuvwxyz"
                                       "0123456789-_";

inline bool is_base64(unsigned char c) {
    return (isalnum(c) || (c == '-') || (c == '_'));
}

std::string base64_encode(const unsigned char* bytes_to_encode, unsigned long in_len) {
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for(i = 0; (i <4) ; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i)
    {
        for(j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = ( char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (j = 0; (j < i + 1); j++)
            ret += base64_chars[char_array_4[j]];

        //while((i++ < 3))
            //ret += '=';

    }

    return ret;

}

std::string base64_decode(std::string const& encoded_string) {
    int in_len = encoded_string.size();
    int i = 0;
    int j = 0;
    int in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];
    std::string ret;

    while (in_len-- && ( encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
        char_array_4[i++] = encoded_string[in_]; in_++;
        if (i ==4) {
            for (i = 0; i <4; i++)
                char_array_4[i] = base64_chars.find(char_array_4[i]);

            char_array_3[0] = ( char_array_4[0] << 2       ) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) +   char_array_4[3];

            for (i = 0; (i < 3); i++)
                ret += char_array_3[i];
            i = 0;
        }
    }

    if (i) {
        for (j = 0; j < i; j++)
            char_array_4[j] = base64_chars.find(char_array_4[j]);

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);

        for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
    }

    return ret;
}



inline std::string rsa256_private_sign(const std::string &data_to_sign, const std::string &priKey) {
    std::string strRet;
    unsigned int len=0;
    BIO *keybio = BIO_new(BIO_s_mem());

    size_t  ret = BIO_write(keybio,priKey.data(),priKey.size());
    if(ret != priKey.size())
    {
        return "";
    }
    std::string password ="";
    RSA *rsa = PEM_read_bio_RSAPrivateKey(keybio, nullptr , nullptr, (void*)password.c_str());
    if(rsa == nullptr)
    {
        return  "";
    }
    EVP_PKEY* key = EVP_PKEY_new();

    EVP_PKEY_assign_RSA(key,rsa);
    strRet.resize(EVP_PKEY_size(key));

    EVP_MD_CTX* ctx = EVP_MD_CTX_create();
    EVP_SignInit(ctx,EVP_sha256());
    EVP_SignUpdate(ctx,data_to_sign.c_str(),data_to_sign.length());
    EVP_SignFinal(ctx,(unsigned char*)strRet.data(),&len,key);
    strRet.resize(len);

    EVP_MD_CTX_destroy(ctx);
    //RSA_free(rsa);
    EVP_PKEY_free(key);
    BIO_free_all(keybio);

    return strRet;
}



inline std::string rsa256_pub_verify(const std::string &data_to_sign,const std::string &cipherText, const std::string &pubKey)
{
    std::string strRet;
    BIO *keybio = BIO_new(BIO_s_mem());

    size_t  ret = BIO_write(keybio,pubKey.data(),pubKey.size());
    if(ret != pubKey.size())
    {
        strRet = "failed to load public key: bio_write failed";
        return strRet;
    }
    std::string password ="";
    RSA *rsa = PEM_read_bio_RSAPublicKey(keybio, nullptr , nullptr, (void*)password.c_str());
    if(rsa == nullptr)
    {
        strRet = "failed to load public key: PEM_read_bio_PUBKEY failed";
        return strRet;
    }
    EVP_PKEY* key = EVP_PKEY_new();

    EVP_PKEY_assign_RSA(key,rsa);
    EVP_MD_CTX* ctx =EVP_MD_CTX_create();

    if (!ctx)
    {
        strRet = "failed to verify signature: could not create context";
        return strRet;
    }
    if (!EVP_VerifyInit(ctx, EVP_sha256()))
    {
        strRet = "failed to verify signature: VerifyInit failed";
        return strRet;
    }
    if (!EVP_VerifyUpdate(ctx, data_to_sign.data(), data_to_sign.size()))
    {
        strRet = "failed to verify signature: VerifyUpdate failed";
        return strRet;
    }
    if (!EVP_VerifyFinal(ctx, (const unsigned char*)cipherText.data(), cipherText.size(), key))
    {
        strRet = "failed to load public key: PEM_read_bio_PUBKEY failed";
        return strRet;
    }


    EVP_MD_CTX_destroy(ctx);
    //RSA_free(rsa);
    EVP_PKEY_free(key);
    BIO_free_all(keybio);

    return strRet;
}

inline std::string jwt_create(const std::string& data,const std::string& private_key)
{
    //JWT:
    //1. secret =  RSASHA256(base64UrlEncode(header) + "." + base64UrlEncode(payload),private_key)
    //2. jwt = base64UrlEncode(header) + "." + base64UrlEncode(payload) +base64UrlEncode(secret)
    std::string header =R"({"typ":"JWT","alg":"RS256"})";
    std::string payload = data;

    std::string encoded_header = base64_encode((const unsigned char*)header.c_str(),header.length());
    std::string encoded_payload=base64_encode((const unsigned char*)payload.c_str(),payload.length());
    std::string data_to_sign = encoded_header +"."+encoded_payload;
    std::string signature = rsa256_private_sign(data_to_sign, private_key);
    std::string secret = base64_encode((const unsigned char*)signature.c_str(),signature.length());

    std::string jwt = data_to_sign+"."+secret;
    return  jwt;
}

}}
