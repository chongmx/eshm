# ESHM Changelog

## 1.1.0 - 2026-08-23

Two themes: push wakeup for the read path, and a reorganised example tree that
turned up three latent bugs on the way.

**Upgrading:** the shared-memory protocol went from v2 to v3, so **both ends of
a channel must be rebuilt together**. The C ABI did not change (new symbols
only, `SOVERSION` stays 1), so anything that merely links against ESHM keeps
working without recompilation. `eshm_init()` now refuses a mismatched peer with
a diagnostic instead of failing later in a harder-to-read way.

### Added - push wakeup

- **`ESHM_WAKEUP_PUSH` is the default.** A blocking read now parks on a futex
  inside the shared segment and is woken by the peer's write, instead of waking
  every 100 us to look. Measured on the round-trip benchmark
  (`examples/08_benchmark`): **1,694,894 round trips/s, vs 5,174/s polling** -
  the old path slept 100 us on every miss, so a request/response pair paid two
  sleeps per exchange. An idle reader now uses **0.5 ms of CPU per 300 ms of
  waiting instead of 18 ms**, with 6 context switches instead of ~1,700.
- **`eshm_set_wakeup_mode()` / `eshm_get_wakeup_mode()`** to opt out.
  `ESHM_WAKEUP_POLL` restores the previous behaviour exactly. Deliberately a
  setter rather than an `ESHMConfig` field: adding a field would change the
  struct size and break the ABI for already-compiled callers.
- **`ESHM_TIMEOUT_INFINITE`** for reads that should wait until data arrives.
  Previously there was no way to express this - callers had to loop.
- The writer skips the wake syscall entirely unless a reader is parked, and a
  reader spins briefly before parking, so a hot producer/consumer pair never
  enters the kernel on either side.

### Added - protocol v3 and validation

- **`ESHMHeader.features`**, a capability bitmask. A monotonic version cannot
  express "supports X independently of Y"; this can, so future additive
  features need no version bump.
- **`ESHMHeader.layout_size`**, and `eshm_init()` now validates magic,
  `version` and `layout_size` on attach. `version` was previously written once
  and **never read by anything**.
- All four new fields were carved from existing padding, so **no struct
  changed size**; `static_assert`s in `src/eshm.cpp` enforce that.

### Fixed

- **Two peers built with different `ESHM_MAX_DATA_SIZE` used to SIGBUS.** The
  larger build mapped its own `sizeof(ESHMData)` over a smaller segment and
  faulted past end-of-file on its first channel access. Now rejected at attach:
  `memory layout mismatch (segment is 8576 bytes, this build expects 16768 ...)`.
- **C++ -> Python DER decoding never worked.** `DEREncoder` in C++ emits the
  SEQUENCE tag as `0x30` (constructed bit set, correct DER) while the Python
  decoder compared against a bare `0x10` and rejected it, so every buffer a C++
  peer encoded failed to decode in Python. `py/data_handler.py` now compares
  only the tag number (`tag & 0x1F`), as `src/asn1_decode.cpp` always has. The
  Python *encoder* is unchanged, so the wire format is untouched.
- **Reconnection delivered no data.** On a successful reattach the monitor
  thread reset every counter except `last_read_write_count`, which still held
  the dead master's final write count. Because a new master's channel starts at
  0, everything it wrote was discarded until it passed the old total - a slave
  would log `RECONNECTED` and then sit silent.
- **BINARY values were dropped by the native Python path.**
  `ESHMData.write_data()` raised on `DataType.BINARY` and `read_data()` decoded
  it to `None`, though the C API marshals it correctly.
- A reader parked during reconnection is now woken before the monitor unmaps
  the segment, rather than waiting on an address about to stop existing.
- **A writer killed mid-write hung every subsequent reader, permanently.**
  `seqlock_read_begin()` spins while the sequence number is odd, and a writer
  killed between `seqlock_write_begin()` and `seqlock_write_end()` leaves it
  odd forever - so the reader spun at 100% CPU until its own process was
  killed. This is the exact failure mode the lock-free design exists to
  survive, and it was reachable by anything that terminated a peer at the wrong
  instant. The read path now bounds the wait (`ESHM_SEQLOCK_STALL_MS`, 100 ms)
  and falls through to the caller's timeout, letting stale detection and
  reconnection handle it. Found while benchmarking; pre-existing, not
  introduced by the wakeup work.

