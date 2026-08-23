#include "eshm.h"
#include "data_handler.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <linux/futex.h>
#include <sys/syscall.h>

// The shared layout is append-only into padding: repurposing those bytes must
// never change a struct size, or a peer built from a different revision of this
// header would read every field at the wrong offset.
static_assert(sizeof(ESHMChannel) % 64 == 0, "ESHMChannel must stay cache-line sized");
static_assert(sizeof(ESHMHeader) == 128, "ESHMHeader size changed - v3 layout broken");

// Push wakeup.
//
// A futex on a word inside the shared mapping, rather than an eventfd: an fd is
// an index into one process's table and cannot be published through shared
// memory, and a pthread condvar in shm hangs when its peer dies - the exact
// failure ESHM's seqlock/heartbeat design exists to survive. A futex waiter
// whose waker dies simply times out, and there is nothing to clean up.
//
// FUTEX_PRIVATE_FLAG is deliberately absent: private futexes are keyed by the
// address space, so they silently never wake across processes. The shared
// variant keys by physical page, which is what makes this work on MAP_SHARED.
#define ESHM_WAKE_SPINS      1000u   // spin before parking (~ a few microseconds)
#define ESHM_WAKE_RECHECK_MS 50u     // cap on one park, so peer death surfaces

static inline void cpu_relax(void) {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    __asm__ __volatile__("yield" ::: "memory");
#endif
}

static int futex_wait(volatile uint32_t* addr, uint32_t expected, uint32_t timeout_ms) {
    struct timespec ts;
    ts.tv_sec  = (time_t)(timeout_ms / 1000u);
    ts.tv_nsec = (long)(timeout_ms % 1000u) * 1000000L;
    return (int)syscall(SYS_futex, (uint32_t*)addr, FUTEX_WAIT, expected, &ts, NULL, 0);
}

static int futex_wake_all(volatile uint32_t* addr) {
    return (int)syscall(SYS_futex, (uint32_t*)addr, FUTEX_WAKE, INT_MAX, NULL, NULL, 0);
}

// Publish new data on a channel and wake anyone parked on it. The wake syscall
// is skipped unless a reader has actually registered, which keeps the hot
// producer/consumer path syscall-free.
static inline void channel_signal(ESHMChannel* channel) {
    __atomic_add_fetch(&channel->wake_seq, 1, __ATOMIC_RELEASE);
    if (__atomic_load_n(&channel->waiters, __ATOMIC_ACQUIRE) != 0) {
        futex_wake_all(&channel->wake_seq);
    }
}

// Compiler memory barriers
#define smp_mb() __sync_synchronize()
#define smp_rmb() __asm__ __volatile__("lfence":::"memory")
#define smp_wmb() __asm__ __volatile__("sfence":::"memory")

// Internal handle structure
struct ESHMHandle {
    ESHMConfig config;
    enum ESHMRole actual_role;
    int shm_fd;                   // POSIX shared memory file descriptor
    char shm_name[256];           // POSIX shared memory name (with / prefix)
    ESHMData* volatile shm_data;  // Volatile to ensure visibility across threads
    bool is_creator;
    
    // Threading support
    pthread_t heartbeat_thread;
    pthread_t monitor_thread;
    volatile bool threads_running;
    
    // Stale detection
    uint64_t last_remote_heartbeat;
    uint64_t stale_counter;
    volatile bool remote_is_stale;

    // Read tracking - remember last write_count we read from.
    //
    // have_read_baseline must be a separate flag: write_count == 0 is a
    // perfectly ordinary state (a channel nobody has written to yet), so using
    // last_read_write_count == 0 as the "not yet baselined" sentinel meant the
    // baseline was re-taken on every read until the first write landed - and
    // that first write was then consumed as the baseline instead of delivered.
    uint64_t last_read_write_count;
    bool have_read_baseline;

    // Local statistics
    uint64_t last_master_heartbeat;
    uint64_t last_slave_heartbeat;

    // How blocking reads wait. Read by the reading thread, written by
    // eshm_set_wakeup_mode() from any thread; a stale read costs at most one
    // extra wait cycle, so a plain volatile is enough.
    volatile enum ESHMWakeupMode wakeup_mode;
};

// Sequence lock functions
static inline void seqlock_write_begin(struct ESHMSeqLock* lock) {
    uint32_t seq = lock->sequence;
    lock->sequence = seq + 1;
    smp_wmb();
}

static inline void seqlock_write_end(struct ESHMSeqLock* lock) {
    smp_wmb();
    lock->sequence++;
}

static inline uint32_t seqlock_read_begin(const struct ESHMSeqLock* lock) {
    uint32_t seq;
    do {
        seq = lock->sequence;
        smp_rmb();
    } while (seq & 1);  // Wait if write is in progress
    return seq;
}

// Defined below; declared here because the bounded seqlock wait needs a clock.
static uint64_t get_time_ms(void);

// How long a reader will wait for a writer to finish before concluding that it
// never will. A live writer holds the sequence odd only for one memcpy, so this
// is orders of magnitude more than any healthy write needs - even with a very
// large ESHM_MAX_DATA_SIZE.
#define ESHM_SEQLOCK_STALL_MS 100u

// Bounded form of seqlock_read_begin().
//
// A writer killed between seqlock_write_begin() and seqlock_write_end() leaves
// the sequence odd permanently, and the unbounded loop above then spins until
// the reading process is killed. That is the one failure mode this library is
// built to survive, so the read path uses this instead: give up after
// ESHM_SEQLOCK_STALL_MS and let the caller fall through to its normal timeout,
// where stale detection and reconnection can do their job.
//
// Returns true and stores an even sequence number, or false if the writer
// appears to have died mid-write.
static inline bool seqlock_read_begin_bounded(const struct ESHMSeqLock* lock,
                                              uint32_t* out_seq) {
    uint64_t deadline = 0;
    for (uint32_t i = 0; ; ++i) {
        uint32_t seq = lock->sequence;
        smp_rmb();
        if ((seq & 1) == 0) {
            *out_seq = seq;
            return true;
        }
        // Only consult the clock occasionally: this is the hot path when a
        // writer is genuinely mid-write, and it should stay cheap.
        if ((i & 0xFF) == 0xFF) {
            uint64_t now = get_time_ms();
            if (deadline == 0) {
                deadline = now + ESHM_SEQLOCK_STALL_MS;
            } else if (now >= deadline) {
                return false;
            }
        }
        cpu_relax();
    }
}

