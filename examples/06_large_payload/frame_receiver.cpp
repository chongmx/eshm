// Reassembles frames sent by frame_sender.cpp (or peer.py send) and verifies
// that every byte survived the trip.
//
// Demonstrates: reassembling a payload larger than ESHM_MAX_DATA_SIZE,
//               acknowledging each chunk to drive stop-and-wait, and checking
//               integrity end to end.
//
// Run:   ./frame_receiver [channel] [--frames N]     (start a sender first)
// Pairs with ./frame_sender or `python3 peer.py send <channel>`.

#include "frame_protocol.h"

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

void send_ack(ESHMHandle* handle, uint32_t frame_id, int32_t index) {
    FrameAck ack{};
    ack.magic = FRAME_MAGIC_ACK;
    ack.frame_id = frame_id;
    ack.index = index;
    eshm_write(handle, &ack, sizeof(ack));
}
}

int main(int argc, char** argv) {
    const char* channel = "frames";
    int want_frames = 0;                  // 0 = until the sender leaves

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--frames" && i + 1 < argc) want_frames = std::atoi(argv[++i]);
        else if (!arg.empty() && arg[0] != '-') channel = argv[i];
    }

    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);

    ESHMConfig config = eshm_default_config(channel);
    config.role = ESHM_ROLE_SLAVE;
    config.auto_cleanup = false;
    config.max_reconnect_attempts = 0;
    config.reconnect_wait_ms = 0;

    ESHMHandle* handle = nullptr;
    for (int i = 0; i < 100 && !handle; ++i) {
        handle = eshm_init(&config);
        if (!handle) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!handle) {
        std::fprintf(stderr, "receiver: no channel '%s' - is the sender running?\n", channel);
        return 1;
    }
    std::printf("receiver: attached to '%s' (Ctrl-C to stop)\n\n", channel);

    std::vector<uint8_t> message(ESHM_MAX_DATA_SIZE);
    std::vector<uint8_t> frame;
    FrameHeader header{};
    bool assembling = false;
    uint32_t next_chunk = 0;

    int good = 0, bad = 0;
    bool seen_sender = false;
    auto frame_start = std::chrono::steady_clock::now();

    while (g_running && (want_frames == 0 || good + bad < want_frames)) {
        size_t received = 0;
        const int rc = eshm_read_ex(handle, message.data(), message.size(), &received, 200);

        if (rc == ESHM_ERROR_TIMEOUT || rc == ESHM_ERROR_NO_DATA) {
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

        if (received < sizeof(uint32_t)) continue;
        uint32_t magic = 0;
        std::memcpy(&magic, message.data(), sizeof(magic));

        if (magic == FRAME_MAGIC_HEADER && received >= sizeof(FrameHeader)) {
            std::memcpy(&header, message.data(), sizeof(header));
            frame.assign(header.total_bytes, 0);
            assembling = true;
            next_chunk = 0;
            frame_start = std::chrono::steady_clock::now();

            std::printf("<- frame %u: %ux%ux%u, %.2f MB in %u chunk(s)\n",
                        header.frame_id, header.width, header.height, header.channels,
                        header.total_bytes / 1048576.0, header.chunk_count);
            std::fflush(stdout);
            send_ack(handle, header.frame_id, -1);
            continue;
        }

        if (magic == FRAME_MAGIC_CHUNK && assembling && received >= sizeof(ChunkHeader)) {
            ChunkHeader chunk{};
            std::memcpy(&chunk, message.data(), sizeof(chunk));

            if (chunk.frame_id != header.frame_id) continue;      // stale frame
            if (chunk.index != next_chunk) {
                // Stop-and-wait means this should not happen; acknowledge
                // anyway so the sender is not left waiting on a lost ack.
                send_ack(handle, chunk.frame_id, static_cast<int32_t>(chunk.index));
                continue;
            }

            const uint64_t offset = static_cast<uint64_t>(chunk.index) * FRAME_CHUNK_PAYLOAD;
            if (offset + chunk.bytes <= frame.size() &&
                received >= sizeof(ChunkHeader) + chunk.bytes) {
                std::memcpy(frame.data() + offset,
                            message.data() + sizeof(ChunkHeader), chunk.bytes);
            }
            ++next_chunk;
            send_ack(handle, chunk.frame_id, static_cast<int32_t>(chunk.index));

            if (next_chunk == header.chunk_count) {
                const double ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - frame_start).count();
                const uint64_t sum = frame_checksum(frame.data(), frame.size());
                const bool ok = (sum == header.checksum);
                ok ? ++good : ++bad;

                std::printf("   frame %u %s in %.1f ms (%.2f GB/s)\n",
                            header.frame_id, ok ? "verified" : "CHECKSUM MISMATCH",
                            ms, frame.size() / (ms / 1000.0) / 1e9);
                std::fflush(stdout);
                assembling = false;
            }
        }
    }

    std::printf("\nreceiver: %d frame(s) verified, %d corrupted\n", good, bad);
    eshm_destroy(handle);
    return (good > 0 && bad == 0) ? 0 : 1;
}
