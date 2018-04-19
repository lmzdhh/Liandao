cmake_minimum_required(VERSION 2.8.9)
set (CMAKE_CXX_STANDARD 11)

project (liandao_test)

set (RAPIDJSON_TEST_SRC rapidjson_test.cpp)
add_executable(rapidjson_test ${RAPIDJSON_TEST_SRC})

set (BINANCE_TEST_HEADER LFConstants.h LFDataStruct.h binance_order_translation_utils.h)
add_executable(binance_rapidjson_test binance_rapidjson_test.cpp ${BINANCE_TEST_HEADER})
add_library(openssl_util ../utils/crypto/openssl_util.cpp)
target_link_libraries(binance_rapidjson_test ${openssl_util} crypto)

include_directories(../utils/crypto/)
include_directories(../utils/rapidjson/include/)


