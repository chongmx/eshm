// One-directional frame streaming throughput - the camera case, isolated.
//
// robot_sim measures a whole control loop; this measures just how much pixel
// data a channel will carry, which is the question when you are sizing a
// multi-camera rig.
//
// Two numbers matter and they are not the same:
//
//   send rate      how fast the publisher can push frames in (memcpy bound)
//   delivered rate how much the consumer actually got out
//
// They differ because the channel is latest-value: a publisher running flat out
// will overwrite frames the consumer has not collected. That is not a fault, it
// is the contract - but it means "MB/s written" alone tells you nothing about
// what arrived.
//
// Run:  ./frame_bench send [--width W] [--height H] [--channels C]
//                          [--fps F] [--seconds S] [--channel NAME]
//       ./frame_bench recv [same options]
//
// --fps 0 means flat out. Pairs with `python3 frame_drain.py` for the
// C++ -> Python direction.

#include "robot_link.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

volatile sig_atomic_t g_running = 1;
void stop(int) { g_running = 0; }

struct Options {
    const char* channel = "framebench";
    uint32_t width = 1280, height = 720, ch = 3;
    double fps = 0.0;          // 0 = flat out
    double seconds = 5.0;
};

double pct(std::vector<double>& v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(p * (v.size() - 1))];
}

