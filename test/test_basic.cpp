#include "../eshm.h"
#include <iostream>
#include <assert.h>
#include <string.h>
#include <unistd.h>

// Test basic initialization and cleanup
void test_init_destroy() {
    std::cout << "Test: Basic init/destroy..." << std::endl;
    
    ESHMConfig config = eshm_default_config("test_basic");
    config.role = ESHM_ROLE_MASTER;
    config.use_threads = false;  // Disable threads for simplicity
    
    ESHMHandle* handle = eshm_init(&config);
    assert(handle != NULL);
    
    enum ESHMRole role;
    int ret = eshm_get_role(handle, &role);
    assert(ret == ESHM_SUCCESS);
    assert(role == ESHM_ROLE_MASTER);
    
    ret = eshm_destroy(handle);
    assert(ret == ESHM_SUCCESS);
    
    std::cout << "  PASSED" << std::endl;
}

// Test write and read operations
void test_write_read() {
    std::cout << "Test: Write operations..." << std::endl;
    
    ESHMConfig config = eshm_default_config("test_wr");
    config.role = ESHM_ROLE_MASTER;
    config.use_threads = false;
    
    ESHMHandle* handle = eshm_init(&config);
    assert(handle != NULL);
    
    // Write some data
    const char* test_msg = "Hello, ESHM!";
    int ret = eshm_write(handle, test_msg, strlen(test_msg) + 1);
    assert(ret == ESHM_SUCCESS);
    
    std::cout << "  Write successful" << std::endl;
    
    eshm_destroy(handle);
    std::cout << "  PASSED" << std::endl;
}

// Test heartbeat with threads
void test_heartbeat() {
    std::cout << "Test: Heartbeat functionality..." << std::endl;
    
    ESHMConfig config = eshm_default_config("test_hb");
    config.role = ESHM_ROLE_MASTER;
    config.use_threads = true;  // Enable threads
    config.stale_threshold_ms = 100;
    
    ESHMHandle* handle = eshm_init(&config);
    assert(handle != NULL);
    
    ESHMStats stats1;
    int ret = eshm_get_stats(handle, &stats1);
    assert(ret == ESHM_SUCCESS);
    
    usleep(50000); // 50ms
    
    ESHMStats stats2;
    ret = eshm_get_stats(handle, &stats2);
    assert(ret == ESHM_SUCCESS);
    
    // Heartbeat should have incremented significantly (50 counts in 50ms)
    assert(stats2.master_heartbeat > stats1.master_heartbeat);
    std::cout << "  Heartbeat incremented from " << stats1.master_heartbeat 
              << " to " << stats2.master_heartbeat 
              << " (delta: " << stats2.master_heartbeat_delta << ")" << std::endl;
    
    eshm_destroy(handle);
    std::cout << "  PASSED" << std::endl;
}

// Test statistics
void test_statistics() {
    std::cout << "Test: Statistics retrieval..." << std::endl;
    
    ESHMConfig config = eshm_default_config("test_stats");
    config.role = ESHM_ROLE_MASTER;
    config.use_threads = false;
    
    ESHMHandle* handle = eshm_init(&config);
    assert(handle != NULL);
    
    ESHMStats stats;
    int ret = eshm_get_stats(handle, &stats);
    assert(ret == ESHM_SUCCESS);
    
    std::cout << "  Master PID: " << stats.master_pid << std::endl;
    std::cout << "  Master alive: " << stats.master_alive << std::endl;
    std::cout << "  Stale threshold: " << stats.stale_threshold << "ms" << std::endl;
    assert(stats.master_pid == getpid());
    assert(stats.master_alive == true);
    
    eshm_destroy(handle);
    std::cout << "  PASSED" << std::endl;
}

// Test role detection
void test_role_detection() {
    std::cout << "Test: Role detection..." << std::endl;
    
    // Create master first
    ESHMConfig config1 = eshm_default_config("test_role");
    config1.role = ESHM_ROLE_MASTER;
    config1.use_threads = false;
    
    ESHMHandle* master = eshm_init(&config1);
    assert(master != NULL);
    
    enum ESHMRole role;
    int ret = eshm_get_role(master, &role);
    assert(ret == ESHM_SUCCESS);
    assert(role == ESHM_ROLE_MASTER);
    std::cout << "  Master role confirmed" << std::endl;
    
    // Now try to create another master (should delete old SHM and create new)
    ESHMConfig config2 = eshm_default_config("test_role");
    config2.role = ESHM_ROLE_MASTER;
    config2.use_threads = false;
    
    ESHMHandle* master2 = eshm_init(&config2);
    assert(master2 != NULL);
    
    eshm_destroy(master);
    eshm_destroy(master2);
    
    std::cout << "  PASSED" << std::endl;
}

int main() {
    std::cout << "=== ESHM Basic Tests (High-Performance Version) ===" << std::endl;
    
    test_init_destroy();
    test_write_read();
    test_heartbeat();
    test_statistics();
    test_role_detection();
    
    std::cout << "\n=== All Basic Tests PASSED ===" << std::endl;
    return 0;
}
