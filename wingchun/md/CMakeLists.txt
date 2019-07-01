# Copyright [2017] [taurus.ai]
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

CMAKE_MINIMUM_REQUIRED(VERSION 3.7)
PROJECT(wingchunmd)

#SET(CTP_SOURCE_FILES MDEngineCTP.cpp MDEngineCTP.h IMDEngine.cpp IMDEngine.h ../base/IEngine.cpp ../base/IEngine.h)
#ADD_LIBRARY(ctpmd SHARED ${CTP_SOURCE_FILES})
#TARGET_LINK_LIBRARIES(ctpmd journal kflog thostmduserapi ${Boost_LIBRARIES})
#INSTALL(TARGETS ctpmd DESTINATION "lib/wingchun")

#SET(XTP_SOURCE_FILES MDEngineXTP.cpp MDEngineXTP.h IMDEngine.cpp IMDEngine.h ../base/IEngine.cpp ../base/IEngine.h)
#ADD_LIBRARY(xtpmd SHARED ${XTP_SOURCE_FILES})
#TARGET_LINK_LIBRARIES(xtpmd journal kflog xtpquoteapi ${Boost_LIBRARIES})
#INSTALL(TARGETS xtpmd DESTINATION "lib/wingchun")

INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/utils/cpr/include ${CMAKE_SOURCE_DIR}/utils/rapidjson/include/ ${CMAKE_SOURCE_DIR}/utils/libwebsockets/include)
SET(COMMON_LIBRARIES journal kflog monitorapi)
SET(BINANCE_SOURCE_FILES MDEngineBinance.cpp MDEngineBinance.h IMDEngine.cpp IMDEngine.h ../base/IEngine.cpp ../base/IEngine.h ../base/CoinPairWhiteList.cpp ../base/CoinPairWhiteList.h)
ADD_LIBRARY(binancemd SHARED ${BINANCE_SOURCE_FILES})
TARGET_LINK_LIBRARIES(binancemd ${COMMON_LIBRARIES} ${Boost_LIBRARIES} ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcpr.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcurl.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcrypto.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libssl.so ${CMAKE_SOURCE_DIR}/utils/libwebsockets/lib/libwebsockets.so)
INSTALL(TARGETS binancemd DESTINATION "lib/wingchun")

SET(COINMEX_SOURCE_FILES MDEngineCoinmex.cpp MDEngineCoinmex.h IMDEngine.cpp IMDEngine.h ../base/IEngine.cpp ../base/IEngine.h ../base/CoinPairWhiteList.cpp ../base/CoinPairWhiteList.h PriceBook20Assembler.cpp PriceBook20Assembler.h)
ADD_LIBRARY(coinmexmd SHARED ${COINMEX_SOURCE_FILES})
TARGET_LINK_LIBRARIES(coinmexmd ${COMMON_LIBRARIES} ${Boost_LIBRARIES} ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcpr.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcurl.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcrypto.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libssl.so ${CMAKE_SOURCE_DIR}/utils/libwebsockets/lib/libwebsockets.so)
INSTALL(TARGETS coinmexmd DESTINATION "lib/wingchun")

SET(MOCK_SOURCE_FILES MDEngineMock.cpp MDEngineMock.h IMDEngine.cpp IMDEngine.h ../base/IEngine.cpp ../base/IEngine.h ../base/CoinPairWhiteList.cpp ../base/CoinPairWhiteList.h)
ADD_LIBRARY(mockmd SHARED ${MOCK_SOURCE_FILES})
TARGET_LINK_LIBRARIES(mockmd ${COMMON_LIBRARIES} ${Boost_LIBRARIES} ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcpr.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcurl.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcrypto.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libssl.so ${CMAKE_SOURCE_DIR}/utils/libwebsockets/lib/libwebsockets.so)
INSTALL(TARGETS mockmd DESTINATION "lib/wingchun")

