// Throughput benchmark. Measures round-trip message rate between two peers,
// in any language combination.
//
// The driver (master) sends a message and waits for the echo before sending
// the next, so the number reported is complete round trips per second - not
// one-way writes, which are much faster and much less informative.
//
// Demonstrates: how the channel behaves under load, and what the language and
//               codec choice on the far end actually costs.
//
// Run:   ./bench drive [channel] [--seconds S] [--size N]
//        ./bench echo  [channel]
//
// Pairs with `python3 bench.py drive|echo <channel>` in either direction.

#include <eshm.h>

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

int drive(const char* channel, double seconds, size_t size) {
    ESHMConfig config = eshm_default_config(channel);
    config.role = ESHM_ROLE_MASTER;

    ESHMHandle* handle = eshm_init(&config);
    if (!handle) {
        std::fprintf(stderr, "bench: could not create channel '%s'\n", channel);
        return 1;
    }
    std::printf("bench: channel '%s' is live, waiting for an echo peer...\n", channel);
    std::fflush(stdout);

    for (int i = 0; g_running && i < 300; ++i) {
        ESHMStats stats;
        if (eshm_get_stats(handle, &stats) == ESHM_SUCCESS && stats.slave_alive) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (size > ESHM_MAX_DATA_SIZE) size = ESHM_MAX_DATA_SIZE;
    std::printf("bench: %zu byte payload, %.0f s\n\n", size, seconds);

    std::vector<uint8_t> payload(size, 0xA5);
    std::vector<uint8_t> reply(ESHM_MAX_DATA_SIZE);

    uint64_t round_trips = 0, dropped = 0;
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(seconds));

    while (g_running && std::chrono::steady_clock::now() < deadline) {
        // Sequence number in the first 8 bytes, so a late echo is detectable.
        std::memcpy(payload.data(), &round_trips, sizeof(round_trips));

        if (eshm_write(handle, payload.data(), payload.size()) != ESHM_SUCCESS) break;

        size_t received = 0;
        const int rc = eshm_read_ex(handle, reply.data(), reply.size(), &received, 1000);
        if (rc != ESHM_SUCCESS) { ++dropped; continue; }

        ++round_trips;
    }

    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();

    std::printf("round trips   %llu\n", static_cast<unsigned long long>(round_trips));
    std::printf("elapsed       %.2f s\n", elapsed);
    std::printf("rate          %.0f round trips/s\n", round_trips / elapsed);
    std::printf("latency       %.1f us per round trip\n", elapsed / round_trips * 1e6);
    std::printf("throughput    %.1f MB/s (payload only, both directions)\n",
                2.0 * round_trips * size / elapsed / 1e6);
    if (dropped) std::printf("timeouts      %llu\n", static_cast<unsigned long long>(dropped));

    eshm_destroy(handle);
    return 0;
}

int echo(const char* channel) {
    ESHMConfig config = eshm_default_config(channel);
    config.role = ESHM_ROLE_SLAVE;
    config.auto_cleanup = false;
    config.max_reconnect_attempts = 0;
    config.reconnect_wait_ms = 0;

    ESHMHandle* handle = nullptr;
    for (int i = 0; i < 300 && !handle; ++i) {
        handle = eshm_init(&config);
        if (!handle) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!handle) {
        std::fprintf(stderr, "bench: no channel '%s' - is a driver running?\n", channel);
        return 1;
    }
    std::printf("bench: echoing on '%s' (Ctrl-C to stop)\n", channel);
    std::fflush(stdout);

    std::vector<uint8_t> buffer(ESHM_MAX_DATA_SIZE);
    uint64_t echoed = 0;
    bool seen_driver = false;

    while (g_running) {
        size_t received = 0;
        const int rc = eshm_read_ex(handle, buffer.data(), buffer.size(), &received, 200);
        if (rc != ESHM_SUCCESS) {
            bool alive = false;
            eshm_check_remote_alive(handle, &alive);
            if (seen_driver && !alive) break;
            continue;
        }
        seen_driver = true;
        eshm_write(handle, buffer.data(), received);
        ++echoed;
    }

    std::printf("\nbench: echoed %llu message(s)\n", static_cast<unsigned long long>(echoed));
    eshm_destroy(handle);
    return 0;
}
}

int main(int argc, char** argv) {
    const std::string mode = (argc > 1) ? argv[1] : "";
    const char* channel = "bench";
    double seconds = 5.0;
    size_t size = 256;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_next = (i + 1 < argc);
        if (arg == "--seconds" && has_next)      seconds = std::atof(argv[++i]);
        else if (arg == "--size" && has_next)    size = std::strtoul(argv[++i], nullptr, 10);
        else if (!arg.empty() && arg[0] != '-')  channel = argv[i];
    }

    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);

    if (mode == "drive") return drive(channel, seconds, size);
    if (mode == "echo")  return echo(channel);

    std::fprintf(stderr,
                 "usage: %s drive|echo [channel] [--seconds S] [--size N]\n\n"
                 "  drive   master role: sends and waits for each echo, reports the rate\n"
                 "  echo    slave role:  echoes whatever arrives\n\n"
                 "Pairs with `python3 bench.py drive|echo <channel>`.\n",
                 argv[0]);
    return 2;
}
