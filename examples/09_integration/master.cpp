// Master half of the integration example: a stand-in for your application.
//
// Nothing here is new - see ../01_hello_channel for the API. The point of this
// directory is the build: CMakeLists.txt shows the three supported ways to
// consume ESHM from your own project, and these two programs exist to prove
// the wiring works.
//
// Run:   ./integration_master        (pairs with ./integration_slave or peer.py)

#include <eshm.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>

int main() {
    std::cout << "Starting ESHM Master..." << std::endl;

    // Initialize as master
    ESHMConfig config = eshm_default_config("demo_shm");
    config.role = ESHM_ROLE_MASTER;

    ESHMHandle* handle = eshm_init(&config);
    if (!handle) {
        std::cerr << "Failed to initialize ESHM" << std::endl;
        return 1;
    }

    std::cout << "Master initialized. Waiting for slave..." << std::endl;

    int counter = 0;
    char buffer[256];

    while (true) {
        // Send message to slave
        snprintf(buffer, sizeof(buffer), "Message #%d from master", counter++);
        int ret = eshm_write(handle, buffer, strlen(buffer) + 1);

        if (ret == ESHM_SUCCESS) {
            std::cout << "Sent: " << buffer << std::endl;
        }

        // Read response from slave
        int bytes_read = eshm_read(handle, buffer, sizeof(buffer));
        if (bytes_read > 0) {
            std::cout << "Received: " << buffer << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    eshm_destroy(handle);
    return 0;
}
