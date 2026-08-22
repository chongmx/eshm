// Sends synthetic image frames of any size over a channel of any size.
//
// Demonstrates: ESHM_MAX_DATA_SIZE and what it does and does not limit,
//               raw eshm_write framing (no ASN.1), stop-and-wait chunking over
//               a latest-value channel, ESHM_ERROR_BUFFER_TOO_SMALL,
//               and measuring achieved throughput.
//
// Run:   ./frame_sender [channel] [--width W] [--height H] [--frames N]
// Pairs with ./frame_receiver or `python3 peer.py receive <channel>`.

#include "frame_protocol.h"

#include <algorithm>
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

// Wait for the receiver's ack of a specific chunk. Anything else on the
// channel is a stale reply from an earlier step, so keep reading.
bool await_ack(ESHMHandle* handle, uint32_t frame_id, int32_t index, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (g_running && std::chrono::steady_clock::now() < deadline) {
        FrameAck ack{};
        size_t received = 0;
        const int rc = eshm_read_ex(handle, &ack, sizeof(ack), &received, 50);
        if (rc == ESHM_SUCCESS && received >= sizeof(ack) &&
            ack.magic == FRAME_MAGIC_ACK && ack.frame_id == frame_id &&
            ack.index == index) {
            return true;
        }
    }
    return false;
}

bool send_frame(ESHMHandle* handle, uint32_t frame_id,
                uint32_t width, uint32_t height, uint32_t channels,
                const std::vector<uint8_t>& pixels) {
    const uint64_t total = pixels.size();
    const uint32_t payload = static_cast<uint32_t>(FRAME_CHUNK_PAYLOAD);
    const uint32_t chunks = static_cast<uint32_t>((total + payload - 1) / payload);

    FrameHeader header{};
    header.magic = FRAME_MAGIC_HEADER;
    header.frame_id = frame_id;
    header.width = width;
    header.height = height;
    header.channels = channels;
    header.chunk_count = chunks;
    header.total_bytes = total;
    header.checksum = frame_checksum(pixels.data(), total);

    int rc = eshm_write(handle, &header, sizeof(header));
    if (rc != ESHM_SUCCESS) {
        std::fprintf(stderr, "sender: header write failed: %s\n", eshm_error_string(rc));
        return false;
    }
    if (!await_ack(handle, frame_id, -1, 5000)) {
        std::fprintf(stderr, "sender: no ack for frame %u header\n", frame_id);
        return false;
    }

    // One chunk per write, each acknowledged before the next goes out. The
    // buffer is sized so header + payload is exactly one channel write.
    std::vector<uint8_t> message(sizeof(ChunkHeader) + payload);
    for (uint32_t i = 0; i < chunks && g_running; ++i) {
        const uint64_t offset = static_cast<uint64_t>(i) * payload;
        const uint32_t bytes = static_cast<uint32_t>(
            std::min<uint64_t>(payload, total - offset));

        ChunkHeader chunk{};
        chunk.magic = FRAME_MAGIC_CHUNK;
        chunk.frame_id = frame_id;
        chunk.index = i;
        chunk.bytes = bytes;

        std::memcpy(message.data(), &chunk, sizeof(chunk));
        std::memcpy(message.data() + sizeof(chunk), pixels.data() + offset, bytes);

        rc = eshm_write(handle, message.data(), sizeof(chunk) + bytes);
        if (rc != ESHM_SUCCESS) {
            // A payload bigger than the channel is the one error worth calling
            // out by name: rebuild with a larger -DESHM_MAX_DATA_SIZE.
            std::fprintf(stderr, "sender: chunk %u write failed: %s\n",
                         i, eshm_error_string(rc));
            return false;
        }
        if (!await_ack(handle, frame_id, static_cast<int32_t>(i), 5000)) {
            std::fprintf(stderr, "sender: no ack for frame %u chunk %u\n", frame_id, i);
            return false;
        }
    }
    return g_running != 0;
}
}

