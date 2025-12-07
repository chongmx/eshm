#include "../../eshm.h"
#include <iostream>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

// Test: Master restart with slave reconnection
void test_master_restart_reconnect() {
    std::cout << "\n=== Test: Master Restart with Slave Reconnection ===" << std::endl;
    
    const char* shm_name = "test_reconnect";
    pid_t master_pid, slave_pid;
    
    // Fork slave process
    slave_pid = fork();
    if (slave_pid == 0) {
        // SLAVE PROCESS
        usleep(100000); // Wait for master to start
        
        ESHMConfig config = eshm_default_config(shm_name);
        config.role = ESHM_ROLE_SLAVE;
        config.use_threads = true;
        config.stale_threshold_ms = 100;
        config.disconnect_behavior = ESHM_DISCONNECT_ON_TIMEOUT;
        
        ESHMHandle* handle = eshm_init(&config);
        if (!handle) {
            std::cerr << "[Slave] Failed to initialize" << std::endl;
            exit(1);
        }
        
        std::cout << "[Slave] Connected to master" << std::endl;
        
        // Communicate with master
        for (int i = 0; i < 3; i++) {
            char recv_buffer[64];
            size_t bytes_read;
            int ret = eshm_read_ex(handle, recv_buffer, sizeof(recv_buffer), &bytes_read, 1000);
            if (ret == ESHM_SUCCESS) {
                std::cout << "[Slave] Received: " << recv_buffer << std::endl;
            }
        }
        
        // Wait for master to crash and detect stale
        std::cout << "[Slave] Waiting for master crash..." << std::endl;
        sleep(2);
        
        bool master_alive;
        eshm_check_remote_alive(handle, &master_alive);
        if (!master_alive) {
            std::cout << "[Slave] DETECTED: Master is stale!" << std::endl;
        }
        
        // Wait for master to restart
        std::cout << "[Slave] Waiting for master to restart..." << std::endl;
        sleep(3);
        
        // Try to communicate again
        eshm_check_remote_alive(handle, &master_alive);
        std::cout << "[Slave] Master alive after restart: " << (master_alive ? "YES" : "NO") << std::endl;
        
        // Try to read from restarted master
        for (int i = 0; i < 3; i++) {
            char recv_buffer[64];
            size_t bytes_read;
            int ret = eshm_read_ex(handle, recv_buffer, sizeof(recv_buffer), &bytes_read, 1000);
            if (ret == ESHM_SUCCESS) {
                std::cout << "[Slave] Received after restart: " << recv_buffer << std::endl;
            } else {
                std::cout << "[Slave] Read failed: " << eshm_error_string(ret) << std::endl;
            }
        }
        
        eshm_destroy(handle);
        exit(0);
    }
    
    // Fork first master process
    master_pid = fork();
    if (master_pid == 0) {
        // FIRST MASTER PROCESS
        ESHMConfig config = eshm_default_config(shm_name);
        config.role = ESHM_ROLE_MASTER;
        config.use_threads = true;
        config.stale_threshold_ms = 100;
        
        ESHMHandle* handle = eshm_init(&config);
        if (!handle) {
            std::cerr << "[Master1] Failed to initialize" << std::endl;
            exit(1);
        }
        
        std::cout << "[Master1] Started, sending messages..." << std::endl;
        
        // Send some messages
        for (int i = 0; i < 3; i++) {
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "Message %d from master1", i);
            eshm_write(handle, buffer, strlen(buffer) + 1);
            usleep(200000);
        }
        
        std::cout << "[Master1] Simulating crash (abrupt exit)..." << std::endl;
        _exit(0);  // Crash without cleanup
    }
    
    // PARENT PROCESS
    usleep(500000); // Wait for first master to start
    
    // Wait for first master to crash
    int status;
    waitpid(master_pid, &status, 0);
    std::cout << "[Parent] First master crashed" << std::endl;
    
    // Wait a bit, then start second master
    sleep(2);
    std::cout << "[Parent] Starting second master..." << std::endl;
    
    pid_t master_pid2 = fork();
    if (master_pid2 == 0) {
        // SECOND MASTER PROCESS
        ESHMConfig config = eshm_default_config(shm_name);
        config.role = ESHM_ROLE_MASTER;
        config.use_threads = true;
        config.stale_threshold_ms = 100;
        
        ESHMHandle* handle = eshm_init(&config);
        if (!handle) {
            std::cerr << "[Master2] Failed to initialize" << std::endl;
            exit(1);
        }
        
        std::cout << "[Master2] Restarted, sending messages..." << std::endl;
        
        // Send some messages
        for (int i = 0; i < 3; i++) {
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "Message %d from master2 (restarted)", i);
            eshm_write(handle, buffer, strlen(buffer) + 1);
            usleep(200000);
        }
        
        sleep(1);
        eshm_destroy(handle);
        exit(0);
    }
    
    // Wait for second master to finish
    waitpid(master_pid2, &status, 0);
    std::cout << "[Parent] Second master finished" << std::endl;
    
    // Wait for slave to finish
    waitpid(slave_pid, &status, 0);
    std::cout << "[Parent] Slave finished" << std::endl;
    
    std::cout << "=== Test Complete ===" << std::endl;
}

int main() {
    std::cout << "=== ESHM Reconnection Tests ===" << std::endl;
    
    test_master_restart_reconnect();
    
    std::cout << "\n=== All Reconnection Tests COMPLETED ===" << std::endl;
    return 0;
}
