# Test executables
add_executable(test_basic test_basic.cpp)
target_link_libraries(test_basic eshm)

add_executable(test_master_slave test_master_slave.cpp)
target_link_libraries(test_master_slave eshm)

add_executable(test_auto_role test_auto_role.cpp)
target_link_libraries(test_auto_role eshm)

add_executable(test_stale_detection test_stale_detection.cpp)
target_link_libraries(test_stale_detection eshm)

add_executable(test_performance test_performance.cpp)
target_link_libraries(test_performance eshm)

add_executable(test_error_handling test_error_handling.cpp)
target_link_libraries(test_error_handling eshm)

add_executable(test_reconnect test_reconnect.cpp)
target_link_libraries(test_reconnect eshm)

# Add tests
add_test(NAME BasicTest COMMAND test_basic)
add_test(NAME ErrorHandlingTest COMMAND test_error_handling)
