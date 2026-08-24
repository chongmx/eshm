cmake_minimum_required(VERSION 3.10)
project(ESHM VERSION 1.2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)  # Upgraded to C++17 for std::variant
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Needed before any target's install(), not just the main one below - the
# eshm_cuda block runs first and used to see CMAKE_INSTALL_LIBDIR unset,
# landing its .so outside the multiarch directory the other libraries use.
include(GNUInstallDirs)

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
option(ESHM_INSTALL_PYTHON "Install the Python bindings (python3-eshm)" ON)

# Optional GPU VRAM sharing module (src/eshm_cuda.cpp). AUTO builds it only if
# a CUDA toolkit is found; ON fails the configure if it is not; OFF always
# skips it. Needs the driver API only (libcuda), not nvcc, so no CUDA language
# is enabled - find_package(CUDAToolkit) is enough to get headers + libcuda.
set(ESHM_ENABLE_CUDA "AUTO" CACHE STRING "Build the GPU VRAM sharing module: AUTO, ON, or OFF")
set_property(CACHE ESHM_ENABLE_CUDA PROPERTY STRINGS AUTO ON OFF)

if(NOT ESHM_ENABLE_CUDA STREQUAL "OFF")
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
        set(ESHM_CUDA_AVAILABLE TRUE)
    elseif(ESHM_ENABLE_CUDA STREQUAL "ON")
        message(FATAL_ERROR "ESHM_ENABLE_CUDA=ON but no CUDA toolkit was found")
    else()
        set(ESHM_CUDA_AVAILABLE FALSE)
        message(STATUS "ESHM: no CUDA toolkit found, skipping eshm_cuda (GPU VRAM sharing)")
    endif()
else()
    set(ESHM_CUDA_AVAILABLE FALSE)
endif()

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
    src/data_handler_c_api.cpp
    include/asn1_der.h
    include/data_handler.h
    include/data_handler_c_api.h
)

target_include_directories(eshm_data PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

set_target_properties(eshm_data PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER "include/asn1_der.h;include/data_handler.h;include/data_handler_c_api.h"
)

# ESHM core library
add_library(eshm SHARED
    src/eshm.cpp
    src/eshm_data_api.cpp
    src/eshm_rpc.cpp
    include/eshm.h
    include/eshm_data.h
    include/eshm_data_api.h
    include/eshm_rpc.h
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
    PUBLIC_HEADER "include/eshm.h;include/eshm_data.h;include/eshm_data_api.h;include/eshm_rpc.h"
)

# Create alias targets for use when included as subdirectory
add_library(ESHM::eshm ALIAS eshm)
add_library(ESHM::eshm_data ALIAS eshm_data)

# Optional GPU VRAM sharing library. Separate .so on purpose: the core eshm
# library must keep building on machines with no CUDA toolkit at all.
if(ESHM_CUDA_AVAILABLE)
    add_library(eshm_cuda SHARED
        src/eshm_cuda.cpp
        include/eshm_cuda.h
    )
    target_include_directories(eshm_cuda PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    # cuda.h only - never linked against libcuda.so. eshm_cuda.cpp dlopen()s
    # libcuda.so.1 itself at first use, so this library carries no ELF NEEDED
    # entry on anything CUDA-related and loads fine even with no NVIDIA
    # driver installed; only actually calling eshm_cuda_create()/attach()
    # fails, with a clear error, if there is no compatible driver.
    target_include_directories(eshm_cuda PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
    target_link_libraries(eshm_cuda PUBLIC pthread ${CMAKE_DL_LIBS})
    set_target_properties(eshm_cuda PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION 1
        PUBLIC_HEADER "include/eshm_cuda.h"
    )
    add_library(ESHM::eshm_cuda ALIAS eshm_cuda)

    install(TARGETS eshm_cuda
        EXPORT ESHMTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()

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

# Python bindings
#
# Debian keeps arch-independent system modules in /usr/lib/python3/dist-packages,
# but a /usr/local install must use the versioned directory that interpreter
# actually searches. Override with -DESHM_PYTHON_INSTALL_DIR=... on other
# distributions (site-packages).
if(ESHM_INSTALL_PYTHON)
    if(NOT DEFINED ESHM_PYTHON_INSTALL_DIR)
        if(CMAKE_INSTALL_PREFIX STREQUAL "/usr")
            set(_eshm_pydir "lib/python3/dist-packages")
        else()
            find_package(Python3 QUIET COMPONENTS Interpreter)
            if(Python3_Interpreter_FOUND)
                set(_eshm_pydir
                    "lib/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/dist-packages")
            else()
                set(_eshm_pydir "lib/python3/dist-packages")
            endif()
        endif()
        set(ESHM_PYTHON_INSTALL_DIR "${_eshm_pydir}" CACHE STRING
            "Directory (relative to the prefix) for the Python package")
    endif()

    install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/py/__init__.py
        ${CMAKE_CURRENT_SOURCE_DIR}/py/eshm.py
        ${CMAKE_CURRENT_SOURCE_DIR}/py/data_handler.py
        ${CMAKE_CURRENT_SOURCE_DIR}/py/data_handler_native.py
        ${CMAKE_CURRENT_SOURCE_DIR}/py/eshm_data.py
        ${CMAKE_CURRENT_SOURCE_DIR}/py/rpc.py
        ${CMAKE_CURRENT_SOURCE_DIR}/py/eshm_cuda.py
        DESTINATION ${ESHM_PYTHON_INSTALL_DIR}/eshm
    )
    message(STATUS "ESHM: Python bindings -> ${CMAKE_INSTALL_PREFIX}/${ESHM_PYTHON_INSTALL_DIR}/eshm")
endif()

# Licence and readme
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
    ${CMAKE_CURRENT_SOURCE_DIR}/README.md
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/eshm
)

# CMake has no uninstall rule of its own; this removes exactly what the last
# `cmake --install` recorded in install_manifest.txt.
if(NOT TARGET uninstall)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
        IMMEDIATE @ONLY)
    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()

# Export targets for use in build tree (without installation)
export(EXPORT ESHMTargets
    FILE ${CMAKE_CURRENT_BINARY_DIR}/ESHMTargets.cmake
    NAMESPACE ESHM::
)
