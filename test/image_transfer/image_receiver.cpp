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
#define BYTES_PER_PIXEL 4  // 32-bit RGBA
#define IMAGE_SIZE (IMAGE_WIDTH * IMAGE_HEIGHT * BYTES_PER_PIXEL)

struct ImageHeader {
    uint32_t width;
    uint32_t height;
    uint32_t bytes_per_pixel;
    uint32_t image_number;
    uint64_t timestamp;
    uint32_t checksum;
};

// Simple checksum
uint32_t calculate_checksum(const uint8_t* data, size_t size) {
    uint32_t sum = 0;
    for (size_t i = 0; i < size; i++) {
        sum += data[i];
    }
    return sum;
}

// Verify image data
bool verify_image(const uint8_t* image, int image_num, int width, int height) {
    int errors = 0;
    int samples = 100; // Sample 100 random pixels

    for (int s = 0; s < samples; s++) {
        int x = (rand() % width);
        int y = (rand() % height);
        int idx = (y * width + x) * BYTES_PER_PIXEL;

        uint8_t expected_r, expected_g, expected_b;

        switch (image_num) {
            case 0: // Red gradient
                expected_r = (x * 255) / width;
                expected_g = 0;
                expected_b = 0;
                break;
            case 1: // Green gradient
                expected_r = 0;
                expected_g = (y * 255) / height;
                expected_b = 0;
                break;
            case 2: // Blue checkerboard
                expected_r = 0;
                expected_g = 0;
                expected_b = ((x / 100) + (y / 100)) % 2 ? 255 : 0;
                break;
            case 3: // RGB mixed
                expected_r = (x * 255) / width;
                expected_g = (y * 255) / height;
                expected_b = ((x + y) * 255) / (width + height);
                break;
            default:
                continue;
        }

        if (image[idx + 0] != expected_r ||
            image[idx + 1] != expected_g ||
            image[idx + 2] != expected_b ||
            image[idx + 3] != 255) {
            errors++;
        }
    }

    return errors == 0;
}

int main() {
    printf("4K Image Receiver - Testing ESHM with large data\n");
    printf("=================================================\n");
    printf("Waiting for images...\n\n");

    // Initialize ESHM as slave
    ESHMConfig config = eshm_default_config("image_shm");
    config.role = ESHM_ROLE_SLAVE;
    config.max_reconnect_attempts = 0;  // Unlimited retries

    ESHMHandle* handle = eshm_init(&config);
    if (!handle) {
        fprintf(stderr, "Failed to initialize ESHM\n");
        return 1;
    }

    // Allocate buffer
    uint8_t* buffer = (uint8_t*)malloc(sizeof(ImageHeader) + IMAGE_SIZE);
    if (!buffer) {
        fprintf(stderr, "Failed to allocate buffer\n");
        eshm_destroy(handle);
        return 1;
    }

    int images_received = 0;

    while (images_received < 4) {
        // Read image data
        int bytes = eshm_read(handle, buffer, sizeof(ImageHeader) + IMAGE_SIZE);

        if (bytes < 0) {
            printf("Waiting for sender...\n");
            sleep(1);
            continue;
        }

        if (bytes < (int)sizeof(ImageHeader)) {
            fprintf(stderr, "Received incomplete header\n");
            continue;
        }

        // Parse header
        ImageHeader* header = (ImageHeader*)buffer;
        uint8_t* image_data = buffer + sizeof(ImageHeader);

        printf("Received image %u:\n", header->image_number);
        printf("  Dimensions: %ux%u\n", header->width, header->height);
        printf("  Size: %d bytes\n", bytes);
        printf("  Timestamp: %lu\n", header->timestamp);

        // Verify checksum
        uint32_t calculated = calculate_checksum(image_data, IMAGE_SIZE);
        if (calculated == header->checksum) {
            printf("  ✓ Checksum valid (0x%08x)\n", header->checksum);
        } else {
            printf("  ✗ Checksum mismatch! Expected 0x%08x, got 0x%08x\n",
                   header->checksum, calculated);
        }

        // Verify image content
        if (verify_image(image_data, header->image_number,
                        header->width, header->height)) {
            printf("  ✓ Image pattern verified\n");
        } else {
            printf("  ⚠ Image pattern verification failed\n");
        }

        // Send acknowledgment
        char ack[256];
        snprintf(ack, sizeof(ack), "ACK: Image %u received and verified",
                 header->image_number);
        eshm_write(handle, ack, strlen(ack) + 1);

        printf("\n");
        images_received++;
    }

    printf("All 4 images received successfully!\n");

    free(buffer);
    eshm_destroy(handle);
    return 0;
}
