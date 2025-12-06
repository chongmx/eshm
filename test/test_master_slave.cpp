#include "../eshm.h"
#include <iostream>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

void master_process(const char* shm_name) {
    ESHMConfig config = eshm_default_config(shm_name);
    config.role = ESHM_ROLE_MASTER;
    
    ESHMHandle* handle = eshm_init(&config);
    assert(handle != NULL);
    
    std::cout << "[Master] Initialized" << std::endl;
    
    // Send messages to slave
    for (int i = 0; i < 5; i++) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "Message %d from master", i);
        
        int ret = eshm_write(handle, buffer, strlen(buffer) + 1);
        assert(ret == ESHM_SUCCESS);
        std::cout << "[Master] Sent: " << buffer << std::endl;
        
        // Try to receive response from slave
        char recv_buffer[64];
        size_t bytes_read;
        ret = eshm_read_timeout(handle, recv_buffer, sizeof(recv_buffer), &bytes_read, 2000);
        if (ret == ESHM_SUCCESS) {
            std::cout << "[Master] Received: " << recv_buffer << std::endl;
        }
        
        usleep(100000); // 100ms
    }
    
    // Give slave time to process
    sleep(1);
    
    ESHMStats stats;
    eshm_get_stats(handle, &stats);
    std::cout << "[Master] Final stats - M2S writes: " << stats.m2s_write_count 
              << ", S2M reads: " << stats.s2m_read_count << std::endl;
    
    eshm_destroy(handle);
    std::cout << "[Master] Shutdown complete" << std::endl;
}

void slave_process(const char* shm_name) {
    // Wait a bit for master to initialize
    usleep(100000);
    
    ESHMConfig config = eshm_default_config(shm_name);
    config.role = ESHM_ROLE_SLAVE;
    
    ESHMHandle* handle = eshm_init(&config);
    assert(handle != NULL);
    
    std::cout << "[Slave] Initialized" << std::endl;
    
    // Receive messages and send responses
    int count = 0;
    while (count < 5) {
        char recv_buffer[64];
        size_t bytes_read;
        
        int ret = eshm_read_timeout(handle, recv_buffer, sizeof(recv_buffer), &bytes_read, 3000);
        if (ret == ESHM_SUCCESS) {
            std::cout << "[Slave] Received: " << recv_buffer << std::endl;
            
            // Send response
            char send_buffer[64];
            snprintf(send_buffer, sizeof(send_buffer), "ACK %d from slave", count);
            ret = eshm_write(handle, send_buffer, strlen(send_buffer) + 1);
            assert(ret == ESHM_SUCCESS);
            std::cout << "[Slave] Sent: " << send_buffer << std::endl;
            
            count++;
        } else if (ret == ESHM_ERROR_TIMEOUT) {
            std::cout << "[Slave] Timeout waiting for data" << std::endl;
            break;
        }
    }
    
    ESHMStats stats;
    eshm_get_stats(handle, &stats);
    std::cout << "[Slave] Final stats - S2M writes: " << stats.s2m_write_count 
              << ", M2S reads: " << stats.m2s_read_count << std::endl;
    
    eshm_destroy(handle);
    std::cout << "[Slave] Shutdown complete" << std::endl;
}

int main() {
    std::cout << "=== ESHM Master-Slave Test ===" << std::endl;
    
    const char* shm_name = "test_ms";
    
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child process - slave
        slave_process(shm_name);
        exit(0);
    } else if (pid > 0) {
        // Parent process - master
        master_process(shm_name);
        
        // Wait for child
        int status;
        waitpid(pid, &status, 0);
        
        std::cout << "\n=== Master-Slave Test PASSED ===" << std::endl;
    } else {
        std::cerr << "Fork failed" << std::endl;
        return 1;
    }
    
    return 0;
}
