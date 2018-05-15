
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
PROJECT(example)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 ")

include_directories(${CMAKE_SOURCE_DIR}/lib/libbinacpp/include ${CMAKE_SOURCE_DIR}/lib/jsoncpp-1.8.3/include ${CMAKE_SOURCE_DIR}/lib/libcurl-7.56.0/include ${CMAKE_SOURCE_DIR}/lib/libwebsockets-2.4.0/include)
SET(LIB_SOURCE_FILES example.cpp)

LINK_DIRECTORIES(${CMAKE_SOURCE_DIR}/lib/libbinacpp/lib ${CMAKE_SOURCE_DIR}/../cpr/lib ${CMAKE_SOURCE_DIR}/lib/libwebsockets-2.4.0/lib)


ADD_EXECUTABLE(example example.cpp ${CMAKE_SOURCE_DIR}/lib/jsoncpp-1.8.3/src/jsoncpp.cpp)
ADD_EXECUTABLE(example_aggTrades example_aggTrades.cpp ${CMAKE_SOURCE_DIR}/lib/jsoncpp-1.8.3/src/jsoncpp.cpp)
ADD_EXECUTABLE(example_depthCache example_depthCache.cpp ${CMAKE_SOURCE_DIR}/lib/jsoncpp-1.8.3/src/jsoncpp.cpp)
ADD_EXECUTABLE(example_klines example_klines.cpp ${CMAKE_SOURCE_DIR}/lib/jsoncpp-1.8.3/src/jsoncpp.cpp)
ADD_EXECUTABLE(example_template example_template.cpp ${CMAKE_SOURCE_DIR}/lib/jsoncpp-1.8.3/src/jsoncpp.cpp)
ADD_EXECUTABLE(example_userStream example_userStream.cpp ${CMAKE_SOURCE_DIR}/lib/jsoncpp-1.8.3/src/jsoncpp.cpp)
ADD_EXECUTABLE(example_wapi example_wapi.cpp ${CMAKE_SOURCE_DIR}/lib/jsoncpp-1.8.3/src/jsoncpp.cpp)

TARGET_LINK_LIBRARIES(example -lcurl -lwebsockets -lbinacpp)
TARGET_LINK_LIBRARIES(example_aggTrades -lcurl -lwebsockets -lbinacpp)
TARGET_LINK_LIBRARIES(example_depthCache -lcurl -lwebsockets -lbinacpp)
TARGET_LINK_LIBRARIES(example_klines -lcurl -lwebsockets -lbinacpp)
TARGET_LINK_LIBRARIES(example_template -lcurl -lwebsockets -lbinacpp)
TARGET_LINK_LIBRARIES(example_userStream -lcurl -lwebsockets -lbinacpp)
TARGET_LINK_LIBRARIES(example_wapi -lcurl -lwebsockets -lbinacpp)

