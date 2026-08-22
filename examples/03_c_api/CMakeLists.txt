# 03 - the ABI-stable C API.
#
# These two are compiled as C, not C++, which is the point: they prove the
# surface in eshm_data_api.h / data_handler_c_api.h is callable without a C++
# compiler, and is therefore safe for ctypes and for other language bindings.
cmake_minimum_required(VERSION 3.12)

if(NOT TARGET ESHM::eshm)
    project(EshmCApi VERSION 1.0.0 LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(ESHM 1.0 REQUIRED)
else()
    # In-tree: the top-level project() only enables CXX.
    enable_language(C)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(c_writer c_writer.c)
target_link_libraries(c_writer PRIVATE ESHM::eshm)

add_executable(c_reader c_reader.c)
target_link_libraries(c_reader PRIVATE ESHM::eshm)
