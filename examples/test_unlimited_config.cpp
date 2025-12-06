#include "eshm.h"
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

volatile bool g_running = true;

void signal_handler(int sig) {
    (void)sig;
    g_running = false;
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    ESHMConfig config = eshm_default_config("test_unlimited");
    config.role = ESHM_ROLE_SLAVE;
    config.max_reconnect_attempts = 0;  // UNLIMITED retries
    config.reconnect_retry_interval_ms = 100;
    
    ESHMHandle* handle = eshm_init(&config);
    if (!handle) {
        fprintf(stderr, "Failed to initialize\n");
        return 1;
    }
    
    printf("[TEST] Slave with UNLIMITED retry started\n");
    
    while (g_running) {
        char buffer[256];
        size_t bytes_read;
        int ret = eshm_read_timeout(handle, buffer, sizeof(buffer), &bytes_read, 1000);
        if (ret == ESHM_SUCCESS) {
            printf("[SLAVE] Received: %s\n", buffer);
        }
        // Continue regardless of error
    }
    
    printf("[TEST] Shutting down\n");
    eshm_destroy(handle);
    return 0;
}
