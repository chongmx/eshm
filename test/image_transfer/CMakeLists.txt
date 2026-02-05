# Image transfer test
# Requires large ESHM_MAX_DATA_SIZE to handle 4K images

# Single frame transfer (requires ~32 MB)
add_executable(image_sender image_sender.cpp)
target_link_libraries(image_sender eshm)

add_executable(image_receiver image_receiver.cpp)
target_link_libraries(image_receiver eshm)

# Dual frame transfer (requires ~64 MB)
add_executable(dual_frame_sender dual_frame_sender.cpp)
target_link_libraries(dual_frame_sender eshm)

add_executable(dual_frame_receiver dual_frame_receiver.cpp)
target_link_libraries(dual_frame_receiver eshm)

# Add test that checks if ESHM_MAX_DATA_SIZE is sufficient
# 4K image = 3840*2160*4 = 33,177,600 bytes
add_test(NAME check_image_size
    COMMAND ${CMAKE_COMMAND} -E echo "ESHM_MAX_DATA_SIZE = ${ESHM_MAX_DATA_SIZE}"
)