### Changed
- **examples/ reorganised into nine numbered directories**, each self-contained,
  each buildable standalone against an installed ESHM, and each pairing a C++
  program with a `peer.py` that talks to it **in both directions**:

  | | Directory | Covers |
  |---|---|---|
  | 01 | `hello_channel` | init/attach/write/read/timeouts/cleanup |
  | 02 | `structured_data` | ASN.1 records, all five wire types incl. BINARY |
  | 03 | `c_api` | `eshm_write_data`/`eshm_read_data`/`dh_*`, compiled as C |
  | 04 | `monitoring` | `eshm_get_stats` (all 13 fields), role, liveness |
  | 05 | `reconnection` | every recovery knob, `disconnect_behavior`, `ROLE_AUTO` |
  | 06 | `large_payload` | payloads beyond `ESHM_MAX_DATA_SIZE`, chunking |
  | 07 | `rich_types` | `Event`/`FunctionCall`/`ImageFrame` (C++ only) |
  | 08 | `benchmark` | round-trip rate; pure-Python vs native codec |
  | 09 | `integration` | `find_package` / submodule / FetchContent |

- **Python examples moved out of `py/examples/`** and into the numbered
  directory of the C++ program each one pairs with. `py/tests/performance/`
  is unchanged.
- **`examples/run_all.sh`** smoke-tests every C++/Python pairing, bounded and
  exit-code driven. `scripts/test_interop.sh` now delegates to it;
  `scripts/test_cpp_master_py_slave.sh` was removed as a duplicate.

### Removed
- Superseded example programs, folded into the numbered directories:
  `simple_api_demo.cpp`, `simple_exchange.cpp`, `interop_cpp_{master,slave}.cpp`,
  and the near-identical `test_unlimited_config.cpp` / `test_truly_unlimited.cpp`
  (now `--attempts`/`--wait` flags on `resilient_consumer`).
- `test_write_count.py` (stray, at the repo root).

### Documentation
- Every example directory has a README explaining what it teaches, plus four
  facts the examples exist to make concrete: start the master first; a reader
  only sees writes made after its first read; the channel holds one value per
  direction rather than a queue; and `eshm_check_remote_alive()` reports "not
  stale", which is true on a channel nobody ever attached to - use the
  `slave_alive`/`master_alive` stats to ask whether a peer is there.
- `examples/README.md` carries an API coverage table mapping every public entry
  point to the example that demonstrates it.
- Fixed README references to `py/examples/performance_test.py` and
  `py/examples/benchmark_slave.py`, neither of which has ever existed.

### Added - installation, packaging and Python bindings (also first shipped in 1.1.0)

### Added
- **LICENSE**: MIT.
- **scripts/export_deb.sh**: builds policy-conformant Debian packages -
  `libeshm1` (runtime), `libeshm-dev` (headers, `.so` symlinks, CMake package)
  and `python3-eshm` (bindings), with `shlibs`, the `ldconfig` trigger,
  `copyright` and `changelog.Debian.gz`.
- **scripts/check_install.sh**: reports which pieces are installed on a machine
  and what each missing one enables.
- **CMake `uninstall` target**: removes what the last `cmake --install` recorded.
- **Python bindings are installable**: `py/` is installed as the `eshm` package
  into `python3/dist-packages` (`ESHM_INSTALL_PYTHON`, `ESHM_PYTHON_INSTALL_DIR`).
