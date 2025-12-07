# ESHM Changelog

## December 2025 - Documentation Cleanup & Consolidation

### Documentation Simplification (Phase 2)
- **Created docs/TEST.md**: Consolidated interoperability testing guide
  - Merged INTEROP_TEST_RESULTS.md + RUN_INTEROP_DEMO.md
  - Added unit test documentation
  - Comprehensive troubleshooting section
  - **Benchmarked C++↔Python performance**: 577-581 msg/sec (bidirectional, tested Dec 2025)
- **Enhanced README.md**: Added "High-Performance Features" section
  - Sequence locks implementation details
  - Heartbeat and monitor thread architecture
  - Cache-line alignment and memory layout
  - Performance characteristics table
  - **Updated with benchmark results**: C++↔Python: 577-581 msg/sec (bidirectional)
- **Created py/examples/benchmark_slave.py**: Bidirectional benchmark tool
  - Based on simple_slave.py pattern (read + ACK write)
  - Prints stats at configurable intervals (default: 1000 messages)
  - Tested: 20s (571 msg/s), 30s (580 msg/s), 60s (581 msg/s)
- **Removed redundant files**: HIGH_PERFORMANCE_FEATURES.md, INTEROP_TEST_RESULTS.md, RUN_INTEROP_DEMO.md

### Documentation Simplification (Phase 1)
- **Removed redundant files**: RECONNECTION_GUIDE.md, PERFORMANCE_TUNING.md, POSIX_SHM_MIGRATION.md, DOCUMENTATION_INDEX.md
- **Consolidated into README**: All reconnection and performance tuning info now in main README
- **Cleaner structure**: Single source of truth for documentation

### What Changed
- Interop testing: docs/TEST.md (was INTEROP_TEST_RESULTS.md + RUN_INTEROP_DEMO.md)
- High-performance features: README "High-Performance Features" section (was HIGH_PERFORMANCE_FEATURES.md)
- Reconnection features: README "Reconnection Features" section (was RECONNECTION_GUIDE.md)
- Performance tuning: README "Performance Testing" section (was PERFORMANCE_TUNING.md)
- POSIX SHM implementation: README "Technical Details" section (was POSIX_SHM_MIGRATION.md)
- Removed all System V IPC references (fully migrated to POSIX)

## December 2025 - Performance & Interoperability Improvements

### Performance Enhancements

**C++ Demo (`main.cpp`)**:
- **500x Speed Increase**: Messaging rate from 2 msg/sec to 1000 msg/sec (default)
- **Configurable Parameters**:
  ```cpp
  #define MESSAGE_INTERVAL_US 1000           // 1ms = 1000 msg/sec
  #define STATS_PRINT_INTERVAL_SEC 1.0       // Print stats every 1 second
  ```
- **Smart Stats Printing**: Cycle-based to prevent terminal flooding
- **Performance Metrics**: C++ master sends at 1000 msg/sec

### Python Interoperability Fixes

**Null Terminator Support**:
- Fixed garbage bytes issue when Python messages read by C++
- All Python examples now add `'\0'` null terminators
- Python strips null terminators when receiving from C++

**Updated Files**:
- `py/examples/simple_master.py`
- `py/examples/simple_slave.py`
- `py/examples/performance_test.py`

**Example**:
```python
# Send to C++
eshm.write(b"Hello\0")

# Receive from C++
data = eshm.read()
message = data.decode('utf-8').rstrip('\0')
```

### Python Performance Test Enhancements

**Configurable Stats Interval**:
```bash
# Default (10000 messages)
python3 performance_test.py slave eshm1

# Custom interval (2000 messages)
python3 performance_test.py slave eshm1 2000
```

**Fixed Issues**:
- Changed from `try_read()` to `read(timeout_ms=10)` for reliable reception
- Added `sys.stdout.flush()` for immediate output
- Shows actual message rate (~80-100 msg/sec for Python)

### POSIX Shared Memory Migration

**Migrated from System V to POSIX**:
- Replaced `shmget()/shmat()` with `shm_open()/mmap()`
- Shared memory visible in `/dev/shm/eshm_<name>`
- Easier debugging with standard file tools
- Better portability across Unix systems

### Default Configuration Updates

**Unified SHM Names**:
- C++ demo: `"eshm1"` (was `"eshm_demo"`)
- Python examples: `"eshm1"` (was `"python_demo"`)
- Performance test: `"perf_test"` (configurable)

### Testing Improvements

**Verified Configurations**:
- C++ Master (1000 msg/sec) + Python Slave (~80-100 msg/sec) ✓
- Python Master + C++ Slave ✓
- C++ Master + Python Performance Test Slave ✓

**Test Results**:
```
C++ Master:   [MASTER] Messages: sent=1000 (1000 msg/sec)
Python Slave: [SLAVE] Messages: received=1000, rate=84 msg/sec
```

## Migration Notes

### For Users Upgrading

**Breaking Changes**:
- Default SHM names changed to `"eshm1"`
- System V IPC completely removed (POSIX only)
- Python requires null terminators for C++ interop

**Non-Breaking Changes**:
- C++ API unchanged
- Python API unchanged (compatible additions only)
- Configuration options unchanged

### Removed Features
- System V shared memory support (use POSIX instead)
- Old default SHM names (use "eshm1" or specify custom)

### Deprecated Documentation
- RECONNECTION_GUIDE.md → See README "Reconnection Features"
- PERFORMANCE_TUNING.md → See README "Performance Testing"
- POSIX_SHM_MIGRATION.md → See README "Technical Details"
- HIGH_PERFORMANCE_FEATURES.md → See README "High-Performance Features"
- INTEROP_TEST_RESULTS.md → See docs/TEST.md
- RUN_INTEROP_DEMO.md → See docs/TEST.md
