# ESHM Quick Start Guide

## Build

```bash
mkdir build && cd build
cmake ..
make
```

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
int ret = eshm_read_timeout(handle, buffer, sizeof(buffer), &bytes_read, 1000);
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

// Heartbeat
config.heartbeat_interval_ms = 100;  // Update every 100ms

// Stale detection
config.stale_timeout_ms = 1000;  // 1 second timeout

// Disconnect behavior
config.disconnect_behavior = ESHM_DISCONNECT_ON_TIMEOUT;
// or: ESHM_DISCONNECT_IMMEDIATELY
// or: ESHM_DISCONNECT_NEVER

// Auto cleanup
config.auto_cleanup = true;  // Delete SHM on destroy
```

## Troubleshooting

### Clean up orphaned shared memory

```bash
# List shared memory segments
ipcs -m

# Remove a specific segment
ipcrm -m <shmid>
```

### Check for processes using SHM

```bash
ps aux | grep eshm_demo
```

### View system logs for errors

```bash
dmesg | grep -i shm
```
