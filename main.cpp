#include "eshm.h"
#include <iostream>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>

// Performance tuning parameters
#define MESSAGE_INTERVAL_US 1000           // Microseconds between messages (1ms = 1000 msg/sec)
#define STATS_PRINT_INTERVAL_SEC 1.0       // Print stats every N seconds
#define STATS_PRINT_CYCLES ((int)((STATS_PRINT_INTERVAL_SEC * 1000000.0) / MESSAGE_INTERVAL_US))

static volatile bool g_running = true;

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_running = false;
    }
}

void print_stats(ESHMHandle* handle) {
    return;
    ESHMStats stats;
    if (eshm_get_stats(handle, &stats) == ESHM_SUCCESS) {
        std::cout << "\n=== ESHM Statistics ===" << std::endl;
        std::cout << "Master PID: " << stats.master_pid 
                  << " (alive: " << (stats.master_alive ? "yes" : "no") << ")" << std::endl;
        std::cout << "Slave PID: " << stats.slave_pid 
                  << " (alive: " << (stats.slave_alive ? "yes" : "no") << ")" << std::endl;
        std::cout << "Master heartbeat: " << stats.master_heartbeat 
                  << " (delta: " << stats.master_heartbeat_delta << "/sec)" << std::endl;
        std::cout << "Slave heartbeat: " << stats.slave_heartbeat 
                  << " (delta: " << stats.slave_heartbeat_delta << "/sec)" << std::endl;
        std::cout << "Stale threshold: " << stats.stale_threshold << "ms" << std::endl;
        std::cout << "Master->Slave: writes=" << stats.m2s_write_count 
                  << ", reads=" << stats.m2s_read_count << std::endl;
        std::cout << "Slave->Master: writes=" << stats.s2m_write_count 
                  << ", reads=" << stats.s2m_read_count << std::endl;
        std::cout << "=======================" << std::endl;
    }
}

void run_master(const char* shm_name) {
    std::cout << "[MASTER] Starting high-performance master process..." << std::endl;
    
    ESHMConfig config = eshm_default_config(shm_name);
    config.role = ESHM_ROLE_MASTER;
    config.use_threads = true;
    config.stale_threshold_ms = 100;  // 100ms stale detection
    
    ESHMHandle* handle = eshm_init(&config);
    if (!handle) {
        std::cerr << "[MASTER] Failed to initialize ESHM" << std::endl;
        return;
    }
    
    enum ESHMRole role;
    eshm_get_role(handle, &role);
    std::cout << "[MASTER] Initialized with role: "
              << (role == ESHM_ROLE_MASTER ? "MASTER" : "SLAVE") << std::endl;
    std::cout << "[MASTER] Heartbeat thread running at 1ms intervals" << std::endl;
    std::cout << "[MASTER] Starting message loop at " << (1000000.0 / MESSAGE_INTERVAL_US)
              << " msg/sec (printing stats every " << STATS_PRINT_CYCLES << " messages)" << std::endl;
    std::cout.flush();

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int message_count = 0;
    int cycle_count = 0;

    while (g_running) {
        // Send message to slave
        char send_buffer[256];
        snprintf(send_buffer, sizeof(send_buffer), "Hello from master #%d", message_count++);

        int ret = eshm_write(handle, send_buffer, strlen(send_buffer) + 1);
        if (ret != ESHM_SUCCESS) {
            std::cerr << "[MASTER] Write error: " << eshm_error_string(ret) << std::endl;
        }

        // Try to receive message from slave (non-blocking with short timeout)
        char recv_buffer[256];
        size_t bytes_read;
        ret = eshm_read_ex(handle, recv_buffer, sizeof(recv_buffer), &bytes_read, 10);

        // Print progress at controlled intervals (not every message to avoid flooding)
        cycle_count++;
        if (cycle_count >= STATS_PRINT_CYCLES) {
            cycle_count = 0;
            std::cout << "[MASTER] Messages: sent=" << message_count
                      << " (" << (1000000.0 / MESSAGE_INTERVAL_US) << " msg/sec)"
                      << std::endl;
            std::cout.flush();

            // Check if slave is alive
            bool slave_alive;
            if (eshm_check_remote_alive(handle, &slave_alive) == ESHM_SUCCESS) {
                if (!slave_alive) {
                    std::cout << "[MASTER] WARNING: Slave is stale/disconnected!" << std::endl;
                }
            }
        }

        usleep(MESSAGE_INTERVAL_US);
    }
    
    std::cout << "\n[MASTER] Shutting down..." << std::endl;
    print_stats(handle);
    eshm_destroy(handle);
}

