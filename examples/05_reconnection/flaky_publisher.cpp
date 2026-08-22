// A publisher that deliberately dies and comes back, so the recovery policies
// in resilient_consumer.cpp have something to recover from.
//
// Each cycle: create the channel, publish for --up seconds, destroy it, stay
// down for --down seconds, repeat. That is the same sequence a crashing or
// restarting master produces, and it bumps master_generation each time.
//
// Run:   ./flaky_publisher [channel] [--up S] [--down S] [--cycles N]
// Pairs with ./resilient_consumer or `python3 peer.py consume <channel>`.

#include <eshm.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace {
volatile sig_atomic_t g_running = 1;
void stop(int) { g_running = 0; }
}

int main(int argc, char** argv) {
    const char* channel = "reconnect";
    int up_seconds = 5, down_seconds = 3, cycles = 0;   // cycles 0 = forever

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_next = (i + 1 < argc);

        if (arg == "-h" || arg == "--help") {
            std::fprintf(stderr,
                "usage: %s [channel] [--up S] [--down S] [--cycles N]\n\n"
                "  --up S      seconds publishing per cycle   (default 5)\n"
                "  --down S    seconds dead per cycle         (default 3)\n"
                "  --cycles N  stop after N cycles (0 = forever, default)\n",
                argv[0]);
            return 0;
        }
        else if (arg == "--up" && has_next)     up_seconds = std::atoi(argv[++i]);
        else if (arg == "--down" && has_next)   down_seconds = std::atoi(argv[++i]);
        else if (arg == "--cycles" && has_next) cycles = std::atoi(argv[++i]);
        else if (!arg.empty() && arg[0] != '-') channel = argv[i];
    }

    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);

    std::printf("publisher: '%s', %ds up / %ds down", channel, up_seconds, down_seconds);
    if (cycles) std::printf(", %d cycle(s)", cycles);
    std::printf(" (Ctrl-C to stop)\n\n");

    long total = 0;
    for (int cycle = 1; g_running && (cycles == 0 || cycle <= cycles); ++cycle) {
        ESHMConfig config = eshm_default_config(channel);
        config.role = ESHM_ROLE_MASTER;
        config.auto_cleanup = true;      // unlink on destroy: a real restart

        ESHMHandle* handle = eshm_init(&config);
        if (!handle) {
            std::fprintf(stderr, "publisher: could not create '%s'\n", channel);
            return 1;
        }
        std::printf("[cycle %d] up\n", cycle);
        std::fflush(stdout);

        const auto until = std::chrono::steady_clock::now() +
                           std::chrono::seconds(up_seconds);
        while (g_running && std::chrono::steady_clock::now() < until) {
            char message[64];
            const int len = std::snprintf(message, sizeof(message),
                                          "cycle %d message %ld", cycle, ++total) + 1;
            eshm_write(handle, message, static_cast<size_t>(len));
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        // Destroying the master unlinks the segment. The slave's monitor
        // thread notices the heartbeat stop within stale_threshold_ms and
        // starts retrying.
        std::printf("[cycle %d] down\n", cycle);
        std::fflush(stdout);
        eshm_destroy(handle);

        if (!g_running) break;
        std::this_thread::sleep_for(std::chrono::seconds(down_seconds));
    }

    std::printf("\npublisher: %ld message(s) over all cycles\n", total);
    return 0;
}
