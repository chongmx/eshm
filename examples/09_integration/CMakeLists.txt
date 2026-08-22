# 09 - integration: how your project consumes ESHM.
#
# This file is the example. It shows all three supported ways to pull ESHM into
# a project; pick one and delete the rest.
#
cmake_minimum_required(VERSION 3.12)

# The `if(NOT TARGET ...)` guard is what lets this directory build both inside
# the ESHM tree and standalone. Your project only needs the standalone half.
if(NOT TARGET ESHM::eshm)
    project(MyApp VERSION 1.0.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    # --- Option 1: installed system-wide (recommended) ----------------------
    # Provided by libeshm-dev at <libdir>/cmake/ESHM/ESHMConfig.cmake.
    # Add -DCMAKE_PREFIX_PATH=/usr/local if you installed from source there.
    find_package(ESHM 1.0 REQUIRED)

    # --- Option 2: git submodule -------------------------------------------
    # No install step, and your build controls the ESHM options. ESHM detects
    # that it is a subproject and skips its own tests, examples and demo.
    #
    #   git submodule add <eshm-git-url> 3rdparty/eshm
    #   git submodule update --init --recursive
    #
    # add_subdirectory(3rdparty/eshm)
    #
    # Set options before add_subdirectory to pin the memory layout:
    #   set(ESHM_MAX_DATA_SIZE 8388608 CACHE STRING "" FORCE)

    # --- Option 3: FetchContent --------------------------------------------
    # Same as the submodule, without vendoring the source.
    #
    # include(FetchContent)
    # FetchContent_Declare(eshm
    #     GIT_REPOSITORY <eshm-git-url>
    #     GIT_TAG        v1.0.0)
    # FetchContent_MakeAvailable(eshm)
endif()

# Whichever option you picked, the target name is the same. ESHM::eshm carries
# the include directory, libeshm, libeshm_data and pthread/rt, so this single
# line is the whole integration.
add_executable(integration_master master.cpp)
target_link_libraries(integration_master PRIVATE ESHM::eshm)

add_executable(integration_slave slave.cpp)
target_link_libraries(integration_slave PRIVATE ESHM::eshm)

# Link ESHM::eshm_data instead if you only need the ASN.1 codec and never open
# a channel - see ../07_rich_types.