static inline bool seqlock_read_retry(const struct ESHMSeqLock* lock, uint32_t seq) {
    smp_rmb();
    return lock->sequence != seq;
}

// Helper function to get current time in milliseconds
static uint64_t get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000;
}

// Helper function to sleep for milliseconds
static void sleep_ms(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

// Initialize a channel
static int init_channel(ESHMChannel* channel) {
    channel->seqlock.sequence = 0;
    channel->data_size = 0;
    channel->write_count = 0;
    channel->read_count = 0;
    memset(channel->data, 0, ESHM_MAX_DATA_SIZE);
    return ESHM_SUCCESS;
}

// Initialize shared memory data structure
static int init_shm_data(ESHMData* data, uint32_t stale_threshold_ms) {
    memset(data, 0, sizeof(ESHMData));
    
    data->header.magic = ESHM_MAGIC;
    data->header.version = ESHM_VERSION;
    data->header.master_heartbeat = 0;
    data->header.slave_heartbeat = 0;
    data->header.master_pid = 0;
    data->header.slave_pid = 0;
    data->header.master_alive = 0;
    data->header.slave_alive = 0;
    data->header.stale_threshold = stale_threshold_ms;
    data->header.features = ESHM_FEATURE_FUTEX_WAKE;
    // Recorded so a peer built with a different ESHM_MAX_DATA_SIZE is
    // rejected at attach instead of mmapping past end-of-file and taking
    // SIGBUS on its first channel access.
    data->header.layout_size = (uint32_t)sizeof(ESHMData);
    
    int ret = init_channel(&data->master_to_slave);
    if (ret != ESHM_SUCCESS) {
        return ret;
    }
    
    ret = init_channel(&data->slave_to_master);
    if (ret != ESHM_SUCCESS) {
        return ret;
    }
    
    return ESHM_SUCCESS;
}

// Generate POSIX SHM name from user-provided name
// POSIX SHM names must start with / and contain no other /
static void generate_shm_name(char* dest, size_t dest_size, const char* name) {
    // Ensure name starts with /
    snprintf(dest, dest_size, "/eshm_%s", name);

    // Replace any / in the name with _
    for (char* p = dest + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '_';
        }
    }
}

// Check if POSIX SHM exists
static bool shm_exists(const char* shm_name) {
    int fd = shm_open(shm_name, O_RDONLY, 0);
    if (fd == -1) {
        return false;
    }
    close(fd);
    return true;
}

// Delete existing POSIX SHM
static int delete_shm(const char* shm_name) {
    if (shm_unlink(shm_name) == -1 && errno != ENOENT) {
        return ESHM_ERROR_SHM_DELETE;
    }
    return ESHM_SUCCESS;
}

// Heartbeat thread function - updates heartbeat every 1ms
static void* heartbeat_thread_func(void* arg) {
    ESHMHandle* handle = (ESHMHandle*)arg;
    
    fprintf(stderr, "[ESHM] Heartbeat thread started (role: %s)\n",
            handle->actual_role == ESHM_ROLE_MASTER ? "MASTER" : "SLAVE");
    
    while (handle->threads_running) {
        // Increment heartbeat counter (check if SHM is attached first)
        // Cache the pointer to avoid race condition with monitor thread
        ESHMData* shm_data_snapshot = handle->shm_data;
        if (shm_data_snapshot) {
            if (handle->actual_role == ESHM_ROLE_MASTER) {
                __sync_fetch_and_add(&shm_data_snapshot->header.master_heartbeat, 1);
            } else {
                __sync_fetch_and_add(&shm_data_snapshot->header.slave_heartbeat, 1);
            }
        }

        // Sleep for 1ms
        sleep_ms(1);
    }
    
    fprintf(stderr, "[ESHM] Heartbeat thread stopped\n");
    return NULL;
}

