find_package(crypto)

set(CORELIBS crypto)

add_library(openssl_util_lib openssl_util.cpp)

add_executable(openssl_util_test openssl_util_test.cpp)
target_link_libraries(openssl_util_test openssl_util_lib ${CORELIBS})
