// GPU VRAM frame-streaming throughput - the camera case, but the pixels never
// leave the device. Mirrors examples/11_robot_loop/frame_bench.cpp; the
// question is the same ("how much pixel data will this carry"), the transport
// is not: a channel there is host RAM, seqlock-protected, bounded by
// ESHM_MAX_DATA_SIZE; here it is a VRAM allocation eshm_cuda maps directly
// into both processes, with a tiny host-channel trigger to say "look".
//
// Two numbers matter and they are not the same, same as the host version:
//
//   send rate      how fast the producer can push frames in (cudaMemcpy bound)
//   delivered rate how many distinct frames the consumer actually dispatched
//
// A third number does NOT exist for a host channel: torn rate. eshm_cuda maps
// memory, it does not lock it, so a producer's cudaMemcpy racing a consumer's
// read can hand back a mix of two frames. See frame_format.h for the cheap
// header/footer check this benchmark uses to catch that, and this directory's
// README for the double-buffering fix once torn rate is non-zero for you.
//
// Run:  ./gpu_frame_bench send [--width W] [--height H] [--channels C]
//                              [--fps F] [--seconds S] [--name NAME]
//       ./gpu_frame_bench recv [same options]        (C++ -> C++ ceiling)
//
// --fps 0 means flat out. Pairs with `python3 gpu_frame_drain.py` for the
// C++ -> Python number, which is the one that matters when Python is the
// consumer.

#include "frame_format.h"

#include <eshm_cuda.h>
#include <eshm_rpc.h>

#include <cuda_runtime.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <string>
#include <thread>
#include <vector>

namespace {

volatile sig_atomic_t g_running = 1;
void stop(int) { g_running = 0; }

struct Options {
    const char* name = "gpu_framebench";
    uint32_t width = 1280, height = 720, ch = 3;
    double fps = 0.0;  // 0 = flat out
    double seconds = 5.0;
};

double pct(std::vector<double>& v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(p * (v.size() - 1))];
}