SET(BITMAX_SOURCE_FILES MDEngineBitmax.cpp MDEngineBitmax.h IMDEngine.cpp IMDEngine.h ../base/IEngine.cpp ../base/IEngine.h ../base/CoinPairWhiteList.cpp ../base/CoinPairWhiteList.h PriceBook20Assembler.cpp PriceBook20Assembler.h)
ADD_LIBRARY(bitmaxmd SHARED ${BITMAX_SOURCE_FILES})
TARGET_LINK_LIBRARIES(bitmaxmd ${COMMON_LIBRARIES} ${Boost_LIBRARIES} ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcpr.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcurl.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcrypto.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libssl.so ${CMAKE_SOURCE_DIR}/utils/libwebsockets/lib/libwebsockets.so)
INSTALL(TARGETS bitmaxmd DESTINATION "lib/wingchun")

SET(BITFINEX_SOURCE_FILES MDEngineBitfinex.cpp MDEngineBitfinex.h IMDEngine.cpp IMDEngine.h ../base/IEngine.cpp ../base/IEngine.h ../base/CoinPairWhiteList.cpp ../base/CoinPairWhiteList.h PriceBook20Assembler.cpp PriceBook20Assembler.h)
ADD_LIBRARY(bitfinexmd SHARED ${BITFINEX_SOURCE_FILES})
TARGET_LINK_LIBRARIES(bitfinexmd ${COMMON_LIBRARIES} ${Boost_LIBRARIES} ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcpr.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcurl.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcrypto.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libssl.so ${CMAKE_SOURCE_DIR}/utils/libwebsockets/lib/libwebsockets.so)
INSTALL(TARGETS bitfinexmd DESTINATION "lib/wingchun")

SET(BITMEX_SOURCE_FILES MDEngineBitmex.cpp MDEngineBitmex.h IMDEngine.cpp IMDEngine.h ../base/IEngine.cpp ../base/IEngine.h ../base/CoinPairWhiteList.cpp ../base/CoinPairWhiteList.h PriceBook20Assembler.cpp PriceBook20Assembler.h)
ADD_LIBRARY(bitmexmd SHARED ${BITMEX_SOURCE_FILES})
TARGET_LINK_LIBRARIES(bitmexmd ${COMMON_LIBRARIES} ${Boost_LIBRARIES} ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcpr.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcurl.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcrypto.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libssl.so ${CMAKE_SOURCE_DIR}/utils/libwebsockets/lib/libwebsockets.so)
INSTALL(TARGETS bitmexmd DESTINATION "lib/wingchun")

SET(HITBTC_SOURCE_FILES MDEngineHitBTC.cpp MDEngineHitBTC.h IMDEngine.cpp IMDEngine.h ../base/IEngine.cpp ../base/IEngine.h ../base/CoinPairWhiteList.cpp ../base/CoinPairWhiteList.h PriceBook20Assembler.cpp PriceBook20Assembler.h)
ADD_LIBRARY(hitbtcmd SHARED ${HITBTC_SOURCE_FILES})
TARGET_LINK_LIBRARIES(hitbtcmd ${COMMON_LIBRARIES} ${Boost_LIBRARIES} ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcpr.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcurl.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcrypto.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libssl.so ${CMAKE_SOURCE_DIR}/utils/libwebsockets/lib/libwebsockets.so)
INSTALL(TARGETS hitbtcmd DESTINATION "lib/wingchun")

SET(OCEANEX_SOURCE_FILES MDEngineOceanEx.cpp MDEngineOceanEx.h IMDEngine.cpp IMDEngine.h ../base/IEngine.cpp ../base/IEngine.h ../base/CoinPairWhiteList.cpp ../base/CoinPairWhiteList.h PriceBook20Assembler.cpp PriceBook20Assembler.h)
ADD_LIBRARY(oceanexmd SHARED ${OCEANEX_SOURCE_FILES})
TARGET_LINK_LIBRARIES(oceanexmd ${COMMON_LIBRARIES} ${Boost_LIBRARIES} ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcpr.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcurl.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcrypto.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libssl.so ${CMAKE_SOURCE_DIR}/utils/libwebsockets/lib/libwebsockets.so)
INSTALL(TARGETS oceanexmd DESTINATION "lib/wingchun")


