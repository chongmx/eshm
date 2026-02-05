# Enhanced Shared Memory (ESHM) Library

A high-performance, production-ready shared memory IPC library for Linux with master-slave architecture, automatic reconnection, and lock-free communication using POSIX shared memory.

**Ready for integration:** ESHM is designed to be easily included as a git submodule in your project's `3rdparty/` directory, with automatic CMake configuration and shared library (.so) builds.

## Features

- **Master-Slave Architecture**: Automatic role negotiation with master takeover on restart
- **High-Performance Communication**:
  - Sequence locks for lock-free reads (<100ns latency)
  - Dedicated heartbeat thread (1ms updates)
- **Automatic Reconnection**:
  - Slave automatically retries connection every 100ms (configurable)
  - Configurable retry limits (default: 50 attempts, 0 = unlimited)
  - Monitor thread detects stale endpoints and triggers reconnection
- **Stale Detection**: Counter-based heartbeat monitoring (default: 100ms threshold)
- **Bidirectional Channels**: Separate master→slave and slave→master channels
- **Python Support**: Full Python wrapper with C++ interoperability

## Integration into Your Project

ESHM can be integrated into your project in three ways:

### Option 1: Git Submodule (Recommended)

Add ESHM as a git submodule in your project's `3rdparty/` or `external/` directory:

```bash
# Add ESHM as a submodule
cd your_project/
git submodule add https://github.com/yourusername/eshm.git 3rdparty/eshm
git submodule update --init --recursive
```

In your project's `CMakeLists.txt`:

```cmake
# Add ESHM subdirectory
add_subdirectory(3rdparty/eshm)

# Link your target to ESHM
add_executable(your_app main.cpp)
target_link_libraries(your_app PRIVATE ESHM::eshm)
```

When ESHM is included as a subdirectory, tests and examples are automatically disabled.

### Option 2: System Installation

Install ESHM system-wide:

```bash
# Build and install
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
sudo make install
```

In your project's `CMakeLists.txt`:

```cmake
# Find ESHM package
find_package(ESHM 1.0 REQUIRED)

# Link your target
add_executable(your_app main.cpp)
target_link_libraries(your_app PRIVATE ESHM::eshm)
```

### Option 3: Manual Integration

Copy the necessary files into your project:

```bash
# Copy headers
cp include/*.h your_project/include/

# Copy source files
cp src/*.cpp your_project/src/

# Add to your CMakeLists.txt (build both libraries)
add_library(eshm_data SHARED
    src/asn1_encode.cpp src/asn1_decode.cpp src/data_handler.cpp)
add_library(eshm SHARED src/eshm.cpp)
target_link_libraries(eshm PUBLIC eshm_data pthread rt)
```

## Quick Start

### Build Standalone

```bash
mkdir build && cd build
cmake ..
make
```

This builds:
- `libeshm.so` and `libeshm_data.so` - Shared libraries
- `eshm_demo` - Demo application in [demo/main.cpp](demo/main.cpp)
- Tests and examples (in `test/` and `examples/`)

### Build Options

Control what gets built:

```bash
cmake -DESHM_BUILD_TESTS=OFF \       # Skip tests
      -DESHM_BUILD_EXAMPLES=OFF \    # Skip examples
      -DESHM_BUILD_DEMO=OFF \        # Skip demo
      ..
```

### Memory Layout Customization

Customize the memory layout for your specific needs:

```bash
# Larger channel data size for bigger messages
cmake -DESHM_MAX_DATA_SIZE=8192 ..

# Different heartbeat interval
cmake -DESHM_HEARTBEAT_INTERVAL_MS=5 ..

# Default values (if not specified):
# - ESHM_MAX_DATA_SIZE: 4096 bytes
# - ESHM_HEARTBEAT_INTERVAL_MS: 1 ms
```

These settings are baked into the library at compile time via the generated `eshm_config.h` header.

### Basic C++ Usage