int main(int argc, char** argv) {
    const char* channel = "frames";
    uint32_t width = 640, height = 480, channels = 4;
    int frames = 10;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_next = (i + 1 < argc);
        if (arg == "-h" || arg == "--help") {
            std::fprintf(stderr,
                "usage: %s [channel] [--width W] [--height H] [--channels C] [--frames N]\n\n"
                "  default 640x480x4 (1.2 MB), 10 frames\n"
                "  4K RGBA is --width 3840 --height 2160 (33 MB)\n",
                argv[0]);
            return 0;
        }
        else if (arg == "--width" && has_next)    width = std::strtoul(argv[++i], nullptr, 10);
        else if (arg == "--height" && has_next)   height = std::strtoul(argv[++i], nullptr, 10);
        else if (arg == "--channels" && has_next) channels = std::strtoul(argv[++i], nullptr, 10);
        else if (arg == "--frames" && has_next)   frames = std::atoi(argv[++i]);
        else if (!arg.empty() && arg[0] != '-')   channel = argv[i];
    }

    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);

    const uint64_t frame_bytes = static_cast<uint64_t>(width) * height * channels;
    const uint32_t payload = static_cast<uint32_t>(FRAME_CHUNK_PAYLOAD);
    const uint64_t chunks = (frame_bytes + payload - 1) / payload;

    std::printf("sender: %ux%ux%u = %.2f MB per frame\n",
                width, height, channels, frame_bytes / 1048576.0);
    std::printf("        channel holds %d bytes, so %llu chunk(s) of up to %u bytes\n",
                ESHM_MAX_DATA_SIZE, static_cast<unsigned long long>(chunks), payload);
    if (chunks > 1) {
        std::printf("        (rebuild with -DESHM_MAX_DATA_SIZE=%llu to send whole frames)\n",
                    static_cast<unsigned long long>(frame_bytes + sizeof(ChunkHeader)));
    }

    ESHMConfig config = eshm_default_config(channel);
    config.role = ESHM_ROLE_MASTER;

    ESHMHandle* handle = eshm_init(&config);
    if (!handle) {
        std::fprintf(stderr, "sender: could not create channel '%s'\n", channel);
        return 1;
    }
    std::printf("sender: channel '%s' is live, waiting for a receiver...\n", channel);
    std::fflush(stdout);

    for (int i = 0; g_running && i < 150; ++i) {
        ESHMStats stats;
        if (eshm_get_stats(handle, &stats) == ESHM_SUCCESS && stats.slave_alive) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::printf("sender: receiver attached\n\n");

    // A recognisable gradient, so a corrupted frame is visible in the checksum.
    std::vector<uint8_t> pixels(frame_bytes);
    for (uint64_t i = 0; i < frame_bytes; ++i) pixels[i] = static_cast<uint8_t>(i & 0xff);

    uint64_t sent_bytes = 0;
    int sent_frames = 0;
    const auto start = std::chrono::steady_clock::now();

    for (int i = 0; g_running && i < frames; ++i) {
        pixels[0] = static_cast<uint8_t>(i);          // make each frame distinct
        const auto frame_start = std::chrono::steady_clock::now();

        if (!send_frame(handle, static_cast<uint32_t>(i), width, height, channels, pixels)) break;

        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - frame_start).count();
        sent_bytes += frame_bytes;
        ++sent_frames;

        std::printf("-> frame %d in %.1f ms (%.2f GB/s)\n", i, ms,
                    frame_bytes / (ms / 1000.0) / 1e9);
        std::fflush(stdout);
    }

    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
    std::printf("\nsender: %d frame(s), %.2f MB in %.2f s (%.2f GB/s, %.1f fps)\n",
                sent_frames, sent_bytes / 1048576.0, elapsed,
                elapsed > 0 ? sent_bytes / elapsed / 1e9 : 0.0,
                elapsed > 0 ? sent_frames / elapsed : 0.0);

    eshm_destroy(handle);
    return sent_frames > 0 ? 0 : 1;
}
