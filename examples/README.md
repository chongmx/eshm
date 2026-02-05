# ESHM Examples

This directory contains various examples demonstrating different aspects of ESHM usage.

## Example Categories

### Basic Usage

#### [simple_api_demo.cpp](simple_api_demo.cpp)
Demonstrates the basic ESHM API for master-slave communication.
- Master initialization
- Writing data
- Reading with timeout
- Error handling

**Run:**
```bash
./simple_api_demo
```

#### [simple_exchange.cpp](simple_exchange.cpp)
Complete example showing ESHM combined with DataHandler for structured data exchange.
- Master-slave bidirectional communication
- Encoding/decoding structured data
- Performance measurement

**Run:**
```bash
# See README_simple_exchange.md for details
./run_simple_exchange.sh
```

### Data Handler Examples

#### [example_data_handler.cpp](example_data_handler.cpp)
Comprehensive demonstration of DataHandler features:
- Simple data types (integers, floats, booleans, strings)
- Events with parameters
- Function calls with arguments and return values
- Image frames (1080p, 4K)
- Mixed data payloads
- Performance analysis

**Run:**
```bash
./example_data_handler
```

### Interoperability

#### C++ <-> Python Communication

- [interop_cpp_master.cpp](interop_cpp_master.cpp) - C++ master
- [interop_cpp_slave.cpp](interop_cpp_slave.cpp) - C++ slave
- See `py/examples/` for Python counterparts

Demonstrates cross-language communication using ASN.1 encoding.

**Run:**
```bash
# See README_interop.md for details
./test_interop.sh
```

### Advanced Configuration

#### [test_unlimited_config.cpp](test_unlimited_config.cpp)
Demonstrates unlimited reconnection attempts:
- `max_reconnect_attempts = 0` (unlimited retries)
- Slave continues waiting for master indefinitely
- Configurable retry interval

**Run:**
```bash
# Terminal 1
./test_unlimited_config

# Terminal 2 (start/stop master to test reconnection)
./eshm_demo master
```

#### [test_truly_unlimited.cpp](test_truly_unlimited.cpp)
Demonstrates truly unlimited reconnection:
- `max_reconnect_attempts = 0` (unlimited retries)
- `reconnect_wait_ms = 0` (unlimited wait time)
- Never gives up reconnecting

**Run:**
```bash
./test_truly_unlimited
```

### Client Integration

#### [client_integration/](client_integration/)
Complete example showing how to integrate ESHM into your own project:
- Git submodule setup
- CMake integration
- Master and slave applications
- System-wide installation alternative

See [client_integration/README.md](client_integration/README.md) for detailed instructions.

## Building Examples

All examples are built automatically when building ESHM:

```bash
mkdir build && cd build
cmake ..
make
```

Executables will be in `build/examples/`.

To disable examples when using ESHM as a submodule, CMake automatically sets `ESHM_BUILD_EXAMPLES=OFF`.

## Quick Reference

| Example | Purpose | Complexity |
|---------|---------|------------|
| simple_api_demo | Basic ESHM API | Beginner |
| simple_exchange | ESHM + DataHandler | Intermediate |
| example_data_handler | DataHandler features | Intermediate |
| interop_cpp_master/slave | C++/Python interop | Advanced |
| test_unlimited_config | Reconnection config | Intermediate |
| test_truly_unlimited | Unlimited reconnection | Intermediate |
| client_integration | Project integration | Beginner |

## Additional Resources

- [Quick Start Guide](../docs/QUICK_START.md)
- [Integration Guide](../docs/INTEGRATION_GUIDE.md)
- [Memory Layout Guide](../docs/MEMORY_LAYOUT.md)
- [Main README](../README.md)

## Running Multiple Examples

When running multiple examples, ensure they use different shared memory names to avoid conflicts:

```c
ESHMConfig config = eshm_default_config("my_unique_name");
```

Or clean up shared memory between runs:
```bash
rm /dev/shm/eshm_*
```
