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

# Self-test: forks a slave and drives it as master, so one command verifies the
# whole round trip. Also usable standalone: test_selftest master|slave <name>
add_executable(test_selftest functional/test_selftest.cpp)
target_link_libraries(test_selftest eshm)

# C API test: the ABI-stable surface used by the language bindings
add_executable(test_c_api functional/test_c_api.cpp)
target_link_libraries(test_c_api eshm eshm_data)

# Performance tests
add_executable(test_benchmark_master performance/test_benchmark_master.cpp)
target_link_libraries(test_benchmark_master eshm)

# Data Handler tests
add_executable(test_data_handler functional/test_data_handler.cpp)
target_link_libraries(test_data_handler eshm_data)

# Add tests
add_test(NAME SelfTest COMMAND test_selftest)
set_tests_properties(SelfTest PROPERTIES TIMEOUT 60)
add_test(NAME CApiTest COMMAND test_c_api)
set_tests_properties(CApiTest PROPERTIES TIMEOUT 60)
add_test(NAME BasicTest COMMAND test_basic)
add_test(NAME ErrorHandlingTest COMMAND test_error_handling)
add_test(NAME DataHandlerTest COMMAND test_data_handler)

# Python bindings against the C API (skipped when no interpreter is present)
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME PythonNativeApiTest
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/py/tests/test_native_api.py)
    set_tests_properties(PythonNativeApiTest PROPERTIES
        TIMEOUT 60
        ENVIRONMENT "ESHM_LIB=$<TARGET_FILE:eshm>")
endif()

# Image transfer test (requires large ESHM_MAX_DATA_SIZE)
add_subdirectory(image_transfer)
