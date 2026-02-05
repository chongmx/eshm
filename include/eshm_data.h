#ifndef ESHM_DATA_H
#define ESHM_DATA_H

#include <stdint.h>
#include <sys/types.h>
#include <pthread.h>
#include "eshm_config.h"

#define ESHM_MAGIC 0x4553484D  // "ESHM"
#define ESHM_VERSION 2
/* ESHM_MAX_DATA_SIZE and ESHM_HEARTBEAT_INTERVAL_MS are now defined in eshm_config.h */

// Channel states
enum ESHMChannelState {
    ESHM_CHANNEL_EMPTY = 0,
    ESHM_CHANNEL_READY = 1,
    ESHM_CHANNEL_READ = 2
};

// ESHM role types
enum ESHMRole {
    ESHM_ROLE_MASTER = 0,
    ESHM_ROLE_SLAVE = 1,
    ESHM_ROLE_AUTO = 2
};

// ESHM error codes
enum ESHMError {
    ESHM_SUCCESS = 0,
    ESHM_ERROR_INVALID_PARAM = -1,
    ESHM_ERROR_SHM_CREATE = -2,
    ESHM_ERROR_SHM_ATTACH = -3,
    ESHM_ERROR_SHM_DETACH = -4,
    ESHM_ERROR_SHM_DELETE = -5,
    ESHM_ERROR_MUTEX_INIT = -6,
    ESHM_ERROR_MUTEX_LOCK = -7,
    ESHM_ERROR_MUTEX_UNLOCK = -8,
    ESHM_ERROR_NO_DATA = -9,
    ESHM_ERROR_TIMEOUT = -10,
    ESHM_ERROR_MASTER_STALE = -11,
    ESHM_ERROR_BUFFER_FULL = -12,
    ESHM_ERROR_BUFFER_TOO_SMALL = -13,
    ESHM_ERROR_NOT_INITIALIZED = -14,
    ESHM_ERROR_ROLE_MISMATCH = -15
};

// Disconnect behavior on stale master detection
enum ESHMDisconnectBehavior {
    ESHM_DISCONNECT_IMMEDIATELY = 0,  // Disconnect immediately on stale master
    ESHM_DISCONNECT_ON_TIMEOUT = 1,   // Wait for timeout before disconnecting
    ESHM_DISCONNECT_NEVER = 2          // Wait indefinitely for master
};

// Sequence lock for lock-free reads
struct ESHMSeqLock {
    volatile uint32_t sequence;        // Sequence number (odd = write in progress)
};

// Single direction channel with sequence lock (unidirectional)
struct ESHMChannel {
    struct ESHMSeqLock seqlock;        // Sequence lock for lock-free reads
    volatile uint32_t data_size;       // Size of data in buffer
    uint8_t data[ESHM_MAX_DATA_SIZE]; // Data buffer
    volatile uint64_t write_count;     // Number of writes
    volatile uint64_t read_count;      // Number of reads
    uint8_t padding[48];               // Cache line padding
} __attribute__((aligned(64)));

// Shared memory header with cache-line alignment
struct ESHMHeader {
    uint32_t magic;                     // Magic number for validation
    uint32_t version;                   // Protocol version
    volatile uint64_t master_heartbeat; // Master heartbeat counter (updated every 1ms)
    volatile uint64_t slave_heartbeat;  // Slave heartbeat counter (updated every 1ms)
    volatile pid_t master_pid;          // Master process PID
    volatile pid_t slave_pid;           // Slave process PID
    volatile uint32_t master_alive;     // Master alive flag
    volatile uint32_t slave_alive;      // Slave alive flag
    volatile uint32_t stale_threshold;  // Stale detection threshold in heartbeat counts
    volatile uint32_t master_generation; // Incremented each time master restarts
    uint8_t padding[32];                // Cache line padding
} __attribute__((aligned(64)));

// Complete shared memory structure
struct ESHMData {
    struct ESHMHeader header;
    struct ESHMChannel master_to_slave; // Master writes, slave reads
    struct ESHMChannel slave_to_master; // Slave writes, master reads
};

#endif // ESHM_DATA_H
