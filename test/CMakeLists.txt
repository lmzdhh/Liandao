cmake_minimum_required(VERSION 2.8.9)

project (liandao_test)

set (RAPIDJSON_TEST_SRC rapidjson_test.cpp)
add_executable(rapidjson_test ${RAPIDJSON_TEST_SRC})

include_directories(../utils/rapidjson/include/)

