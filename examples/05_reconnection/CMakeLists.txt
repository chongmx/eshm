# 05 - reconnection: recovery policy and role negotiation.
cmake_minimum_required(VERSION 3.12)

if(NOT TARGET ESHM::eshm)
    project(EshmReconnection VERSION 1.0.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(ESHM 1.0 REQUIRED)
endif()

add_executable(resilient_consumer resilient_consumer.cpp)
target_link_libraries(resilient_consumer PRIVATE ESHM::eshm)

add_executable(flaky_publisher flaky_publisher.cpp)
target_link_libraries(flaky_publisher PRIVATE ESHM::eshm)