// Monitor thread function - checks for stale remote endpoint
static void* monitor_thread_func(void* arg) {
    ESHMHandle* handle = (ESHMHandle*)arg;

    fprintf(stderr, "[ESHM] Monitor thread started (role: %s)\n",
            handle->actual_role == ESHM_ROLE_MASTER ? "MASTER" : "SLAVE");

    handle->last_remote_heartbeat = 0;
    handle->stale_counter = 0;
    handle->remote_is_stale = false;

    uint32_t check_interval_ms = 1;  // Check every 1ms (1kHz)
    bool first_check = true;  // Track first heartbeat check
    uint64_t reconnect_wait_counter = 0;
    uint64_t reconnect_attempt_counter = 0;
    uint32_t reconnect_attempts = 0;  // Count of reconnection attempts
    bool in_reconnect_mode = false;

    while (handle->threads_running) {
        uint64_t current_remote_heartbeat = 0;

        // If slave and in reconnect mode, try to reattach
        if (handle->actual_role == ESHM_ROLE_SLAVE && in_reconnect_mode) {
            reconnect_wait_counter += check_interval_ms;
            reconnect_attempt_counter += check_interval_ms;

            // Time to attempt reconnection?
            if (reconnect_attempt_counter >= handle->config.reconnect_retry_interval_ms) {
                reconnect_attempt_counter = 0;
                reconnect_attempts++;

                fprintf(stderr, "[ESHM] Slave attempting to reattach to SHM (attempt %u/%u)...\n",
                        reconnect_attempts,
                        handle->config.max_reconnect_attempts == 0 ? 999999 : handle->config.max_reconnect_attempts);

                // Detach from old SHM (use memory barrier to ensure visibility)
                ESHMData* old_shm_data = handle->shm_data;
                if (old_shm_data) {
                    handle->shm_data = NULL;
                    __sync_synchronize();  // Memory barrier to ensure NULL is visible to other threads

                    // Kick any reader parked on a futex word inside the
                    // mapping we are about to drop. Without this it would
                    // sit there until its own timeout, waiting on an
                    // address that is about to stop existing.
                    futex_wake_all(&old_shm_data->master_to_slave.wake_seq);
                    futex_wake_all(&old_shm_data->slave_to_master.wake_seq);

                    // Delay to allow other threads to see the NULL and stop accessing
                    // Heartbeat thread runs every 1ms, monitor checks every 10ms
                    // Wait 20ms to be safe (2 heartbeat cycles + 2 monitor cycles)
                    sleep_ms(20);

                    munmap(old_shm_data, sizeof(ESHMData));
                }

                // Try to attach to new/restarted SHM
                int new_shm_fd = shm_open(handle->shm_name, O_RDWR, 0666);
                if (new_shm_fd != -1) {
                    ESHMData* new_shm_data = (ESHMData*)mmap(NULL, sizeof(ESHMData),
                                                             PROT_READ | PROT_WRITE,
                                                             MAP_SHARED, new_shm_fd, 0);
                    if (new_shm_data != MAP_FAILED && new_shm_data->header.magic == ESHM_MAGIC) {
                        // Check if this is a NEW master (heartbeat should be different from last known)
                        uint64_t new_master_heartbeat = new_shm_data->header.master_heartbeat;

                        // If heartbeat hasn't changed, this is still the old/dead master, don't reconnect
                        if (new_master_heartbeat == handle->last_remote_heartbeat) {
                            munmap(new_shm_data, sizeof(ESHMData));
                            close(new_shm_fd);
                            // Don't print error message, just silently retry
                        } else {
                            // Successfully reattached to a NEW master!
                            handle->shm_fd = new_shm_fd;
                            handle->shm_data = new_shm_data;

                            // Reset slave info
                            handle->shm_data->header.slave_pid = getpid();
                            handle->shm_data->header.slave_alive = 1;

                            fprintf(stderr, "[ESHM] Slave RECONNECTED to master (after %lu ms)!\n",
                                    reconnect_wait_counter);

                            // Reset all counters
                            in_reconnect_mode = false;
                            handle->remote_is_stale = false;
                            handle->stale_counter = 0;
                            handle->last_remote_heartbeat = handle->shm_data->header.master_heartbeat;
                            reconnect_wait_counter = 0;
                            reconnect_attempt_counter = 0;
                            reconnect_attempts = 0;

                            // The new master's channel starts its write_count
                            // at 0, while last_read_write_count still holds the
                            // dead master's final value. Leaving it set makes
                            // eshm_read_timeout() discard everything the new
                            // master writes until it passes that old total, so
                            // a "successful" reconnect delivers no data. Clear
                            // the baseline: the next read re-takes it against
                            // the new channel, as the first read after init does.
                            handle->last_read_write_count = 0;
                            handle->have_read_baseline = false;

                            continue;
                        }
                    } else {
                        // Failed to attach or invalid
                        if (new_shm_data != MAP_FAILED) {
                            munmap(new_shm_data, sizeof(ESHMData));
                        }
                        close(new_shm_fd);
                    }
                } else {
                    // shm_open failed, no fd to close
                }

                fprintf(stderr, "[ESHM] Reattach failed, will retry...\n");

                // Check if max reconnect attempts reached (0 = unlimited)
                if (handle->config.max_reconnect_attempts > 0 &&
                    reconnect_attempts >= handle->config.max_reconnect_attempts) {
                    fprintf(stderr, "[ESHM] Maximum reconnection attempts (%u) reached, giving up\n",
                            handle->config.max_reconnect_attempts);
                    handle->threads_running = false;
                    break;
                }
            }

            // Also check time-based timeout if configured (for backwards compatibility)
            if (handle->config.reconnect_wait_ms > 0 &&
                reconnect_wait_counter >= handle->config.reconnect_wait_ms) {
                fprintf(stderr, "[ESHM] Reconnect wait timeout expired (%lu ms), giving up\n",
                        reconnect_wait_counter);
                handle->threads_running = false;
                break;
            }

            sleep_ms(check_interval_ms);
            continue;
        }

        // Normal monitoring (not in reconnect mode)
        // Check shm_data is valid before accessing (prevent race condition)
        ESHMData* shm_data_snapshot = handle->shm_data;
        if (shm_data_snapshot) {
            // Read remote heartbeat
            if (handle->actual_role == ESHM_ROLE_MASTER) {
                current_remote_heartbeat = shm_data_snapshot->header.slave_heartbeat;
            } else {
                current_remote_heartbeat = shm_data_snapshot->header.master_heartbeat;
            }

            // Get stale threshold from SHM (cache it to prevent accessing NULL later)
            uint32_t stale_threshold = shm_data_snapshot->header.stale_threshold;

            // On first check, initialize last_remote_heartbeat to current value
            if (first_check) {
                handle->last_remote_heartbeat = current_remote_heartbeat;
                first_check = false;
                fprintf(stderr, "[ESHM] Monitor initialized with remote heartbeat: %lu\n", current_remote_heartbeat);
            }

            // Check if heartbeat changed
            if (current_remote_heartbeat == handle->last_remote_heartbeat) {
                // Heartbeat didn't change, increment stale counter
                handle->stale_counter += check_interval_ms;

                if (handle->stale_counter >= stale_threshold) {
                    if (!handle->remote_is_stale) {
                        fprintf(stderr, "[ESHM] Remote endpoint detected as STALE! (counter: %lu ms)\n",
                                handle->stale_counter);
                        handle->remote_is_stale = true;

                        // For slave, start reconnection process
                        if (handle->actual_role == ESHM_ROLE_SLAVE) {
                            if (handle->config.disconnect_behavior == ESHM_DISCONNECT_IMMEDIATELY) {
                                fprintf(stderr, "[ESHM] Slave configured to disconnect immediately\n");
                                handle->threads_running = false;
                                break;
                            } else {
                                fprintf(stderr, "[ESHM] Slave entering reconnection mode...\n");
                                in_reconnect_mode = true;
                                reconnect_wait_counter = 0;
                                reconnect_attempt_counter = handle->config.reconnect_retry_interval_ms; // Try immediately
                            }
                        }
                    }
                }
            } else {
                // Heartbeat changed, reset stale counter
                if (handle->remote_is_stale) {
                    fprintf(stderr, "[ESHM] Remote endpoint recovered\n");
                }
                handle->stale_counter = 0;
                handle->remote_is_stale = false;
                handle->last_remote_heartbeat = current_remote_heartbeat;
            }
        }

        // Sleep for check interval
        sleep_ms(check_interval_ms);
    }

    fprintf(stderr, "[ESHM] Monitor thread stopped\n");
    return NULL;
}

