/**
 * ESHM C++ Benchmark Tool
 *
 * Similar to py/examples/benchmark_master.py but for C++
 * Tests bidirectional communication (write + read ACK)
 */

#include "eshm.h"
#include <iostream>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>

volatile sig_atomic_t g_running = 1;

void signal_handler(int signum) {
    (void)signum;
    g_running = 0;
}

uint64_t get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000ULL + tv.tv_usec;
}

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " <master|slave> [shm_name] [stats_interval]" << std::endl;
    std::cout << "\nBenchmark C++ Master ↔ C++ Slave bidirectional performance" << std::endl;
    std::cout << "\nParameters:" << std::endl;
    std::cout << "  shm_name: Shared memory name (default: 'cpp_bench')" << std::endl;
    std::cout << "  stats_interval: Print stats every N messages (default: 1000)" << std::endl;
    std::cout << "\nExamples:" << std::endl;
    std::cout << "  Terminal 1: " << prog_name << " master" << std::endl;
    std::cout << "  Terminal 2: " << prog_name << " slave" << std::endl;
}

void run_master(const char* shm_name, int stats_interval) {
    std::cout << "=== ESHM C++ Benchmark Master ===" << std::endl;
    std::cout << "PID: " << getpid() << std::endl;
    std::cout << "SHM Name: " << shm_name << std::endl;
    std::cout << "Stats interval: every " << stats_interval << " messages\n" << std::endl;

    ESHMConfig config = eshm_default_config(shm_name);
    config.role = ESHM_ROLE_MASTER;

    ESHMHandle* handle = eshm_init(&config);
    if (!handle) {
        std::cerr << "Failed to initialize ESHM" << std::endl;
        return;
    }

    std::cout << "Initialized as MASTER" << std::endl;
    std::cout << "Waiting for slave to connect..." << std::endl;

    // Wait for slave
    bool slave_alive = false;
    while (!slave_alive && g_running) {
        eshm_check_remote_alive(handle, &slave_alive);
        if (!slave_alive) {
            usleep(100000); // 100ms
        }
    }

    if (!g_running) {
        eshm_destroy(handle);
        return;
    }

    std::cout << "Slave connected. Starting benchmark..." << std::endl;
    std::cout << "Press Ctrl+C to stop\n" << std::endl;
    std::cout.flush();

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int message_count = 0;
    int response_count = 0;
    uint64_t start_time = get_time_us();
    uint64_t last_print_time = start_time;

    char send_buffer[256];
    char recv_buffer[256];
    size_t bytes_read;

    while (g_running) {
        // Send message
        snprintf(send_buffer, sizeof(send_buffer), "Hello from C++ master #%d", message_count);
        int ret = eshm_write(handle, send_buffer, strlen(send_buffer) + 1);
        if (ret == ESHM_SUCCESS) {
            message_count++;
        }

        // Try to read response (non-blocking)
        ret = eshm_read_ex(handle, recv_buffer, sizeof(recv_buffer), &bytes_read, 0);
        if (ret == ESHM_SUCCESS) {
            response_count++;
        }

        // Print stats at intervals
        if (message_count % stats_interval == 0) {
            uint64_t now = get_time_us();
            double elapsed = (now - start_time) / 1000000.0;
            double interval_elapsed = (now - last_print_time) / 1000000.0;
            double send_rate = message_count / elapsed;
            double interval_rate = stats_interval / interval_elapsed;

            printf("[%6d] Total: %6.1fs, %6.1f msg/s | Interval: %6.1f msg/s | Responses: %d\n",
                   message_count, elapsed, send_rate, interval_rate, response_count);
            fflush(stdout);
            last_print_time = now;
        }
    }

    // Print final stats
    uint64_t end_time = get_time_us();
    double elapsed = (end_time - start_time) / 1000000.0;
    double send_rate = message_count / elapsed;

    std::cout << "\n=== Final Benchmark Results ===" << std::endl;
    std::cout << "Total messages sent: " << message_count << std::endl;
    std::cout << "Total responses: " << response_count << std::endl;
    std::cout << "Total time: " << elapsed << "s" << std::endl;
    std::cout << "Average send rate: " << send_rate << " msg/s" << std::endl;

    ESHMStats stats;
    eshm_get_stats(handle, &stats);
    std::cout << "\n=== ESHM Statistics ===" << std::endl;
    std::cout << "Master->Slave: writes=" << stats.m2s_write_count
              << ", reads=" << stats.m2s_read_count << std::endl;
    std::cout << "Slave->Master: writes=" << stats.s2m_write_count
              << ", reads=" << stats.s2m_read_count << std::endl;

    eshm_destroy(handle);
}

