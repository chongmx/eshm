# Functional tests
add_executable(test_basic functional/test_basic.cpp)
target_link_libraries(test_basic eshm)

add_executable(test_master_slave functional/test_master_slave.cpp)
target_link_libraries(test_master_slave eshm)

add_executable(test_auto_role functional/test_auto_role.cpp)
target_link_libraries(test_auto_role eshm)

add_executable(test_stale_detection functional/test_stale_detection.cpp)
target_link_libraries(test_stale_detection eshm)

add_executable(test_error_handling functional/test_error_handling.cpp)
target_link_libraries(test_error_handling eshm)

add_executable(test_reconnect functional/test_reconnect.cpp)
target_link_libraries(test_reconnect eshm)

# Performance tests
add_executable(test_benchmark_master performance/test_benchmark_master.cpp)
target_link_libraries(test_benchmark_master eshm)

# Data Handler tests
add_executable(test_data_handler functional/test_data_handler.cpp)
target_link_libraries(test_data_handler eshm_data)

# Add tests
add_test(NAME BasicTest COMMAND test_basic)
add_test(NAME ErrorHandlingTest COMMAND test_error_handling)
add_test(NAME DataHandlerTest COMMAND test_data_handler)

# Image transfer test (requires large ESHM_MAX_DATA_SIZE)
add_subdirectory(image_transfer)
