# ESHM v1.0.0 Release Notes

## Overview

ESHM v1.0.0 is a production-ready shared memory IPC library designed for easy integration into C++ projects. It provides high-performance inter-process communication with automatic reconnection, master-slave architecture, and comprehensive CMake support.

## Key Features

### Shared Library Distribution

ESHM is distributed as shared libraries with proper versioning:

- **libeshm.so.1.0.0** (~155 KB) - Core ESHM functionality
- **libeshm_data.so.1.0.0** (~920 KB) - ASN.1 data handler

Benefits:
- Smaller final binaries when multiple executables link to ESHM
- Runtime library updates without recompiling applications
- Standard Linux shared library conventions with SOVERSION

### Smart CMake Integration

ESHM automatically adapts to how it's being built:

**Standalone Build:**
```bash
cd eshm/
mkdir build && cd build
cmake ..
make
```
Builds: Libraries + Demo + Tests + Examples

**Submodule Build:**
```cmake
# In parent project
add_subdirectory(3rdparty/eshm)
```
Builds: Libraries only (tests/examples/demo automatically skipped)

### Modern CMake Targets

Clean integration with CMake namespace:
```cmake
# Just add and link
add_subdirectory(3rdparty/eshm)
target_link_libraries(my_app PRIVATE ESHM::eshm)
```

The `ESHM::eshm` target automatically provides:
- Include directories
- Required libraries (eshm, eshm_data, pthread, rt)
- Compiler flags (C++17)
- Proper dependency ordering

### Build Control Options

Fine-grained control over what gets built:

```bash
cmake -DESHM_BUILD_TESTS=OFF \       # Skip tests
      -DESHM_BUILD_EXAMPLES=OFF \    # Skip examples
      -DESHM_BUILD_DEMO=OFF \        # Skip demo
      ..
```

Options default to ON in standalone mode, OFF when used as a submodule.

### Memory Layout Customization

Configure the memory layout at build time:

```bash
# Custom channel data size (default: 4096 bytes)
cmake -DESHM_MAX_DATA_SIZE=8192 ..

# Custom heartbeat interval (default: 1 ms)
cmake -DESHM_HEARTBEAT_INTERVAL_MS=2 ..

# Combine multiple options
cmake -DESHM_MAX_DATA_SIZE=16384 \
      -DESHM_HEARTBEAT_INTERVAL_MS=5 \
      ..
```

The configuration is baked into the generated `eshm_config.h` header, which is included by all ESHM code. This allows you to customize the memory footprint and performance characteristics for your specific use case.

### Package Configuration

System-wide installation support with `find_package()`:

```cmake
find_package(ESHM 1.0 REQUIRED)
target_link_libraries(my_app PRIVATE ESHM::eshm)
```

Includes:
- CMake config files for version checking
- Exported targets with proper namespacing
- Dependency management (pthread, rt)

## Memory Layout

ESHM provides a **configurable memory layout** that can be customized at build time.

### Default Configuration

With default settings (`ESHM_MAX_DATA_SIZE=4096`):

```
┌─────────────────────────────────────┐
│ Header (64 bytes)                   │  Control info, heartbeats, PIDs
├─────────────────────────────────────┤
│ master_to_slave Channel (~4.2 KB)  │  Seqlock + 4096 data + metadata
├─────────────────────────────────────┤
│ slave_to_master Channel (~4.2 KB)  │  Seqlock + 4096 data + metadata
└─────────────────────────────────────┘
Total: ~8.5 KB
```

### Large Data Configuration

For 4K image transfer (`ESHM_MAX_DATA_SIZE=33554432`):

```
┌─────────────────────────────────────┐
│ Header (64 bytes)                   │
├─────────────────────────────────────┤
│ master_to_slave Channel (32 MB)    │  Single 4K RGBA frame
├─────────────────────────────────────┤
│ slave_to_master Channel (32 MB)    │  Single 4K RGBA frame
└─────────────────────────────────────┘
Total: ~64 MB
```

**Performance:** 2.5 GB/s throughput for 32 MB transfers

### Dual-Frame Configuration

For dual 4K frames (`ESHM_MAX_DATA_SIZE=67108864`):

```
┌─────────────────────────────────────┐
│ Header (64 bytes)                   │
├─────────────────────────────────────┤
│ master_to_slave Channel (64 MB)    │  Two 4K RGBA frames
├─────────────────────────────────────┤
│ slave_to_master Channel (64 MB)    │  Two 4K RGBA frames
└─────────────────────────────────────┘
Total: ~128 MB
```

**Performance:** 4.1 GB/s throughput, equivalent to 130+ fps at 4K

### Test Verification

The [test/image_transfer/](../test/image_transfer/) directory contains programs that verify:
- Single 4K frame transfers (31.6 MB per frame)
- Dual 4K frame transfers (63.3 MB per packet)
- Data integrity via checksums
- Pattern verification

## Directory Structure

```
eshm/
├── include/                          # Public API headers
│   ├── eshm.h
│   ├── eshm_data.h
│   ├── data_handler.h
│   ├── asn1_der.h
│   └── eshm_config.h.in            # Configuration template
├── src/                              # Implementation
│   ├── eshm.cpp
│   ├── data_handler.cpp
│   ├── asn1_encode.cpp
│   └── asn1_decode.cpp
├── demo/                             # Demo application
│   └── main.cpp
├── test/                             # Test suite
│   ├── functional/
│   ├── performance/
│   └── image_transfer/             # 4K image transfer tests
├── examples/                         # Example programs
├── cmake/                            # CMake configuration
│   └── ESHMConfig.cmake.in
├── docs/                             # Documentation
│   ├── INTEGRATION_GUIDE.md
│   ├── MEMORY_LAYOUT.md
│   └── examples/client_integration/
└── CMakeLists.txt                    # Build configuration
```