ESHMHandle* eshm_init(const ESHMConfig* config) {
    if (!config || !config->shm_name) {
        return NULL;
    }

    ESHMHandle* handle = (ESHMHandle*)calloc(1, sizeof(ESHMHandle));
    if (!handle) {
        return NULL;
    }

    // Copy configuration
    handle->config = *config;
    handle->config.shm_name = strdup(config->shm_name);
    handle->actual_role = config->role;
    handle->is_creator = false;
    handle->shm_fd = -1;
    handle->shm_data = NULL;
    handle->threads_running = false;
    handle->last_read_write_count = 0;  // Initialize read tracking
    handle->have_read_baseline = false;
    handle->wakeup_mode = ESHM_WAKEUP_PUSH;  // Push wakeup is the default

    // Generate POSIX SHM name (e.g., "/eshm_demo")
    generate_shm_name(handle->shm_name, sizeof(handle->shm_name), config->shm_name);

    bool shm_existed = shm_exists(handle->shm_name);
    
    // Role-based initialization
    if (config->role == ESHM_ROLE_MASTER) {
        if (shm_existed) {
            // Try to attach and check if slave is still alive
            int temp_shm_fd = shm_open(handle->shm_name, O_RDWR, 0666);
            if (temp_shm_fd != -1) {
                ESHMData* temp_data = (ESHMData*)mmap(NULL, sizeof(ESHMData),
                                                       PROT_READ | PROT_WRITE,
                                                       MAP_SHARED, temp_shm_fd, 0);
                if (temp_data != MAP_FAILED) {
                    bool slave_alive = temp_data->header.slave_alive;
                    uint32_t old_generation = temp_data->header.master_generation;
                    munmap(temp_data, sizeof(ESHMData));

                    if (slave_alive) {
                        // Slave is alive, don't delete - just takeover
                        fprintf(stderr, "[ESHM] Master found existing SHM with alive slave, taking over (generation %u->%u)...\n",
                                old_generation, old_generation + 1);
                        handle->shm_fd = temp_shm_fd;
                        handle->is_creator = false;  // Not creating, taking over
                    } else {
                        // Slave not alive, safe to delete
                        fprintf(stderr, "[ESHM] Master found stale SHM, deleting it...\n");
                        close(temp_shm_fd);
                        delete_shm(handle->shm_name);
                        handle->shm_fd = shm_open(handle->shm_name, O_CREAT | O_EXCL | O_RDWR, 0666);
                        if (handle->shm_fd == -1) {
                            fprintf(stderr, "[ESHM] Failed to create SHM: %s\n", strerror(errno));
                            free((void*)handle->config.shm_name);
                            free(handle);
                            return NULL;
                        }
                        if (ftruncate(handle->shm_fd, sizeof(ESHMData)) == -1) {
                            fprintf(stderr, "[ESHM] Failed to set SHM size: %s\n", strerror(errno));
                            close(handle->shm_fd);
                            shm_unlink(handle->shm_name);
                            free((void*)handle->config.shm_name);
                            free(handle);
                            return NULL;
                        }
                        handle->is_creator = true;
                    }
                } else {
                    close(temp_shm_fd);
                    delete_shm(handle->shm_name);
                    handle->shm_fd = shm_open(handle->shm_name, O_CREAT | O_EXCL | O_RDWR, 0666);
                    if (handle->shm_fd == -1) {
                        fprintf(stderr, "[ESHM] Failed to create SHM: %s\n", strerror(errno));
                        free((void*)handle->config.shm_name);
                        free(handle);
                        return NULL;
                    }
                    if (ftruncate(handle->shm_fd, sizeof(ESHMData)) == -1) {
                        fprintf(stderr, "[ESHM] Failed to set SHM size: %s\n", strerror(errno));
                        close(handle->shm_fd);
                        shm_unlink(handle->shm_name);
                        free((void*)handle->config.shm_name);
                        free(handle);
                        return NULL;
                    }
                    handle->is_creator = true;
                }
            } else {
                handle->shm_fd = shm_open(handle->shm_name, O_CREAT | O_EXCL | O_RDWR, 0666);
                if (handle->shm_fd == -1) {
                    fprintf(stderr, "[ESHM] Failed to create SHM: %s\n", strerror(errno));
                    free((void*)handle->config.shm_name);
                    free(handle);
                    return NULL;
                }
                if (ftruncate(handle->shm_fd, sizeof(ESHMData)) == -1) {
                    fprintf(stderr, "[ESHM] Failed to set SHM size: %s\n", strerror(errno));
                    close(handle->shm_fd);
                    shm_unlink(handle->shm_name);
                    free((void*)handle->config.shm_name);
                    free(handle);
                    return NULL;
                }
                handle->is_creator = true;
            }
        } else {
            handle->shm_fd = shm_open(handle->shm_name, O_CREAT | O_EXCL | O_RDWR, 0666);
            if (handle->shm_fd == -1) {
                fprintf(stderr, "[ESHM] Failed to create SHM: %s\n", strerror(errno));
                free((void*)handle->config.shm_name);
                free(handle);
                return NULL;
            }
            if (ftruncate(handle->shm_fd, sizeof(ESHMData)) == -1) {
                fprintf(stderr, "[ESHM] Failed to set SHM size: %s\n", strerror(errno));
                close(handle->shm_fd);
                shm_unlink(handle->shm_name);
                free((void*)handle->config.shm_name);
                free(handle);
                return NULL;
            }
            handle->is_creator = true;
        }

        handle->actual_role = ESHM_ROLE_MASTER;
        
    } else if (config->role == ESHM_ROLE_SLAVE) {
        handle->shm_fd = shm_open(handle->shm_name, O_RDWR, 0666);
        if (handle->shm_fd == -1) {
            fprintf(stderr, "[ESHM] Slave failed to attach to SHM: %s\n", strerror(errno));
            free((void*)handle->config.shm_name);
            free(handle);
            return NULL;
        }

        handle->is_creator = false;
        handle->actual_role = ESHM_ROLE_SLAVE;
        
    } else if (config->role == ESHM_ROLE_AUTO) {
        if (shm_existed) {
            handle->shm_fd = shm_open(handle->shm_name, O_RDWR, 0666);
            if (handle->shm_fd != -1) {
                handle->is_creator = false;
                handle->actual_role = ESHM_ROLE_SLAVE;
                fprintf(stderr, "[ESHM] Auto role: attached as SLAVE\n");
            } else {
                delete_shm(handle->shm_name);
                handle->shm_fd = shm_open(handle->shm_name, O_CREAT | O_EXCL | O_RDWR, 0666);
                if (handle->shm_fd == -1) {
                    fprintf(stderr, "[ESHM] Auto role failed to create SHM: %s\n", strerror(errno));
                    free((void*)handle->config.shm_name);
                    free(handle);
                    return NULL;
                }
                if (ftruncate(handle->shm_fd, sizeof(ESHMData)) == -1) {
                    fprintf(stderr, "[ESHM] Failed to set SHM size: %s\n", strerror(errno));
                    close(handle->shm_fd);
                    shm_unlink(handle->shm_name);
                    free((void*)handle->config.shm_name);
                    free(handle);
                    return NULL;
                }
                handle->is_creator = true;
                handle->actual_role = ESHM_ROLE_MASTER;
                fprintf(stderr, "[ESHM] Auto role: promoted to MASTER\n");
            }
        } else {
            handle->shm_fd = shm_open(handle->shm_name, O_CREAT | O_EXCL | O_RDWR, 0666);
            if (handle->shm_fd == -1) {
                fprintf(stderr, "[ESHM] Auto role failed to create SHM: %s\n", strerror(errno));
                free((void*)handle->config.shm_name);
                free(handle);
                return NULL;
            }
            if (ftruncate(handle->shm_fd, sizeof(ESHMData)) == -1) {
                fprintf(stderr, "[ESHM] Failed to set SHM size: %s\n", strerror(errno));
                close(handle->shm_fd);
                shm_unlink(handle->shm_name);
                free((void*)handle->config.shm_name);
                free(handle);
                return NULL;
            }
            handle->is_creator = true;
            handle->actual_role = ESHM_ROLE_MASTER;
            fprintf(stderr, "[ESHM] Auto role: promoted to MASTER\n");
        }
    }
    
    // Map shared memory
    handle->shm_data = (ESHMData*)mmap(NULL, sizeof(ESHMData),
                                        PROT_READ | PROT_WRITE,
                                        MAP_SHARED, handle->shm_fd, 0);
    if (handle->shm_data == MAP_FAILED) {
        fprintf(stderr, "[ESHM] Failed to map SHM: %s\n", strerror(errno));
        if (handle->is_creator) {
            shm_unlink(handle->shm_name);
        }
        close(handle->shm_fd);
        free((void*)handle->config.shm_name);
        free(handle);
        return NULL;
    }
    
    // Initialize SHM data if creator
    if (handle->is_creator) {
        int ret = init_shm_data(handle->shm_data, config->stale_threshold_ms);
        if (ret != ESHM_SUCCESS) {
            fprintf(stderr, "[ESHM] Failed to initialize SHM data\n");
            munmap(handle->shm_data, sizeof(ESHMData));
            close(handle->shm_fd);
            shm_unlink(handle->shm_name);
            free((void*)handle->config.shm_name);
            free(handle);
            return NULL;
        }
    } else {
        // Validate existing SHM.
        //
        // Only the header is touched here. It lives at offset 0, inside the
        // first page, so it is safe to read even when the creator's segment is
        // smaller than our sizeof(ESHMData) - which is exactly the case
        // layout_size exists to catch, before anything touches a channel and
        // faults past end-of-file.
        const uint32_t magic   = handle->shm_data->header.magic;
        const uint32_t version = handle->shm_data->header.version;
        const uint32_t layout  = handle->shm_data->header.layout_size;

        const char* reject = NULL;
        char detail[192];
        detail[0] = '\0';

        if (magic != ESHM_MAGIC) {
            reject = "invalid magic number";
            snprintf(detail, sizeof(detail), "got 0x%08X, expected 0x%08X",
                     magic, (unsigned)ESHM_MAGIC);
        } else if (version != ESHM_VERSION) {
            reject = "protocol version mismatch";
            snprintf(detail, sizeof(detail),
                     "segment is v%u, this build speaks v%u - rebuild both ends",
                     version, (unsigned)ESHM_VERSION);
        } else if (layout != (uint32_t)sizeof(ESHMData)) {
            reject = "memory layout mismatch";
            snprintf(detail, sizeof(detail),
                     "segment is %u bytes, this build expects %zu "
                     "- ESHM_MAX_DATA_SIZE differs between the two builds",
                     layout, sizeof(ESHMData));
        }

        if (reject) {
            fprintf(stderr, "[ESHM] Cannot attach to '%s': %s (%s)\n",
                    handle->shm_name, reject, detail);
            munmap(handle->shm_data, sizeof(ESHMData));
            close(handle->shm_fd);
            free((void*)handle->config.shm_name);
            free(handle);
            return NULL;
        }
    }
    
    // Set role-specific information
    if (handle->actual_role == ESHM_ROLE_MASTER) {
        // Increment generation to signal reconnection to slave
        uint32_t old_gen = handle->shm_data->header.master_generation;
        handle->shm_data->header.master_generation = old_gen + 1;

        handle->shm_data->header.master_pid = getpid();
        handle->shm_data->header.master_alive = 1;
        handle->shm_data->header.master_heartbeat = 0;

        fprintf(stderr, "[ESHM] Master starting with generation %u\n",
                handle->shm_data->header.master_generation);
    } else {
        handle->shm_data->header.slave_pid = getpid();
        handle->shm_data->header.slave_alive = 1;
        handle->shm_data->header.slave_heartbeat = 0;
    }
    
    // Start threads if configured
    if (config->use_threads) {
        handle->threads_running = true;
        
        if (pthread_create(&handle->heartbeat_thread, NULL, heartbeat_thread_func, handle) != 0) {
            fprintf(stderr, "[ESHM] Failed to create heartbeat thread\n");
            handle->threads_running = false;
            eshm_destroy(handle);
            return NULL;
        }
        
        if (pthread_create(&handle->monitor_thread, NULL, monitor_thread_func, handle) != 0) {
            fprintf(stderr, "[ESHM] Failed to create monitor thread\n");
            handle->threads_running = false;
            pthread_join(handle->heartbeat_thread, NULL);
            eshm_destroy(handle);
            return NULL;
        }
    }
    
    return handle;
}

