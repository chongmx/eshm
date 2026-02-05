#include <eshm.h>
#include <iostream>
#include <cstring>

int main() {
    std::cout << "Starting ESHM Slave..." << std::endl;

    // Initialize as slave with automatic reconnection
    ESHMConfig config = eshm_default_config("demo_shm");
    config.role = ESHM_ROLE_SLAVE;
    config.max_reconnect_attempts = 0;  // Unlimited retries
    config.reconnect_wait_ms = 0;       // Unlimited time

    ESHMHandle* handle = eshm_init(&config);
    if (!handle) {
        std::cerr << "Failed to initialize ESHM" << std::endl;
        return 1;
    }

    std::cout << "Slave initialized. Waiting for messages..." << std::endl;

    char buffer[256];

    while (true) {
        // Read message from master
        int bytes_read = eshm_read(handle, buffer, sizeof(buffer));

        if (bytes_read > 0) {
            std::cout << "Received: " << buffer << std::endl;

            // Send acknowledgment
            const char* ack = "ACK";
            eshm_write(handle, ack, strlen(ack) + 1);
        } else if (bytes_read == -ESHM_ERROR_TIMEOUT) {
            std::cout << "Waiting for master..." << std::endl;
        }
    }

    eshm_destroy(handle);
    return 0;
}
