#include "eshm.h"
#include "data_handler.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <cmath>
#include <signal.h>

using namespace shm_protocol;

volatile bool running = true;

void signal_handler(int) {
    running = false;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <shm_name> [count]\n";
        std::cerr << "\nExample:\n";
        std::cerr << "  Terminal 1 (C++ Master): " << argv[0] << " test_interop 100\n";
        std::cerr << "  Terminal 2 (Python Slave): python3 py/examples/interop_py_slave.py test_interop\n";
        return 1;
    }

    const char* shm_name = argv[1];
    int max_count = (argc > 2) ? std::atoi(argv[2]) : 100;

    std::cout << "========================================\n";
    std::cout << "  C++ Master -> Python Slave Test\n";
    std::cout << "========================================\n";
    std::cout << "  Shared Memory: " << shm_name << "\n";
    std::cout << "  Max Count: " << max_count << "\n";
    std::cout << "========================================\n\n";

    // Initialize ESHM
    ESHMConfig config = eshm_default_config(shm_name);
    config.role = ESHM_ROLE_MASTER;
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

    std::cout << "C++ Master ready. Waiting for Python slave to connect...\n";

    // Wait for slave
    bool slave_alive = false;
    while (running && !slave_alive) {
        eshm_check_remote_alive(eshm, &slave_alive);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!running) {
        eshm_destroy(eshm);
        return 0;
    }

    std::cout << "Python slave connected! Starting data exchange...\n\n";

    auto start_time = std::chrono::steady_clock::now();
    int64_t counter = 0;

    while (running && counter < max_count) {
        // Generate test data
        double temperature = 20.0 + 5.0 * std::sin(counter * 0.1);
        bool enabled = (counter % 2 == 0);

        std::vector<DataItem> items;
        items.push_back(DataHandler::createInteger("counter", counter));
        items.push_back(DataHandler::createReal("temperature", temperature));
        items.push_back(DataHandler::createBoolean("enabled", enabled));
        items.push_back(DataHandler::createString("status", "OK"));
        items.push_back(DataHandler::createString("source", "C++ Master"));

        // Encode
        auto buffer = handler.encodeDataBuffer(items);

        // Send via ESHM
        int ret = eshm_write(eshm, buffer.data(), buffer.size());
        if (ret < 0) {
            std::cerr << "Write error: " << ret << "\n";
            break;
        }

        // Print every 10th exchange
        if (counter % 10 == 0) {
            std::cout << "[C++ Master] #" << std::setw(4) << counter
                      << " - temp=" << std::fixed << std::setprecision(2) << temperature
                      << ", enabled=" << (enabled ? "true" : "false")
                      << ", buffer=" << buffer.size() << " bytes\n";
        }

        counter++;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto end_time = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end_time - start_time).count();

    std::cout << "\n========================================\n";
    std::cout << "  C++ Master Complete\n";
    std::cout << "========================================\n";
    std::cout << "  Sent: " << counter << " messages\n";
    std::cout << "  Time: " << std::fixed << std::setprecision(2) << elapsed << " s\n";
    std::cout << "  Rate: " << std::fixed << std::setprecision(1) << (counter / elapsed) << " Hz\n";
    std::cout << "========================================\n";

    eshm_destroy(eshm);
    return 0;
}
