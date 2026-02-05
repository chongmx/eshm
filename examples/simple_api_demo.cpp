#include "eshm.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main() {
    printf("=== Simple API Demo ===\n");

    // Create master
    ESHMConfig config = eshm_default_config("simple_demo");
    config.role = ESHM_ROLE_MASTER;
    ESHMHandle* handle = eshm_init(&config);
    if (!handle) {
        fprintf(stderr, "Failed to initialize ESHM\n");
        return 1;
    }

    printf("Master initialized\n");
    sleep(1);

    // Write some data
    const char* message = "Hello, World!";
    int ret = eshm_write(handle, message, strlen(message) + 1);
    if (ret == ESHM_SUCCESS) {
        printf("Wrote: %s\n", message);
    }

    // Try to read using the simple API (should timeout as no slave)
    char buffer[256];
    int bytes_read = eshm_read(handle, buffer, sizeof(buffer));
    if (bytes_read >= 0) {
        printf("Read %d bytes: %s\n", bytes_read, buffer);
    } else if (bytes_read == ESHM_ERROR_TIMEOUT) {
        printf("Read timed out (no data available) - this is expected\n");
    } else {
        printf("Read error: %s\n", eshm_error_string(bytes_read));
    }

    // Demonstrate zero-byte read support (for future event triggering)
    printf("\nZero-byte reads are supported for event triggering\n");

    eshm_destroy(handle);
    printf("\nDemo complete!\n");

    return 0;
}