```cpp
#include "eshm.h"

// Initialize as slave with automatic reconnection
ESHMConfig config = eshm_default_config("eshm1");
config.role = ESHM_ROLE_SLAVE;
ESHMHandle* handle = eshm_init(&config);

// Read data (default 1000ms timeout)
char buffer[256];
int bytes_read = eshm_read(handle, buffer, sizeof(buffer));
if (bytes_read >= 0) {
    printf("Received %d bytes: %s\n", bytes_read, buffer);
}

// Write data
eshm_write(handle, "Hello", 6);

// Cleanup
eshm_destroy(handle);
```

### Basic Python Usage

```python
from eshm import ESHM, ESHMRole

# Initialize as slave
with ESHM("eshm1", role=ESHMRole.SLAVE) as eshm:
    # Read message (strips null terminators)
    data = eshm.read()
    message = data.decode('utf-8').rstrip('\0')

    # Send response (add null terminator for C++ compatibility)
    eshm.write(b"ACK\0")
```

## Reconnection Features

The slave automatically reconnects when the master crashes or restarts:

### Default Configuration
- **Retry interval**: 100ms between attempts
- **Max attempts**: 50 (then gives up)
- **Total timeout**: 5000ms (5 seconds)

### Unlimited Reconnection
```cpp
ESHMConfig config = eshm_default_config("eshm1");
config.role = ESHM_ROLE_SLAVE;
config.max_reconnect_attempts = 0;  // Unlimited retries
config.reconnect_wait_ms = 0;       // Unlimited time
config.reconnect_retry_interval_ms = 100;  // Retry every 100ms
```

### How It Works
1. **Monitor Thread**: Continuously checks master's heartbeat
2. **Stale Detection**: If heartbeat stops updating for 100ms, master is considered stale
3. **Automatic Retry**: Slave detaches from old SHM and retries connection
4. **Transparent Resumption**: Once master restarts, slave reconnects and communication resumes

## Complete Integration Example

Here's a minimal example showing how to use ESHM in your project:

**your_project/CMakeLists.txt:**
```cmake
cmake_minimum_required(VERSION 3.10)
project(MyApp)

set(CMAKE_CXX_STANDARD 17)

# Add ESHM as subdirectory
add_subdirectory(3rdparty/eshm)

# Create your application
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE ESHM::eshm)
```

**your_project/main.cpp:**
```cpp
#include <eshm.h>
#include <stdio.h>

int main() {
    // Initialize as master
    ESHMConfig config = eshm_default_config("my_shm");
    config.role = ESHM_ROLE_MASTER;
    ESHMHandle* handle = eshm_init(&config);

    // Write data
    const char* msg = "Hello from ESHM!";
    eshm_write(handle, msg, strlen(msg) + 1);

    // Read response
    char buffer[256];
    int bytes = eshm_read(handle, buffer, sizeof(buffer));
    if (bytes > 0) {
        printf("Received: %s\n", buffer);
    }

    eshm_destroy(handle);
    return 0;
}
```

**Build your project:**
```bash
cd your_project
git submodule add <eshm-repo-url> 3rdparty/eshm
mkdir build && cd build
cmake ..
make
./my_app
```

## Performance Testing

### C++ Demo (1000 msg/sec)

```bash
# Terminal 1 - Start master (default SHM name: "eshm1")
./build/eshm_demo master

# Terminal 2 - Start slave
./build/eshm_demo slave eshm1
```

**Tune performance** by editing [demo/main.cpp](demo/main.cpp):
```cpp
#define MESSAGE_INTERVAL_US 1000        // 1ms = 1000 msg/sec
#define MESSAGE_INTERVAL_US 100         // 0.1ms = 10,000 msg/sec
#define MESSAGE_INTERVAL_US 10000       // 10ms = 100 msg/sec
```

### Python Performance Test

```bash
# Build Python wrapper first
cd py && ./build_shared_lib.sh && cd ..

# Terminal 1 - Python master
python3 py/examples/performance_test.py master

# Terminal 2 - Python slave (stats every 10000 messages by default)
python3 py/examples/performance_test.py slave

# Custom stats interval (every 2000 messages)
python3 py/examples/performance_test.py slave perf_test 2000
```

