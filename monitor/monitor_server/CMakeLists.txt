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


PROJECT(monitorserver)

INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/utils/rapidjson/include/ ${CMAKE_SOURCE_DIR}/utils/uwebsockets/include/)
INCLUDE_DIRECTORIES(../../yijinjing/log)
INCLUDE_DIRECTORIES(../../yijinjing/utils)

SET(MONITOR_SOURCE_FILES  MonitorWrapper.cpp MonitorWrapper.h Monitor.cpp Monitor.h )

ADD_LIBRARY(${PROJECT_NAME} SHARED ${MONITOR_SOURCE_FILES})
#ADD_EXECUTABLE(${PROJECT_NAME} ${MONITOR_SOURCE_FILES})

SET(MONITOR_LIBRARIES ${CMAKE_SOURCE_DIR}/utils/cpr/lib/libssl.so.10  ${CMAKE_SOURCE_DIR}/utils/uwebsockets/lib/libuWS.so  ${CMAKE_SOURCE_DIR}/utils/zlib/lib/libz.so.1.2.11)
TARGET_LINK_LIBRARIES(${PROJECT_NAME}  python kflog ${Boost_LIBRARIES}  ${MONITOR_LIBRARIES})

INSTALL(TARGETS ${PROJECT_NAME} DESTINATION "lib/wingchun")

INSTALL(FILES ${MONITOR_LIBRARIES} DESTINATION "lib/wingchun")
