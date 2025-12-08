# Data handler example
add_executable(example_data_handler example_data_handler.cpp)
target_link_libraries(example_data_handler eshm_data)

# Simple exchange example (ESHM + DataHandler)
add_executable(simple_exchange simple_exchange.cpp)
target_link_libraries(simple_exchange eshm eshm_data)

# Interoperability examples (C++ <-> Python)
add_executable(interop_cpp_master interop_cpp_master.cpp)
target_link_libraries(interop_cpp_master eshm eshm_data)

add_executable(interop_cpp_slave interop_cpp_slave.cpp)
target_link_libraries(interop_cpp_slave eshm eshm_data)