### C++ ↔ Python Interop Benchmark

```bash
# Terminal 1 - C++ master (1000 msg/sec)
./build/eshm_demo master eshm1

# Terminal 2 - Python slave (benchmarking with ACK responses)
python3 py/examples/benchmark_slave.py eshm1 1000
```

**Performance Results:**

| Configuration | Throughput | Test Duration | Tool |
|--------------|------------|---------------|------|
| **C++ Master ↔ C++ Slave** | **~2.7M msg/sec** | **30s** | **build/test/test_benchmark_master** |
| **C++ Master ↔ Python Slave** | **~2,700-2,800 msg/sec** | **30s** | **build/test/test_benchmark_master + py/tests/performance/benchmark_slave.py** |
| **Python Master ↔ Python Slave** | **~2,000-2,400 msg/sec** | **30s** | **py/tests/performance/benchmark_master.py + benchmark_slave.py** |

All tests use bidirectional communication (read message + send ACK response).

## Configuration Options

| Parameter | Description | Default |
|-----------|-------------|---------|
| `stale_threshold_ms` | Stale detection threshold | 100ms |
| `reconnect_retry_interval_ms` | Interval between reconnection attempts | 100ms |
| `max_reconnect_attempts` | Maximum reconnection attempts (0 = unlimited) | 50 |
| `reconnect_wait_ms` | Total wait time for reconnection (0 = unlimited) | 5000ms |
| `use_threads` | Use dedicated threads for heartbeat/monitoring | true |
| `disconnect_behavior` | Behavior on stale master (IMMEDIATELY, ON_TIMEOUT, NEVER) | ON_TIMEOUT |

## C++ ↔ Python Interoperability

ESHM supports seamless interoperability between C++ and Python processes using the same POSIX shared memory.

**Key Requirements:**
- Use same SHM name (default: "eshm1")
- Python must add null terminators (`\0`) to strings for C++ compatibility
- Python should strip null terminators when reading from C++

**Example:**
```bash
# C++ master
./build/eshm_demo master eshm1

# Python slave (in another terminal)
python3 py/examples/simple_slave.py eshm1
```

See [docs/TEST.md](docs/TEST.md) for comprehensive testing guide.

## High-Performance Features

ESHM achieves high performance through lock-free design and dedicated monitoring threads.

### 1. Sequence Locks for Lock-Free Reads

**Zero-contention reads** using sequence numbers:
- Writer increments sequence before/after write (odd = writing)
- Reader checks sequence before/after read, retries if changed
- No mutex blocking on read path
- Read latency: <100ns

**Implementation:**
```cpp
// Write side
seqlock_write_begin(&channel->seqlock);  // seq++, memory barrier
memcpy(channel->data, data, size);
seqlock_write_end(&channel->seqlock);    // memory barrier, seq++

// Read side
do {
    seq = seqlock_read_begin(&channel->seqlock);  // Wait for even seq
    memcpy(buffer, channel->data, size);
} while (seqlock_read_retry(&channel->seqlock, seq));  // Retry if changed
```

### 2. Dedicated Heartbeat Thread

**Automatic 1ms heartbeat updates:**
- Separate pthread updates counter every 1ms
- Atomic increment operations (lock-free)
- CPU overhead: <0.1% per process
- Enables precise stale detection