- **examples/getting_started/**: publisher/consumer pair that builds standalone
  against an installed ESHM via `find_package(ESHM)`, plus the Python twin in
  `py/examples/getting_started.py`.
- **test/functional/test_selftest.cpp**: one-command self-test (forks a slave,
  drives it as master, verifies every byte of the round trip); wired into ctest.
- **test/functional/test_c_api.cpp**: covers the C API - `dh_encode`/`dh_decode`
  and a two-process `eshm_write_data`/`eshm_read_data` exchange.
- **py/tests/test_native_api.py**: the same ground from Python, so the ctypes
  bindings that sit on the C API stay honest. Both are wired into ctest.
- **docs/INSTALL.md**: install, uninstall, packaging and troubleshooting guide.

### Fixed
- **The C API is now built and shipped**. `src/data_handler_c_api.cpp` (`dh_*`)
  and `src/eshm_data_api.cpp` (`eshm_write_data`, `eshm_data_free_value`,
  `eshm_data_get_last_error`) were in the tree but in no target, so those
  symbols existed in no library. Both are compiled now - `dh_*` into
  `libeshm_data`, the ESHM+DataHandler calls into `libeshm` - and declared by
  two new public headers, `include/data_handler_c_api.h` and
  `include/eshm_data_api.h`.
- **Removed a stale duplicate `eshm_read_data`** from `src/eshm_data_api.cpp`.
  It took six parameters and returned an item count, contradicting the
  eight-parameter version that `eshm.h` declares and `src/eshm.cpp` implements
  (which adds `item_count` and `timeout_ms`); compiling both would have been a
  duplicate-symbol error.
- **py/data_handler_native.py and py/eshm_data.py work again**: they looked for
  `build/libeshm_data.so` and `build/libeshm_data_combined.so`, neither of which
  any build produced, and `eshm_data.py` still called the old six-argument
  `eshm_read_data`. Both now resolve the library through `library_path()` and
  use the current signature; `read_data()` gained a `timeout_ms` argument.
- **py/eshm.py**: locates `libeshm.so` automatically (build tree, system
  directories, `ldconfig` cache, `ESHM_LIB` override) instead of a hard-coded
  path that no build produced; `library_path()` exposes the result.
- **py/eshm.py**: `ESHM.__init__` sets `_handle` before it can fail, so a slave
  waiting for its master no longer floods stderr with `AttributeError` from
  `__del__`.
- **py/__init__.py**: also exports `DataHandler`, `DataItem`, `DataType`.


## December 2025 - Documentation Cleanup & Consolidation

### Documentation Simplification (Phase 2)
- **Created docs/TEST.md**: Consolidated interoperability testing guide
  - Merged INTEROP_TEST_RESULTS.md + RUN_INTEROP_DEMO.md
  - Added unit test documentation
  - Comprehensive troubleshooting section
  - **Complete benchmark suite**: All C++/Python configurations tested
- **Enhanced README.md**: Added "High-Performance Features" section
  - Sequence locks implementation details
  - Heartbeat and monitor thread architecture
  - Cache-line alignment and memory layout
  - Performance characteristics table
  - **Complete performance matrix**: All language combinations benchmarked
- **Created test/performance/test_benchmark_master.cpp**: C++ bidirectional benchmark tool
  - Similar to Python benchmark_master.py pattern (write + try_read ACK)
  - C++ Master ↔ C++ Slave: ~2.7M msg/s (30s bidirectional test)
- **Created py/tests/performance/benchmark_slave.py**: Python slave benchmark tool
  - Based on simple_slave.py pattern (read + ACK write)
  - Prints stats at configurable intervals (default: 1000 messages)
  - C++ Master ↔ Python Slave: ~2,700-2,800 msg/s (30s bidirectional test)
- **Created py/tests/performance/benchmark_master.py**: Python master benchmark tool
  - Based on simple_master.py pattern (write + read ACK)
  - Python Master ↔ Python Slave: ~2,000-2,400 msg/s (30s bidirectional test)
- **Removed redundant files**: HIGH_PERFORMANCE_FEATURES.md, INTEROP_TEST_RESULTS.md, RUN_INTEROP_DEMO.md

### Test Organization
- **Organized C++ tests into subdirectories**:
  - `test/functional/`: Basic functionality tests (test_basic, test_master_slave, test_auto_role, test_stale_detection, test_error_handling, test_reconnect)
  - `test/performance/`: Performance benchmarks (test_benchmark_master)
- **Organized Python tests into subdirectories**:
  - `py/tests/performance/`: Performance benchmarks (benchmark_master.py, benchmark_slave.py)
  - `py/examples/`: Example scripts (simple_master.py, simple_slave.py, etc.)

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