SET(HUOBI_SOURCE_FILES MDEngineHuobi.cpp MDEngineHuobi.h  IMDEngine.cpp IMDEngine.h ../base/IEngine.cpp ../base/IEngine.h ../base/CoinPairWhiteList.cpp ../base/CoinPairWhiteList.h PriceBook20Assembler.cpp PriceBook20Assembler.h ../../utils/common/ld_utils.h ../../utils/common/ld_utils.cpp)
ADD_LIBRARY(huobimd SHARED ${HUOBI_SOURCE_FILES})
TARGET_LINK_LIBRARIES(huobimd ${COMMON_LIBRARIES} ${Boost_LIBRARIES}  ${CMAKE_SOURCE_DIR}/utils/libwebsockets/lib/libwebsockets.so ${CMAKE_SOURCE_DIR}/utils/zlib/lib/libz.so.1.2.11)
INSTALL(TARGETS huobimd DESTINATION "lib/wingchun")

SET(PROBIT_SOURCE_FILES MDEngineProbit.cpp MDEngineProbit.h IMDEngine.cpp IMDEngine.h ../base/IEngine.cpp ../base/IEngine.h ../base/CoinPairWhiteList.cpp ../base/CoinPairWhiteList.h PriceBook20Assembler.cpp PriceBook20Assembler.h)
ADD_LIBRARY(probitmd SHARED ${PROBIT_SOURCE_FILES})
TARGET_LINK_LIBRARIES(probitmd ${COMMON_LIBRARIES} ${Boost_LIBRARIES} ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcpr.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcurl.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcrypto.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libssl.so ${CMAKE_SOURCE_DIR}/utils/libwebsockets/lib/libwebsockets.so)
INSTALL(TARGETS probitmd DESTINATION "lib/wingchun")

SET(UPBIT_SOURCE_FILES MDEngineUpbit.cpp MDEngineUpbit.h IMDEngine.cpp IMDEngine.h ../base/IEngine.cpp ../base/IEngine.h ../base/CoinPairWhiteList.cpp ../base/CoinPairWhiteList.h PriceBook20Assembler.cpp PriceBook20Assembler.h)
ADD_LIBRARY(upbitmd SHARED ${UPBIT_SOURCE_FILES})
TARGET_LINK_LIBRARIES(upbitmd journal kflog ${Boost_LIBRARIES} ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcpr.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcurl.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcrypto.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libssl.so ${CMAKE_SOURCE_DIR}/utils/libwebsockets/lib/libwebsockets.so)
INSTALL(TARGETS upbitmd DESTINATION "lib/wingchun")

SET(BITHUMB_SOURCE_FILES MDEngineBithumb.cpp MDEngineBithumb.h IMDEngine.cpp IMDEngine.h ../base/IEngine.cpp ../base/IEngine.h ../base/CoinPairWhiteList.cpp ../base/CoinPairWhiteList.h PriceBook20Assembler.cpp PriceBook20Assembler.h)
ADD_LIBRARY(bithumbmd SHARED ${BITHUMB_SOURCE_FILES})
TARGET_LINK_LIBRARIES(bithumbmd ${COMMON_LIBRARIES} ${Boost_LIBRARIES} ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcpr.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcurl.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcrypto.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libssl.so ${CMAKE_SOURCE_DIR}/utils/libwebsockets/lib/libwebsockets.so)
INSTALL(TARGETS bithumbmd DESTINATION "lib/wingchun")


SET(DAYBIT_SOURCE_FILES MDEngineDaybit.cpp MDEngineDaybit.h IMDEngine.cpp IMDEngine.h ../base/IEngine.cpp ../base/IEngine.h ../base/CoinPairWhiteList.cpp ../base/CoinPairWhiteList.h PriceBook20Assembler.cpp PriceBook20Assembler.h ../../utils/common/Utils.h ../../utils/common/Utils.cpp)
ADD_LIBRARY(daybitmd SHARED ${DAYBIT_SOURCE_FILES})
TARGET_LINK_LIBRARIES(daybitmd ${COMMON_LIBRARIES} ${Boost_LIBRARIES}  ${CMAKE_SOURCE_DIR}/utils/libwebsockets/lib/libwebsockets.so ${CMAKE_SOURCE_DIR}/utils/zlib/lib/libz.so.1.2.11)
INSTALL(TARGETS daybitmd DESTINATION "lib/wingchun")

