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

struct ImageHeader {
    uint32_t width;
    uint32_t height;
    uint32_t bytes_per_pixel;
    uint32_t image_number;  // 0-3 for 4 images
    uint64_t timestamp;
    uint32_t checksum;
};

// Generate a test pattern image
void generate_test_image(uint8_t* image, int image_num) {
    for (int y = 0; y < IMAGE_HEIGHT; y++) {
        for (int x = 0; x < IMAGE_WIDTH; x++) {
            int idx = (y * IMAGE_WIDTH + x) * BYTES_PER_PIXEL;

            // Generate different pattern for each image
            switch (image_num) {
                case 0: // Red gradient
                    image[idx + 0] = (x * 255) / IMAGE_WIDTH;  // R
                    image[idx + 1] = 0;                        // G
                    image[idx + 2] = 0;                        // B
                    image[idx + 3] = 255;                      // A
                    break;
                case 1: // Green gradient
                    image[idx + 0] = 0;
                    image[idx + 1] = (y * 255) / IMAGE_HEIGHT;
                    image[idx + 2] = 0;
                    image[idx + 3] = 255;
                    break;
                case 2: // Blue checkerboard
                    image[idx + 0] = 0;
                    image[idx + 1] = 0;
                    image[idx + 2] = ((x / 100) + (y / 100)) % 2 ? 255 : 0;
                    image[idx + 3] = 255;
                    break;
                case 3: // RGB mixed
                    image[idx + 0] = (x * 255) / IMAGE_WIDTH;
                    image[idx + 1] = (y * 255) / IMAGE_HEIGHT;
                    image[idx + 2] = ((x + y) * 255) / (IMAGE_WIDTH + IMAGE_HEIGHT);
                    image[idx + 3] = 255;
                    break;
            }
        }
    }
}

// Simple checksum
uint32_t calculate_checksum(const uint8_t* data, size_t size) {
    uint32_t sum = 0;
    for (size_t i = 0; i < size; i++) {
        sum += data[i];
    }
    return sum;
}

int main() {
    printf("4K Image Sender - Testing ESHM with large data\n");
    printf("================================================\n");
    printf("Image specs: %dx%d, %d bytes/pixel\n", IMAGE_WIDTH, IMAGE_HEIGHT, BYTES_PER_PIXEL);
    printf("Size per image: %d bytes (%.2f MB)\n", IMAGE_SIZE, IMAGE_SIZE / (1024.0 * 1024.0));
    printf("Total buffer size: %zu bytes\n\n", sizeof(ImageHeader) + IMAGE_SIZE);

    // Check if buffer fits
    if (sizeof(ImageHeader) + IMAGE_SIZE > ESHM_MAX_DATA_SIZE) {
        fprintf(stderr, "ERROR: Image size (%zu) exceeds ESHM_MAX_DATA_SIZE (%d)\n",
                sizeof(ImageHeader) + IMAGE_SIZE, ESHM_MAX_DATA_SIZE);
        fprintf(stderr, "Rebuild with: cmake -DESHM_MAX_DATA_SIZE=%zu ..\n",
                sizeof(ImageHeader) + IMAGE_SIZE);
        return 1;
    }

    // Initialize ESHM as master
    ESHMConfig config = eshm_default_config("image_shm");
    config.role = ESHM_ROLE_MASTER;
    ESHMHandle* handle = eshm_init(&config);
    if (!handle) {
        fprintf(stderr, "Failed to initialize ESHM\n");
        return 1;
    }

    printf("ESHM initialized as master. Waiting for receiver...\n\n");

    // Allocate buffer for image + header
    uint8_t* buffer = (uint8_t*)malloc(sizeof(ImageHeader) + IMAGE_SIZE);
    if (!buffer) {
        fprintf(stderr, "Failed to allocate buffer\n");
        eshm_destroy(handle);
        return 1;
    }

    // Send 4 images
    for (int img_num = 0; img_num < 4; img_num++) {
        printf("Generating image %d...\n", img_num);

        // Generate image data
        uint8_t* image_data = buffer + sizeof(ImageHeader);
        generate_test_image(image_data, img_num);

        // Prepare header
        ImageHeader* header = (ImageHeader*)buffer;
        header->width = IMAGE_WIDTH;
        header->height = IMAGE_HEIGHT;
        header->bytes_per_pixel = BYTES_PER_PIXEL;
        header->image_number = img_num;
        header->timestamp = time(NULL);
        header->checksum = calculate_checksum(image_data, IMAGE_SIZE);

        printf("Sending image %d (checksum: 0x%08x)...\n", img_num, header->checksum);

        // Send via ESHM
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        int ret = eshm_write(handle, buffer, sizeof(ImageHeader) + IMAGE_SIZE);

        clock_gettime(CLOCK_MONOTONIC, &end);
        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

        if (ret == ESHM_SUCCESS) {
            double throughput = (sizeof(ImageHeader) + IMAGE_SIZE) / (1024.0 * 1024.0) / elapsed;
            printf("✓ Sent image %d in %.3f ms (%.2f MB/s)\n\n",
                   img_num, elapsed * 1000, throughput);
        } else {
            fprintf(stderr, "✗ Failed to send image %d: %s\n",
                    img_num, eshm_error_string(ret));
        }

        // Wait for acknowledgment
        char ack[256];
        int bytes = eshm_read(handle, ack, sizeof(ack));
        if (bytes > 0) {
            printf("Receiver: %s\n\n", ack);
        }

        sleep(1);
    }

    printf("All images sent!\n");

    free(buffer);
    eshm_destroy(handle);
    return 0;
}
