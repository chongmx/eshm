#include "../../eshm.h"
#include <iostream>
#include <assert.h>
#include <string.h>

void test_null_params() {
    std::cout << "Test: NULL parameter handling..." << std::endl;
    
    // NULL config
    ESHMHandle* handle = eshm_init(NULL);
    assert(handle == NULL);
    std::cout << "  NULL config handled correctly" << std::endl;
    
    // NULL handle operations
    int ret = eshm_destroy(NULL);
    assert(ret == ESHM_ERROR_INVALID_PARAM);
    
    ret = eshm_write(NULL, "test", 5);
    assert(ret == ESHM_ERROR_INVALID_PARAM);
    
    char buffer[64];
    ret = eshm_read(NULL, buffer, sizeof(buffer));
    assert(ret == ESHM_ERROR_INVALID_PARAM);
    
    std::cout << "  PASSED" << std::endl;
}

void test_invalid_shm_name() {
    std::cout << "Test: Invalid SHM name..." << std::endl;
    
    ESHMConfig config = eshm_default_config(NULL);
    config.role = ESHM_ROLE_MASTER;
    
    ESHMHandle* handle = eshm_init(&config);
    assert(handle == NULL);
    
    std::cout << "  PASSED" << std::endl;
}

void test_buffer_overflow() {
    std::cout << "Test: Buffer overflow protection..." << std::endl;
    
    ESHMConfig config = eshm_default_config("test_overflow");
    config.role = ESHM_ROLE_MASTER;
    config.use_threads = false;
    
    ESHMHandle* handle = eshm_init(&config);
    assert(handle != NULL);
    
    // Try to write data larger than max size
    char large_buffer[ESHM_MAX_DATA_SIZE + 100];
    memset(large_buffer, 'X', sizeof(large_buffer));
    
    int ret = eshm_write(handle, large_buffer, sizeof(large_buffer));
    assert(ret == ESHM_ERROR_BUFFER_TOO_SMALL);
    std::cout << "  Large buffer rejected correctly" << std::endl;
    
    eshm_destroy(handle);
    std::cout << "  PASSED" << std::endl;
}

void test_read_nonexistent() {
    std::cout << "Test: Read with no data..." << std::endl;
    
    ESHMConfig config = eshm_default_config("test_nodata");
    config.role = ESHM_ROLE_MASTER;
    config.use_threads = false;
    
    ESHMHandle* handle = eshm_init(&config);
    assert(handle != NULL);
    
    // Try to read when no data available (timeout immediately)
    char buffer[64];
    size_t bytes_read;
    int ret = eshm_read_ex(handle, buffer, sizeof(buffer), &bytes_read, 0);
    assert(ret == ESHM_ERROR_NO_DATA);
    std::cout << "  No data condition handled correctly" << std::endl;
    
    eshm_destroy(handle);
    std::cout << "  PASSED" << std::endl;
}

void test_slave_without_master() {
    std::cout << "Test: Slave without master..." << std::endl;
    
    ESHMConfig config = eshm_default_config("test_no_master");
    config.role = ESHM_ROLE_SLAVE;
    config.use_threads = false;
    
    // Try to attach as slave when no master exists
    ESHMHandle* handle = eshm_init(&config);
    // Should fail since no master has created the SHM
    assert(handle == NULL);
    std::cout << "  Slave correctly fails without master" << std::endl;
    
    std::cout << "  PASSED" << std::endl;
}

void test_error_strings() {
    std::cout << "Test: Error string messages..." << std::endl;
    
    const char* str = eshm_error_string(ESHM_SUCCESS);
    assert(str != NULL);
    assert(strcmp(str, "Success") == 0);
    
    str = eshm_error_string(ESHM_ERROR_INVALID_PARAM);
    assert(str != NULL);
    assert(strcmp(str, "Invalid parameter") == 0);
    
    str = eshm_error_string(ESHM_ERROR_TIMEOUT);
    assert(str != NULL);
    
    std::cout << "  PASSED" << std::endl;
}

int main() {
    std::cout << "=== ESHM Error Handling Tests (High-Performance Version) ===" << std::endl;
    
    test_null_params();
    test_invalid_shm_name();
    test_buffer_overflow();
    test_read_nonexistent();
    test_slave_without_master();
    test_error_strings();
    
    std::cout << "\n=== All Error Handling Tests PASSED ===" << std::endl;
    return 0;
}