**Code location:** [src/eshm.cpp:155](src/eshm.cpp#L155)

### 3. Counter-Based Stale Detection

**Millisecond-precision monitoring:**
- Monitor thread checks remote heartbeat every 10ms
- Compares heartbeat value across checks
- If unchanged for N consecutive checks, marks as stale
- Default threshold: 100ms (configurable)

**Benefits:**
- Detection time: Configurable (50-500ms typical)
- False positive rate: Near zero
- Recovery time: <10ms

### 4. Bidirectional Channels

**Two unidirectional channels for optimal performance:**
- `master_to_slave`: Master writes, Slave reads
- `slave_to_master`: Slave writes, Master reads
- Automatic channel selection based on role
- No contention between directions

### 5. Cache-Line Aligned Structures

**Prevent false sharing:**
- All structures aligned to 64-byte cache lines
- Separate cache lines for master/slave data
- Optimal CPU cache performance

**Memory layout (default configuration):**
```
ESHMData (~8.5 KB with default ESHM_MAX_DATA_SIZE=4096):
├── ESHMHeader (64 bytes aligned)
│   ├── master_heartbeat (atomic counter)
│   ├── slave_heartbeat (atomic counter)
│   └── stale_threshold, PIDs, flags
│
├── master_to_slave Channel (64 bytes aligned)
│   ├── seqlock (sequence number)
│   ├── data[ESHM_MAX_DATA_SIZE] (default: 4096 bytes)
│   └── write_count, read_count
│
└── slave_to_master Channel (64 bytes aligned)
    ├── seqlock (sequence number)
    ├── data[ESHM_MAX_DATA_SIZE] (default: 4096 bytes)
    └── write_count, read_count
```

**Note:** Memory layout is customizable via `ESHM_MAX_DATA_SIZE`. See [Memory Layout Customization](#memory-layout-customization).

### Performance Characteristics

| Metric | Value | Details |
|--------|-------|---------|
| **Throughput** | 3.3M+ msg/sec | C++ write benchmark |
| **Read Latency** | <100ns | Lock-free sequence locks |
| **Write Latency** | <200ns | Two memory barriers + memcpy |
| **Heartbeat Rate** | 1000 updates/sec | 1ms interval |
| **Stale Detection** | 100ms | Configurable threshold |
| **CPU Overhead** | <0.1% | Per process (both threads) |

### Thread Safety

- **Heartbeat thread**: Atomic increments, no locks
- **Monitor thread**: Read-only access to remote heartbeat
- **Read/Write operations**: Sequence locks + atomic operations
- **All operations**: Memory barriers ensure ordering

### Usage Example

```cpp
// High-throughput writer
for (int i = 0; i < 1000000; i++) {
    eshm_write(handle, data, size);  // Lock-free write
}

// Lock-free reader with retry
while (running) {
    ret = eshm_read_ex(handle, buffer, size, &bytes_read, 100);
    if (ret == ESHM_SUCCESS) {
        process_data(buffer, bytes_read);
    }
}
```

## API Reference

### Initialization
- `eshm_init(config)` - Initialize ESHM with configuration
- `eshm_destroy(handle)` - Destroy handle and cleanup
- `eshm_default_config(name)` - Get default configuration

### Communication
- `eshm_write(handle, data, size)` - Write data (auto-selects channel)
- `eshm_read(handle, buffer, size)` - Read with default 1000ms timeout (returns bytes read or negative error)
- `eshm_read_ex(handle, buffer, size, bytes_read, timeout_ms)` - Read with custom timeout

### Monitoring
- `eshm_check_remote_alive(handle, alive)` - Check if remote endpoint is alive
- `eshm_get_stats(handle, stats)` - Get statistics (heartbeat, PIDs, message counts)
- `eshm_get_role(handle, role)` - Get current role (MASTER/SLAVE)
- `eshm_error_string(error)` - Get error description

### Error Codes
- `ESHM_SUCCESS` - Operation successful
- `ESHM_ERROR_TIMEOUT` - Operation timed out (or in reconnection mode)
- `ESHM_ERROR_MASTER_STALE` - Master is stale
- `ESHM_ERROR_NO_DATA` - No data available
- `ESHM_ERROR_BUFFER_TOO_SMALL` - Buffer too small
- `ESHM_ERROR_NOT_INITIALIZED` - SHM not initialized

## Project Structure

```
eshm/
├── include/                # Public headers
│   ├── eshm.h              # Core ESHM API
│   ├── eshm_data.h         # Data structures
│   ├── data_handler.h      # ASN.1 data handler
│   ├── asn1_der.h          # ASN.1 encoder/decoder
│   └── eshm_config.h.in    # Configuration template (generates eshm_config.h)
├── src/                    # Implementation files
│   ├── eshm.cpp            # Core ESHM implementation
│   ├── data_handler.cpp
│   ├── asn1_encode.cpp
│   └── asn1_decode.cpp
├── demo/                   # Demo application
│   └── main.cpp            # Example usage (1000 msg/sec)
├── test/                   # Unit and integration tests
│   ├── functional/         # Functional tests
│   ├── performance/        # Performance benchmarks
│   └── image_transfer/     # 4K image transfer tests
├── examples/               # Additional examples
├── py/                     # Python wrapper
│   ├── eshm.py             # Python bindings
│   ├── build_shared_lib.sh
│   └── examples/
├── cmake/                  # CMake configuration files
│   └── ESHMConfig.cmake.in
├── docs/                   # Documentation
│   ├── INTEGRATION_GUIDE.md
│   ├── MEMORY_LAYOUT.md
│   ├── QUICK_START.md
│   ├── TEST.md
│   └── examples/client_integration/
└── CMakeLists.txt          # Build configuration
```

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

### Large Data Transfer Test (4K Images)

Test ESHM with large data by transferring 4K resolution images:

```bash
# Build with 64 MB channels (for dual 4K frames)
rm -rf build && mkdir build && cd build
cmake -DESHM_MAX_DATA_SIZE=67108864 ..
make

# Terminal 1: Start sender
./test/image_transfer/dual_frame_sender

# Terminal 2: Start receiver
./test/image_transfer/dual_frame_receiver
```

**Results:** Transfers two 4K RGBA frames (63 MB) in ~15 ms at 4+ GB/s

See [test/image_transfer/README.md](test/image_transfer/README.md) for details.

## Library Information

**Version:** 1.0.0

**Shared Libraries:**
- `libeshm.so.1.0.0` - Core ESHM library (~155 KB with default 4KB channels)
- `libeshm_data.so.1.0.0` - ASN.1 data handler library (~920 KB)

**Versioning:**
- SOVERSION: 1 (binary compatibility within major version)
- Full version: 1.0.0 (follows semantic versioning)

**CMake Namespace:** `ESHM::`
- Link with `ESHM::eshm` to get both core and data libraries

**Memory Layout:**
- Default channel size: 4096 bytes (customizable via `ESHM_MAX_DATA_SIZE`)
- Default heartbeat interval: 1 ms (customizable via `ESHM_HEARTBEAT_INTERVAL_MS`)
- Configuration is compile-time via CMake options

## Documentation

- **[Integration Guide](docs/INTEGRATION_GUIDE.md)** - **Start here!** Complete guide for integrating ESHM into your project
- [Memory Layout Guide](docs/MEMORY_LAYOUT.md) - Detailed guide to memory layout customization
- [Client Integration Example](docs/examples/client_integration/) - Working example with master/slave applications
- [4K Image Transfer Test](test/image_transfer/README.md) - Large data transfer examples
- [Quick Start Guide](docs/QUICK_START.md) - Getting started tutorial
- [Testing Guide](docs/TEST.md) - C++↔Python interoperability and unit tests
- [Python README](py/README.md) - Complete Python documentation
- [Changelog](CHANGELOG.md) - Recent improvements

## Technical Details

**Shared Memory:**
- Uses POSIX `shm_open()` and `mmap()` (not System V IPC)
- Visible in `/dev/shm/eshm_<name>` for easy inspection
- File descriptor-based API for better portability

**Performance:**
- Throughput: 3.3M+ messages/second (C++ write benchmark)
- Latency: <100ns lock-free reads via sequence locks
- Heartbeat: 1ms update interval via dedicated thread
- Zero-copy reads with memory barriers

**Threading:**
- Heartbeat thread: Updates counter every 1ms
- Monitor thread: Checks remote heartbeat, triggers reconnection
- Lock-free data channels: Sequence locks for reads, atomic operations

## Requirements

- Linux with POSIX shared memory support
- CMake 3.10+
- C++17 compiler (GCC 7+, Clang 5+)
- pthread and rt libraries

## License

This is a demonstration/educational project.