int eshm_destroy(ESHMHandle* handle) {
    if (!handle) {
        return ESHM_ERROR_INVALID_PARAM;
    }

    // Stop threads
    if (handle->threads_running) {
        handle->threads_running = false;
        pthread_join(handle->heartbeat_thread, NULL);
        pthread_join(handle->monitor_thread, NULL);
    }

    // Mark ourselves as not alive
    if (handle->shm_data && handle->shm_data != MAP_FAILED) {
        if (handle->actual_role == ESHM_ROLE_MASTER) {
            handle->shm_data->header.master_alive = 0;
        } else {
            handle->shm_data->header.slave_alive = 0;
        }

        munmap(handle->shm_data, sizeof(ESHMData));
    }

    // Close file descriptor
    if (handle->shm_fd != -1) {
        close(handle->shm_fd);
    }

    // Delete shared memory if we created it and auto_cleanup is enabled
    if (handle->is_creator && handle->config.auto_cleanup) {
        shm_unlink(handle->shm_name);
    }

    // Free resources
    if (handle->config.shm_name) {
        free((void*)handle->config.shm_name);
    }
    free(handle);

    return ESHM_SUCCESS;
}

int eshm_write(ESHMHandle* handle, const void* data, size_t size) {
    if (!handle || !data) {
        return ESHM_ERROR_INVALID_PARAM;
    }

    // Cache shm_data pointer to avoid race condition with monitor thread
    ESHMData* shm_data_snapshot = handle->shm_data;

    // Check if SHM is attached (might be detached during reconnection)
    if (!shm_data_snapshot) {
        // If remote is stale and we're in reconnection mode, return timeout
        // This allows the caller to retry while reconnection is in progress
        if (handle->remote_is_stale) {
            return ESHM_ERROR_TIMEOUT;
        }
        return ESHM_ERROR_NOT_INITIALIZED;
    }

    if (size > ESHM_MAX_DATA_SIZE) {
        return ESHM_ERROR_BUFFER_TOO_SMALL;
    }

    // Select channel based on role - use cached pointer
    ESHMChannel* channel;
    if (handle->actual_role == ESHM_ROLE_MASTER) {
        channel = &shm_data_snapshot->master_to_slave;
    } else {
        channel = &shm_data_snapshot->slave_to_master;
    }

    // Write with sequence lock
    seqlock_write_begin(&channel->seqlock);
    memcpy(channel->data, data, size);
    channel->data_size = size;
    seqlock_write_end(&channel->seqlock);

    __sync_fetch_and_add(&channel->write_count, 1);

    // Wake a parked reader. No syscall unless one is registered, so a hot
    // producer/consumer pair never pays for this.
    channel_signal(channel);

    return ESHM_SUCCESS;
}