int run_send(const Options& opt) {
    const size_t frame_bytes = (size_t)opt.width * opt.height * opt.ch;
    const size_t msg_bytes = sizeof(FrameHeader) + frame_bytes;

    if (msg_bytes > (size_t)ESHM_MAX_DATA_SIZE) {
        std::fprintf(stderr,
            "send: a %zu-byte frame does not fit in a %d-byte channel.\n"
            "      Rebuild with -DESHM_MAX_DATA_SIZE=%zu or larger.\n",
            msg_bytes, ESHM_MAX_DATA_SIZE, (size_t)1 << (size_t)std::ceil(std::log2((double)msg_bytes)));
        return 1;
    }

    ESHMConfig cfg = eshm_default_config(opt.channel);
    cfg.role = ESHM_ROLE_MASTER;
    ESHMHandle* h = eshm_init(&cfg);
    if (!h) { std::fprintf(stderr, "send: cannot create '%s'\n", opt.channel); return 1; }

    std::printf("send: %ux%ux%u = %.2f MB/frame, %s, %.0f s\n",
                opt.width, opt.height, opt.ch, frame_bytes / 1e6,
                opt.fps > 0 ? "paced" : "flat out", opt.seconds);
    std::fflush(stdout);

    for (int i = 0; g_running && i < 300; ++i) {
        ESHMStats st;
        if (eshm_get_stats(h, &st) == ESHM_SUCCESS && st.slave_alive) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::vector<uint8_t> msg(msg_bytes);
    for (size_t i = 0; i < frame_bytes; ++i)
        msg[sizeof(FrameHeader) + i] = static_cast<uint8_t>(i);

    const uint64_t period_ns = (opt.fps > 0) ? (uint64_t)(1e9 / opt.fps) : 0;
    const uint64_t t0 = robot_now_ns();
    const uint64_t t_end = t0 + (uint64_t)(opt.seconds * 1e9);
    uint64_t next = t0;
    uint64_t sent = 0;
    std::vector<double> write_us;
    write_us.reserve(100000);

    while (g_running && robot_now_ns() < t_end) {
        if (period_ns) {
            const uint64_t now = robot_now_ns();
            if (now < next) {
                std::this_thread::sleep_for(std::chrono::nanoseconds(
                    std::min<uint64_t>(next - now, 500000ull)));
                continue;
            }
            next += period_ns;
            if (next < now) next = now + period_ns;
        }

        FrameHeader fh;
        fh.magic = ROBOT_FRAME_MAGIC;
        fh.cam_id = 0;
        fh.seq = ++sent;
        fh.width = opt.width; fh.height = opt.height; fh.channels = opt.ch;
        fh.bytes = (uint32_t)frame_bytes;
        fh.stamp_ns = robot_now_ns();
        std::memcpy(msg.data(), &fh, sizeof(fh));

        const uint64_t w0 = robot_now_ns();
        eshm_write(h, msg.data(), msg_bytes);
        write_us.push_back((robot_now_ns() - w0) / 1000.0);
    }

    const double elapsed = (robot_now_ns() - t0) / 1e9;
    const double mbs = sent * msg_bytes / elapsed / 1e6;

    std::printf("\n=== send ===\n");
    std::printf("  frames written  %llu in %.2f s  (%.1f fps)\n",
                (unsigned long long)sent, elapsed, sent / elapsed);
    std::printf("  SEND RATE       %.0f MB/s  (%.2f Gbps)\n", mbs, mbs * 8 / 1000.0);
    std::printf("  per write       p50 %.0f us   p99 %.0f us   max %.0f us\n",
                pct(write_us, 0.50), pct(write_us, 0.99), pct(write_us, 1.0));
    std::fflush(stdout);

    eshm_destroy(h);
    return 0;
}

int run_recv(const Options& opt) {
    const size_t frame_bytes = (size_t)opt.width * opt.height * opt.ch;
    const size_t msg_bytes = sizeof(FrameHeader) + frame_bytes;

    ESHMConfig cfg = eshm_default_config(opt.channel);
    cfg.role = ESHM_ROLE_SLAVE;
    cfg.auto_cleanup = false;
    cfg.max_reconnect_attempts = 0;
    cfg.reconnect_wait_ms = 0;

    ESHMHandle* h = nullptr;
    for (int i = 0; i < 200 && !h; ++i) {
        h = eshm_init(&cfg);
        if (!h) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!h) { std::fprintf(stderr, "recv: no channel '%s'\n", opt.channel); return 1; }

    std::vector<uint8_t> buf(msg_bytes);
    size_t got = 0;
    eshm_read_ex(h, buf.data(), buf.size(), &got, 0);   // prime the baseline

    std::printf("recv: attached, draining\n");
    std::fflush(stdout);

    const uint64_t t0 = robot_now_ns();
    const uint64_t t_end = t0 + (uint64_t)((opt.seconds + 1.0) * 1e9);
    uint64_t frames = 0, first_seq = 0, last_seq = 0;
    std::vector<double> read_us, age_ms;
    read_us.reserve(100000);

    bool seen = false;
    while (g_running && robot_now_ns() < t_end) {
        const uint64_t r0 = robot_now_ns();
        const int rc = eshm_read_ex(h, buf.data(), buf.size(), &got, 200);
        if (rc != ESHM_SUCCESS || got < sizeof(FrameHeader)) {
            bool alive = false;
            eshm_check_remote_alive(h, &alive);
            if (seen && !alive) break;
            continue;
        }
        const uint64_t r1 = robot_now_ns();

        FrameHeader fh;
        std::memcpy(&fh, buf.data(), sizeof(fh));
        if (fh.magic != ROBOT_FRAME_MAGIC) continue;

        seen = true;
        ++frames;
        if (!first_seq) first_seq = fh.seq;
        last_seq = fh.seq;
        read_us.push_back((r1 - r0) / 1000.0);
        if (r1 > fh.stamp_ns) age_ms.push_back((r1 - fh.stamp_ns) / 1e6);
    }

    const double elapsed = (robot_now_ns() - t0) / 1e9;
    const double mbs = frames * msg_bytes / elapsed / 1e6;
    const uint64_t span = (last_seq >= first_seq) ? (last_seq - first_seq + 1) : 0;

    std::printf("\n=== recv ===\n");
    std::printf("  frames read     %llu of %llu published while we listened\n",
                (unsigned long long)frames, (unsigned long long)span);
    std::printf("  DELIVERED RATE  %.0f MB/s  (%.2f Gbps)\n", mbs, mbs * 8 / 1000.0);
    if (span) {
        std::printf("  dropped         %.1f%%  (superseded before we read them)\n",
                    100.0 * (span - frames) / span);
    }
    std::printf("  per read        p50 %.0f us   p99 %.0f us\n",
                pct(read_us, 0.50), pct(read_us, 0.99));
    if (!age_ms.empty()) {
        std::printf("  frame age       p50 %.2f ms   p99 %.2f ms\n",
                    pct(age_ms, 0.50), pct(age_ms, 0.99));
    }
    std::fflush(stdout);

    eshm_destroy(h);
    return frames > 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    const std::string mode = (argc > 1) ? argv[1] : "";
    Options opt;

    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        const bool has = (i + 1 < argc);
        if (a == "--width" && has)         opt.width = std::strtoul(argv[++i], nullptr, 10);
        else if (a == "--height" && has)   opt.height = std::strtoul(argv[++i], nullptr, 10);
        else if (a == "--channels" && has) opt.ch = std::strtoul(argv[++i], nullptr, 10);
        else if (a == "--fps" && has)      opt.fps = std::atof(argv[++i]);
        else if (a == "--seconds" && has)  opt.seconds = std::atof(argv[++i]);
        else if (a == "--channel" && has)  opt.channel = argv[++i];
    }

    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);

    if (mode == "send") return run_send(opt);
    if (mode == "recv") return run_recv(opt);

    std::fprintf(stderr,
        "usage: %s send|recv [--width W] [--height H] [--channels C]\n"
        "                    [--fps F] [--seconds S] [--channel NAME]\n\n"
        "  --fps 0 (default) publishes flat out; any other value paces.\n"
        "  Channel must be built large enough: -DESHM_MAX_DATA_SIZE=...\n",
        argv[0]);
    return 2;
}
