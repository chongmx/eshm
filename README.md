# Enhanced Shared Memory (ESHM) Library

A high-performance, production-ready shared memory IPC library for Linux with master-slave architecture, automatic reconnection, and lock-free communication.

## Features

- **Master-Slave Architecture**: Automatic role negotiation with master takeover on restart
- **High-Performance Communication**:
  - Sequence locks for lock-free reads (<100ns latency)
  - Dedicated heartbeat thread (1ms updates)
  - 3.3M+ messages/second throughput
  - Cache-line aligned data structures
- **Robust Reconnection**:
  - Slave automatically retries connection every 100ms
  - Configurable retry limits (default: 50 attempts, 0 = unlimited)
  - No segmentation faults during reconnection
  - Successfully resumes communication after master restart
- **Stale Detection**: Counter-based heartbeat monitoring (default: 100ms threshold)
- **Bidirectional Channels**: Separate master→slave and slave→master channels
- **Thread-Safe**: Process-shared pthreads with memory barriers

## Quick Start

### Build

```bash
mkdir build && cd build
cmake ..
make
```

### Basic Usage

```cpp
#include "eshm.h"

// Initialize as slave with default config (50 retry attempts, 100ms interval)
ESHMConfig config = eshm_default_config("my_shm");
config.role = ESHM_ROLE_SLAVE;
ESHMHandle* handle = eshm_init(&config);

// Simple read API (returns bytes read or negative error code, default 1000ms timeout)
char buffer[256];
int bytes_read = eshm_read(handle, buffer, sizeof(buffer));
if (bytes_read >= 0) {
    // Success - bytes_read contains number of bytes (can be 0 for event trigger)
    printf("Received %d bytes\n", bytes_read);
} else {
    // Error - bytes_read contains negative error code
    printf("Error: %s\n", eshm_error_string(bytes_read));
}

// Write data
eshm_write(handle, "Hello", 6);

// Cleanup
eshm_destroy(handle);
```

### Advanced Usage (Extended API)

```cpp
// For custom timeout or explicit bytes_read parameter, use eshm_read_ex()
size_t bytes_read;
int ret = eshm_read_ex(handle, buffer, sizeof(buffer), &bytes_read, 500);
if (ret == ESHM_SUCCESS) {
    printf("Received %zu bytes\n", bytes_read);
}
```

### Unlimited Retries

```cpp
ESHMConfig config = eshm_default_config("my_shm");
config.role = ESHM_ROLE_SLAVE;
config.max_reconnect_attempts = 0;  // Unlimited retries
config.reconnect_wait_ms = 0;       // Unlimited time
config.reconnect_retry_interval_ms = 100;  // Retry every 100ms
```

## Configuration Options

| Parameter | Description | Default |
|-----------|-------------|---------|
| `stale_threshold_ms` | Stale detection threshold | 100ms |
| `reconnect_retry_interval_ms` | Interval between reconnection attempts | 100ms |
| `max_reconnect_attempts` | Maximum reconnection attempts (0 = unlimited) | 50 |
| `reconnect_wait_ms` | Total wait time for reconnection (0 = unlimited) | 5000ms |
| `use_threads` | Use dedicated threads for heartbeat/monitoring | true |

## Running Tests

```bash
cd build
# Unit tests
./test/test_basic
./test/test_error_handling

# Integration tests
./test/test_master_slave
./test/test_reconnect

# Performance test
./test/test_performance
```

## Project Structure

```
testESHM/
├── eshm.h              # Public API
├── eshm.cpp            # Implementation
├── eshm_data.h         # Data structures
├── main.cpp            # Demo application
├── CMakeLists.txt      # Build configuration
├── test/               # Unit and integration tests
│   ├── test_basic.cpp
│   ├── test_master_slave.cpp
│   ├── test_reconnect.cpp
│   └── ...
├── examples/           # Example programs
│   ├── test_unlimited_config.cpp
│   └── test_truly_unlimited.cpp
├── scripts/            # Test and utility scripts
│   ├── test_reconnect_retry.sh
│   └── test_full_reconnect.sh
├── py/                 # Python wrapper
│   ├── eshm.py         # Python bindings
│   ├── build_shared_lib.sh  # Build script
│   ├── README.md       # Python documentation
│   └── examples/       # Python examples
│       ├── simple_master.py
│       ├── simple_slave.py
│       ├── advanced_example.py
│       ├── reconnect_demo.py
│       └── performance_test.py
└── docs/               # Documentation
    ├── QUICK_START.md
    ├── HIGH_PERFORMANCE_FEATURES.md
    └── RECONNECTION_GUIDE.md
```

## Demo Application

### C++ Demo
```bash
# Start master
./build/eshm_demo master my_shm

# Start slave (in another terminal)
./build/eshm_demo slave my_shm

# Auto role (becomes master if no SHM, slave otherwise)
./build/eshm_demo auto my_shm
```

### Python Demo
```bash
# Build Python wrapper first
cd py && ./build_shared_lib.sh && cd ..

# Start master
python3 py/examples/simple_master.py

# Start slave (in another terminal)
python3 py/examples/simple_slave.py
```

See [py/README.md](py/README.md) for complete Python documentation.

## C++ ↔ Python Interoperability

ESHM supports **seamless interoperability** between C++ and Python processes!

You can mix and match:
- C++ Master + Python Slave
- Python Master + C++ Slave

```bash
# Terminal 1: C++ master
./build/eshm_demo master my_shm

# Terminal 2: Python slave
python3 py/examples/simple_slave.py my_shm
```

Both use the same SHM naming convention. See [INTEROP_TEST_RESULTS.md](INTEROP_TEST_RESULTS.md) for details and test results.

## Documentation

- [Quick Start Guide](docs/QUICK_START.md) - Getting started tutorial
- [High Performance Features](docs/HIGH_PERFORMANCE_FEATURES.md) - Performance optimization details
- [Reconnection Guide](docs/RECONNECTION_GUIDE.md) - Reconnection mechanism explained

## API Reference

### Initialization
- `eshm_init()` - Initialize ESHM with configuration
- `eshm_destroy()` - Destroy handle and cleanup

### Communication
- `eshm_write()` - Write data (auto-selects channel)
- `eshm_read()` - Simple read API (returns bytes read or negative error, default 1000ms timeout)
- `eshm_read_ex()` - Extended read API with custom timeout and explicit bytes_read parameter

### Monitoring
- `eshm_check_remote_alive()` - Check if remote endpoint is alive
- `eshm_get_stats()` - Get statistics (heartbeat, PIDs, message counts)
- `eshm_get_role()` - Get current role (master/slave)

### Error Handling
- `eshm_error_string()` - Get error description

## Error Codes

- `ESHM_SUCCESS` - Operation successful
- `ESHM_ERROR_TIMEOUT` - Operation timed out (or in reconnection mode)
- `ESHM_ERROR_MASTER_STALE` - Master is stale
- `ESHM_ERROR_NO_DATA` - No data available
- `ESHM_ERROR_BUFFER_TOO_SMALL` - Buffer too small
- `ESHM_ERROR_NOT_INITIALIZED` - SHM not initialized

## Performance

- **Throughput**: 3.3M+ messages/second write
- **Latency**: <100ns lock-free reads
- **Heartbeat**: 1ms update interval
- **Stale Detection**: 100ms default threshold

## License

This is a demonstration/educational project.

## Requirements

- Linux (System V shared memory)
- CMake 3.10+
- C++11 compiler
- pthread, rt libraries
