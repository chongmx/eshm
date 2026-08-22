# 07 - the C++-only DataHandler types.
#
# Links eshm_data (the codec) alone: this example never opens a channel.
cmake_minimum_required(VERSION 3.12)

if(NOT TARGET ESHM::eshm_data)
    project(EshmRichTypes VERSION 1.0.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(ESHM 1.0 REQUIRED)
endif()

add_executable(rich_types rich_types.cpp)
target_link_libraries(rich_types PRIVATE ESHM::eshm_data)