int run_send(const Options& opt) {
    const size_t frame_bytes = (size_t)opt.width * opt.height * opt.ch;
    const size_t total_bytes = sizeof(GpuFrameHeader) + frame_bytes + sizeof(uint64_t);

    EshmCudaConfig cuda_config = eshm_cuda_default_config(opt.name, total_bytes);
    EshmCudaBuffer* buf = eshm_cuda_create(&cuda_config);
    if (!buf) {
        std::fprintf(stderr, "send: %s\n", eshm_cuda_get_last_error());
        return 1;
    }
    void* devptr = nullptr;
    size_t mapped_size = 0;
    eshm_cuda_get_ptr(buf, &devptr, &mapped_size);

    EshmRpc* rpc = eshm_rpc_create(opt.name, ESHM_ROLE_MASTER);
    if (!rpc) {
        std::fprintf(stderr, "send: %s\n", eshm_rpc_get_last_error());
        eshm_cuda_destroy(buf);
        return 1;
    }
    eshm_rpc_start(rpc);

    std::printf("send: %ux%ux%u = %.2f MB/frame in VRAM (device %d), %s, %.0f s\n",
                opt.width, opt.height, opt.ch, frame_bytes / 1e6, eshm_cuda_device(buf),
                opt.fps > 0 ? "paced" : "flat out", opt.seconds);
    std::fflush(stdout);

    std::vector<uint8_t> msg(total_bytes);
    for (size_t i = 0; i < frame_bytes; ++i)
        msg[sizeof(GpuFrameHeader) + i] = static_cast<uint8_t>(i);

    const uint64_t period_ns = (opt.fps > 0) ? (uint64_t)(1e9 / opt.fps) : 0;
    const uint64_t t0 = gpu_bench_now_ns();
    const uint64_t t_end = t0 + (uint64_t)(opt.seconds * 1e9);
    uint64_t next = t0;
    uint64_t sent = 0;
    std::vector<double> write_us;
    write_us.reserve(200000);

    while (g_running && gpu_bench_now_ns() < t_end) {
        if (period_ns) {
            const uint64_t now = gpu_bench_now_ns();
            if (now < next) {
                std::this_thread::sleep_for(std::chrono::nanoseconds(
                    std::min<uint64_t>(next - now, 500000ull)));
                continue;
            }
            next += period_ns;
            if (next < now) next = now + period_ns;
        }

        GpuFrameHeader fh;
        fh.magic = GPU_FRAME_MAGIC;
        fh.cam_id = 0;
        fh.seq = ++sent;
        fh.width = opt.width; fh.height = opt.height; fh.channels = opt.ch;
        fh.bytes = (uint32_t)frame_bytes;
        fh.stamp_ns = gpu_bench_now_ns();
        std::memcpy(msg.data(), &fh, sizeof(fh));
        std::memcpy(msg.data() + sizeof(fh) + frame_bytes, &fh.seq, sizeof(fh.seq));

        const uint64_t w0 = gpu_bench_now_ns();
        cudaMemcpy(devptr, msg.data(), total_bytes, cudaMemcpyHostToDevice);
        cudaDeviceSynchronize();
        write_us.push_back((gpu_bench_now_ns() - w0) / 1000.0);

        eshm_rpc_call(rpc, "frame_ready");
    }

    const double elapsed = (gpu_bench_now_ns() - t0) / 1e9;
    const double mbs = sent * total_bytes / elapsed / 1e6;

    std::printf("\n=== send ===\n");
    std::printf("  frames written  %llu in %.2f s  (%.1f fps)\n",
                (unsigned long long)sent, elapsed, sent / elapsed);
    std::printf("  SEND RATE       %.0f MB/s  (%.2f Gbps)\n", mbs, mbs * 8 / 1000.0);
    std::printf("  per write       p50 %.0f us   p99 %.0f us   max %.0f us"
                "   (cudaMemcpy H2D + cudaDeviceSynchronize)\n",
                pct(write_us, 0.50), pct(write_us, 0.99), pct(write_us, 1.0));
    std::printf("  triggers dispatched here (peer -> us): %llu\n",
                (unsigned long long)eshm_rpc_dispatched(rpc));
    std::fflush(stdout);

    eshm_rpc_destroy(rpc);
    eshm_cuda_destroy(buf);
    return sent > 0 ? 0 : 1;
}

