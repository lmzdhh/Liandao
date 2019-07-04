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
#include <iomanip>
#include <vector>

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
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len =EVP_MAX_MD_SIZE;
    HMAC(EVP_sha256(), key, strlen(key), (unsigned char*)(data), strlen(data), digest, &len);
    return b2a_hex((char *)digest, 32);
}

inline unsigned char* hmac_sha256_byte( const char *key, const char *data, unsigned char* md = NULL,unsigned int len = 0) {
    return HMAC(EVP_sha256(), key, strlen(key), (unsigned char*)(data), strlen(data), md, &len);
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
    digest = HMAC(EVP_sha512(), key, strlen(key), (unsigned char*)(data), strlen(data), NULL, NULL);
    return b2a_hex((char *)digest, 64);
}

static const std::string base64_chars ="ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                       "abcdefghijklmnopqrstuvwxyz"
                                       "0123456789+/";

inline bool is_base64(unsigned char c) {
    return (isalnum(c) || (c == '+') || (c == '/'));
}

static const std::string base64_url_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
											"abcdefghijklmnopqrstuvwxyz"
											"0123456789-_";

inline bool is_base64_url(unsigned char c) {
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

        while((i++ < 3))
            ret += '=';

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

std::string base64_url_encode(const unsigned char* bytes_to_encode, unsigned long in_len) {
	std::string ret;
	int i = 0;
	int j = 0;
    int len=0;
	unsigned char char_array_3[3];
	unsigned char char_array_4[4];

	while (1) {

		char_array_3[i] = *(bytes_to_encode++);
        i++;
		if (i == 3) {
			char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
			char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
			char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
			char_array_4[3] = char_array_3[2] & 0x3f;

			for (i = 0; (i <4); i++)
				ret += base64_url_chars[char_array_4[i]];
			i = 0;
		}
        len++;
        if(len==in_len) break;
	}

	if (i)
	{
		for (j = i; j < 3; j++)
			char_array_3[j] = '\0';

		char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
		char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
		char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3]=char_array_3[2] & 0x3f;

		for (j = 0; (j < i + 1); j++)
			ret += base64_url_chars[char_array_4[j]];


	}

	return ret;

}

std::string base64_url_decode(std::string const& encoded_string) {
	int in_len = encoded_string.size();
	int i = 0;
	int j = 0;
	int in_ = 0;
	unsigned char char_array_4[4], char_array_3[3];
	std::string ret;

	while (in_len-- && (encoded_string[in_] != '=') && is_base64_url(encoded_string[in_])) {
		char_array_4[i++] = encoded_string[in_]; in_++;
		if (i == 4) {
			for (i = 0; i <4; i++)
				char_array_4[i] = base64_url_chars.find(char_array_4[i]);

			char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
			char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
			char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

			for (i = 0; (i < 3); i++)
				ret += char_array_3[i];
			i = 0;
		}
	}

	if (i) {
		for (j = 0; j < i; j++)
			char_array_4[j] = base64_url_chars.find(char_array_4[j]);

		char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
		char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);

		for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
	}

	return ret;
}


//base64_url from https://gist.github.com/darelf/0f96e1d313e1d0da5051e1a6eff8d329
const char base64_url_alphabet[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '-', '_'
};
std::string base64_url_encode_github(const std::string & in) {
    std::string out;
    int val =0, valb=-6;
    size_t len = in.length();
    unsigned int i = 0;
    for (i = 0; i < len; i++) {
        unsigned char c = in[i];
        val = (val<<8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(base64_url_alphabet[(val>>valb)&0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        out.push_back(base64_url_alphabet[((val<<8)>>(valb+8))&0x3F]);
    }
    return out;
}

std::string base64_url_decode_github(const std::string & in) {
    std::string out;
    std::vector<int> T(256, -1);
    unsigned int i;
    for (i =0; i < 64; i++) T[base64_url_alphabet[i]] = i;

    int val = 0, valb = -8;
    for (i = 0; i < in.length(); i++) {
        unsigned char c = in[i];
        if (T[c] == -1) break;
        val = (val<<6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val>>valb)&0xFF));
            valb -= 8;
        }
    }
    return out;
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
    std::string header =R"({"alg":"RS256","typ":"JWT"})";
    std::string payload = data;

    std::string encoded_header = base64_url_encode((const unsigned char*)header.c_str(),header.length());
    std::string encoded_payload=base64_url_encode((const unsigned char*)payload.c_str(),payload.length());
    std::string data_to_sign = encoded_header +"."+encoded_payload;
    std::string signature = rsa256_private_sign(data_to_sign, private_key);
    std::string secret = base64_url_encode((const unsigned char*)signature.c_str(),signature.length());

    std::string jwt = data_to_sign+"."+secret;
    return  jwt;
}

inline std::string jwt_hs256_create(const std::string& data,const std::string& private_key)
{
    //JWT:
    //1. secret =  RSASHA256(base64UrlEncode(header) + "." + base64UrlEncode(payload),private_key)
    //2. jwt = base64UrlEncode(header) + "." + base64UrlEncode(payload) +base64UrlEncode(secret)
    std::string header =R"({"alg":"HS256","typ":"JWT"})";
    std::string payload = data;

    std::string encoded_header = base64_url_encode_github(header);
    std::string encoded_payload=base64_url_encode_github(payload);

    std::string data_to_sign = encoded_header +"."+encoded_payload;
    auto signature = hmac_sha256_byte(private_key.c_str(),data_to_sign.c_str());
    //std::string secret = base64_url_encode(signature,strlen((char*)signature));
    std::string secret = base64_url_encode(signature,32);//strlen不能用来求unsigned char数组的长度，此处填32是由于hmac_sha256的输出长度固定为32个unsigned char

    std::string jwt = data_to_sign+"."+secret;
    return  jwt;
}

inline std::string jwt_hash_sha512(const std::string& str)//quest5v6
{
    unsigned char hash[SHA512_DIGEST_LENGTH];
    SHA512_CTX sha512;
    SHA512_Init(&sha512);
    SHA512_Update(&sha512, str.c_str(), str.size());
    SHA512_Final(hash, &sha512);
    std::string NewString = "";
    char buf[2];
    for(int i = 0; i < SHA512_DIGEST_LENGTH; i++)
    {
        sprintf(buf,"%02x",hash[i]);
        NewString = NewString + buf;
    }
    return NewString;
}

}}
