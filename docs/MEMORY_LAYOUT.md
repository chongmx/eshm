# ESHM Memory Layout Customization

ESHM provides compile-time configuration of memory layout parameters to suit different use cases.

## Overview

The memory layout consists of:
- **Header** (64 bytes): Control information, heartbeat counters, PIDs
- **Two Channels** (variable size each): Bidirectional communication buffers

By default, each channel can hold 4096 bytes of data. You can customize this at build time.

## Configuration Options

### ESHM_MAX_DATA_SIZE

Controls the maximum data size per channel.

**Default:** 4096 bytes

**Usage:**
```bash
cmake -DESHM_MAX_DATA_SIZE=8192 ..
```

**Examples:**
- `2048` - Smaller footprint for embedded systems or small messages
- `8192` - Double the capacity for larger messages
- `16384` - Large messages (e.g., binary data, serialized objects)
- `65536` - Very large messages (use with caution, affects memory and performance)

**Impact:**
- **Memory:** Total shared memory = 64 + 2 × (72 + ESHM_MAX_DATA_SIZE) bytes (approximate)
- **Performance:** Larger sizes may affect cache performance

### ESHM_HEARTBEAT_INTERVAL_MS

Controls how frequently the heartbeat counter is updated.

**Default:** 1 ms

**Usage:**
```bash
cmake -DESHM_HEARTBEAT_INTERVAL_MS=5 ..
```

**Examples:**
- `1` - Default, provides fast stale detection (100ms threshold = 100 updates)
- `2` - Reduces CPU usage slightly
- `5` - Lower CPU usage, slower stale detection
- `10` - Minimal CPU usage (not recommended for production)

**Impact:**
- **CPU:** Higher values reduce CPU overhead of heartbeat thread
- **Stale Detection:** Affects actual time for stale detection (threshold × interval)

## Memory Layout

```
Total Shared Memory Layout:
┌─────────────────────────────────────────┐
│ ESHMHeader (64 bytes, aligned)          │
│  - Magic number, version                │
│  - Master/slave heartbeat counters      │
│  - PIDs, alive flags                    │
│  - Stale threshold                      │
└─────────────────────────────────────────┘
┌─────────────────────────────────────────┐
│ master_to_slave Channel (aligned 64)    │
│  - Sequence lock (4 bytes)              │
│  - Data size (4 bytes)                  │
│  - Data buffer (ESHM_MAX_DATA_SIZE)     │
│  - Write/read counters (16 bytes)       │
│  - Padding (48 bytes)                   │
└─────────────────────────────────────────┘
┌─────────────────────────────────────────┐
│ slave_to_master Channel (aligned 64)    │
│  - Sequence lock (4 bytes)              │
│  - Data size (4 bytes)                  │
│  - Data buffer (ESHM_MAX_DATA_SIZE)     │
│  - Write/read counters (16 bytes)       │
│  - Padding (48 bytes)                   │
└─────────────────────────────────────────┘
```

## Size Calculations

Default configuration (ESHM_MAX_DATA_SIZE=4096):
- Header: 64 bytes
- Each channel: ~4168 bytes (4096 + metadata + padding)
- **Total: ~8576 bytes**

Custom configuration (ESHM_MAX_DATA_SIZE=8192):
- Header: 64 bytes
- Each channel: ~8264 bytes (8192 + metadata + padding)
- **Total: ~16592 bytes**

## Usage Examples

### Standard Build
```bash
mkdir build && cd build
cmake ..
make
```

### Custom Data Size
```bash
mkdir build && cd build
cmake -DESHM_MAX_DATA_SIZE=8192 ..
make
```

### Multiple Customizations
```bash
mkdir build && cd build
cmake -DESHM_MAX_DATA_SIZE=16384 \
      -DESHM_HEARTBEAT_INTERVAL_MS=2 \
      ..
make
```

### In Parent Project (Submodule)
```bash
# In your project's build directory
cmake -DESHM_MAX_DATA_SIZE=8192 ..
```

The settings are automatically applied to ESHM when built as a submodule.

## Generated Configuration

The settings are written to `build/include/eshm_config.h`:

```c
#define ESHM_MAX_DATA_SIZE 8192
#define ESHM_HEARTBEAT_INTERVAL_MS 1
#define ESHM_CHANNEL_SIZE_APPROX (72 + ESHM_MAX_DATA_SIZE)
#define ESHM_TOTAL_SIZE_APPROX (64 + 2 * ESHM_CHANNEL_SIZE_APPROX)
```

This header is automatically included by `eshm_data.h` and used throughout the library.

## Recommendations

### Small Messages (<1KB)
```bash
cmake -DESHM_MAX_DATA_SIZE=2048 ..
```
Reduces memory footprint with minimal impact on functionality.

### Large Messages (>4KB)
```bash
cmake -DESHM_MAX_DATA_SIZE=16384 ..
```
Allows larger payloads without fragmenting data externally.

### Low-Power Devices
```bash
cmake -DESHM_HEARTBEAT_INTERVAL_MS=5 ..
```
Reduces CPU wake-ups from heartbeat thread.

### Production Systems
```bash
# Use defaults
cmake ..
```
The defaults (4KB, 1ms) are optimized for most use cases.

## Important Notes

1. **Consistency Required:** All processes sharing the same memory must be compiled with identical settings.

2. **No Runtime Changes:** These are compile-time settings. To change them, you must recompile ESHM.

3. **Cache Performance:** Very large channel sizes (>64KB) may negatively impact CPU cache performance.

4. **Alignment:** Structures are cache-line aligned (64 bytes) for optimal performance.

5. **Binary Compatibility:** Changing these settings breaks binary compatibility - all linked applications must be recompiled.

## Verification

Check the active configuration:

```bash
# View generated config
cat build/include/eshm_config.h

# Calculate actual memory size
# For default (4096): ~8576 bytes
# For 8192: ~16592 bytes
# For 16384: ~32624 bytes
```

## See Also

- [README.md](../README.md) - Main documentation
- [Integration Guide](INTEGRATION_GUIDE.md) - Integration methods
- [Technical Details](../README.md#technical-details) - Performance characteristics
