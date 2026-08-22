// Publisher: creates the channel (master role) and sends a reading twice a
// second, printing any acknowledgement the consumer sends back.
//
// Build: see CMakeLists.txt in this directory.
// Run:   ./publisher [channel_name]

#include <eshm.h>          // installed by libeshm-dev

#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>

namespace {
volatile sig_atomic_t g_running = 1;
void stop(int) { g_running = 0; }
}

int main(int argc, char** argv) {
    const char* channel = (argc > 1) ? argv[1] : "sensor";
    std::signal(SIGINT, stop);

    // The master creates the shared memory segment and owns its lifetime.
    ESHMConfig config = eshm_default_config(channel);
    config.role = ESHM_ROLE_MASTER;

    ESHMHandle* handle = eshm_init(&config);
    if (!handle) {
        std::fprintf(stderr, "publisher: could not create channel '%s'\n", channel);
        return 1;
    }
    std::printf("publisher: channel '%s' is live (Ctrl-C to stop)\n", channel);

    for (int i = 1; g_running; ++i) {
        char reading[128];
        const int len = std::snprintf(reading, sizeof(reading),
                                      "reading %d temperature=%.1f", i, 20.0 + (i % 10) * 0.5) + 1;

        const int rc = eshm_write(handle, reading, static_cast<size_t>(len));
        if (rc != ESHM_SUCCESS) {
            std::fprintf(stderr, "publisher: write failed: %s\n", eshm_error_string(rc));
        } else {
            std::printf("-> %s\n", reading);
        }

        // Wait briefly for an acknowledgement; ESHM_ERROR_TIMEOUT just means
        // nobody answered this round, which is fine for a periodic publisher.
        char ack[128];
        size_t received = 0;
        if (eshm_read_ex(handle, ack, sizeof(ack), &received, 100) == ESHM_SUCCESS && received > 0) {
            std::printf("   <- %.*s\n", static_cast<int>(received), ack);
        }
        std::fflush(stdout);

        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }

    std::printf("\npublisher: closing channel\n");
    eshm_destroy(handle);   // unlinks the segment: the master owns it
    return 0;
}
