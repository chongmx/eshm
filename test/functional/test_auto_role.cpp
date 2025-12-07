#include "../../eshm.h"
#include <iostream>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

void auto_process_1(const char* shm_name) {
    std::cout << "[Process 1] Starting with AUTO role" << std::endl;
    
    ESHMConfig config = eshm_default_config(shm_name);
    config.role = ESHM_ROLE_AUTO;
    
    ESHMHandle* handle = eshm_init(&config);
    assert(handle != NULL);
    
    enum ESHMRole actual_role;
    eshm_get_role(handle, &actual_role);
    std::cout << "[Process 1] Actual role: " 
              << (actual_role == ESHM_ROLE_MASTER ? "MASTER" : "SLAVE") << std::endl;
    
    // First process should become master (no existing SHM)
    assert(actual_role == ESHM_ROLE_MASTER);
    
    // Send some messages
    for (int i = 0; i < 3; i++) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "Message %d", i);
        eshm_write(handle, buffer, strlen(buffer) + 1);
        
        char recv_buffer[64];
        size_t bytes_read;
        int ret = eshm_read_ex(handle, recv_buffer, sizeof(recv_buffer), &bytes_read, 1000);
        if (ret == ESHM_SUCCESS) {
            std::cout << "[Process 1] Received: " << recv_buffer << std::endl;
        }
        
        usleep(200000);
    }
    
    sleep(1);
    eshm_destroy(handle);
    std::cout << "[Process 1] Shutdown" << std::endl;
}

void auto_process_2(const char* shm_name) {
    // Wait for process 1 to create SHM
    usleep(200000);
    
    std::cout << "[Process 2] Starting with AUTO role" << std::endl;
    
    ESHMConfig config = eshm_default_config(shm_name);
    config.role = ESHM_ROLE_AUTO;
    
    ESHMHandle* handle = eshm_init(&config);
    assert(handle != NULL);
    
    enum ESHMRole actual_role;
    eshm_get_role(handle, &actual_role);
    std::cout << "[Process 2] Actual role: " 
              << (actual_role == ESHM_ROLE_MASTER ? "MASTER" : "SLAVE") << std::endl;
    
    // Second process should become slave (existing SHM)
    assert(actual_role == ESHM_ROLE_SLAVE);
    
    // Receive and respond
    for (int i = 0; i < 3; i++) {
        char recv_buffer[64];
        size_t bytes_read;
        int ret = eshm_read_ex(handle, recv_buffer, sizeof(recv_buffer), &bytes_read, 2000);
        if (ret == ESHM_SUCCESS) {
            std::cout << "[Process 2] Received: " << recv_buffer << std::endl;
            
            char send_buffer[64];
            snprintf(send_buffer, sizeof(send_buffer), "ACK %d", i);
            eshm_write(handle, send_buffer, strlen(send_buffer) + 1);
        }
    }
    
    eshm_destroy(handle);
    std::cout << "[Process 2] Shutdown" << std::endl;
}

int main() {
    std::cout << "=== ESHM Auto Role Test ===" << std::endl;
    
    const char* shm_name = "test_auto";
    
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child process
        auto_process_2(shm_name);
        exit(0);
    } else if (pid > 0) {
        // Parent process
        auto_process_1(shm_name);
        
        // Wait for child
        int status;
        waitpid(pid, &status, 0);
        
        std::cout << "\n=== Auto Role Test PASSED ===" << std::endl;
    } else {
        std::cerr << "Fork failed" << std::endl;
        return 1;
    }
    
    return 0;
}