int run_recv(const Options& opt) {
    const size_t frame_bytes = (size_t)opt.width * opt.height * opt.ch;
    const size_t total_bytes = sizeof(GpuFrameHeader) + frame_bytes + sizeof(uint64_t);

    EshmCudaBuffer* buf = eshm_cuda_attach(opt.name, 20000);
    if (!buf) {
        std::fprintf(stderr, "recv: %s\n", eshm_cuda_get_last_error());
        return 1;
    }
    void* devptr = nullptr;
    size_t mapped_size = 0;
    eshm_cuda_get_ptr(buf, &devptr, &mapped_size);

    EshmRpc* rpc = eshm_rpc_create(opt.name, ESHM_ROLE_SLAVE);
    if (!rpc) {
        std::fprintf(stderr, "recv: %s\n", eshm_rpc_get_last_error());
        eshm_cuda_destroy(buf);
        return 1;
    }

    struct Stats {
        std::atomic<uint64_t> frames{0};
        std::atomic<uint64_t> torn{0};
        uint64_t first_seq = 0, last_seq = 0;
        std::vector<double> age_ms;
        std::vector<double> read_us;
    } stats;
    stats.age_ms.reserve(200000);
    stats.read_us.reserve(200000);

    std::vector<uint8_t> host(total_bytes);

    struct Ctx { Stats* stats; void* devptr; size_t total_bytes; size_t frame_bytes;
                 std::vector<uint8_t>* host; };
    Ctx ctx{&stats, devptr, total_bytes, frame_bytes, &host};

    eshm_rpc_on_call(rpc, "frame_ready", [](void* user) {
        auto* c = static_cast<Ctx*>(user);
        const uint64_t r0 = gpu_bench_now_ns();
        cudaMemcpy(c->host->data(), c->devptr, c->total_bytes, cudaMemcpyDeviceToHost);
        const uint64_t r1 = gpu_bench_now_ns();

        GpuFrameHeader fh;
        std::memcpy(&fh, c->host->data(), sizeof(fh));
        if (fh.magic != GPU_FRAME_MAGIC) return;

        uint64_t footer = 0;
        std::memcpy(&footer, c->host->data() + sizeof(fh) + c->frame_bytes, sizeof(footer));

        auto* s = c->stats;
        if (footer != fh.seq) { s->torn.fetch_add(1); return; }

        s->frames.fetch_add(1);
        if (!s->first_seq) s->first_seq = fh.seq;
        s->last_seq = fh.seq;
        s->read_us.push_back((r1 - r0) / 1000.0);
        if (r1 > fh.stamp_ns) s->age_ms.push_back((r1 - fh.stamp_ns) / 1e6);
    }, &ctx);

    eshm_rpc_start(rpc);
    std::printf("recv: attached (device %d), draining\n", eshm_cuda_device(buf));
    std::fflush(stdout);

    const uint64_t t0 = gpu_bench_now_ns();
    const uint64_t t_end = t0 + (uint64_t)((opt.seconds + 1.0) * 1e9);
    while (g_running && gpu_bench_now_ns() < t_end) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    const double elapsed = (gpu_bench_now_ns() - t0) / 1e9;
    const uint64_t frames = stats.frames.load();
    const double mbs = frames * total_bytes / elapsed / 1e6;
    const uint64_t span = (stats.last_seq >= stats.first_seq)
                               ? (stats.last_seq - stats.first_seq + 1) : 0;

    std::printf("\n=== recv ===\n");
    std::printf("  frames read     %llu of %llu published while we listened\n",
                (unsigned long long)frames, (unsigned long long)span);
    std::printf("  DELIVERED RATE  %.0f MB/s  (%.2f Gbps)\n", mbs, mbs * 8 / 1000.0);
    if (span) {
        std::printf("  coalesced       %.1f%%  (superseded before a trigger dispatched)\n",
                    100.0 * (span - frames - stats.torn.load()) / span);
    }
    std::printf("  torn            %llu  (%.2f%% of dispatched - header/footer seq mismatch)\n",
                (unsigned long long)stats.torn.load(),
                (frames + stats.torn.load()) ? 100.0 * stats.torn.load() / (frames + stats.torn.load()) : 0.0);
    std::printf("  per read        p50 %.0f us   p99 %.0f us   (cudaMemcpy D2H, whole frame)\n",
                pct(stats.read_us, 0.50), pct(stats.read_us, 0.99));
    if (!stats.age_ms.empty()) {
        std::printf("  frame age       p50 %.2f ms   p99 %.2f ms\n",
                    pct(stats.age_ms, 0.50), pct(stats.age_ms, 0.99));
    }
    std::fflush(stdout);

    eshm_rpc_destroy(rpc);
    eshm_cuda_destroy(buf);
    return frames > 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string mode = (argc > 1) ? argv[1] : "";
    Options opt;

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--width") opt.width = (uint32_t)std::atoi(next());
        else if (a == "--height") opt.height = (uint32_t)std::atoi(next());
        else if (a == "--channels") opt.ch = (uint32_t)std::atoi(next());
        else if (a == "--fps") opt.fps = std::atof(next());
        else if (a == "--seconds") opt.seconds = std::atof(next());
        else if (a == "--name") opt.name = argv[++i];
    }

    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);

    if (mode == "send") return run_send(opt);
    if (mode == "recv") return run_recv(opt);

    std::fprintf(stderr,
        "usage: %s send|recv [--width W] [--height H] [--channels C] "
        "[--fps F] [--seconds S] [--name NAME]\n", argv[0]);
    return 2;
}