## Installation Layout

When installed to `/usr/local`:

```
/usr/local/
├── bin/
│   └── eshm_demo                     # Demo application
├── include/
│   ├── eshm.h
│   ├── eshm_data.h
│   ├── data_handler.h
│   └── asn1_der.h
├── lib/
│   ├── libeshm.so -> libeshm.so.1
│   ├── libeshm.so.1 -> libeshm.so.1.0.0
│   ├── libeshm.so.1.0.0
│   ├── libeshm_data.so -> libeshm_data.so.1
│   ├── libeshm_data.so.1 -> libeshm_data.so.1.0.0
│   ├── libeshm_data.so.1.0.0
│   └── cmake/ESHM/
│       ├── ESHMConfig.cmake
│       ├── ESHMConfigVersion.cmake
│       └── ESHMTargets.cmake
```

## Integration Examples

### As Git Submodule

```bash
# Add to your project
git submodule add <eshm-url> 3rdparty/eshm
```

**your_project/CMakeLists.txt:**
```cmake
add_subdirectory(3rdparty/eshm)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE ESHM::eshm)
```

**Build:**
```bash
mkdir build && cd build
cmake ..
make
```

### System-Wide Installation

```bash
# Install ESHM
cd eshm/
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
sudo make install
sudo ldconfig
```

**your_project/CMakeLists.txt:**
```cmake
find_package(ESHM 1.0 REQUIRED)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE ESHM::eshm)
```

### Basic Usage Example

```cpp
#include <eshm.h>

int main() {
    // Initialize
    ESHMConfig config = eshm_default_config("my_shm");
    config.role = ESHM_ROLE_MASTER;
    ESHMHandle* handle = eshm_init(&config);

    // Write data
    eshm_write(handle, "Hello", 6);

    // Read response
    char buffer[256];
    int bytes = eshm_read(handle, buffer, sizeof(buffer));

    // Cleanup
    eshm_destroy(handle);
    return 0;
}
```

## Documentation

### Integration Resources

- **[Integration Guide](docs/INTEGRATION_GUIDE.md)** - Complete integration guide with three methods
- **[Client Integration Example](docs/examples/client_integration/)** - Working example with master/slave applications

### API and Usage

- **[README.md](README.md)** - API documentation, features, and examples
- **[Quick Start Guide](docs/QUICK_START.md)** - Getting started tutorial
- **[Testing Guide](docs/TEST.md)** - Testing and C++/Python interoperability

### Additional Resources

- **[Examples](examples/)** - Additional usage examples
- **[Python README](py/README.md)** - Python wrapper documentation
- **[Test Suite](test/)** - Comprehensive test cases

## Technical Specifications

### Requirements

- **Operating System:** Linux with POSIX shared memory support
- **Build System:** CMake 3.10 or later
- **Compiler:** C++17 capable compiler (GCC 7+, Clang 5+)
- **Dependencies:** pthread, rt (POSIX real-time extensions)

### Performance Characteristics

**Small Messages (default 4KB channels):**
- **Throughput:** 2.7M+ messages/second (C++ bidirectional)
- **Read Latency:** <100ns (lock-free sequence locks)
- **Write Latency:** <200ns (memory barriers + memcpy)

**Large Data Transfers:**
- **Single 4K Frame (32 MB):** 2.5 GB/s throughput
- **Dual 4K Frames (64 MB):** 4.1 GB/s throughput, 130+ fps equivalent

**System:**
- **Heartbeat Rate:** 1000 updates/second (1ms interval, configurable)
- **Stale Detection:** 100ms threshold (configurable)
- **CPU Overhead:** <0.1% per process

### Memory Layout (Configurable)

**Default Configuration (`ESHM_MAX_DATA_SIZE=4096`):**
- Total shared memory: ~8.5 KB
- ESHMHeader: 64 bytes (aligned)
- master_to_slave channel: ~4.2 KB
- slave_to_master channel: ~4.2 KB

**4K Image Configuration (`ESHM_MAX_DATA_SIZE=33554432`):**
- Total shared memory: ~64 MB
- Supports single 4K RGBA frame (31.6 MB) per channel

**Dual-Frame Configuration (`ESHM_MAX_DATA_SIZE=67108864`):**
- Total shared memory: ~128 MB
- Supports two 4K RGBA frames (63.3 MB) per channel

All configurations use cache-line alignment (64 bytes) to prevent false sharing.

### Versioning

- **Semantic Version:** 1.0.0 (Major.Minor.Patch)
- **SOVERSION:** 1 (shared library ABI version)
- **CMake Package Version:** Compatible with 1.x.x

## Build Verification

```bash
# Clean build
rm -rf build && mkdir build && cd build
cmake ..
make

# Verify libraries
ls -lh libeshm*.so*
# Output:
# libeshm.so -> libeshm.so.1
# libeshm.so.1 -> libeshm.so.1.0.0
# libeshm.so.1.0.0
# libeshm_data.so -> libeshm_data.so.1
# libeshm_data.so.1 -> libeshm_data.so.1.0.0
# libeshm_data.so.1.0.0

# Test demo
./eshm_demo master &
./eshm_demo slave eshm1
```

## Support

For questions, issues, or contributions:

- Review the [Integration Guide](docs/INTEGRATION_GUIDE.md) for setup help
- Check [examples](examples/) and [tests](test/) for usage patterns
- See the [README](README.md) for complete API documentation

## License

This is a demonstration/educational project.
