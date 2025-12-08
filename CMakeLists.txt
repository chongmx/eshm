cmake_minimum_required(VERSION 3.10)
project(ESHM VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)  # Upgraded to C++17 for std::variant
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pthread")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# ESHM core library
add_library(eshm STATIC
    src/eshm.cpp
    include/eshm.h
    include/eshm_data.h
)

target_include_directories(eshm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(eshm PUBLIC pthread rt)

# ASN.1 and DataHandler library
add_library(eshm_data STATIC
    src/asn1_encode.cpp
    src/asn1_decode.cpp
    src/data_handler.cpp
    include/asn1_der.h
    include/data_handler.h
)

target_include_directories(eshm_data PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Main demo executable
add_executable(eshm_demo main.cpp)
target_link_libraries(eshm_demo eshm)

# Enable testing
enable_testing()

# Test directory
add_subdirectory(test)

# Examples directory
add_subdirectory(examples)

# Installation
install(TARGETS eshm eshm_data eshm_demo
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES
    include/eshm.h
    include/eshm_data.h
    include/asn1_der.h
    include/data_handler.h
    DESTINATION include
)
