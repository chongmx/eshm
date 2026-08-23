// Wire format shared by gpu_frame_bench.cpp and gpu_frame_drain.py.
//
// Layout of the VRAM buffer itself:
//
//     [ GpuFrameHeader ][ pixel bytes ][ uint64_t footer_seq ]
//
// eshm_cuda maps memory, it does not lock it - unlike an ordinary ESHM
// channel (sequence-lock protected, so a reader never sees a torn message),
// a VRAM buffer being cudaMemcpy'd by the producer while a consumer reads it
// can hand back a mix of an old and a new frame. footer_seq is a cheap way
// to catch that: it is written last, so header.seq == footer_seq means the
// whole frame landed as one coherent write; a mismatch means a torn read,
// visible to the consumer instead of silently wrong.

#ifndef ESHM_EXAMPLE_GPU_FRAME_FORMAT_H
#define ESHM_EXAMPLE_GPU_FRAME_FORMAT_H

#include <stdint.h>
#include <time.h>

#define GPU_FRAME_MAGIC 0x47465243u /* "GFRC" */

struct GpuFrameHeader {
    uint32_t magic;
    uint32_t cam_id;
    uint64_t seq;
    uint64_t stamp_ns;
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint32_t bytes; /* pixel bytes following this header */
};

static inline uint64_t gpu_bench_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

#endif /* ESHM_EXAMPLE_GPU_FRAME_FORMAT_H */
