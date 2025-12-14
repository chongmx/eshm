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

struct Statistics {
    uint64_t exchanges = 0;
    uint64_t decode_errors = 0;
    double min_temp = 1e9;
    double max_temp = -1e9;
    double sum_temp = 0;
    int64_t min_counter = INT64_MAX;
    int64_t max_counter = INT64_MIN;

    std::chrono::steady_clock::time_point start_time;

    void update(double temp, int64_t counter) {
        exchanges++;
        min_temp = std::min(min_temp, temp);
        max_temp = std::max(max_temp, temp);
        sum_temp += temp;
        min_counter = std::min(min_counter, counter);
        max_counter = std::max(max_counter, counter);
    }

    void print() {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start_time).count();
        double avg_temp = exchanges > 0 ? sum_temp / exchanges : 0;

        std::cout << "\n=== Statistics (after " << exchanges << " exchanges) ===\n";
        std::cout << "  Elapsed time: " << std::fixed << std::setprecision(3)
                  << elapsed << " s\n";
        std::cout << "  Exchange rate: " << std::fixed << std::setprecision(1)
                  << (exchanges / elapsed) << " Hz\n";
        std::cout << "  Temperature: min=" << std::fixed << std::setprecision(2)
                  << min_temp << ", max=" << max_temp
                  << ", avg=" << avg_temp << "\n";
        std::cout << "  Counter: min=" << min_counter
                  << ", max=" << max_counter << "\n";
        std::cout << "  Decode errors: " << decode_errors << "\n";
    }

    void reset_interval() {
        min_temp = 1e9;
        max_temp = -1e9;
        sum_temp = 0;
        min_counter = INT64_MAX;
        max_counter = INT64_MIN;
    }
};

void run_master(const char* shm_name) {
    std::cout << "Starting MASTER mode\n";

    ESHMConfig config = {0};
    config.shm_name = shm_name;
    config.role = ESHM_ROLE_MASTER;
    config.disconnect_behavior = ESHM_DISCONNECT_ON_TIMEOUT;
    config.stale_threshold_ms = 100;  // 100ms (100 checks at 1kHz)
    config.auto_cleanup = true;
    config.use_threads = true;  // Enable heartbeat and monitor threads

    ESHMHandle* eshm = eshm_init(&config);
    if (!eshm) {
        std::cerr << "Failed to create ESHM\n";
        return;
    }

    DataHandler handler;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int64_t counter = 0;
    auto cycle_start = std::chrono::steady_clock::now();

    std::cout << "Master ready. Starting data exchange at 1kHz...\n";
    std::cout << "(Slave will connect when ready)\n\n";

    while (running) {
        auto frame_start = std::chrono::steady_clock::now();

        // Generate data: counter, temperature, status message
        double temperature = 20.0 + 5.0 * std::sin(counter * 0.01);

        std::vector<DataItem> items;
        items.push_back(DataHandler::createInteger("counter", counter));
        items.push_back(DataHandler::createReal("temperature", temperature));
        items.push_back(DataHandler::createString("status", "OK"));

        // Encode
        auto buffer = handler.encodeDataBuffer(items);

        // Send via ESHM
        int ret = eshm_write(eshm, buffer.data(), buffer.size());
        if (ret < 0) {
            std::cerr << "Write error: " << ret << "\n";
        }

        // Print every 1000th exchange
        if (counter % 1000 == 0 && counter > 0) {
            std::cout << "[Master] Exchange #" << counter
                      << " - temp=" << std::fixed << std::setprecision(2) << temperature
                      << ", buffer_size=" << buffer.size() << " bytes\n";
        }

        counter++;

        // Sleep to maintain 1kHz
        auto frame_end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(frame_end - frame_start);
        auto sleep_time = std::chrono::microseconds(1000) - elapsed;

        if (sleep_time.count() > 0) {
            std::this_thread::sleep_for(sleep_time);
        }
    }

    std::cout << "\nMaster shutting down after " << counter << " exchanges\n";
    eshm_destroy(eshm);
}

void run_slave(const char* shm_name) {
    std::cout << "Starting SLAVE mode\n";

    ESHMConfig config = {0};
    config.shm_name = shm_name;
    config.role = ESHM_ROLE_SLAVE;
    config.disconnect_behavior = ESHM_DISCONNECT_ON_TIMEOUT;
    config.stale_threshold_ms = 100;
    config.auto_cleanup = true;
    config.use_threads = true;  // Enable heartbeat and monitor threads

    ESHMHandle* eshm = eshm_init(&config);
    if (!eshm) {
        std::cerr << "Failed to create ESHM\n";
        return;
    }

    DataHandler handler;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    Statistics stats;
    stats.start_time = std::chrono::steady_clock::now();
    uint8_t buffer[ESHM_MAX_DATA_SIZE];

    std::cout << "Slave ready. Waiting for master...\n";

    // Wait for first data
    while (running) {
        int ret = eshm_read(eshm, buffer, sizeof(buffer));
        if (ret > 0) {
            std::cout << "Master detected! Starting to receive data...\n\n";
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!running) {
        eshm_destroy(eshm);
        return;
    }

    while (running) {
        int ret = eshm_read(eshm, buffer, sizeof(buffer));

        if (ret > 0) {
            try {
                // Decode
                auto items = handler.decodeDataBuffer(buffer, ret);
                auto values = DataHandler::extractSimpleValues(items);

                int64_t counter = std::get<int64_t>(values["counter"]);
                double temperature = std::get<double>(values["temperature"]);
                std::string status = std::get<std::string>(values["status"]);

                stats.update(temperature, counter);

                // Print every 1000th exchange
                if (counter % 1000 == 0 && counter > 0) {
                    std::cout << "[Slave] Exchange #" << counter
                              << " - temp=" << std::fixed << std::setprecision(2) << temperature
                              << ", status=\"" << status << "\"\n";
                }

                // Print statistics every 5000 exchanges
                if (counter % 5000 == 0 && counter > 0) {
                    stats.print();
                    stats.reset_interval();
                }

            } catch (const std::exception& e) {
                stats.decode_errors++;
                if (stats.decode_errors < 10) {
                    std::cerr << "Decode error: " << e.what() << "\n";
                }
            }
        } else if (ret == ESHM_ERROR_NO_DATA) {
            // No new data, wait a bit
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        } else {
            std::cerr << "Read error: " << ret << "\n";
            break;
        }
    }

    std::cout << "\nSlave shutting down\n";
    stats.print();
    eshm_destroy(eshm);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <master|slave> <shm_name>\n";
        std::cerr << "\nExample:\n";
        std::cerr << "  Terminal 1: " << argv[0] << " master test_exchange\n";
        std::cerr << "  Terminal 2: " << argv[0] << " slave test_exchange\n";
        return 1;
    }

    std::string mode = argv[1];
    const char* shm_name = argv[2];

    if (mode == "master") {
        run_master(shm_name);
    } else if (mode == "slave") {
        run_slave(shm_name);
    } else {
        std::cerr << "Invalid mode: " << mode << " (expected 'master' or 'slave')\n";
        return 1;
    }

    return 0;
}
