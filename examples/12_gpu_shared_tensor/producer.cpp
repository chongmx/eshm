// C++ side of example 12 - GPU VRAM sharing.
//
// Owns two things:
//   the VRAM buffer     eshm_cuda_create("gpu_frame", N * sizeof(float))
//   the control channel  eshm_rpc_create("gpu_frame") -> "gpu_frame_ctl"
//
// Same pattern as example 10 (named triggers), except the payload the
// trigger announces lives in VRAM instead of a host ESHM channel:
//
//     write into VRAM  ->  sync the stream  ->  fire the trigger
//
// eshm_cuda has no idea when a cudaMemcpy/kernel has finished - it only maps
// memory, it does not order it across processes. The explicit
// cudaDeviceSynchronize() before eshm_rpc_call() is what makes "the trigger
// fired" mean "the data is actually there"; skip it and a Python consumer
// could observe a partial write.
//
// Run:   ./gpu_tensor_producer [name] [rounds]
// Pairs with: python3 peer.py consume [name]

#include <eshm_cuda.h>
#include <eshm_rpc.h>

#include <cuda_runtime.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

namespace {

volatile sig_atomic_t g_running = 1;
void stop(int) { g_running = 0; }

constexpr int N = 256;  // elements in the shared float32 "tensor"

}  // namespace

int main(int argc, char** argv) {
    const char* name = (argc > 1) ? argv[1] : "gpu_frame";
    const int rounds = (argc > 2) ? std::atoi(argv[2]) : 20;

    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);

    EshmCudaConfig cuda_config = eshm_cuda_default_config(name, N * sizeof(float));
    EshmCudaBuffer* buf = eshm_cuda_create(&cuda_config);
    if (!buf) {
        std::fprintf(stderr, "producer: %s\n", eshm_cuda_get_last_error());
        return 1;
    }

    void* devptr = nullptr;
    size_t size = 0;
    eshm_cuda_get_ptr(buf, &devptr, &size);
    std::printf("producer: %d float32s (%zu bytes) live in VRAM on device %d, generation %llu\n",
                N, size, eshm_cuda_device(buf),
                static_cast<unsigned long long>(eshm_cuda_generation(buf)));

    EshmRpc* rpc = eshm_rpc_create(name, ESHM_ROLE_MASTER);
    if (!rpc) {
        std::fprintf(stderr, "producer: %s\n", eshm_rpc_get_last_error());
        eshm_cuda_destroy(buf);
        return 1;
    }
    eshm_rpc_start(rpc);

    std::printf("producer: buffer '%s', triggers on '%s_ctl'\n", name, name);
    std::fflush(stdout);

    std::vector<float> host(N);
    int rounds_done = 0;
    for (int round = 0; g_running && round < rounds; ++round) {
        for (int i = 0; i < N; ++i) host[i] = static_cast<float>(round * 1000 + i);

        cudaError_t cerr = cudaMemcpy(devptr, host.data(), size, cudaMemcpyHostToDevice);
        if (cerr != cudaSuccess) {
            std::fprintf(stderr, "producer: cudaMemcpy failed: %s\n", cudaGetErrorString(cerr));
            break;
        }
        cudaDeviceSynchronize();

        std::printf("-> round %d: wrote [%.0f .. %.0f] into VRAM, firing 'frame_ready'\n",
                    round, host.front(), host.back());
        std::fflush(stdout);
        eshm_rpc_call(rpc, "frame_ready");
        ++rounds_done;

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    eshm_rpc_emit(rpc, "shutting_down");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::printf("producer: done, %d round(s) sent, %llu trigger(s) dispatched here\n",
                rounds_done, static_cast<unsigned long long>(eshm_rpc_dispatched(rpc)));

    eshm_rpc_destroy(rpc);
    eshm_cuda_destroy(buf);
    return rounds_done > 0 ? 0 : 1;
}
