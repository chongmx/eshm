// The C++ half of the robot loop: publishes state at the control rate,
// publishes camera frames, and consumes action chunks from the policy.
//
// Run:  ./robot_sim [--rate HZ] [--seconds S] [--cameras N]
//                   [--width W] [--height H] [--fps F] [--channel NAME]
//
// Pairs with `python3 policy.py`.

#include "robot_link.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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

// Percentile over a sorted-in-place sample vector.
double pct(std::vector<double>& v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t i = static_cast<size_t>(p * (v.size() - 1));
    return v[i];
}

struct Options {
    const char* channel = "robot";
    double rate_hz = 1000.0;
    double seconds = 5.0;
    int cameras = 2;
    uint32_t width = 640, height = 480, ch = 3;
    double fps = 30.0;
    bool spin = false;
};

void usage(const char* argv0) {
    std::fprintf(stderr,
        "usage: %s [options]\n\n"
        "  --rate HZ      control loop rate      (default 1000)\n"
        "  --seconds S    run duration           (default 5)\n"
        "  --cameras N    camera streams, 0 = none (default 2)\n"
        "  --width W      frame width            (default 640)\n"
        "  --height H     frame height           (default 480)\n"
        "  --fps F        frames per second      (default 30)\n"
        "  --channel NAME base channel name      (default \"robot\")\n",
        argv0);
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool has = (i + 1 < argc);
        if (a == "-h" || a == "--help")           { usage(argv[0]); return 0; }
        else if (a == "--rate" && has)            opt.rate_hz = std::atof(argv[++i]);
        else if (a == "--seconds" && has)         opt.seconds = std::atof(argv[++i]);
        else if (a == "--cameras" && has)         opt.cameras = std::atoi(argv[++i]);
        else if (a == "--width" && has)           opt.width = std::strtoul(argv[++i], nullptr, 10);
        else if (a == "--height" && has)          opt.height = std::strtoul(argv[++i], nullptr, 10);
        else if (a == "--fps" && has)             opt.fps = std::atof(argv[++i]);
        else if (a == "--channel" && has)         opt.channel = argv[++i];
        else if (a == "--spin")                   opt.spin = true;
        else { usage(argv[0]); return 2; }
    }

    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);

    const size_t frame_bytes = static_cast<size_t>(opt.width) * opt.height * opt.ch;
    const size_t frame_msg   = sizeof(FrameHeader) + frame_bytes;

    std::printf("robot: control %.0f Hz, %d camera(s) %ux%ux%u @ %.0f fps, %.0f s\n",
                opt.rate_hz, opt.cameras, opt.width, opt.height, opt.ch, opt.fps, opt.seconds);
    std::printf("robot: channel holds %d bytes; state %zu, action %zu, frame %zu\n",
                ESHM_MAX_DATA_SIZE, sizeof(RobotState), sizeof(ActionChunk), frame_msg);

    if (opt.cameras > 0 && frame_msg > (size_t)ESHM_MAX_DATA_SIZE) {
        std::fprintf(stderr,
            "\nrobot: a %zu-byte frame does not fit in a %d-byte channel.\n"
            "       Rebuild with a channel at least that big, e.g.\n"
            "         cmake -S . -B build-robot -DESHM_MAX_DATA_SIZE=%zu\n"
            "       or use a smaller --width/--height.\n",
            frame_msg, ESHM_MAX_DATA_SIZE,
            (size_t)1 << (size_t)std::ceil(std::log2((double)frame_msg)));
        return 1;
    }

    // One bidirectional channel carries the control loop: the robot writes
    // state into master->slave, the policy writes actions into slave->master.
    // Cameras get a channel each, so a frame never overwrites a state sample.
    ESHMConfig cfg = eshm_default_config(opt.channel);
    cfg.role = ESHM_ROLE_MASTER;
    ESHMHandle* ctrl = eshm_init(&cfg);
    if (!ctrl) { std::fprintf(stderr, "robot: cannot create '%s'\n", opt.channel); return 1; }

    std::vector<ESHMHandle*> cams;
    std::vector<std::string> cam_names;
    for (int c = 0; c < opt.cameras; ++c) {
        cam_names.push_back(std::string(opt.channel) + "_cam" + std::to_string(c));
        ESHMConfig ccfg = eshm_default_config(cam_names.back().c_str());
        ccfg.role = ESHM_ROLE_MASTER;
        ESHMHandle* h = eshm_init(&ccfg);
        if (!h) { std::fprintf(stderr, "robot: cannot create '%s'\n", cam_names.back().c_str()); return 1; }
        cams.push_back(h);
    }

    // Take the read baseline before the policy can write, so the first action
    // chunk is delivered rather than swallowed as the baseline.
    ActionChunk discard;
    size_t ignored = 0;
    eshm_read_ex(ctrl, &discard, sizeof(discard), &ignored, 0);

    std::printf("robot: waiting for a policy...\n");
    std::fflush(stdout);
    for (int i = 0; g_running && i < 300; ++i) {
        ESHMStats st;
        if (eshm_get_stats(ctrl, &st) == ESHM_SUCCESS && st.slave_alive) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::printf("robot: policy attached, running\n\n");
    std::fflush(stdout);

    // A recognisable frame; the content does not matter, the size does.
    std::vector<uint8_t> pixels(frame_bytes);
    for (size_t i = 0; i < frame_bytes; ++i) pixels[i] = static_cast<uint8_t>(i);
    std::vector<uint8_t> frame_msg_buf(frame_msg);

    const uint64_t period_ns = static_cast<uint64_t>(1e9 / opt.rate_hz);
    const uint64_t frame_period_ns = (opt.fps > 0) ? static_cast<uint64_t>(1e9 / opt.fps) : 0;

    const uint64_t t0 = robot_now_ns();
    const uint64_t t_end = t0 + static_cast<uint64_t>(opt.seconds * 1e9);
    uint64_t next_tick = t0;
    uint64_t next_frame = t0;

    uint64_t state_seq = 0, frame_seq = 0;
    uint64_t chunks = 0, stale_chunks = 0;
    uint64_t last_action_seq = 0;

    std::vector<double> jitter_us;      // |actual tick - scheduled tick|
    std::vector<double> loop_ms;        // closed-loop latency per chunk
    std::vector<double> action_age_ms;  // how old a chunk was when we saw it
    jitter_us.reserve(static_cast<size_t>(opt.rate_hz * opt.seconds) + 64);

    while (g_running) {
        const uint64_t now = robot_now_ns();
        if (now >= t_end) break;

        if (now >= next_tick) {
            // ---- publish state -------------------------------------------
            RobotState st;
            std::memset(&st, 0, sizeof(st));
            st.seq = ++state_seq;
            const double phase = state_seq * 0.001;
            for (int j = 0; j < ROBOT_JOINTS; ++j) {
                st.joint_pos[j] = std::sin(phase + j);
                st.joint_vel[j] = std::cos(phase + j);
            }
            st.stamp_ns = robot_now_ns();     // stamp as late as possible
            eshm_write(ctrl, &st, sizeof(st));

            jitter_us.push_back(static_cast<double>(now - next_tick) / 1000.0);
            next_tick += period_ns;
            // If we fell far behind, resynchronise rather than spiral.
            if (next_tick < now) next_tick = now + period_ns;
        }

        // ---- publish camera frames ---------------------------------------
        if (frame_period_ns && now >= next_frame && !cams.empty()) {
            ++frame_seq;
            for (size_t c = 0; c < cams.size(); ++c) {
                FrameHeader fh;
                fh.magic = ROBOT_FRAME_MAGIC;
                fh.cam_id = static_cast<uint32_t>(c);
                fh.seq = frame_seq;
                fh.width = opt.width; fh.height = opt.height; fh.channels = opt.ch;
                fh.bytes = static_cast<uint32_t>(frame_bytes);
                fh.stamp_ns = robot_now_ns();
                std::memcpy(frame_msg_buf.data(), &fh, sizeof(fh));
                std::memcpy(frame_msg_buf.data() + sizeof(fh), pixels.data(), frame_bytes);
                eshm_write(cams[c], frame_msg_buf.data(), frame_msg);
            }
            next_frame += frame_period_ns;
            if (next_frame < now) next_frame = now + frame_period_ns;
        }

        // ---- consume action chunks ---------------------------------------
        // Non-blocking: the control loop must never wait on the policy.
        ActionChunk chunk;
        size_t got = 0;
        if (eshm_read_ex(ctrl, &chunk, sizeof(chunk), &got, 0) == ESHM_SUCCESS &&
            got == sizeof(chunk)) {
            const uint64_t seen = robot_now_ns();
            ++chunks;
            if (chunk.state_stamp_ns && seen > chunk.state_stamp_ns) {
                loop_ms.push_back(static_cast<double>(seen - chunk.state_stamp_ns) / 1e6);
            }
            if (chunk.stamp_ns && seen > chunk.stamp_ns) {
                action_age_ms.push_back(static_cast<double>(seen - chunk.stamp_ns) / 1e6);
            }
            if (last_action_seq && chunk.seq > last_action_seq + 1) {
                stale_chunks += chunk.seq - last_action_seq - 1;
            }
            last_action_seq = chunk.seq;
        }

        // Sleep until the next thing is due, but never past ~200 us, so the
        // 1 kHz case stays tight without spinning a core flat out.
        const uint64_t after = robot_now_ns();
        uint64_t due = next_tick;
        if (frame_period_ns && !cams.empty()) due = std::min(due, next_frame);
        if (due > after) {
            const uint64_t remaining = due - after;
            if (opt.spin && remaining < 200000ull) {
                // Busy-wait the last stretch. Sleeping into a deadline costs
                // whatever the scheduler feels like adding; a real high-rate
                // control loop spends a little CPU to avoid that, and the
                // jitter numbers below show the difference plainly.
                while (robot_now_ns() < due) { /* spin */ }
            } else {
                std::this_thread::sleep_for(
                    std::chrono::nanoseconds(std::min<uint64_t>(remaining, 200000ull)));
            }
        }
    }

    const double elapsed = static_cast<double>(robot_now_ns() - t0) / 1e9;

    std::printf("=== robot side ===\n");
    std::printf("  control       target %.0f Hz, achieved %.1f Hz  (%llu ticks in %.2f s)\n",
                opt.rate_hz, state_seq / elapsed,
                static_cast<unsigned long long>(state_seq), elapsed);
    if (!jitter_us.empty()) {
        std::printf("  tick jitter   p50 %.1f us   p99 %.1f us   max %.1f us\n",
                    pct(jitter_us, 0.50), pct(jitter_us, 0.99), pct(jitter_us, 1.0));
    }
    if (!cams.empty()) {
        const double fps_actual = frame_seq / elapsed;
        std::printf("  cameras       %zu x %.1f fps, %.1f MB/s total\n",
                    cams.size(), fps_actual,
                    cams.size() * fps_actual * frame_bytes / 1e6);
    }
    std::printf("  action chunks %llu received (%.1f Hz), %llu superseded before we read them\n",
                static_cast<unsigned long long>(chunks), chunks / elapsed,
                static_cast<unsigned long long>(stale_chunks));
    if (!loop_ms.empty()) {
        std::printf("  CLOSED LOOP   p50 %.2f ms   p99 %.2f ms   max %.2f ms\n",
                    pct(loop_ms, 0.50), pct(loop_ms, 0.99), pct(loop_ms, 1.0));
        std::printf("                (state written -> policy read -> inferred -> action read)\n");
    }
    if (!action_age_ms.empty()) {
        std::printf("  action age    p50 %.2f ms   p99 %.2f ms   (transport only)\n",
                    pct(action_age_ms, 0.50), pct(action_age_ms, 0.99));
    }
    std::fflush(stdout);

    for (ESHMHandle* h : cams) eshm_destroy(h);
    eshm_destroy(ctrl);
    return chunks > 0 ? 0 : 1;
}
