// Sender: master role. Encodes a record with every wire type the ASN.1 codec
// carries across the language boundary, writes it to the channel, and prints
// any acknowledgement the receiver sends back.
//
// Demonstrates: DataHandler::createInteger / createReal / createBoolean /
//               createString / createBinary, encodeDataBuffer,
//               decodeDataBuffer, extractSimpleValues.
//
// Run:   ./structured_sender [channel] [count]      (count 0 = until Ctrl-C)
// Pairs with ./structured_receiver or `python3 peer.py receive <channel>`.

#include <eshm.h>
#include <data_handler.h>

#include <chrono>
#include <cmath>
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

// Wait until a peer has attached, so a bounded run is not a race. A reader
// only sees writes made after its first read (see ../01_hello_channel), so
// there is nothing to gain by sending into an empty channel.
//
// Note which call this uses. eshm_check_remote_alive() answers "has the peer
// been detected as stale", which is false before anyone has ever attached - so
// it reports "alive" on an empty channel. The slave_alive flag in the stats is
// the one that means a peer is actually there: the slave sets it in
// eshm_init() and clears it in eshm_destroy().
bool wait_for_peer(ESHMHandle* handle, int seconds) {
    for (int i = 0; g_running && i < seconds * 10; ++i) {
        ESHMStats stats;
        if (eshm_get_stats(handle, &stats) == ESHM_SUCCESS && stats.slave_alive) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}
}

int main(int argc, char** argv) {
    const char* channel = (argc > 1) ? argv[1] : "structured";
    const long  count   = (argc > 2) ? std::atol(argv[2]) : 0;   // 0 = until Ctrl-C
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);

    ESHMConfig config = eshm_default_config(channel);
    config.role = ESHM_ROLE_MASTER;

    ESHMHandle* handle = eshm_init(&config);
    if (!handle) {
        std::fprintf(stderr, "sender: could not create channel '%s'\n", channel);
        return 1;
    }
    std::printf("sender: channel '%s' is live, waiting for a receiver...\n", channel);
    std::fflush(stdout);

    if (!wait_for_peer(handle, 15)) {
        std::fprintf(stderr, "sender: no receiver attached\n");
        eshm_destroy(handle);
        return 1;
    }
    std::printf("sender: receiver attached (Ctrl-C to stop)\n");

    DataHandler handler;

    for (long i = 0; g_running && (count == 0 || i < count); ++i) {
        // One record, five ASN.1 types. Keys travel with the values, so the
        // receiver needs no shared struct definition - only the same codec.
        const double temperature = 20.0 + 5.0 * std::sin(i * 0.1);
        const std::vector<uint8_t> checksum = {
            static_cast<uint8_t>(i & 0xff), 0xde, 0xad, 0xbe, 0xef
        };

        std::vector<DataItem> items;
        items.push_back(DataHandler::createInteger("counter",     i));
        items.push_back(DataHandler::createReal   ("temperature", temperature));
        items.push_back(DataHandler::createBoolean("enabled",     (i % 2) == 0));
        items.push_back(DataHandler::createString ("source",      "C++ sender"));
        items.push_back(DataHandler::createBinary ("checksum",    checksum));

        const std::vector<uint8_t> buffer = handler.encodeDataBuffer(items);

        const int rc = eshm_write(handle, buffer.data(), buffer.size());
        if (rc != ESHM_SUCCESS) {
            std::fprintf(stderr, "sender: write failed: %s\n", eshm_error_string(rc));
            break;
        }

        if (i % 10 == 0) {
            std::printf("-> #%ld temperature=%.2f enabled=%s (%zu bytes on the wire)\n",
                        i, temperature, ((i % 2) == 0) ? "true" : "false", buffer.size());
        }

        // The receiver answers with an encoded record of its own.
        uint8_t reply[ESHM_MAX_DATA_SIZE];
        size_t received = 0;
        if (eshm_read_ex(handle, reply, sizeof(reply), &received, 20) == ESHM_SUCCESS &&
            received > 0) {
            try {
                auto values = DataHandler::extractSimpleValues(
                    handler.decodeDataBuffer(reply, received));
                std::printf("   <- ack #%lld from \"%s\"\n",
                            static_cast<long long>(std::get<int64_t>(values["ack"])),
                            std::get<std::string>(values["source"]).c_str());
            } catch (const std::exception& e) {
                std::fprintf(stderr, "sender: could not decode reply: %s\n", e.what());
            }
        }
        std::fflush(stdout);

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::printf("\nsender: closing channel\n");
    eshm_destroy(handle);
    return 0;
}
