// Receiver: slave role. Decodes each ASN.1 record off the channel, prints the
// typed values, and acknowledges with an encoded record of its own.
//
// Demonstrates: decodeDataBuffer, extractSimpleValues, reading a std::variant
//               back out by type, encoding a reply, and noticing that the
//               sender has gone away.
//
// Run:   ./structured_receiver [channel] [count]   (start a sender first;
//                                                   count 0 = until the sender
//                                                   leaves or Ctrl-C)
// Pairs with ./structured_sender or `python3 peer.py send <channel>`.

#include <eshm.h>
#include <data_handler.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using namespace shm_protocol;

namespace {
volatile sig_atomic_t g_running = 1;
void stop(int) { g_running = 0; }

// A slave cannot attach before the master has created the segment.
ESHMHandle* attach(const char* channel, int attempts) {
    ESHMConfig config = eshm_default_config(channel);
    config.role = ESHM_ROLE_SLAVE;
    config.auto_cleanup = false;          // the master owns the segment
    config.max_reconnect_attempts = 0;    // reconnect forever if it restarts
    config.reconnect_wait_ms = 0;

    for (int i = 0; i < attempts; ++i) {
        if (ESHMHandle* handle = eshm_init(&config)) return handle;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return nullptr;
}

std::string hex(const std::vector<uint8_t>& bytes) {
    std::string out;
    char byte[4];
    for (uint8_t b : bytes) {
        std::snprintf(byte, sizeof(byte), "%02x", b);
        out += byte;
    }
    return out;
}
}

int main(int argc, char** argv) {
    const char* channel = (argc > 1) ? argv[1] : "structured";
    const long  count   = (argc > 2) ? std::atol(argv[2]) : 0;
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);

    ESHMHandle* handle = attach(channel, 50);   // ~5 seconds
    if (!handle) {
        std::fprintf(stderr, "receiver: no channel '%s' - is the sender running?\n", channel);
        return 1;
    }
    std::printf("receiver: attached to '%s' (Ctrl-C to stop)\n", channel);

    DataHandler handler;
    long received_count = 0;
    long decode_errors = 0;
    long highest_counter = -1;
    bool seen_sender = false;

    while (g_running && (count == 0 || received_count < count)) {
        uint8_t buffer[ESHM_MAX_DATA_SIZE];
        size_t received = 0;

        const int rc = eshm_read_ex(handle, buffer, sizeof(buffer), &received, 200);

        if (rc == ESHM_ERROR_TIMEOUT) {
            // Nothing new this round. Once the sender has been seen, a stale
            // peer means it has exited - stop rather than spin forever.
            bool alive = false;
            eshm_check_remote_alive(handle, &alive);
            if (seen_sender && !alive) {
                std::printf("receiver: sender went away\n");
                break;
            }
            continue;
        }
        if (rc != ESHM_SUCCESS) {
            std::fprintf(stderr, "receiver: read failed: %s\n", eshm_error_string(rc));
            continue;
        }
        seen_sender = true;

        try {
            const std::vector<DataItem> items = handler.decodeDataBuffer(buffer, received);
            auto values = DataHandler::extractSimpleValues(items);

            const int64_t     counter     = std::get<int64_t>(values["counter"]);
            const double      temperature = std::get<double>(values["temperature"]);
            const bool        enabled     = std::get<bool>(values["enabled"]);
            const std::string source      = std::get<std::string>(values["source"]);
            const auto&       checksum    = std::get<std::vector<uint8_t>>(values["checksum"]);

            ++received_count;
            highest_counter = static_cast<long>(counter);

            if (counter % 10 == 0) {
                std::printf("<- #%lld temperature=%.2f enabled=%s source=\"%s\" checksum=%s\n",
                            static_cast<long long>(counter), temperature,
                            enabled ? "true" : "false", source.c_str(), hex(checksum).c_str());
                std::fflush(stdout);
            }

            // Acknowledge in the same encoding, so the sender can decode it.
            std::vector<DataItem> ack;
            ack.push_back(DataHandler::createInteger("ack", counter));
            ack.push_back(DataHandler::createString("source", "C++ receiver"));
            const std::vector<uint8_t> reply = handler.encodeDataBuffer(ack);
            eshm_write(handle, reply.data(), reply.size());

        } catch (const std::exception& e) {
            if (++decode_errors < 10) {
                std::fprintf(stderr, "receiver: decode error: %s\n", e.what());
            }
        }
    }

    // The channel holds one value per direction: if the sender outruns the
    // reader, intermediate records are overwritten rather than queued. Seeing
    // fewer records than were sent is expected, and is not data loss in the
    // sense of corruption - decode_errors is what has to stay at zero.
    std::printf("\nreceiver: %ld record(s) decoded, highest counter %ld, %ld decode error(s)\n",
                received_count, highest_counter, decode_errors);

    eshm_destroy(handle);
    return (received_count > 0 && decode_errors == 0) ? 0 : 1;
}
