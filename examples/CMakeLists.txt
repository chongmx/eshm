# Data handler example
add_executable(example_data_handler example_data_handler.cpp)
target_link_libraries(example_data_handler eshm_data)

# Simple exchange example (ESHM + DataHandler)
add_executable(simple_exchange simple_exchange.cpp)
target_link_libraries(simple_exchange eshm eshm_data)
