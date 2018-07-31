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
PROJECT(wingchuntd)

SET(TD_SOURCE_FILES  ITDEngine.cpp ITDEngine.h ../base/IEngine.cpp ../base/IEngine.h ../base/TDUserStruct.cpp ../base/TDUserStruct.h)

#SET(CTP_SOURCE_FILES ${TD_SOURCE_FILES} TDEngineCTP.cpp TDEngineCTP.h)
#ADD_LIBRARY(ctptd SHARED ${CTP_SOURCE_FILES})
#TARGET_LINK_LIBRARIES(ctptd journal kflog thosttraderapi ${Boost_LIBRARIES})
#INSTALL(TARGETS ctptd DESTINATION "lib/wingchun")

#SET(XTP_SOURCE_FILES ${TD_SOURCE_FILES} TDEngineXTP.cpp TDEngineXTP.h)
#ADD_LIBRARY(xtptd SHARED ${XTP_SOURCE_FILES})
#TARGET_LINK_LIBRARIES(xtptd journal kflog xtptraderapi ${Boost_LIBRARIES})
#INSTALL(TARGETS xtptd DESTINATION "lib/wingchun")

include_directories(${CMAKE_SOURCE_DIR}/utils/cpr/include ${CMAKE_SOURCE_DIR}/utils/rapidjson/include/)
SET(BINANCE_SOURCE_FILES ${TD_SOURCE_FILES} TDEngineBinance.cpp TDEngineBinance.h)
ADD_LIBRARY(binancetd SHARED ${BINANCE_SOURCE_FILES})
TARGET_LINK_LIBRARIES(binancetd journal kflog ${Boost_LIBRARIES} ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcpr.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcurl.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcrypto.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libssl.so)
INSTALL(TARGETS binancetd DESTINATION "lib/wingchun")

include_directories(${CMAKE_SOURCE_DIR}/utils/cpr/include ${CMAKE_SOURCE_DIR}/utils/rapidjson/include/)
SET(COINMEX_SOURCE_FILES ${TD_SOURCE_FILES} TDEngineCoinmex.cpp TDEngineCoinmex.h)
ADD_LIBRARY(coinmextd SHARED ${COINMEX_SOURCE_FILES})
TARGET_LINK_LIBRARIES(coinmextd journal kflog ${Boost_LIBRARIES} ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcpr.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcurl.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcrypto.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libssl.so)
INSTALL(TARGETS coinmextd DESTINATION "lib/wingchun")

include_directories(${CMAKE_SOURCE_DIR}/utils/cpr/include ${CMAKE_SOURCE_DIR}/utils/rapidjson/include/)
SET(MOCK_SOURCE_FILES ${TD_SOURCE_FILES} TDEngineMock.cpp TDEngineMock.h)
ADD_LIBRARY(mocktd SHARED ${MOCK_SOURCE_FILES})
TARGET_LINK_LIBRARIES(mocktd journal kflog ${Boost_LIBRARIES} ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcpr.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcurl.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcrypto.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libssl.so)
INSTALL(TARGETS mocktd DESTINATION "lib/wingchun")

include_directories(${CMAKE_SOURCE_DIR}/utils/cpr/include ${CMAKE_SOURCE_DIR}/utils/rapidjson/include/)
SET(BITMAX_SOURCE_FILES ${TD_SOURCE_FILES} TDEngineBitmax.cpp TDEngineBitmax.h)
ADD_LIBRARY(bitmaxtd SHARED ${BITMAX_SOURCE_FILES})
TARGET_LINK_LIBRARIES(bitmaxtd journal kflog ${Boost_LIBRARIES} ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcpr.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcurl.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libcrypto.so ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libssl.so)
INSTALL(TARGETS bitmaxtd DESTINATION "lib/wingchun")
