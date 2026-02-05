# ESHM Integration Guide

This guide shows how to integrate ESHM into your C++ project.

## Integration Methods

ESHM supports three integration methods:

1. **Git Submodule** - Add ESHM as a submodule in your project
2. **System Installation** - Install ESHM system-wide
3. **Manual Copy** - Copy source files directly into your project

---

## Method 1: Git Submodule

### Add ESHM to Your Project

```bash
cd your_project/
git submodule add <eshm-repo-url> 3rdparty/eshm
git submodule update --init --recursive
```

### Update CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.10)
project(MyApp)

set(CMAKE_CXX_STANDARD 17)

# Add ESHM
add_subdirectory(3rdparty/eshm)

# Link your executable
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE ESHM::eshm)
```

### Build Your Project

```bash
mkdir build && cd build
cmake ..
make
```

When included as a subdirectory, ESHM automatically skips building tests, examples, and demo.

---

## Method 2: System Installation

### Install ESHM

```bash
cd eshm/
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
sudo make install
sudo ldconfig
```

### Update CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.10)
project(MyApp)

set(CMAKE_CXX_STANDARD 17)

# Find ESHM package
find_package(ESHM 1.0 REQUIRED)

# Link your executable
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE ESHM::eshm)
```

### Build Your Project

```bash
mkdir build && cd build
cmake ..
make
```

---

## Method 3: Manual Copy

### Copy Files

```bash
# Copy headers
cp -r eshm/include/* your_project/include/

# Copy source files
cp -r eshm/src/* your_project/src/
```

### Update CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.10)
project(MyApp)

set(CMAKE_CXX_STANDARD 17)

# Build ESHM libraries
add_library(eshm_data SHARED
    src/asn1_encode.cpp
    src/asn1_decode.cpp
    src/data_handler.cpp
)
target_include_directories(eshm_data PUBLIC include)

add_library(eshm SHARED
    src/eshm.cpp
)
target_include_directories(eshm PUBLIC include)
target_link_libraries(eshm PUBLIC eshm_data pthread rt)

# Link your executable
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE eshm)
```

### Build Your Project

```bash
mkdir build && cd build
cmake ..
make
```

---

## Using ESHM in Your Code

After integration, include the header and use the API:

```cpp
#include <eshm.h>
#include <stdio.h>

int main() {
    // Initialize
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

    // Cleanup
    eshm_destroy(handle);
    return 0;
}
```

---

## What You Get

When you link to `ESHM::eshm`, the following are automatically included:

**Headers:**
- `eshm.h` - Core API
- `eshm_data.h` - Data structures
- `data_handler.h` - ASN.1 handler
- `asn1_der.h` - ASN.1 encoder/decoder

**Libraries:**
- `libeshm.so` - Core functionality
- `libeshm_data.so` - Data handling
- `pthread` - POSIX threads
- `rt` - Real-time extensions

**Compiler Settings:**
- C++17 standard
- Proper include paths
- Thread safety flags

---

## Build Options

Control what gets built with CMake options:

```bash
cmake -DESHM_BUILD_TESTS=OFF \       # Skip tests
      -DESHM_BUILD_EXAMPLES=OFF \    # Skip examples
      -DESHM_BUILD_DEMO=OFF \        # Skip demo
      ..
```

Default behavior:
- Standalone build: All options ON
- Submodule build: All options OFF

## Memory Layout Customization

Customize the memory layout at build time to fit your requirements:

```bash
# Custom channel data size (default: 4096 bytes)
cmake -DESHM_MAX_DATA_SIZE=8192 ..

# Custom heartbeat interval (default: 1 ms)
cmake -DESHM_HEARTBEAT_INTERVAL_MS=5 ..

# Multiple customizations
cmake -DESHM_MAX_DATA_SIZE=16384 \
      -DESHM_HEARTBEAT_INTERVAL_MS=2 \
      ..
```

**Example use cases:**
- Increase `ESHM_MAX_DATA_SIZE` for larger messages (e.g., 8192, 16384)
- Increase `ESHM_HEARTBEAT_INTERVAL_MS` to reduce CPU usage in low-performance environments
- Decrease `ESHM_HEARTBEAT_INTERVAL_MS` for faster stale detection (not recommended)

The configuration generates `eshm_config.h` which is included by all ESHM code. When using ESHM as a submodule, pass these options when configuring your parent project:

```bash
# In your project
cmake -DESHM_MAX_DATA_SIZE=8192 ..
```

---

## Troubleshooting

### Package Not Found

**For submodule:**
```cmake
# Check the path matches your submodule location
add_subdirectory(3rdparty/eshm)
```

**For system install:**
```bash
# Specify install prefix
cmake -DCMAKE_PREFIX_PATH=/usr/local ..
```

### Library Not Found at Runtime

```bash
# Temporary fix
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# Permanent fix
sudo ldconfig
```

### Using Specific ESHM Version

```bash
cd your_project/3rdparty/eshm
git checkout v1.0.0
cd ../..
git add 3rdparty/eshm
git commit -m "Use ESHM v1.0.0"
```

---

## Complete Example

See [docs/examples/client_integration/](../examples/client_integration/) for a working example with:
- CMakeLists.txt configuration
- Master application
- Slave application
- Build instructions

---

## Documentation

- [README.md](../README.md) - API reference and features
- [Quick Start Guide](QUICK_START.md) - Getting started
- [Testing Guide](TEST.md) - Tests and interoperability
- [Examples](../examples/) - Usage examples