void run_slave(const char* shm_name) {
    std::cout << "[SLAVE] Starting high-performance slave process..." << std::endl;
    
    ESHMConfig config = eshm_default_config(shm_name);
    config.role = ESHM_ROLE_SLAVE;
    config.use_threads = true;
    config.disconnect_behavior = ESHM_DISCONNECT_ON_TIMEOUT;
    config.stale_threshold_ms = 100;  // 100ms stale detection
    
    ESHMHandle* handle = eshm_init(&config);
    if (!handle) {
        std::cerr << "[SLAVE] Failed to initialize ESHM" << std::endl;
        return;
    }
    
    enum ESHMRole role;
    eshm_get_role(handle, &role);
    std::cout << "[SLAVE] Initialized with role: "
              << (role == ESHM_ROLE_MASTER ? "MASTER" : "SLAVE") << std::endl;
    std::cout << "[SLAVE] Heartbeat thread running at 1ms intervals" << std::endl;
    std::cout << "[SLAVE] Monitor thread checking master health" << std::endl;
    std::cout << "[SLAVE] Starting message loop at " << (1000000.0 / MESSAGE_INTERVAL_US)
              << " msg/sec (printing stats every " << STATS_PRINT_CYCLES << " messages)" << std::endl;
    std::cout.flush();

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int message_count = 0;
    int cycle_count = 0;

    while (g_running) {
        // Try to receive message from master (with short timeout matching message interval)
        char recv_buffer[256];
        size_t bytes_read_size;
        int ret = eshm_read_ex(handle, recv_buffer, sizeof(recv_buffer), &bytes_read_size,
                               (MESSAGE_INTERVAL_US / 1000) + 10);  // Convert to ms with margin

        if (ret == ESHM_SUCCESS && bytes_read_size > 0) {
            // Send response
            char send_buffer[256];
            snprintf(send_buffer, sizeof(send_buffer), "ACK from slave #%d", message_count++);
            eshm_write(handle, send_buffer, strlen(send_buffer) + 1);
        } else if (ret == ESHM_ERROR_MASTER_STALE) {
            std::cerr << "[SLAVE] Master is stale, disconnecting..." << std::endl;
            break;
        }

        // Print progress at controlled intervals (not every message to avoid flooding)
        cycle_count++;
        if (cycle_count >= STATS_PRINT_CYCLES) {
            cycle_count = 0;
            std::cout << "[SLAVE] Messages: received=" << message_count
                      << " (" << (1000000.0 / MESSAGE_INTERVAL_US) << " msg/sec)"
                      << std::endl;
            std::cout.flush();

            // Check if master is alive
            bool master_alive;
            if (eshm_check_remote_alive(handle, &master_alive) == ESHM_SUCCESS) {
                if (!master_alive) {
                    std::cout << "[SLAVE] WARNING: Master is stale/disconnected!" << std::endl;
                }
            }
        }

        usleep(MESSAGE_INTERVAL_US);
    }
    
    std::cout << "\n[SLAVE] Shutting down..." << std::endl;
    print_stats(handle);
    eshm_destroy(handle);
}

