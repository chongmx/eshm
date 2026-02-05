#include "eshm.h"
#include "data_handler.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <signal.h>

using namespace shm_protocol;

volatile bool running = true;

void signal_handler(int) {
    running = false;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <shm_name>\n";
        std::cerr << "\nExample:\n";
        std::cerr << "  Terminal 1 (Python Master): python3 py/examples/interop_py_master.py test_interop2 100\n";
        std::cerr << "  Terminal 2 (C++ Slave): " << argv[0] << " test_interop2\n";
        return 1;
    }

    const char* shm_name = argv[1];

    std::cout << "========================================\n";
    std::cout << "  C++ Slave <- Python Master Test\n";
    std::cout << "========================================\n";
    std::cout << "  Shared Memory: " << shm_name << "\n";
    std::cout << "========================================\n\n";

    // Initialize ESHM
    ESHMConfig config = eshm_default_config(shm_name);
    config.role = ESHM_ROLE_SLAVE;
    config.disconnect_behavior = ESHM_DISCONNECT_ON_TIMEOUT;
    config.stale_threshold_ms = 100;
    config.auto_cleanup = true;

    ESHMHandle* eshm = eshm_init(&config);
    if (!eshm) {
        std::cerr << "Failed to create ESHM\n";
        return 1;
    }

    DataHandler handler;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::cout << "C++ slave ready. Waiting for Python master...\n";

    uint8_t buffer[ESHM_MAX_DATA_SIZE];

    // Wait for first data
    bool first_data = false;
    while (running && !first_data) {
        int ret = eshm_read(eshm, buffer, sizeof(buffer));
        if (ret > 0) {
            first_data = true;
            std::cout << "Python master detected! Starting to receive data...\n\n";

            // Process first message
            try {
                auto items = handler.decodeDataBuffer(buffer, ret);
                auto values = DataHandler::extractSimpleValues(items);
                int64_t counter = std::get<int64_t>(values["counter"]);
                std::cout << "[C++ Slave] #" << std::setw(4) << counter << " - First message received\n";
            } catch (const std::exception& e) {
                std::cerr << "Decode error on first message: " << e.what() << "\n";
            }
        } else if (ret != ESHM_ERROR_NO_DATA) {
            std::cerr << "Read error: " << ret << "\n";
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!running || !first_data) {
        eshm_destroy(eshm);
        return 0;
    }

    // Statistics
    uint64_t total_received = 1;
    uint64_t decode_errors = 0;
    auto start_time = std::chrono::steady_clock::now();

    // Receive loop
    while (running) {
        int ret = eshm_read(eshm, buffer, sizeof(buffer));

        if (ret > 0) {
            try {
                // Decode
                auto items = handler.decodeDataBuffer(buffer, ret);
                auto values = DataHandler::extractSimpleValues(items);

                int64_t counter = std::get<int64_t>(values["counter"]);
                double temperature = std::get<double>(values["temperature"]);
                bool enabled = std::get<bool>(values["enabled"]);
                std::string status = std::get<std::string>(values["status"]);
                std::string source = std::get<std::string>(values["source"]);

                total_received++;

                // Print every 10th exchange
                if (counter % 10 == 0) {
                    std::cout << "[C++ Slave] #" << std::setw(4) << counter
                              << " - temp=" << std::fixed << std::setprecision(2) << temperature
                              << ", enabled=" << (enabled ? "true" : "false")
                              << ", status=\"" << status << "\", source=\"" << source << "\"\n";
                }

            } catch (const std::exception& e) {
                decode_errors++;
                if (decode_errors < 10) {
                    std::cerr << "Decode error: " << e.what() << "\n";
                }
            }
        } else if (ret == ESHM_ERROR_NO_DATA) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        } else {
            std::cerr << "Read error: " << ret << "\n";
            break;
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end_time - start_time).count();

    std::cout << "\n========================================\n";
    std::cout << "  C++ Slave Complete\n";
    std::cout << "========================================\n";
    std::cout << "  Received: " << total_received << " messages\n";
    std::cout << "  Time: " << std::fixed << std::setprecision(2) << elapsed << " s\n";
    if (elapsed > 0) {
        std::cout << "  Rate: " << std::fixed << std::setprecision(1) << (total_received / elapsed) << " Hz\n";
    }
    std::cout << "  Decode errors: " << decode_errors << "\n";
    std::cout << "========================================\n";

    eshm_destroy(eshm);
    return 0;
}