int eshm_read_timeout(ESHMHandle* handle, void* buffer, size_t buffer_size,
                      size_t* bytes_read, uint32_t timeout_ms) {
    if (!handle || !buffer) {
        return ESHM_ERROR_INVALID_PARAM;
    }

    // Cache shm_data pointer to avoid race condition with monitor thread
    ESHMData* shm_data_snapshot = handle->shm_data;

    // Check if SHM is attached (might be detached during reconnection)
    if (!shm_data_snapshot) {
        // If remote is stale and we're in reconnection mode, return timeout
        // This allows the caller to retry while reconnection is in progress
        if (handle->remote_is_stale) {
            return ESHM_ERROR_TIMEOUT;
        }
        return ESHM_ERROR_NOT_INITIALIZED;
    }

    // Check if remote is stale
    if (handle->remote_is_stale &&
        handle->config.disconnect_behavior == ESHM_DISCONNECT_IMMEDIATELY) {
        return ESHM_ERROR_MASTER_STALE;
    }

    // Select channel based on role (opposite of write) - use cached pointer
    ESHMChannel* channel;
    if (handle->actual_role == ESHM_ROLE_MASTER) {
        channel = &shm_data_snapshot->slave_to_master;
    } else {
        channel = &shm_data_snapshot->master_to_slave;
    }

    uint64_t start_time = get_time_ms();
    uint32_t spins = 0;

    // Use persistent tracking across calls instead of resetting each time
    // This ensures we don't miss messages that arrived between calls
    if (!handle->have_read_baseline) {
        // First read on this channel: start from where it is now, so a reader
        // joining a long-running channel is not handed a backlog of one stale
        // value. Taking the baseline here - rather than when write_count is
        // first seen non-zero - is what lets a reader prime itself before the
        // writer starts, and then receive every message including the first.
        handle->last_read_write_count = channel->write_count;
        handle->have_read_baseline = true;
    }

    while (true) {
        // Check if SHM was detached during reconnection (recheck handle->shm_data)
        if (!handle->shm_data) {
            if (handle->remote_is_stale) {
                // Return timeout to allow caller to retry during reconnection
                return ESHM_ERROR_TIMEOUT;
            }
            return ESHM_ERROR_NOT_INITIALIZED;
        }

        // Check if new data is available
        uint64_t current_write_count = channel->write_count;
        if (current_write_count > handle->last_read_write_count) {
            // New data available, read with sequence lock.
            //
            // Retry bounded rather than forever: a writer that died mid-write
            // leaves the sequence odd, and an unbounded retry would hang this
            // process permanently instead of letting stale detection notice.
            uint32_t seq;
            bool snapshot_ok = false;

            for (int attempt = 0; attempt < 16 && !snapshot_ok; ++attempt) {
                if (!seqlock_read_begin_bounded(&channel->seqlock, &seq)) {
                    break;      // writer appears to have died mid-write
                }

                // Check buffer size
                if (buffer_size < channel->data_size) {
                    return ESHM_ERROR_BUFFER_TOO_SMALL;
                }

                // Copy data
                size_t data_size = channel->data_size;
                memcpy(buffer, (void*)channel->data, data_size);

                if (bytes_read) {
                    *bytes_read = data_size;
                }

                snapshot_ok = !seqlock_read_retry(&channel->seqlock, seq);
            }

            if (snapshot_ok) {
                __sync_fetch_and_add(&channel->read_count, 1);

                // Update persistent read tracking to current write count
                handle->last_read_write_count = current_write_count;

                return ESHM_SUCCESS;
            }

            // No stable snapshot. Fall through to the wait path below, so the
            // caller's timeout still applies and the monitor thread gets a
            // chance to declare the peer stale.
        }
        
        // A zero timeout is "try once", the opposite of what 0 means in
        // ESHMConfig. Return before touching the futex: a non-blocking read
        // must never register as a waiter.
        if (timeout_ms == 0) {
            return ESHM_ERROR_NO_DATA;
        }

        uint32_t remaining_ms = ESHM_WAKE_RECHECK_MS;
        if (timeout_ms != ESHM_TIMEOUT_INFINITE) {
            uint64_t elapsed = get_time_ms() - start_time;
            if (elapsed >= timeout_ms) {
                return ESHM_ERROR_TIMEOUT;
            }
            uint64_t left = timeout_ms - elapsed;
            remaining_ms = (left > ESHM_WAKE_RECHECK_MS)
                           ? ESHM_WAKE_RECHECK_MS : (uint32_t)left;
        }

        if (handle->wakeup_mode == ESHM_WAKEUP_POLL) {
            // Caller opted out of push: behave exactly as before.
            usleep(100);  // 100us
            continue;
        }

        // Spin briefly before parking. A hot producer/consumer pair finds its
        // data inside this window and never registers as a waiter, which keeps
        // the writer's wake path syscall-free and preserves peak throughput.
        if (spins < ESHM_WAKE_SPINS) {
            ++spins;
            cpu_relax();
            continue;
        }

        // Park. Sample wake_seq first, then re-check for data: FUTEX_WAIT
        // compares the word against `seen` inside the kernel and returns
        // EAGAIN rather than blocking if the writer moved it in between, which
        // is what closes the check-then-wait race.
        uint32_t seen = __atomic_load_n(&channel->wake_seq, __ATOMIC_ACQUIRE);
        if (channel->write_count > handle->last_read_write_count) {
            continue;
        }

        __atomic_add_fetch(&channel->waiters, 1, __ATOMIC_SEQ_CST);
        futex_wait(&channel->wake_seq, seen, remaining_ms);
        __atomic_sub_fetch(&channel->waiters, 1, __ATOMIC_SEQ_CST);

        // Every futex_wait outcome - woken, timed out, EAGAIN, EINTR, or
        // EFAULT because reconnection unmapped the segment underneath us -
        // means the same thing here: go round and re-evaluate from scratch.
        // The loop head re-checks handle->shm_data, so a vanished mapping is
        // handled by the existing detach path rather than needing its own case.
    }
}

