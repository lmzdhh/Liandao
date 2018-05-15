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
PROJECT(binacpp)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O3 -Wall")

include_directories(${CMAKE_SOURCE_DIR}/lib/jsoncpp-1.8.3/include ${CMAKE_SOURCE_DIR}/lib/libcurl-7.56.0/include ${CMAKE_SOURCE_DIR}/lib/libwebsockets-2.4.0/include)
SET(LIB_SOURCE_FILES binacpp.h binacpp.cpp binacpp_logger.cpp binacpp_logger.h binacpp_utils.cpp binacpp_utils.h binacpp_websocket.cpp binacpp_websocket.h ${CMAKE_SOURCE_DIR}/lib/jsoncpp-1.8.3/src/jsoncpp.cpp)

LINK_DIRECTORIES(${CMAKE_SOURCE_DIR}/../cpr/lib ${CMAKE_SOURCE_DIR}/lib/libwebsockets-2.4.0/lib)

ADD_LIBRARY(${PROJECT_NAME} SHARED ${LIB_SOURCE_FILES})
TARGET_LINK_LIBRARIES(${PROJECT_NAME} -lcurl -lwebsockets)


INSTALL(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_SOURCE_DIR}/lib/libbinacpp/lib)


