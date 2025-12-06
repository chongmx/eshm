#ifndef ESHM_H
#define ESHM_H

#include "eshm_data.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ESHM handle (opaque)
typedef struct ESHMHandle ESHMHandle;

// Configuration structure
typedef struct {
    const char* shm_name;              // Shared memory name/key
    enum ESHMRole role;                // Role: master, slave, or auto
    enum ESHMDisconnectBehavior disconnect_behavior; // Behavior on stale master
    uint32_t stale_threshold_ms;       // Stale detection threshold in milliseconds
    uint32_t reconnect_wait_ms;        // Total time to wait for master reconnection (0 = wait indefinitely)
    uint32_t reconnect_retry_interval_ms; // Interval between reconnection attempts
    uint32_t max_reconnect_attempts;   // Maximum reconnection attempts (0 = unlimited)
    bool auto_cleanup;                 // Auto cleanup on destruction
    bool use_threads;                  // Use dedicated threads for heartbeat and monitoring
} ESHMConfig;

// Statistics structure
typedef struct {
    uint64_t master_heartbeat;
    uint64_t slave_heartbeat;
    pid_t master_pid;
    pid_t slave_pid;
    bool master_alive;
    bool slave_alive;
    uint32_t stale_threshold;         // Stale threshold in heartbeat counts
    uint64_t master_heartbeat_delta;  // Change in master heartbeat
    uint64_t slave_heartbeat_delta;   // Change in slave heartbeat
    uint64_t m2s_write_count;         // Master to slave write count
    uint64_t m2s_read_count;          // Master to slave read count
    uint64_t s2m_write_count;         // Slave to master write count
    uint64_t s2m_read_count;          // Slave to master read count
} ESHMStats;

// Initialize ESHM with configuration
// Returns: handle on success, NULL on failure
ESHMHandle* eshm_init(const ESHMConfig* config);

// Destroy ESHM handle and optionally cleanup shared memory
// Parameters:
//   handle: ESHM handle
// Returns: ESHM_SUCCESS on success, error code on failure
int eshm_destroy(ESHMHandle* handle);

// Write data to shared memory (automatically selects correct channel)
// Parameters:
//   handle: ESHM handle
//   data: pointer to data to write
//   size: size of data in bytes (must be <= ESHM_MAX_DATA_SIZE)
// Returns: ESHM_SUCCESS on success, error code on failure
int eshm_write(ESHMHandle* handle, const void* data, size_t size);

// Simple read with default timeout (1000ms)
// Parameters:
//   handle: ESHM handle
//   buffer: buffer to receive data
//   buffer_size: size of buffer
// Returns: number of bytes read on success (can be 0), negative error code on failure
int eshm_read(ESHMHandle* handle, void* buffer, size_t buffer_size);

// Extended read with timeout and explicit bytes_read parameter
// Parameters:
//   handle: ESHM handle
//   buffer: buffer to receive data
//   buffer_size: size of buffer
//   bytes_read: pointer to receive actual bytes read (can be NULL)
//   timeout_ms: timeout in milliseconds (0 = non-blocking)
// Returns: ESHM_SUCCESS on success, ESHM_ERROR_NO_DATA if no data, error code on failure
int eshm_read_ex(ESHMHandle* handle, void* buffer, size_t buffer_size,
                 size_t* bytes_read, uint32_t timeout_ms);

// Update heartbeat (called automatically by read/write, but can be called manually)
// Parameters:
//   handle: ESHM handle
// Returns: ESHM_SUCCESS on success, error code on failure
int eshm_update_heartbeat(ESHMHandle* handle);

// Check if remote endpoint is alive
// Parameters:
//   handle: ESHM handle
//   is_alive: pointer to receive alive status
// Returns: ESHM_SUCCESS on success, error code on failure
int eshm_check_remote_alive(ESHMHandle* handle, bool* is_alive);

// Get statistics
// Parameters:
//   handle: ESHM handle
//   stats: pointer to structure to receive statistics
// Returns: ESHM_SUCCESS on success, error code on failure
int eshm_get_stats(ESHMHandle* handle, ESHMStats* stats);

// Get current role
// Parameters:
//   handle: ESHM handle
//   role: pointer to receive current role
// Returns: ESHM_SUCCESS on success, error code on failure
int eshm_get_role(ESHMHandle* handle, enum ESHMRole* role);

// Get error string for error code
// Parameters:
//   error_code: error code
// Returns: string description of error
const char* eshm_error_string(int error_code);

// Default configuration
static inline ESHMConfig eshm_default_config(const char* shm_name) {
    ESHMConfig config;
    config.shm_name = shm_name;
    config.role = ESHM_ROLE_AUTO;
    config.disconnect_behavior = ESHM_DISCONNECT_ON_TIMEOUT;
    config.stale_threshold_ms = 100;     // 100ms stale detection threshold
    config.reconnect_wait_ms = 5000;     // 5 seconds total wait for reconnection
    config.reconnect_retry_interval_ms = 100;  // Try reconnect every 100ms
    config.max_reconnect_attempts = 50;  // Try 50 times before giving up (0 = unlimited)
    config.auto_cleanup = true;
    config.use_threads = true;           // Use threads by default
    return config;
}

#ifdef __cplusplus
}
#endif

#endif // ESHM_H