SET(POLONIEX_SOURCE_FILES MDEnginePoloniex.cpp MDEnginePoloniex.h IMDEngine.cpp IMDEngine.h ../base/IEngine.cpp ../base/IEngine.h ../base/CoinPairWhiteList.cpp ../base/CoinPairWhiteList.h PriceBook20Assembler.cpp PriceBook20Assembler.h ../../utils/common/Utils.h ../../utils/common/Utils.cpp)
ADD_LIBRARY(poloniexmd SHARED ${POLONIEX_SOURCE_FILES})
TARGET_LINK_LIBRARIES(poloniexmd ${COMMON_LIBRARIES} ${Boost_LIBRARIES}  ${CMAKE_SOURCE_DIR}/utils/libwebsockets/lib/libwebsockets.so ${CMAKE_SOURCE_DIR}/utils/zlib/lib/libz.so.1.2.11)
INSTALL(TARGETS poloniexmd DESTINATION "lib/wingchun")

SET(KUCOIN_SOURCE_FILES MDEngineKuCoin.cpp MDEngineKuCoin.h IMDEngine.cpp IMDEngine.h ../base/IEngine.cpp ../base/IEngine.h ../base/CoinPairWhiteList.cpp ../base/CoinPairWhiteList.h PriceBook20Assembler.cpp PriceBook20Assembler.h ../../utils/common/Utils.h ../../utils/common/Utils.cpp)
ADD_LIBRARY(kucoinmd SHARED ${KUCOIN_SOURCE_FILES})
TARGET_LINK_LIBRARIES(kucoinmd ${COMMON_LIBRARIES} ${Boost_LIBRARIES} ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcpr.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcurl.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcrypto.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libssl.so ${CMAKE_SOURCE_DIR}/utils/libwebsockets/lib/libwebsockets.so ${CMAKE_SOURCE_DIR}/utils/zlib/lib/libz.so.1.2.11)
INSTALL(TARGETS kucoinmd DESTINATION "lib/wingchun")

SET(BITFLYER_SOURCE_FILES MDEnginebitFlyer.cpp MDEnginebitFlyer.h IMDEngine.cpp IMDEngine.h ../base/IEngine.cpp ../base/IEngine.h ../base/CoinPairWhiteList.cpp ../base/CoinPairWhiteList.h PriceBook20Assembler.cpp PriceBook20Assembler.h)
ADD_LIBRARY(bitflyermd SHARED ${BITFLYER_SOURCE_FILES})
TARGET_LINK_LIBRARIES(bitflyermd ${COMMON_LIBRARIES} ${Boost_LIBRARIES} ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcpr.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcurl.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcrypto.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libssl.so ${CMAKE_SOURCE_DIR}/utils/libwebsockets/lib/libwebsockets.so)
INSTALL(TARGETS bitflyermd DESTINATION "lib/wingchun")

SET(BITSTAMP_SOURCE_FILES MDEngineBitstamp.cpp MDEngineBitstamp.h IMDEngine.cpp IMDEngine.h ../base/IEngine.cpp ../base/IEngine.h ../base/CoinPairWhiteList.cpp ../base/CoinPairWhiteList.h PriceBook20Assembler.cpp PriceBook20Assembler.h)
ADD_LIBRARY(bitstampmd SHARED ${BITSTAMP_SOURCE_FILES})
TARGET_LINK_LIBRARIES(bitstampmd ${COMMON_LIBRARIES} ${Boost_LIBRARIES} ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcpr.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcurl.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcrypto.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libssl.so ${CMAKE_SOURCE_DIR}/utils/libwebsockets/lib/libwebsockets.so)
INSTALL(TARGETS bitstampmd DESTINATION "lib/wingchun")

INSTALL(DIRECTORY ${CMAKE_SOURCE_DIR}/utils/cpr/lib/ DESTINATION "lib/wingchun")

INSTALL(DIRECTORY ${CMAKE_SOURCE_DIR}/utils/zlib/lib/ DESTINATION "lib/wingchun")

INSTALL(FILES ${CMAKE_SOURCE_DIR}/utils/libwebsockets/lib/libwebsockets.so.13 DESTINATION "lib/wingchun")
