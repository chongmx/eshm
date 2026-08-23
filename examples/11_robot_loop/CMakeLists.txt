# 11 - robot loop: policy-in-the-loop benchmark.
#
# Camera frames need a channel big enough to hold one: build the whole tree
# with a larger ESHM_MAX_DATA_SIZE, e.g.
#   cmake -S . -B build-robot -DESHM_MAX_DATA_SIZE=2097152
cmake_minimum_required(VERSION 3.12)

if(NOT TARGET ESHM::eshm)
    project(EshmRobotLoop VERSION 1.1.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(ESHM 1.1 REQUIRED)
endif()

add_executable(robot_sim robot_sim.cpp)
target_link_libraries(robot_sim PRIVATE ESHM::eshm)
target_include_directories(robot_sim PRIVATE )

add_executable(frame_bench frame_bench.cpp)
target_link_libraries(frame_bench PRIVATE ESHM::eshm)
target_include_directories(frame_bench PRIVATE )
