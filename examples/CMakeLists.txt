# ESHM examples.
#
# Every numbered directory is self-contained: it builds as part of this tree
# (cmake -DESHM_BUILD_EXAMPLES=ON ..) and also standalone against an installed
# ESHM, so you can copy one anywhere and build it with
#   cmake -S . -B build && cmake --build build
#
# Executables land in build/examples/<NN_name>/.

add_subdirectory(01_hello_channel)
add_subdirectory(02_structured_data)
add_subdirectory(03_c_api)
add_subdirectory(04_monitoring)
add_subdirectory(05_reconnection)
add_subdirectory(06_large_payload)
add_subdirectory(07_rich_types)
add_subdirectory(08_benchmark)
add_subdirectory(09_integration)

# 06_large_payload needs a channel bigger than the 4 KB default to show frames
# moving in one write; it degrades gracefully and says so when built small.
if(ESHM_MAX_DATA_SIZE LESS 65536)
    message(STATUS
        "ESHM examples: ESHM_MAX_DATA_SIZE=${ESHM_MAX_DATA_SIZE}; "
        "06_large_payload will chunk. Configure with -DESHM_MAX_DATA_SIZE=8388608 "
        "to send whole frames.")
endif()
