# 01 - hello channel: the core read/write API.
#
# Builds two ways:
#   * as part of the ESHM tree (cmake -DESHM_BUILD_EXAMPLES=ON ..)
#   * standalone against an installed ESHM - copy this directory anywhere:
#       sudo apt install ./libeshm1_*.deb ./libeshm-dev_*.deb
#       cmake -S . -B build && cmake --build build
#
cmake_minimum_required(VERSION 3.12)

if(NOT TARGET ESHM::eshm)
    project(EshmHelloChannel VERSION 1.0.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    # Installed by libeshm-dev: <libdir>/cmake/ESHM/ESHMConfig.cmake
    # Add -DCMAKE_PREFIX_PATH=/usr/local if you installed from source there.
    find_package(ESHM 1.0 REQUIRED)
endif()

add_executable(hello_publisher publisher.cpp)
target_link_libraries(hello_publisher PRIVATE ESHM::eshm)

add_executable(hello_consumer consumer.cpp)
target_link_libraries(hello_consumer PRIVATE ESHM::eshm)

# ESHM::eshm carries the include directory, libeshm, libeshm_data and
# pthread/rt, so nothing else needs configuring.
