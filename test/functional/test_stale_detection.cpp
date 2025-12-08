#include "eshm.h"
#include <iostream>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

void master_process_crash(const char* shm_name) {
    ESHMConfig config = eshm_default_config(shm_name);
    config.role = ESHM_ROLE_MASTER;
    config.use_threads = true;
    config.stale_threshold_ms = 100;

    ESHMHandle* handle = eshm_init(&config);
    assert(handle != NULL);

    std::cout << "[Master] Started, will crash after 1 second..." << std::endl;

    // Send a few messages
    for (int i = 0; i < 3; i++) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "Message %d", i);
        eshm_write(handle, buffer, strlen(buffer) + 1);
        usleep(200000);
    }

    // Simulate crash - exit without cleanup
    std::cout << "[Master] Simulating crash (no cleanup)..." << std::endl;
    _exit(0);  // Exit without cleanup
}

void slave_stale_detect(const char* shm_name) {
    usleep(100000); // Wait for master

    ESHMConfig config = eshm_default_config(shm_name);
    config.role = ESHM_ROLE_SLAVE;
    config.use_threads = true;
    config.stale_threshold_ms = 100;  // 100ms threshold
    config.disconnect_behavior = ESHM_DISCONNECT_ON_TIMEOUT;
    
    ESHMHandle* handle = eshm_init(&config);
    assert(handle != NULL);
    
    std::cout << "[Slave] Started, monitoring master health..." << std::endl;
    
    int msg_count = 0;
    bool detected_stale = false;
    
    for (int i = 0; i < 20; i++) {  // Try for up to 10 seconds
        char buffer[64];
        size_t bytes_read;
        
        int ret = eshm_read_ex(handle, buffer, sizeof(buffer), &bytes_read, 500);
        if (ret == ESHM_SUCCESS) {
            std::cout << "[Slave] Received: " << buffer << std::endl;
            msg_count++;
        }
        
        // Check if master is alive
        bool master_alive;
        eshm_check_remote_alive(handle, &master_alive);
        
        if (!master_alive && !detected_stale) {
            std::cout << "[Slave] DETECTED: Master is stale!" << std::endl;
            detected_stale = true;
            break;
        }
        
        usleep(500000); // 500ms
    }
    
    assert(detected_stale);  // Should have detected stale master
    std::cout << "[Slave] Successfully detected stale master" << std::endl;
    
    eshm_destroy(handle);
}

int main() {
    std::cout << "=== ESHM Stale Detection Test ===" << std::endl;
    
    const char* shm_name = "test_stale";
    
    pid_t master_pid = fork();
    
    if (master_pid == 0) {
        // Child - master that will crash
        master_process_crash(shm_name);
        exit(0);
    } else if (master_pid > 0) {
        usleep(100000);
        
        pid_t slave_pid = fork();
        if (slave_pid == 0) {
            // Grandchild - slave that detects stale master
            slave_stale_detect(shm_name);
            exit(0);
        } else if (slave_pid > 0) {
            // Parent - wait for both
            int status;
            waitpid(master_pid, &status, 0);
            std::cout << "[Parent] Master process exited" << std::endl;
            
            waitpid(slave_pid, &status, 0);
            std::cout << "[Parent] Slave process exited" << std::endl;
            
            std::cout << "\n=== Stale Detection Test PASSED ===" << std::endl;
        }
    } else {
        std::cerr << "Fork failed" << std::endl;
        return 1;
    }
    
    return 0;
}
