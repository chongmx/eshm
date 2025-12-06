#include "../eshm.h"
#include <iostream>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>

uint64_t get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000ULL + tv.tv_usec;
}

void master_perf(const char* shm_name, int num_messages) {
    ESHMConfig config = eshm_default_config(shm_name);
    config.role = ESHM_ROLE_MASTER;
    
    ESHMHandle* handle = eshm_init(&config);
    assert(handle != NULL);
    
    std::cout << "[Master] Starting performance test with " << num_messages << " messages" << std::endl;
    
    char buffer[256];
    memset(buffer, 'A', sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    
    uint64_t start_time = get_time_us();
    
    for (int i = 0; i < num_messages; i++) {
        int ret = eshm_write(handle, buffer, sizeof(buffer));
        assert(ret == ESHM_SUCCESS);
    }
    
    uint64_t end_time = get_time_us();
    uint64_t elapsed_us = end_time - start_time;
    
    double elapsed_sec = elapsed_us / 1000000.0;
    double msg_per_sec = num_messages / elapsed_sec;
    double throughput_mbps = (num_messages * sizeof(buffer) * 8) / elapsed_us;
    
    std::cout << "[Master] Performance results:" << std::endl;
    std::cout << "  Time elapsed: " << elapsed_sec << " seconds" << std::endl;
    std::cout << "  Messages sent: " << num_messages << std::endl;
    std::cout << "  Messages/sec: " << msg_per_sec << std::endl;
    std::cout << "  Throughput: " << throughput_mbps << " Mbps" << std::endl;
    
    // Wait for slave to finish reading
    sleep(2);
    
    ESHMStats stats;
    eshm_get_stats(handle, &stats);
    std::cout << "[Master] Slave read " << stats.m2s_read_count << " messages" << std::endl;
    
    eshm_destroy(handle);
}

void slave_perf(const char* shm_name) {
    usleep(100000); // Wait for master
    
    ESHMConfig config = eshm_default_config(shm_name);
    config.role = ESHM_ROLE_SLAVE;
    
    ESHMHandle* handle = eshm_init(&config);
    assert(handle != NULL);
    
    std::cout << "[Slave] Starting to receive messages" << std::endl;
    
    char buffer[256];
    int msg_count = 0;
    uint64_t start_time = get_time_us();
    
    while (true) {
        size_t bytes_read;
        int ret = eshm_read_timeout(handle, buffer, sizeof(buffer), &bytes_read, 100);
        if (ret == ESHM_SUCCESS) {
            msg_count++;
        } else if (ret == ESHM_ERROR_TIMEOUT || ret == ESHM_ERROR_NO_DATA) {
            // No more data, check if we should continue
            if (msg_count > 0) {
                usleep(100000);
                // Try one more time
                ret = eshm_read_timeout(handle, buffer, sizeof(buffer), &bytes_read, 100);
                if (ret != ESHM_SUCCESS) {
                    break;
                }
                msg_count++;
            }
        }
    }
    
    uint64_t end_time = get_time_us();
    uint64_t elapsed_us = end_time - start_time;
    
    double elapsed_sec = elapsed_us / 1000000.0;
    double msg_per_sec = msg_count / elapsed_sec;
    
    std::cout << "[Slave] Performance results:" << std::endl;
    std::cout << "  Time elapsed: " << elapsed_sec << " seconds" << std::endl;
    std::cout << "  Messages received: " << msg_count << std::endl;
    std::cout << "  Messages/sec: " << msg_per_sec << std::endl;
    
    eshm_destroy(handle);
}

int main() {
    std::cout << "=== ESHM Performance Test ===" << std::endl;
    
    const char* shm_name = "test_perf";
    const int num_messages = 10000;
    
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child - slave
        slave_perf(shm_name);
        exit(0);
    } else if (pid > 0) {
        // Parent - master
        master_perf(shm_name, num_messages);
        
        // Wait for child
        int status;
        waitpid(pid, &status, 0);
        
        std::cout << "\n=== Performance Test COMPLETED ===" << std::endl;
    } else {
        std::cerr << "Fork failed" << std::endl;
        return 1;
    }
    
    return 0;
}
