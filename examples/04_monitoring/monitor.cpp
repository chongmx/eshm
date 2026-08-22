// Monitoring: everything ESHM can tell you about a live channel.
//
// Demonstrates: eshm_get_stats and every field of ESHMStats, eshm_get_role,
//               eshm_check_remote_alive (and what it actually means),
//               eshm_update_heartbeat, eshm_error_string.
//
// Run:   ./channel_monitor watch    [channel]   # slave: attaches and reports
//        ./channel_monitor generate [channel]   # master: makes traffic to watch
//
// Pairs with `python3 peer.py generate|watch <channel>` in either direction.

#include <eshm.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace {
volatile sig_atomic_t g_running = 1;
void stop(int) { g_running = 0; }

const char* role_name(ESHMRole role) {
    switch (role) {
        case ESHM_ROLE_MASTER: return "MASTER";
        case ESHM_ROLE_SLAVE:  return "SLAVE";
        case ESHM_ROLE_AUTO:   return "AUTO";
    }
    return "?";
}

// One line per sample, so the numbers can be watched as they move.
void print_stats(ESHMHandle* handle, int sample) {
    ESHMStats stats;
    const int rc = eshm_get_stats(handle, &stats);
    if (rc != ESHM_SUCCESS) {
        std::printf("  stats unavailable: %s\n", eshm_error_string(rc));
        return;
    }

    // eshm_check_remote_alive() reports whether the peer has been detected as
    // STALE - it is true before anyone has ever attached. The *_alive flags
    // below are set on attach and cleared on destroy, so they answer "is a
    // peer actually there".
    bool remote_alive = false;
    eshm_check_remote_alive(handle, &remote_alive);

    ESHMRole role = ESHM_ROLE_AUTO;
    eshm_get_role(handle, &role);

    if (sample % 10 == 0) {
        std::printf("\n%-4s %-6s | %-8s %-8s | %-6s %-6s | %-9s %-9s | %s\n",
                    "#", "role", "m.beat", "s.beat", "m.pid", "s.pid",
                    "m2s w/r", "s2m w/r", "peer");
        std::printf("%s\n", std::string(92, '-').c_str());
    }

    std::printf("%-4d %-6s | %-8llu %-8llu | %-6d %-6d | %4llu/%-4llu %4llu/%-4llu | %s%s\n",
                sample, role_name(role),
                static_cast<unsigned long long>(stats.master_heartbeat),
                static_cast<unsigned long long>(stats.slave_heartbeat),
                static_cast<int>(stats.master_pid), static_cast<int>(stats.slave_pid),
                static_cast<unsigned long long>(stats.m2s_write_count),
                static_cast<unsigned long long>(stats.m2s_read_count),
                static_cast<unsigned long long>(stats.s2m_write_count),
                static_cast<unsigned long long>(stats.s2m_read_count),
                stats.master_alive ? "master " : "",
                stats.slave_alive ? "slave" : (remote_alive ? "(not stale)" : "gone"));

    // The deltas are the useful liveness signal: a heartbeat that stops
    // advancing between samples is a peer that has stopped, whatever the
    // alive flags still say. eshm_get_stats() resets them each call.
    std::printf("     heartbeat delta since last sample: master +%llu, slave +%llu"
                " (stale threshold %u)\n",
                static_cast<unsigned long long>(stats.master_heartbeat_delta),
                static_cast<unsigned long long>(stats.slave_heartbeat_delta),
                stats.stale_threshold);
    std::fflush(stdout);
}

int watch(const char* channel) {
    ESHMConfig config = eshm_default_config(channel);
    config.role = ESHM_ROLE_SLAVE;
    config.auto_cleanup = false;
    config.max_reconnect_attempts = 0;
    config.reconnect_wait_ms = 0;

    ESHMHandle* handle = nullptr;
    for (int i = 0; i < 50 && !handle; ++i) {
        handle = eshm_init(&config);
        if (!handle) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!handle) {
        std::fprintf(stderr, "monitor: no channel '%s' - is a generator running?\n", channel);
        return 1;
    }
    std::printf("monitor: watching '%s' (Ctrl-C to stop)\n", channel);

    for (int sample = 0; g_running; ++sample) {
        // Drain whatever is on the channel so the read counters move too.
        char buffer[ESHM_MAX_DATA_SIZE];
        size_t received = 0;
        while (eshm_read_ex(handle, buffer, sizeof(buffer), &received, 0) == ESHM_SUCCESS) {
            // discard: this example is about the counters, not the payload
        }

        print_stats(handle, sample);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::printf("\nmonitor: detaching\n");
    eshm_destroy(handle);
    return 0;
}

int generate(const char* channel) {
    ESHMConfig config = eshm_default_config(channel);
    config.role = ESHM_ROLE_MASTER;

    ESHMHandle* handle = eshm_init(&config);
    if (!handle) {
        std::fprintf(stderr, "generator: could not create channel '%s'\n", channel);
        return 1;
    }
    std::printf("generator: channel '%s' is live (Ctrl-C to stop)\n", channel);

    for (long i = 0; g_running; ++i) {
        char message[64];
        const int len = std::snprintf(message, sizeof(message), "tick %ld", i) + 1;
        eshm_write(handle, message, static_cast<size_t>(len));

        // Heartbeats are maintained by a dedicated thread when use_threads is
        // true (the default), so this call is a no-op here. It exists for
        // builds that set use_threads = false and drive the beat themselves.
        eshm_update_heartbeat(handle);

        if (i % 20 == 0) {
            std::printf("generator: %ld message(s) written\n", i + 1);
            std::fflush(stdout);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::printf("\ngenerator: closing channel\n");
    eshm_destroy(handle);
    return 0;
}
}

int main(int argc, char** argv) {
    const std::string mode = (argc > 1) ? argv[1] : "";
    const char* channel = (argc > 2) ? argv[2] : "monitored";

    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);

    if (mode == "watch")    return watch(channel);
    if (mode == "generate") return generate(channel);

    std::fprintf(stderr,
                 "usage: %s watch|generate [channel]\n\n"
                 "  generate   master role: creates the channel and makes traffic\n"
                 "  watch      slave role:  attaches and reports the statistics\n\n"
                 "Pairs with `python3 peer.py generate|watch <channel>`.\n",
                 argv[0]);
    return 2;
}
