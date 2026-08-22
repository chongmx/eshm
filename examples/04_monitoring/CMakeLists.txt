# 04 - monitoring: statistics, roles, liveness.
cmake_minimum_required(VERSION 3.12)

if(NOT TARGET ESHM::eshm)
    project(EshmMonitoring VERSION 1.0.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(ESHM 1.0 REQUIRED)
endif()

add_executable(channel_monitor monitor.cpp)
target_link_libraries(channel_monitor PRIVATE ESHM::eshm)
