# 02 - structured data: ASN.1 DER records across the language boundary.
cmake_minimum_required(VERSION 3.12)

if(NOT TARGET ESHM::eshm)
    project(EshmStructuredData VERSION 1.0.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(ESHM 1.0 REQUIRED)
endif()

# ESHM::eshm already pulls in ESHM::eshm_data (the codec), so one link is enough.
add_executable(structured_sender sender.cpp)
target_link_libraries(structured_sender PRIVATE ESHM::eshm)

add_executable(structured_receiver receiver.cpp)
target_link_libraries(structured_receiver PRIVATE ESHM::eshm)
