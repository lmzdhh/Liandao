cmake_minimum_required(VERSION 2.8.9)
set (CMAKE_CXX_STANDARD 11)

project (liandao_test)

include_directories(../utils/rapidjson/include/)
add_executable(rapidjson_test rapidjson_test/rapidjson_test.cpp)

find_package(crypto)
set(CORELIBS crypto)

add_executable(openssl_util_test crypto_test/openssl_util_test.cpp)
target_link_libraries(openssl_util_test ${CORELIBS})

add_executable(binance_rapidjson_test binance_rapidjson_test/binance_rapidjson_test.cpp)
target_link_libraries(binance_rapidjson_test ${CORELIBS})

include_directories(${CPR_INCLUDE_DIRS})
add_executable(binance_test binance_test/binance_test.cpp)
target_link_libraries(binance_test ${CORELIBS})
target_link_libraries(binance_test ${CPR_LIBRARIES})
