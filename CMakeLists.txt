cmake_minimum_required(VERSION 3.10)
project(ESHM VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pthread")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# ESHM library
add_library(eshm STATIC
    eshm.cpp
    eshm.h
    eshm_data.h
)

target_include_directories(eshm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(eshm PUBLIC pthread rt)

# Main demo executable
add_executable(eshm_demo main.cpp)
target_link_libraries(eshm_demo eshm)

# Enable testing
enable_testing()

# Test directory
add_subdirectory(test)

# Installation
install(TARGETS eshm eshm_demo
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES eshm.h eshm_data.h
    DESTINATION include
)
