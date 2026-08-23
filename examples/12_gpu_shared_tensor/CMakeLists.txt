# 12 - GPU VRAM sharing: a C++ producer writes a tensor straight into VRAM
# via cudaMemcpy; a Python consumer reads it back as a zero-copy cupy array.
# Needs the optional eshm_cuda module (include/eshm_cuda.h), which only
# exists when ESHM itself was configured with a CUDA toolkit available.
if(NOT TARGET ESHM::eshm)
    project(EshmGpuSharedTensor VERSION 1.1.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(ESHM 1.2 REQUIRED)
endif()

if(TARGET ESHM::eshm_cuda)
    find_package(CUDAToolkit REQUIRED)
    add_executable(gpu_tensor_producer producer.cpp)
    target_link_libraries(gpu_tensor_producer PRIVATE ESHM::eshm ESHM::eshm_cuda CUDA::cudart)

    add_executable(gpu_frame_bench gpu_frame_bench.cpp)
    target_link_libraries(gpu_frame_bench PRIVATE ESHM::eshm ESHM::eshm_cuda CUDA::cudart)
else()
    message(STATUS "examples/12_gpu_shared_tensor: eshm_cuda not built (no CUDA toolkit found), skipping")
endif()
