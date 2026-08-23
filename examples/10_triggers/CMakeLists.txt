# 10 - named triggers: run a function on the other side.
cmake_minimum_required(VERSION 3.12)

if(NOT TARGET ESHM::eshm)
    project(EshmTriggers VERSION 1.1.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(ESHM 1.1 REQUIRED)
endif()

add_executable(trigger_master trigger_master.cpp)
target_link_libraries(trigger_master PRIVATE ESHM::eshm)

add_executable(trigger_worker trigger_worker.cpp)
target_link_libraries(trigger_worker PRIVATE ESHM::eshm)