// Extended read with explicit bytes_read and timeout
int eshm_read_ex(ESHMHandle* handle, void* buffer, size_t buffer_size,
                 size_t* bytes_read, uint32_t timeout_ms) {
    return eshm_read_timeout(handle, buffer, buffer_size, bytes_read, timeout_ms);
}

// Simple read API - returns number of bytes read or negative error code
int eshm_read(ESHMHandle* handle, void* buffer, size_t buffer_size) {
    if (!handle || !buffer) {
        return ESHM_ERROR_INVALID_PARAM;
    }

    size_t bytes_read = 0;
    int ret = eshm_read_timeout(handle, buffer, buffer_size, &bytes_read, 1000);

    if (ret == ESHM_SUCCESS) {
        // Return number of bytes read (can be 0 for event triggering)
        return (int)bytes_read;
    } else {
        // Return negative error code
        return ret;
    }
}

int eshm_update_heartbeat(ESHMHandle* handle) {
    if (!handle || !handle->shm_data) {
        return ESHM_ERROR_NOT_INITIALIZED;
    }
    
    // Heartbeat is updated by thread, this is a no-op
    return ESHM_SUCCESS;
}

int eshm_check_remote_alive(ESHMHandle* handle, bool* is_alive) {
    if (!handle || !is_alive) {
        return ESHM_ERROR_INVALID_PARAM;
    }

    // If SHM is detached (during reconnection), return not alive
    if (!handle->shm_data) {
        *is_alive = false;
        return ESHM_SUCCESS;
    }

    *is_alive = !handle->remote_is_stale;
    return ESHM_SUCCESS;
}

int eshm_get_stats(ESHMHandle* handle, ESHMStats* stats) {
    if (!handle || !stats) {
        return ESHM_ERROR_INVALID_PARAM;
    }

    memset(stats, 0, sizeof(ESHMStats));

    // If SHM is detached (during reconnection), return empty stats
    if (!handle->shm_data) {
        return ESHM_ERROR_NOT_INITIALIZED;
    }

    uint64_t current_master_hb = handle->shm_data->header.master_heartbeat;
    uint64_t current_slave_hb = handle->shm_data->header.slave_heartbeat;
    
    stats->master_heartbeat = current_master_hb;
    stats->slave_heartbeat = current_slave_hb;
    stats->master_pid = handle->shm_data->header.master_pid;
    stats->slave_pid = handle->shm_data->header.slave_pid;
    stats->master_alive = handle->shm_data->header.master_alive;
    stats->slave_alive = handle->shm_data->header.slave_alive;
    stats->stale_threshold = handle->shm_data->header.stale_threshold;
    
    stats->master_heartbeat_delta = current_master_hb - handle->last_master_heartbeat;
    stats->slave_heartbeat_delta = current_slave_hb - handle->last_slave_heartbeat;
    
    handle->last_master_heartbeat = current_master_hb;
    handle->last_slave_heartbeat = current_slave_hb;
    
    stats->m2s_write_count = handle->shm_data->master_to_slave.write_count;
    stats->m2s_read_count = handle->shm_data->master_to_slave.read_count;
    stats->s2m_write_count = handle->shm_data->slave_to_master.write_count;
    stats->s2m_read_count = handle->shm_data->slave_to_master.read_count;
    
    return ESHM_SUCCESS;
}

int eshm_set_wakeup_mode(ESHMHandle* handle, enum ESHMWakeupMode mode) {
    if (!handle) {
        return ESHM_ERROR_INVALID_PARAM;
    }
    if (mode != ESHM_WAKEUP_PUSH && mode != ESHM_WAKEUP_POLL) {
        return ESHM_ERROR_INVALID_PARAM;
    }

    handle->wakeup_mode = mode;
    __sync_synchronize();

    // A reader already parked on the futex is waiting on a bounded timeout, so
    // it will pick the new mode up within ESHM_WAKE_RECHECK_MS on its own. Wake
    // it anyway so switching to POLL takes effect immediately rather than after
    // an unexplained pause.
    if (mode == ESHM_WAKEUP_POLL && handle->shm_data) {
        ESHMData* data = handle->shm_data;
        futex_wake_all(&data->master_to_slave.wake_seq);
        futex_wake_all(&data->slave_to_master.wake_seq);
    }

    return ESHM_SUCCESS;
}

