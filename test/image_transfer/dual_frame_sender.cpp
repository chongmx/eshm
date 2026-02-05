#include <eshm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

// 4K resolution
#define IMAGE_WIDTH 3840
#define IMAGE_HEIGHT 2160
#define BYTES_PER_PIXEL 4  // 32-bit RGBA
#define IMAGE_SIZE (IMAGE_WIDTH * IMAGE_HEIGHT * BYTES_PER_PIXEL)
#define FRAMES_PER_TRANSFER 2

struct FrameHeader {
    uint32_t width;
    uint32_t height;
    uint32_t bytes_per_pixel;
    uint32_t frame_number;
    uint64_t timestamp;
    uint32_t checksum;
    uint8_t padding[36];  // Pad to 64 bytes
} __attribute__((packed));

struct DualFramePacket {
    uint32_t num_frames;
    uint32_t total_size;
    uint64_t packet_timestamp;
    FrameHeader headers[FRAMES_PER_TRANSFER];
    uint8_t frame_data[FRAMES_PER_TRANSFER][IMAGE_SIZE];
};

// Generate test pattern
void generate_test_frame(uint8_t* frame, int frame_num) {
    for (int y = 0; y < IMAGE_HEIGHT; y++) {
        for (int x = 0; x < IMAGE_WIDTH; x++) {
            int idx = (y * IMAGE_WIDTH + x) * BYTES_PER_PIXEL;

            switch (frame_num % 4) {
                case 0: // Red gradient
                    frame[idx + 0] = (x * 255) / IMAGE_WIDTH;
                    frame[idx + 1] = 0;
                    frame[idx + 2] = 0;
                    frame[idx + 3] = 255;
                    break;
                case 1: // Green gradient
                    frame[idx + 0] = 0;
                    frame[idx + 1] = (y * 255) / IMAGE_HEIGHT;
                    frame[idx + 2] = 0;
                    frame[idx + 3] = 255;
                    break;
                case 2: // Blue checkerboard
                    frame[idx + 0] = 0;
                    frame[idx + 1] = 0;
                    frame[idx + 2] = ((x / 100) + (y / 100)) % 2 ? 255 : 0;
                    frame[idx + 3] = 255;
                    break;
                case 3: // RGB mixed
                    frame[idx + 0] = (x * 255) / IMAGE_WIDTH;
                    frame[idx + 1] = (y * 255) / IMAGE_HEIGHT;
                    frame[idx + 2] = ((x + y) * 255) / (IMAGE_WIDTH + IMAGE_HEIGHT);
                    frame[idx + 3] = 255;
                    break;
            }
        }
    }
}

uint32_t calculate_checksum(const uint8_t* data, size_t size) {
    uint32_t sum = 0;
    for (size_t i = 0; i < size; i++) {
        sum += data[i];
    }
    return sum;
}

int main() {
    printf("Dual-Frame 4K Sender - Testing 2 frames per transfer\n");
    printf("=====================================================\n");
    printf("Frame specs: %dx%d, %d bytes/pixel\n", IMAGE_WIDTH, IMAGE_HEIGHT, BYTES_PER_PIXEL);
    printf("Size per frame: %d bytes (%.2f MB)\n", IMAGE_SIZE, IMAGE_SIZE / (1024.0 * 1024.0));
    printf("Frames per transfer: %d\n", FRAMES_PER_TRANSFER);
    printf("Total packet size: %zu bytes (%.2f MB)\n\n", sizeof(DualFramePacket),
           sizeof(DualFramePacket) / (1024.0 * 1024.0));

    // Check buffer size
    if (sizeof(DualFramePacket) > ESHM_MAX_DATA_SIZE) {
        fprintf(stderr, "ERROR: Packet size (%zu) exceeds ESHM_MAX_DATA_SIZE (%d)\n",
                sizeof(DualFramePacket), ESHM_MAX_DATA_SIZE);
        fprintf(stderr, "Rebuild with: cmake -DESHM_MAX_DATA_SIZE=%zu ..\n",
                sizeof(DualFramePacket) + 1024);
        return 1;
    }

    // Initialize ESHM as master
    ESHMConfig config = eshm_default_config("dual_frame_shm");
    config.role = ESHM_ROLE_MASTER;
    ESHMHandle* handle = eshm_init(&config);
    if (!handle) {
        fprintf(stderr, "Failed to initialize ESHM\n");
        return 1;
    }

    printf("ESHM initialized. Waiting for receiver...\n\n");

    // Allocate packet
    DualFramePacket* packet = (DualFramePacket*)malloc(sizeof(DualFramePacket));
    if (!packet) {
        fprintf(stderr, "Failed to allocate packet\n");
        eshm_destroy(handle);
        return 1;
    }

    // Send 2 dual-frame packets (4 frames total)
    for (int packet_num = 0; packet_num < 2; packet_num++) {
        printf("=== Packet %d ===\n", packet_num);

        packet->num_frames = FRAMES_PER_TRANSFER;
        packet->total_size = sizeof(DualFramePacket);
        packet->packet_timestamp = time(NULL);

        // Generate 2 frames for this packet
        for (int i = 0; i < FRAMES_PER_TRANSFER; i++) {
            int frame_num = packet_num * FRAMES_PER_TRANSFER + i;

            printf("Generating frame %d...\n", frame_num);
            generate_test_frame(packet->frame_data[i], frame_num);

            packet->headers[i].width = IMAGE_WIDTH;
            packet->headers[i].height = IMAGE_HEIGHT;
            packet->headers[i].bytes_per_pixel = BYTES_PER_PIXEL;
            packet->headers[i].frame_number = frame_num;
            packet->headers[i].timestamp = time(NULL);
            packet->headers[i].checksum = calculate_checksum(packet->frame_data[i], IMAGE_SIZE);

            printf("  Frame %d checksum: 0x%08x\n", frame_num, packet->headers[i].checksum);
        }

        printf("Sending packet with 2 frames...\n");

        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        int ret = eshm_write(handle, packet, sizeof(DualFramePacket));

        clock_gettime(CLOCK_MONOTONIC, &end);
        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

        if (ret == ESHM_SUCCESS) {
            double throughput = sizeof(DualFramePacket) / (1024.0 * 1024.0) / elapsed;
            printf("✓ Sent packet %d in %.3f ms (%.2f MB/s)\n",
                   packet_num, elapsed * 1000, throughput);
            printf("  Equivalent to %.0f fps at 4K\n", FRAMES_PER_TRANSFER / elapsed);
        } else {
            fprintf(stderr, "✗ Failed to send packet: %s\n", eshm_error_string(ret));
        }

        // Wait for acknowledgment
        char ack[256];
        int bytes = eshm_read(handle, ack, sizeof(ack));
        if (bytes > 0) {
            printf("Receiver: %s\n", ack);
        }

        printf("\n");
        sleep(1);
    }

    printf("All packets sent! (4 frames total)\n");

    free(packet);
    eshm_destroy(handle);
    return 0;
}
