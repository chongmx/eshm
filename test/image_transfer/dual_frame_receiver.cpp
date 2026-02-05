#include <eshm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>

// 4K resolution
#define IMAGE_WIDTH 3840
#define IMAGE_HEIGHT 2160
#define BYTES_PER_PIXEL 4
#define IMAGE_SIZE (IMAGE_WIDTH * IMAGE_HEIGHT * BYTES_PER_PIXEL)
#define FRAMES_PER_TRANSFER 2

struct FrameHeader {
    uint32_t width;
    uint32_t height;
    uint32_t bytes_per_pixel;
    uint32_t frame_number;
    uint64_t timestamp;
    uint32_t checksum;
    uint8_t padding[36];
} __attribute__((packed));

struct DualFramePacket {
    uint32_t num_frames;
    uint32_t total_size;
    uint64_t packet_timestamp;
    FrameHeader headers[FRAMES_PER_TRANSFER];
    uint8_t frame_data[FRAMES_PER_TRANSFER][IMAGE_SIZE];
};

uint32_t calculate_checksum(const uint8_t* data, size_t size) {
    uint32_t sum = 0;
    for (size_t i = 0; i < size; i++) {
        sum += data[i];
    }
    return sum;
}

int main() {
    printf("Dual-Frame 4K Receiver - Receiving 2 frames per transfer\n");
    printf("=========================================================\n");
    printf("Waiting for packets...\n\n");

    // Initialize ESHM as slave
    ESHMConfig config = eshm_default_config("dual_frame_shm");
    config.role = ESHM_ROLE_SLAVE;
    config.max_reconnect_attempts = 0;

    ESHMHandle* handle = eshm_init(&config);
    if (!handle) {
        fprintf(stderr, "Failed to initialize ESHM\n");
        return 1;
    }

    // Allocate buffer
    DualFramePacket* packet = (DualFramePacket*)malloc(sizeof(DualFramePacket));
    if (!packet) {
        fprintf(stderr, "Failed to allocate buffer\n");
        eshm_destroy(handle);
        return 1;
    }

    int packets_received = 0;
    int total_frames = 0;

    while (packets_received < 2) {
        // Read packet
        int bytes = eshm_read(handle, packet, sizeof(DualFramePacket));

        if (bytes < 0) {
            printf("Waiting for sender...\n");
            sleep(1);
            continue;
        }

        if (bytes < (int)sizeof(DualFramePacket)) {
            fprintf(stderr, "Received incomplete packet (%d bytes)\n", bytes);
            continue;
        }

        printf("=== Packet %d ===\n", packets_received);
        printf("  Num frames: %u\n", packet->num_frames);
        printf("  Total size: %u bytes (%.2f MB)\n", packet->total_size,
               packet->total_size / (1024.0 * 1024.0));
        printf("  Timestamp: %lu\n\n", packet->packet_timestamp);

        // Verify each frame
        for (uint32_t i = 0; i < packet->num_frames && i < FRAMES_PER_TRANSFER; i++) {
            FrameHeader* hdr = &packet->headers[i];

            printf("Frame %u:\n", hdr->frame_number);
            printf("  Dimensions: %ux%u\n", hdr->width, hdr->height);
            printf("  Timestamp: %lu\n", hdr->timestamp);

            // Verify checksum
            uint32_t calculated = calculate_checksum(packet->frame_data[i], IMAGE_SIZE);
            if (calculated == hdr->checksum) {
                printf("  ✓ Checksum valid (0x%08x)\n", hdr->checksum);
            } else {
                printf("  ✗ Checksum mismatch! Expected 0x%08x, got 0x%08x\n",
                       hdr->checksum, calculated);
            }

            total_frames++;
            printf("\n");
        }

        // Send acknowledgment
        char ack[256];
        snprintf(ack, sizeof(ack), "ACK: Packet %d with %u frames received",
                 packets_received, packet->num_frames);
        eshm_write(handle, ack, strlen(ack) + 1);

        packets_received++;
    }

    printf("Received %d packets with %d total frames!\n", packets_received, total_frames);

    free(packet);
    eshm_destroy(handle);
    return 0;
}