void run_auto(const char* shm_name) {
    std::cout << "[AUTO] Starting with auto role (high-performance)..." << std::endl;
    
    ESHMConfig config = eshm_default_config(shm_name);
    config.role = ESHM_ROLE_AUTO;
    config.use_threads = true;
    config.stale_threshold_ms = 100;
    
    ESHMHandle* handle = eshm_init(&config);
    if (!handle) {
        std::cerr << "[AUTO] Failed to initialize ESHM" << std::endl;
        return;
    }
    
    enum ESHMRole role;
    eshm_get_role(handle, &role);
    std::cout << "[AUTO] Initialized with actual role: " 
              << (role == ESHM_ROLE_MASTER ? "MASTER" : "SLAVE") << std::endl;
    std::cout << "[AUTO] Heartbeat thread running at 1ms intervals" << std::endl;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int message_count = 0;
    int cycle_count = 0;

    if (role == ESHM_ROLE_MASTER) {
        // Act as master
        while (g_running) {
            char send_buffer[256];
            snprintf(send_buffer, sizeof(send_buffer), "Hello from AUTO-MASTER #%d", message_count++);

            eshm_write(handle, send_buffer, strlen(send_buffer) + 1);

            char recv_buffer[256];
            size_t bytes_read;
            eshm_read_ex(handle, recv_buffer, sizeof(recv_buffer), &bytes_read, 10);

            // Print progress at controlled intervals
            cycle_count++;
            if (cycle_count >= STATS_PRINT_CYCLES) {
                cycle_count = 0;
                std::cout << "[AUTO-MASTER] Messages: sent=" << message_count
                          << " (" << (1000000.0 / MESSAGE_INTERVAL_US) << " msg/sec)"
                          << std::endl;
            }

            usleep(MESSAGE_INTERVAL_US);
        }
    } else {
        // Act as slave
        while (g_running) {
            char recv_buffer[256];
            size_t bytes_read;
            int ret = eshm_read_ex(handle, recv_buffer, sizeof(recv_buffer), &bytes_read,
                                   (MESSAGE_INTERVAL_US / 1000) + 10);
            if (ret == ESHM_SUCCESS && bytes_read > 0) {
                char send_buffer[256];
                snprintf(send_buffer, sizeof(send_buffer), "ACK from AUTO-SLAVE #%d", message_count++);
                eshm_write(handle, send_buffer, strlen(send_buffer) + 1);
            } else if (ret == ESHM_ERROR_MASTER_STALE) {
                std::cerr << "[AUTO-SLAVE] Master is stale, exiting..." << std::endl;
                break;
            }

            // Print progress at controlled intervals
            cycle_count++;
            if (cycle_count >= STATS_PRINT_CYCLES) {
                cycle_count = 0;
                std::cout << "[AUTO-SLAVE] Messages: received=" << message_count
                          << " (" << (1000000.0 / MESSAGE_INTERVAL_US) << " msg/sec)"
                          << std::endl;
            }

            usleep(MESSAGE_INTERVAL_US);
        }
    }
    
    std::cout << "\n[AUTO] Shutting down..." << std::endl;
    print_stats(handle);
    eshm_destroy(handle);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <master|slave|auto> [shm_name]" << std::endl;
        std::cout << "\nHigh-Performance ESHM Demo:" << std::endl;
        std::cout << "  - 1ms heartbeat updates via dedicated thread" << std::endl;
        std::cout << "  - Sequence locks for lock-free reads" << std::endl;
        std::cout << "  - Configurable stale detection (default 100ms)" << std::endl;
        std::cout << "\nExample:" << std::endl;
        std::cout << "  Terminal 1: " << argv[0] << " master" << std::endl;
        std::cout << "  Terminal 2: " << argv[0] << " slave" << std::endl;
        std::cout << "  or" << std::endl;
        std::cout << "  Terminal 1: " << argv[0] << " auto" << std::endl;
        std::cout << "  Terminal 2: " << argv[0] << " auto" << std::endl;
        return 1;
    }
    
    const char* mode = argv[1];
    const char* shm_name = (argc >= 3) ? argv[2] : "eshm1";
    
    std::cout << "=== High-Performance Enhanced SHM Demo ===" << std::endl;
    std::cout << "SHM Name: " << shm_name << std::endl;
    std::cout << "Mode: " << mode << std::endl;
    std::cout << "PID: " << getpid() << std::endl;
    std::cout << "==========================================" << std::endl;
    
    if (strcmp(mode, "master") == 0) {
        run_master(shm_name);
    } else if (strcmp(mode, "slave") == 0) {
        run_slave(shm_name);
    } else if (strcmp(mode, "auto") == 0) {
        run_auto(shm_name);
    } else {
        std::cerr << "Invalid mode: " << mode << std::endl;
        std::cerr << "Must be 'master', 'slave', or 'auto'" << std::endl;
        return 1;
    }
    
    return 0;
}