int eshm_get_wakeup_mode(ESHMHandle* handle, enum ESHMWakeupMode* mode) {
    if (!handle || !mode) {
        return ESHM_ERROR_INVALID_PARAM;
    }

    *mode = handle->wakeup_mode;
    return ESHM_SUCCESS;
}

int eshm_get_role(ESHMHandle* handle, enum ESHMRole* role) {
    if (!handle || !role) {
        return ESHM_ERROR_INVALID_PARAM;
    }
    
    *role = handle->actual_role;
    return ESHM_SUCCESS;
}

const char* eshm_error_string(int error_code) {
    switch (error_code) {
        case ESHM_SUCCESS: return "Success";
        case ESHM_ERROR_INVALID_PARAM: return "Invalid parameter";
        case ESHM_ERROR_SHM_CREATE: return "Failed to create shared memory";
        case ESHM_ERROR_SHM_ATTACH: return "Failed to attach to shared memory";
        case ESHM_ERROR_SHM_DETACH: return "Failed to detach from shared memory";
        case ESHM_ERROR_SHM_DELETE: return "Failed to delete shared memory";
        case ESHM_ERROR_MUTEX_INIT: return "Failed to initialize mutex";
        case ESHM_ERROR_MUTEX_LOCK: return "Failed to lock mutex";
        case ESHM_ERROR_MUTEX_UNLOCK: return "Failed to unlock mutex";
        case ESHM_ERROR_NO_DATA: return "No data available";
        case ESHM_ERROR_TIMEOUT: return "Operation timed out";
        case ESHM_ERROR_MASTER_STALE: return "Master is stale";
        case ESHM_ERROR_BUFFER_FULL: return "Buffer is full";
        case ESHM_ERROR_BUFFER_TOO_SMALL: return "Buffer too small";
        case ESHM_ERROR_NOT_INITIALIZED: return "Not initialized";
        case ESHM_ERROR_ROLE_MISMATCH: return "Role mismatch";
        default: return "Unknown error";
    }
}

// Read and decode data in one operation (for Python optimization)
int eshm_read_data(ESHMHandle* handle,
                   uint8_t* out_types,
                   char** out_keys,
                   int max_key_len,
                   void** out_values,
                   int max_items,
                   int* item_count,
                   uint32_t timeout_ms)
{
    if (!handle || !out_types || !out_keys || !out_values || !item_count) {
        return ESHM_ERROR_INVALID_PARAM;
    }

    // Read raw data from ESHM
    uint8_t buffer[ESHM_MAX_DATA_SIZE];
    size_t bytes_read = 0;

    int ret = eshm_read_timeout(handle, buffer, sizeof(buffer), &bytes_read, timeout_ms);
    if (ret != ESHM_SUCCESS) {
        return ret;
    }

    // Decode using C++ DataHandler
    try {
        shm_protocol::DataHandler dh;
        auto items = dh.decodeDataBuffer(buffer, bytes_read);

        if ((int)items.size() > max_items) {
            fprintf(stderr, "[ESHM] Too many items: got %zu, max %d\n", items.size(), max_items);
            return ESHM_ERROR_BUFFER_TOO_SMALL;
        }

        *item_count = items.size();

        for (size_t i = 0; i < items.size(); i++) {
            out_types[i] = static_cast<uint8_t>(items[i].type);

            // Copy key
            strncpy(out_keys[i], items[i].key.c_str(), max_key_len - 1);
            out_keys[i][max_key_len - 1] = '\0';

            // Allocate and copy value based on type
            switch (items[i].type) {
                case shm_protocol::DataType::INTEGER: {
                    int64_t* val = (int64_t*)malloc(sizeof(int64_t));
                    if (!val) return ESHM_ERROR_BUFFER_FULL;
                    *val = std::get<int64_t>(items[i].value);
                    out_values[i] = val;
                    break;
                }
                case shm_protocol::DataType::BOOLEAN: {
                    bool* val = (bool*)malloc(sizeof(bool));
                    if (!val) return ESHM_ERROR_BUFFER_FULL;
                    *val = std::get<bool>(items[i].value);
                    out_values[i] = val;
                    break;
                }
                case shm_protocol::DataType::REAL: {
                    double* val = (double*)malloc(sizeof(double));
                    if (!val) return ESHM_ERROR_BUFFER_FULL;
                    *val = std::get<double>(items[i].value);
                    out_values[i] = val;
                    break;
                }
                case shm_protocol::DataType::STRING: {
                    const auto& str = std::get<std::string>(items[i].value);
                    char* val = (char*)malloc(str.size() + 1);
                    if (!val) return ESHM_ERROR_BUFFER_FULL;
                    strcpy(val, str.c_str());
                    out_values[i] = val;
                    break;
                }
                case shm_protocol::DataType::BINARY: {
                    const auto& bin = std::get<std::vector<uint8_t>>(items[i].value);
                    // Allocate struct with data pointer and length
                    struct { uint8_t* data; size_t len; }* val =
                        (decltype(val))malloc(sizeof(*val));
                    if (!val) return ESHM_ERROR_BUFFER_FULL;
                    val->data = (uint8_t*)malloc(bin.size());
                    if (!val->data) {
                        free(val);
                        return ESHM_ERROR_BUFFER_FULL;
                    }
                    memcpy(val->data, bin.data(), bin.size());
                    val->len = bin.size();
                    out_values[i] = val;
                    break;
                }
                default:
                    fprintf(stderr, "[ESHM] Unsupported data type: %d\n", (int)items[i].type);
                    return ESHM_ERROR_INVALID_PARAM;
            }
        }

        return ESHM_SUCCESS;

    } catch (const std::exception& e) {
        fprintf(stderr, "[ESHM] Decode failed: %s\n", e.what());
        return ESHM_ERROR_INVALID_PARAM;
    }
}

// Free a value returned by eshm_read_data
void eshm_free_value(void* value, uint8_t type) {
    if (!value) return;

    if (type == 4) {  // BINARY type needs special handling
        // Binary data is stored as struct { uint8_t* data; size_t len; }
        struct BinaryData { uint8_t* data; size_t len; };
        BinaryData* bin = static_cast<BinaryData*>(value);
        if (bin->data) {
            free(bin->data);
        }
    }
    free(value);
}
