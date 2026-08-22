// Consumer: attaches to an existing channel (slave role), prints every reading
// and acknowledges it.
//
// Run:   ./consumer [channel_name]        (start the publisher first)

#include <eshm.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {
volatile sig_atomic_t g_running = 1;
void stop(int) { g_running = 0; }

// eshm_init() as a slave fails immediately when the segment does not exist,
// so wait for the publisher instead of giving up.
ESHMHandle* attach(const char* channel, int attempts) {
    ESHMConfig config = eshm_default_config(channel);
    config.role = ESHM_ROLE_SLAVE;
    config.auto_cleanup = false;         // the master owns the segment
    config.max_reconnect_attempts = 0;   // reconnect forever if the master restarts
    config.reconnect_wait_ms = 0;

    for (int i = 0; i < attempts; ++i) {
        if (ESHMHandle* handle = eshm_init(&config)) return handle;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return nullptr;
}
}

int main(int argc, char** argv) {
    const char* channel = (argc > 1) ? argv[1] : "sensor";
    std::signal(SIGINT, stop);

    ESHMHandle* handle = attach(channel, 50);   // ~5 seconds
    if (!handle) {
        std::fprintf(stderr, "consumer: no channel '%s' - is the publisher running?\n", channel);
        return 1;
    }
    std::printf("consumer: attached to '%s' (Ctrl-C to stop)\n", channel);

    int count = 0;
    while (g_running) {
        char buffer[4096];              // ESHM_MAX_DATA_SIZE
        size_t received = 0;

        const int rc = eshm_read_ex(handle, buffer, sizeof(buffer), &received, 200);
        if (rc == ESHM_ERROR_TIMEOUT) continue;          // nothing new this round
        if (rc != ESHM_SUCCESS) {
            std::fprintf(stderr, "consumer: read failed: %s\n", eshm_error_string(rc));
            continue;
        }

        std::printf("<- %.*s\n", static_cast<int>(received), buffer);
        std::fflush(stdout);

        char ack[64];
        const int len = std::snprintf(ack, sizeof(ack), "ack %d", ++count) + 1;
        eshm_write(handle, ack, static_cast<size_t>(len));
    }

    std::printf("\nconsumer: detaching after %d reading(s)\n", count);
    eshm_destroy(handle);
    return 0;
}
