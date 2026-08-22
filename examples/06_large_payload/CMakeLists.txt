# 06 - large payloads: moving more than one channel holds.
cmake_minimum_required(VERSION 3.12)

if(NOT TARGET ESHM::eshm)
    project(EshmLargePayload VERSION 1.0.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(ESHM 1.0 REQUIRED)
endif()

add_executable(frame_sender frame_sender.cpp)
target_link_libraries(frame_sender PRIVATE ESHM::eshm)
target_include_directories(frame_sender PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(frame_receiver frame_receiver.cpp)
target_link_libraries(frame_receiver PRIVATE ESHM::eshm)
target_include_directories(frame_receiver PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
