# ESHM Quick Start Guide

## Build

### Standard Build

```bash
mkdir build && cd build
cmake ..
make
```

### Custom Memory Layout

For larger messages, customize the channel size:

```bash
mkdir build && cd build

# 8 KB channels (default is 4 KB)
cmake -DESHM_MAX_DATA_SIZE=8192 ..

# 32 MB channels (for 4K images)
cmake -DESHM_MAX_DATA_SIZE=33554432 ..

make
```

See [Memory Layout Guide](MEMORY_LAYOUT.md) for details.

## Run Demo

### Master-Slave Mode

Terminal 1 (Master):
```bash
./eshm_demo master
```

Terminal 2 (Slave):
```bash
./eshm_demo slave
```

### Auto Mode (Recommended)

Terminal 1:
```bash
./eshm_demo auto
```

Terminal 2:
```bash
./eshm_demo auto
```

The first process becomes master, the second becomes slave automatically.

## Run Tests

```bash
cd build
make test
```

Or run individual tests:
```bash
./test/test_basic              # Basic functionality tests
./test/test_master_slave       # Master-slave communication
./test/test_auto_role          # Auto role negotiation
./test/test_stale_detection    # Stale master detection
./test/test_error_handling     # Error handling tests
./test/test_reconnect          # Reconnection tests
./test/test_benchmark_master   # Performance benchmarks
```

## Basic Code Example

```c
#include "eshm.h"

// Initialize
ESHMConfig config = eshm_default_config("my_app");
config.role = ESHM_ROLE_AUTO;  // Auto-negotiate role
ESHMHandle* handle = eshm_init(&config);

// Write data
const char* msg = "Hello!";
eshm_write(handle, msg, strlen(msg) + 1);

// Read data
char buffer[256];
size_t bytes_read;
int ret = eshm_read_ex(handle, buffer, sizeof(buffer), &bytes_read, 1000);
if (ret == ESHM_SUCCESS) {
    printf("Received: %s\n", buffer);
}

// Cleanup
eshm_destroy(handle);
```

## Configuration Options

```c
ESHMConfig config = eshm_default_config("my_shm");

// Role
config.role = ESHM_ROLE_MASTER;  // or SLAVE, or AUTO

// Stale detection (in milliseconds)
config.stale_threshold_ms = 100;  // Default: 100ms

// Reconnection settings (for slave)
config.reconnect_wait_ms = 5000;  // Total wait time (0 = unlimited)
config.reconnect_retry_interval_ms = 100;  // Retry interval
config.max_reconnect_attempts = 50;  // Max attempts (0 = unlimited)

// Disconnect behavior
config.disconnect_behavior = ESHM_DISCONNECT_ON_TIMEOUT;
// or: ESHM_DISCONNECT_IMMEDIATELY
// or: ESHM_DISCONNECT_NEVER

// Other options
config.auto_cleanup = true;  // Delete SHM on destroy
config.use_threads = true;   // Enable heartbeat/monitor threads (default)
```

**Note:** Heartbeat interval (default: 1ms) is a compile-time setting via `ESHM_HEARTBEAT_INTERVAL_MS`.

## Troubleshooting

### Clean up orphaned shared memory

ESHM uses POSIX shared memory (visible in `/dev/shm/`):

```bash
# List ESHM shared memory files
ls -lh /dev/shm/eshm_*

# Remove specific shared memory
rm /dev/shm/eshm_my_app

# Remove all ESHM shared memory
sudo rm /dev/shm/eshm_*
```

### Check for processes using SHM

```bash
ps aux | grep eshm_demo
```

### View system logs for errors

```bash
dmesg | grep -i shm
```
