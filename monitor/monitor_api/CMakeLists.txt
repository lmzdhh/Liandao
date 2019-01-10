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
PROJECT(monitorapi)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/utils/rapidjson/include/ ${CMAKE_SOURCE_DIR}/utils/libwebsockets/include/ ${CMAKE_SOURCE_DIR}/monitor/)
SET(MONITORAPI_SOURCE_FILES MonitorClient.h  MonitorClient.cpp wsclient.h wsclient.cpp)
ADD_LIBRARY(${PROJECT_NAME} SHARED ${MONITORAPI_SOURCE_FILES})
TARGET_LINK_LIBRARIES(${PROJECT_NAME}  python kflog ${Boost_LIBRARIES}  ${CMAKE_SOURCE_DIR}/utils/poco/lib/libPocoFoundation.a ${CMAKE_SOURCE_DIR}/utils/libwebsockets/lib/libwebsockets.so)
INSTALL(TARGETS ${PROJECT_NAME} DESTINATION "lib/wingchun")
INSTALL(FILES MonitorClient.h   DESTINATION "include/monitor_api")
INSTALL(FILES ${CMAKE_SOURCE_DIR}/monitor/common/MONITOR_DECLARE.h   DESTINATION "include/common")