void run_slave(const char* shm_name, int stats_interval) {
    std::cout << "=== ESHM C++ Benchmark Slave ===" << std::endl;
    std::cout << "PID: " << getpid() << std::endl;
    std::cout << "SHM Name: " << shm_name << std::endl;
    std::cout << "Stats interval: every " << stats_interval << " messages\n" << std::endl;

    ESHMConfig config = eshm_default_config(shm_name);
    config.role = ESHM_ROLE_SLAVE;
    config.max_reconnect_attempts = 0; // Unlimited retries
    config.reconnect_retry_interval_ms = 100;

    ESHMHandle* handle = eshm_init(&config);
    if (!handle) {
        std::cerr << "Failed to initialize ESHM" << std::endl;
        return;
    }

    std::cout << "Initialized as SLAVE" << std::endl;
    std::cout << "Benchmark running..." << std::endl;
    std::cout << "Press Ctrl+C to stop\n" << std::endl;
    std::cout.flush();

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int message_count = 0;
    uint64_t start_time = get_time_us();
    uint64_t last_print_time = start_time;

    char recv_buffer[256];
    char send_buffer[256];
    size_t bytes_read;

    while (g_running) {
        // Read message from master
        int ret = eshm_read_ex(handle, recv_buffer, sizeof(recv_buffer), &bytes_read, 1000);

        if (ret == ESHM_SUCCESS && bytes_read > 0) {
            // Send acknowledgment
            snprintf(send_buffer, sizeof(send_buffer), "ACK from C++ slave #%d", message_count);
            eshm_write(handle, send_buffer, strlen(send_buffer) + 1);

            message_count++;

            // Print stats at intervals
            if (message_count % stats_interval == 0) {
                uint64_t now = get_time_us();
                double elapsed = (now - start_time) / 1000000.0;
                double interval_elapsed = (now - last_print_time) / 1000000.0;
                double rate = message_count / elapsed;
                double interval_rate = stats_interval / interval_elapsed;

                printf("[%6d] Total: %6.1fs, %6.1f msg/s | Interval: %6.1f msg/s\n",
                       message_count, elapsed, rate, interval_rate);
                fflush(stdout);
                last_print_time = now;
            }
        }
    }

    // Print final stats
    uint64_t end_time = get_time_us();
    double elapsed = (end_time - start_time) / 1000000.0;
    double rate = message_count / elapsed;

    std::cout << "\n=== Final Benchmark Results ===" << std::endl;
    std::cout << "Total messages: " << message_count << std::endl;
    std::cout << "Total time: " << elapsed << "s" << std::endl;
    std::cout << "Average rate: " << rate << " msg/s" << std::endl;

    ESHMStats stats;
    eshm_get_stats(handle, &stats);
    std::cout << "\n=== ESHM Statistics ===" << std::endl;
    std::cout << "Master->Slave: writes=" << stats.m2s_write_count
              << ", reads=" << stats.m2s_read_count << std::endl;
    std::cout << "Slave->Master: writes=" << stats.s2m_write_count
              << ", reads=" << stats.s2m_read_count << std::endl;

    eshm_destroy(handle);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char* mode = argv[1];
    const char* shm_name = (argc > 2) ? argv[2] : "cpp_bench";
    int stats_interval = (argc > 3) ? atoi(argv[3]) : 1000;

    if (stats_interval <= 0) {
        std::cerr << "Error: stats_interval must be positive" << std::endl;
        return 1;
    }

    if (strcmp(mode, "master") == 0) {
        run_master(shm_name, stats_interval);
    } else if (strcmp(mode, "slave") == 0) {
        run_slave(shm_name, stats_interval);
    } else {
        std::cerr << "Invalid mode: " << mode << std::endl;
        std::cerr << "Must be 'master' or 'slave'" << std::endl;
        return 1;
    }

    return 0;
}
