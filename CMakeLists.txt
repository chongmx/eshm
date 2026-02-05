cmake_minimum_required(VERSION 3.10)
project(ESHM VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)  # Upgraded to C++17 for std::variant
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pthread")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# Determine if ESHM is built as a subproject (using add_subdirectory) or standalone
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(ESHM_STANDALONE TRUE)
else()
    set(ESHM_STANDALONE FALSE)
endif()

# Options for controlling what gets built
option(ESHM_BUILD_TESTS "Build ESHM tests" ${ESHM_STANDALONE})
option(ESHM_BUILD_EXAMPLES "Build ESHM examples" ${ESHM_STANDALONE})
option(ESHM_BUILD_DEMO "Build ESHM demo application" ${ESHM_STANDALONE})

# Memory layout configuration
set(ESHM_MAX_DATA_SIZE 4096 CACHE STRING "Maximum data size per channel in bytes (default: 4096)")
set(ESHM_HEARTBEAT_INTERVAL_MS 1 CACHE STRING "Heartbeat update interval in milliseconds (default: 1)")

# Generate configuration header
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/eshm_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/eshm_config.h
    @ONLY
)

# ASN.1 and DataHandler library (must be defined first since eshm depends on it)
add_library(eshm_data SHARED
    src/asn1_encode.cpp
    src/asn1_decode.cpp
    src/data_handler.cpp
    include/asn1_der.h
    include/data_handler.h
)

target_include_directories(eshm_data PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

set_target_properties(eshm_data PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER "include/asn1_der.h;include/data_handler.h"
)

# ESHM core library
add_library(eshm SHARED
    src/eshm.cpp
    include/eshm.h
    include/eshm_data.h
)

target_include_directories(eshm PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(eshm PUBLIC eshm_data pthread rt)

set_target_properties(eshm PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER "include/eshm.h;include/eshm_data.h"
)

# Create alias targets for use when included as subdirectory
add_library(ESHM::eshm ALIAS eshm)
add_library(ESHM::eshm_data ALIAS eshm_data)

# Demo executable (only built in standalone mode by default)
if(ESHM_BUILD_DEMO)
    add_executable(eshm_demo demo/main.cpp)
    target_link_libraries(eshm_demo eshm)
endif()

# Tests (only built in standalone mode by default)
if(ESHM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# Examples (only built in standalone mode by default)
if(ESHM_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Installation
include(GNUInstallDirs)

install(TARGETS eshm eshm_data
    EXPORT ESHMTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(ESHM_BUILD_DEMO)
    install(TARGETS eshm_demo
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# Export targets for find_package support
install(EXPORT ESHMTargets
    FILE ESHMTargets.cmake
    NAMESPACE ESHM::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ESHM
)

# Create config file for find_package
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ESHMConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ESHMConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ESHM
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/ESHMConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/ESHMConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/ESHMConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ESHM
)

# Install generated config header
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/include/eshm_config.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Export targets for use in build tree (without installation)
export(EXPORT ESHMTargets
    FILE ${CMAKE_CURRENT_BINARY_DIR}/ESHMTargets.cmake
    NAMESPACE ESHM::
)
