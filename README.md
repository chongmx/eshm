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
- **GPU VRAM Sharing** (optional): zero-copy NVIDIA VRAM sharing between
  processes - see [GPU VRAM Sharing](#gpu-vram-sharing-optional)

## Installation

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
sudo cmake --install build && sudo ldconfig
```

Or build Debian packages and install those:

```bash
./scripts/export_deb.sh          # -> dist/libeshm1, libeshm-dev, python3-eshm
cd dist && sudo apt install ./libeshm1_*.deb ./libeshm-dev_*.deb ./python3-eshm_*.deb
./scripts/check_install.sh       # verify what a machine has
```

Removal is `sudo cmake --build build --target uninstall` for a source install,
`sudo apt remove python3-eshm libeshm-dev libeshm1` for the packages.
[docs/INSTALL.md](docs/INSTALL.md) covers both, the package split, and
troubleshooting.

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

With `python3-eshm` installed (or with `py/` on `sys.path` in the source tree)
the bindings find `libeshm.so` on their own - build tree, system directories,
then the linker cache; `ESHM_LIB` overrides it and `eshm.library_path()` reports
what was picked.

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

### Round-Trip Benchmark (any language pairing)

[examples/08_benchmark/](examples/08_benchmark/) measures complete round trips,
in whichever combination you start:

```bash
# Terminal 1                                  # Terminal 2
./build/examples/08_benchmark/bench echo demo
./build/examples/08_benchmark/bench drive demo --seconds 5     # C++ -> C++
python3 examples/08_benchmark/bench.py drive demo --seconds 5  # Python -> C++
```

It also compares the two Python codec paths, which is the choice that usually
matters most:

```bash
python3 examples/08_benchmark/bench.py codec --records 20000
```

### Fixed-Rate Interop Benchmark

```bash
# Terminal 1 - C++ benchmark master
./build/test/test_benchmark_master master eshm1

# Terminal 2 - Python slave (benchmarking with ACK responses)
python3 py/tests/performance/benchmark_slave.py eshm1 1000
```

**Performance Results:**

Complete round trips per second (write + read the reply), measured with
`examples/08_benchmark` on one machine. Push wakeup is the default from 1.1.0;
`--poll` selects the pre-1.1.0 behaviour, so both columns come from the same
binary on the same hardware.

| Configuration | Push (default) | Poll (pre-1.1.0) |
|---|---|---|
| **C++ ↔ C++** | **1,981,817/sec** (0.5 µs) | 5,492/sec (182 µs) |
| **C++ ↔ Python** | **205,882/sec** (4.9 µs) | 5,235/sec (191 µs) |

Request/response is the worst case for polling: the old read path slept 100 µs
on *every* miss, and a round trip contains two of them. Streaming, where the
reader rarely misses, is largely unaffected.

Absolute numbers vary by an order of magnitude across machines - run
`./build/examples/08_benchmark/bench` on your own hardware before designing
around a figure. The ratios are the portable part.

## Robot / policy-in-the-loop performance

The shape ESHM is designed for - C++ driving a robot and cameras, Python
running a slow neural policy - is benchmarked end to end in
[examples/11_robot_loop/](examples/11_robot_loop/). Measured on one machine
(WSL2 laptop, Release), 1 kHz control with two 640x480 camera streams:

| | Result |
|---|---|
| Control loop | **1000 Hz sustained**, jitter p50 96 us (23 us with `--spin`) |
| Camera streams | 4 x 640x480 @ 30 fps = **110 MB/s** with no control-rate loss |
| Closed loop, inference excluded | **1.36 ms** p50, 2.67 ms p99 |
| State staleness at the policy | ~half a control period (0.84 ms at 1 kHz) |

The closed loop is state written -> policy read -> inferred -> action read
back. With any realistic inference cost the transport is 3-10% of the loop.
Jitter p99 (~570 us) is the OS scheduler, not the channel.

Run it on your own hardware - absolute numbers do not transfer:

```bash
cmake -S . -B build-robot -DCMAKE_BUILD_TYPE=Release -DESHM_MAX_DATA_SIZE=4194304
cmake --build build-robot -j$(nproc)
./examples/11_robot_loop/run_bench.sh build-robot
```

## GPU VRAM Sharing (optional)

Beyond host-memory channels, ESHM can share a block of **NVIDIA VRAM**
between processes with zero copy - one process allocates, the other maps the
same physical memory and reads/writes it directly, no `cudaMemcpy` on either
side of the boundary. Built as a separate optional library,
`libeshm_cuda.so`, only when a CUDA toolkit is found
(`-DESHM_ENABLE_CUDA=AUTO|ON|OFF`, default `AUTO`) - machines without a GPU
build the rest of ESHM exactly as before.

**Deployment:** `libeshm_cuda.so` never links `libcuda.so` - it loads the
driver lazily with `dlopen()` on first use instead, so it has no NVIDIA
entries in its own ELF dependencies. One build works on any driver new
enough for VMM memory sharing (R470+; no per-CUDA-version or per-GPU-
architecture rebuild - this file has no device code), and the same package
installs and links fine on a machine with **no GPU at all**; only actually
calling `eshm_cuda_create()`/`attach()` there fails, with a clear error. That
is also why `scripts/export_deb.sh` ships it inside the ordinary
`libeshm1`/`libeshm-dev` packages rather than a separate one - pass
`--cuda OFF` to that script if you want a build that excludes the GPU code
entirely rather than one that simply won't use it.

```cpp
#include <eshm_cuda.h>

EshmCudaConfig config = eshm_cuda_default_config("frame", 640 * 480 * 4);
EshmCudaBuffer* buf = eshm_cuda_create(&config);   // allocates VRAM, publishes it

void* devptr; size_t size;
eshm_cuda_get_ptr(buf, &devptr, &size);
cudaMemcpy(devptr, host_frame, size, cudaMemcpyHostToDevice);
cudaDeviceSynchronize();          // eshm_cuda maps memory, not stream order -
                                   // sync before you signal "ready" yourself
                                   // (e.g. with an eshm_rpc trigger)
```

```python
from eshm.eshm_cuda import EshmCudaBuffer

buf = EshmCudaBuffer.attach("frame")
arr = buf.as_cupy(shape=(480, 640, 4), dtype="uint8")   # zero-copy cupy.ndarray
```

**Mechanism:** the CUDA driver's VMM API (`cuMemCreate` +
`cuMemExportToShareableHandle` with a POSIX file descriptor), not the legacy
`cudaIpcGetMemHandle` - which is unreliable under WSL2. The fd is handed to
attachers over an abstract-namespace `AF_UNIX` socket (`SCM_RIGHTS`), since a
raw fd number is meaningless across processes and POSIX shared memory cannot
carry a live one. Verified end to end on WSL2: a C++ producer and a Python
consumer in **separate processes** sharing the same VRAM, byte-exact.

**Version independence:** the Python side never touches a numpy/cupy C
struct. `__cuda_array_interface__` is a plain Python dict (an int pointer, a
shape tuple, a dtype string) - not an ABI - so `as_cupy()` works unmodified
regardless of the numpy/cupy version installed. Verified across numpy
1.26.4+cupy 13.6.0 and numpy 2.5.2+cupy 14.2.0 reading the same C++-written
VRAM in the same run.

**What it does not do:** synchronize the data. `eshm_cuda` maps memory; nothing
stops a reader and a writer from touching the same bytes at once. Signal
readiness yourself (an [`eshm_rpc`](examples/10_triggers/) trigger after your
stream has synced is the pattern used throughout), and if read/write can
overlap under load, double-buffer - see the "sharp edge" section of
[examples/12_gpu_shared_tensor/README.md](examples/12_gpu_shared_tensor/README.md),
which reproduces and fixes a real torn read hit while building this feature.

```bash
python3 examples/12_gpu_shared_tensor/peer.py consume gpu_frame        # terminal 1
./build/examples/12_gpu_shared_tensor/gpu_tensor_producer gpu_frame 8  # terminal 2
```

**Video-frame streaming throughput** is benchmarked in the same directory
(`gpu_frame_bench` + `gpu_frame_drain.py`, `./run_gpu_bench.sh`). Measured
on one machine (WSL2, RTX 5060 Laptop): 1080p into Python at a realistic 30
fps delivers **100% of frames, 0.4% torn, 1.96 ms p50 / 8.42 ms p99
latency**, with 48 bytes crossing the host channel per frame regardless of
resolution. Flat-out (unpaced) load pushes the torn rate up to 25% at
1080p - a real, load-dependent effect, not a baseline risk; see the
[benchmark section](examples/12_gpu_shared_tensor/README.md#benchmark-how-fast-can-video-actually-stream-through-vram)
for the full table and what torn rate means for your own workload.

## Configuration Options

| Parameter | Description | Default |
|-----------|-------------|---------|
| `stale_threshold_ms` | Stale detection threshold | 100ms |
| `reconnect_retry_interval_ms` | Interval between reconnection attempts | 100ms |
| `max_reconnect_attempts` | Maximum reconnection attempts (0 = unlimited) | 50 |
| `reconnect_wait_ms` | Total wait time for reconnection (0 = unlimited) | 5000ms |
| `use_threads` | Use dedicated threads for heartbeat/monitoring | true |
| `disconnect_behavior` | Behavior on stale master (IMMEDIATELY, ON_TIMEOUT, NEVER) | ON_TIMEOUT |

Wakeup mode is deliberately **not** in `ESHMConfig` - adding a field would
change the struct size and break the ABI for already-compiled callers. It is a
setter instead, and push wakeup is on by default:

```c
eshm_set_wakeup_mode(handle, ESHM_WAKEUP_POLL);   // opt out of push
```

### `0` means opposite things in the two halves of the API

| Where | `0` means |
|---|---|
| `ESHMConfig.reconnect_wait_ms` | wait **indefinitely** |
| `ESHMConfig.max_reconnect_attempts` | **unlimited** |
| `eshm_read_ex(timeout_ms)` | **do not wait at all** |
| `eshm_read_data(timeout_ms)` | **do not wait at all** |

To block until data arrives, pass `ESHM_TIMEOUT_INFINITE`, not `0`.

## C++ ↔ Python Interoperability

ESHM supports seamless interoperability between C++ and Python processes using the same POSIX shared memory.

**Key Requirements:**
- Use same SHM name (default: "eshm1")
- Python must add null terminators (`\0`) to strings for C++ compatibility
- Python should strip null terminators when reading from C++

**Example:**
```bash
# C++ publisher
./build/examples/01_hello_channel/hello_publisher eshm1

# Python consumer (in another terminal)
python3 examples/01_hello_channel/peer.py consume eshm1
```

Every example in [examples/](examples/) ships both sides and works in either
direction; `./examples/run_all.sh` checks all of them. See
[test/TEST.md](test/TEST.md) for the test suite.

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
| **Round trip** | ~0.5 µs | C++ ↔ C++, push wakeup |
| **Read Latency** | <100ns | Lock-free sequence locks |
| **Idle reader CPU** | ~0 | Parks on a futex; 0.5 ms CPU per 300 ms waiting |
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
  (`0` = do not wait, `ESHM_TIMEOUT_INFINITE` = wait until data arrives)

### Monitoring
- `eshm_check_remote_alive(handle, alive)` - Check if remote endpoint is alive
- `eshm_get_stats(handle, stats)` - Get statistics (heartbeat, PIDs, message counts)
- `eshm_get_role(handle, role)` - Get current role (MASTER/SLAVE)
- `eshm_error_string(error)` - Get error description

### Wakeup
- `eshm_set_wakeup_mode(handle, mode)` - `ESHM_WAKEUP_PUSH` (default) parks on a
  futex and is woken by the peer's write; `ESHM_WAKEUP_POLL` restores the older
  internal poll loop
- `eshm_get_wakeup_mode(handle, mode)` - Read the mode currently in effect

### Error Codes
- `ESHM_SUCCESS` - Operation successful
- `ESHM_ERROR_TIMEOUT` - Operation timed out (or in reconnection mode)
- `ESHM_ERROR_MASTER_STALE` - Master is stale
- `ESHM_ERROR_NO_DATA` - No data available
- `ESHM_ERROR_BUFFER_TOO_SMALL` - Buffer too small
- `ESHM_ERROR_NOT_INITIALIZED` - SHM not initialized

### GPU VRAM Sharing (optional - [include/eshm_cuda.h](include/eshm_cuda.h))
- `eshm_cuda_create(config)` - Allocate VRAM and publish it under a name
- `eshm_cuda_attach(name, timeout_ms)` - Map another process's published VRAM
  into this one, zero copy
- `eshm_cuda_get_ptr(buf, &devptr, &size)` - Process-local device pointer
- `eshm_cuda_device(buf)` / `eshm_cuda_generation(buf)` - Which GPU; reserved
  reallocation counter
- `eshm_cuda_destroy(buf)` - Unmap/release
- Python: `eshm.eshm_cuda.EshmCudaBuffer` mirrors this 1:1, plus
  `.as_cupy(shape, dtype)` for a zero-copy `cupy.ndarray` view

## Project Structure

```
eshm/
├── include/                # Public headers
│   ├── eshm.h              # Core ESHM API
│   ├── eshm_data.h         # Data structures
│   ├── data_handler.h      # ASN.1 data handler
│   ├── asn1_der.h          # ASN.1 encoder/decoder
│   ├── eshm_cuda.h         # GPU VRAM sharing (optional, needs CUDA)
│   └── eshm_config.h.in    # Configuration template (generates eshm_config.h)
├── src/                    # Implementation files
│   ├── eshm.cpp            # Core ESHM implementation
│   ├── data_handler.cpp
│   ├── asn1_encode.cpp
│   ├── asn1_decode.cpp
│   └── eshm_cuda.cpp       # GPU VRAM sharing implementation (libeshm_cuda.so)
├── demo/                   # Demo application
│   └── main.cpp            # Example usage (1000 msg/sec)
├── test/                   # Unit and integration tests
│   ├── functional/         # Functional tests
│   ├── performance/        # Performance benchmarks
│   ├── image_transfer/     # 4K image transfer tests
│   └── TEST.md             # Testing guide
├── examples/               # Worked examples: each a C++ / Python pair
│   ├── 01_hello_channel/   # Core read/write API
│   ├── 02_structured_data/ # ASN.1 records across languages
│   ├── 03_c_api/           # The ABI-stable C surface
│   ├── 04_monitoring/      # Statistics, roles, liveness
│   ├── 05_reconnection/    # Surviving a master restart
│   ├── 06_large_payload/   # Payloads bigger than the channel
│   ├── 07_rich_types/      # Event / FunctionCall / ImageFrame (C++ only)
│   ├── 08_benchmark/       # Round-trip throughput
│   ├── 09_integration/     # Consuming ESHM from your own project
│   ├── 12_gpu_shared_tensor/ # Zero-copy NVIDIA VRAM sharing (needs a GPU)
│   ├── run_all.sh          # Smoke-tests every pairing
│   └── README.md           # Index and API coverage map
├── py/                     # Python wrapper
│   ├── eshm.py             # Python bindings
│   ├── eshm_cuda.py        # GPU VRAM sharing bindings (optional)
│   ├── build_shared_lib.sh
│   └── tests/
├── cmake/                  # CMake configuration files
│   └── ESHMConfig.cmake.in
├── docs/                   # Documentation
│   ├── INTEGRATION_GUIDE.md
│   ├── MEMORY_LAYOUT.md
│   └── QUICK_START.md
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

**Version:** 1.2.0

**Shared Libraries:**
- `libeshm.so.1.2.0` - Core ESHM library (~155 KB with default 4KB channels)
- `libeshm_data.so.1.2.0` - ASN.1 data handler library (~920 KB)
- `libeshm_cuda.so.1.2.0` - Optional GPU VRAM sharing library, built only
  when a CUDA toolkit is found (`-DESHM_ENABLE_CUDA=AUTO|ON|OFF`)

**Versioning:**
- SOVERSION: 1 (binary compatibility within major version) - unchanged by
  1.2.0; `eshm_cuda` is new but additive, and nothing in the existing C ABI
  moved
- Full version: 1.2.0 (follows semantic versioning)
- Shared-memory protocol: `ESHM_VERSION` 3, validated on attach. This is
  versioned separately from the library because it changed incompatibly in
  1.1.0 while the C ABI did not - both ends of a channel must be built
  against the same `ESHM_VERSION`

**CMake Namespace:** `ESHM::`
- Link with `ESHM::eshm` to get both core and data libraries

**Memory Layout:**
- Default channel size: 4096 bytes (customizable via `ESHM_MAX_DATA_SIZE`)
- Default heartbeat interval: 1 ms (customizable via `ESHM_HEARTBEAT_INTERVAL_MS`)
- Configuration is compile-time via CMake options

## Documentation

- **[Integration Guide](docs/INTEGRATION_GUIDE.md)** - **Start here!** Complete guide for integrating ESHM into your project
- [Memory Layout Guide](docs/MEMORY_LAYOUT.md) - Detailed guide to memory layout customization
- [Integration Example](examples/09_integration/) - Working example with master/slave applications
- [Examples Guide](examples/README.md) - Overview of all examples
- [GPU VRAM Sharing Example](examples/12_gpu_shared_tensor/) - Zero-copy NVIDIA VRAM sharing, C++ ↔ Python
- [4K Image Transfer Test](test/image_transfer/README.md) - Large data transfer examples
- [Quick Start Guide](docs/QUICK_START.md) - Getting started tutorial
- [Testing Guide](test/TEST.md) - C++↔Python interoperability and unit tests
- [Python README](py/README.md) - Complete Python documentation
- [Changelog](CHANGELOG.md) - Recent improvements
- [Release Notes v1.2](RELEASE_NOTES_v1.2.md) - GPU VRAM sharing, in depth

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
- Optional, for [GPU VRAM sharing](#gpu-vram-sharing-optional): an NVIDIA GPU,
  driver R470+, and a CUDA toolkit (`find_package(CUDAToolkit)`, CMake 3.17+)

## C API (ABI-stable surface)

`shm_protocol::DataHandler` is a C++ class whose interface uses `std::string`,
`std::vector` and `std::variant`, so it cannot be called from C, from ctypes, or
safely across compiler and standard-library versions. Two headers expose the
same functionality through plain C with opaque handles - this is what the Python
bindings bind to, and what any non-C++ consumer should use:

| Header | Library | API |
|---|---|---|
| [include/data_handler_c_api.h](include/data_handler_c_api.h) | `libeshm_data` | `dh_create`, `dh_encode`, `dh_decode`, `dh_free_value`, `dh_destroy` |
| [include/eshm_data_api.h](include/eshm_data_api.h) | `libeshm` | `eshm_write_data` (encode + write), `eshm_data_free_value`, `eshm_data_get_last_error` |
| [include/eshm.h](include/eshm.h) | `libeshm` | `eshm_read_data` (read + decode), plus the core channel API |

```c
DataHandlerHandle h = dh_create();
uint8_t types[] = {0, 3};                       /* INTEGER, STRING */
const char* keys[] = {"frame_id", "camera"};
int64_t frame = 42;
const void* values[] = {&frame, "front"};

uint8_t buffer[4096];
int n = dh_encode(h, types, keys, values, 2, buffer, sizeof(buffer));
dh_destroy(h);
```

`test/functional/test_c_api.cpp` exercises both headers end to end.

## Examples

- [examples/01_hello_channel/](examples/01_hello_channel/) - publisher/consumer
  pair plus a Python `peer.py`; every combination of the two works, and the
  directory builds standalone with `find_package(ESHM)` so you can copy it into
  your own project
- [examples/](examples/) - nine numbered examples covering the whole API, each
  with a C++ side and a Python side that talk to each other
- `./examples/run_all.sh` checks every C++/Python pairing in both directions
- `ctest --test-dir build` runs the suite; `test_selftest` alone verifies a
  full master/slave round trip in one command

## License

MIT - see [LICENSE](LICENSE).
