#ifndef ESHM_DATA_H
#define ESHM_DATA_H

#include <stdint.h>
#include <sys/types.h>
#include <pthread.h>
#include "eshm_config.h"

#define ESHM_MAGIC 0x4553484D  // "ESHM"

/* Layout/protocol version of the shared segment.
 *
 * Bumped to 3 when push wakeup landed: ESHMChannel repurposed 8 bytes of its
 * padding for the futex word and waiter count, and ESHMHeader repurposed 8 for
 * `features` and `layout_size`. Struct sizes are unchanged, but the meaning of
 * those bytes is not, so a v2 peer and a v3 peer must not share a segment.
 * eshm_init() validates this on attach and refuses a mismatch. */
#define ESHM_VERSION 3

/* Capability bits published in ESHMHeader.features by whoever creates the
 * segment. A monotonic version cannot express "supports X independently of Y";
 * this can, so future additive features do not need a version bump. */
#define ESHM_FEATURE_FUTEX_WAKE 0x00000001u

/* Wait forever in eshm_read_ex()/eshm_read_data(). Distinct from 0, which
 * means the opposite - return immediately without waiting.
 *
 * Note the asymmetry with ESHMConfig, where 0 means "unlimited"
 * (reconnect_wait_ms, max_reconnect_attempts). In the read functions 0 means
 * "do not wait at all". Same literal, inverted meaning. */
#define ESHM_TIMEOUT_INFINITE 0xFFFFFFFFu

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

// How a blocking read waits for data.
//
// Only affects reading, and only on this endpoint. The write side always wakes
// a parked peer when one is registered, whatever mode the writer itself is in -
// so flipping this on one side never strands the other.
enum ESHMWakeupMode {
    ESHM_WAKEUP_PUSH = 0,  // Default: park on a futex, woken by the writer
    ESHM_WAKEUP_POLL = 1   // Never park; poll internally as before
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
//
// wake_seq/waiters implement push wakeup. They are carved out of the former
// 48-byte padding, so sizeof(ESHMChannel) is unchanged (a static_assert in
// eshm.cpp enforces that). One pair per direction: a master's write must wake
// the slave, never the master.
struct ESHMChannel {
    struct ESHMSeqLock seqlock;        // Sequence lock for lock-free reads
    volatile uint32_t data_size;       // Size of data in buffer
    uint8_t data[ESHM_MAX_DATA_SIZE]; // Data buffer
    volatile uint64_t write_count;     // Number of writes
    volatile uint64_t read_count;      // Number of reads
    volatile uint32_t wake_seq;        // Bumped by the writer; the futex word
    volatile uint32_t waiters;         // Readers currently parked on wake_seq
    uint8_t padding[40];               // Cache line padding
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
    volatile uint32_t features;         // ESHM_FEATURE_* bits the creator supports
    uint32_t layout_size;               // sizeof(ESHMData) of the creating build
    uint8_t padding[24];                // Cache line padding
} __attribute__((aligned(64)));

// Complete shared memory structure
struct ESHMData {
    struct ESHMHeader header;
    struct ESHMChannel master_to_slave; // Master writes, slave reads
    struct ESHMChannel slave_to_master; // Slave writes, master reads
};

#endif // ESHM_DATA_H
