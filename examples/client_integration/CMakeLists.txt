cmake_minimum_required(VERSION 3.10)
project(MyProject VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Method 1: Using ESHM as a subdirectory (git submodule)
# Uncomment if you have ESHM as a subdirectory
add_subdirectory(3rdparty/eshm)

# Method 2: Using find_package (if ESHM is installed system-wide)
# Uncomment if ESHM is installed system-wide
# find_package(ESHM 1.0 REQUIRED)

# Your application
add_executable(my_master master.cpp)
target_link_libraries(my_master PRIVATE ESHM::eshm)

add_executable(my_slave slave.cpp)
target_link_libraries(my_slave PRIVATE ESHM::eshm)
