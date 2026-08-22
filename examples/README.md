# ESHM Examples

Nine self-contained directories, in the order worth reading them. Every one
that involves a channel ships a **C++ side and a Python side that talk to each
other**, and both directions are exercised — the shared memory does not care
which language is on the other end.

```
examples/
├── 01_hello_channel/     publisher.cpp   consumer.cpp     peer.py
├── 02_structured_data/   sender.cpp      receiver.cpp     peer.py   run_interop.sh
├── 03_c_api/             c_writer.c      c_reader.c       peer.py
├── 04_monitoring/        monitor.cpp                      peer.py
├── 05_reconnection/      resilient_consumer.cpp  flaky_publisher.cpp   peer.py
├── 06_large_payload/     frame_sender.cpp  frame_receiver.cpp  frame_protocol.h  peer.py
├── 07_rich_types/        rich_types.cpp                             (C++ only)
├── 08_benchmark/         bench.cpp                        bench.py
├── 09_integration/       master.cpp      slave.cpp        peer.py
└── run_all.sh            smoke-tests every pairing
```

| # | Directory | What it teaches | Languages |
|---|---|---|---|
| 01 | [hello_channel](01_hello_channel/) | Create, attach, write, read, timeouts, cleanup | C++ ↔ Python |
| 02 | [structured_data](02_structured_data/) | Typed records over ASN.1 DER, all five wire types | C++ ↔ Python |
| 03 | [c_api](03_c_api/) | The ABI-stable C surface the bindings use | C ↔ Python |
| 04 | [monitoring](04_monitoring/) | Statistics, roles, liveness, heartbeats | C++ ↔ Python |
| 05 | [reconnection](05_reconnection/) | Surviving a master restart; every recovery knob | C++ ↔ Python |
| 06 | [large_payload](06_large_payload/) | Payloads bigger than the channel; `ESHM_MAX_DATA_SIZE` | C++ ↔ Python |
| 07 | [rich_types](07_rich_types/) | `Event`, `FunctionCall`, `ImageFrame` | C++ only (see below) |
| 08 | [benchmark](08_benchmark/) | Round-trip throughput; which codec path to use | C++ ↔ Python |
| 09 | [integration](09_integration/) | Consuming ESHM from your own CMake project | C++ ↔ Python |

## Build and run

Examples build with the tree by default:

```bash
cmake -S . -B build && cmake --build build -j$(nproc)
```

Binaries land in `build/examples/<NN_name>/`. Each directory also builds
**standalone** against an installed ESHM, so you can copy one into your own
project and start from it:

```bash
cd examples/01_hello_channel
cmake -S . -B build && cmake --build build
```

Every C++ program pairs with the `peer.py` beside it. From a source tree the
Python side finds the bindings and `libeshm.so` on its own; to be explicit:

```bash
export PYTHONPATH=$PWD/py ESHM_LIB=$PWD/build/libeshm.so
```

Check every pairing at once:

```bash
./examples/run_all.sh            # 9 bounded cases, exits non-zero on failure
```

## API coverage

Every public entry point is demonstrated somewhere:

| Function | Example |
|---|---|
| `eshm_init`, `eshm_destroy`, `eshm_default_config` | 01, everywhere |
| `eshm_write`, `eshm_read` | 01 |
| `eshm_read_ex` (timeout and non-blocking) | 01, 02, 05 |
| `eshm_write_data`, `eshm_read_data`, `eshm_free_value` | 03 |
| `dh_create`, `dh_encode`, `dh_decode`, `dh_free_value`, `dh_destroy` | 03 |
| `eshm_get_stats` (all 13 fields) | 04 |
| `eshm_get_role`, `eshm_check_remote_alive`, `eshm_update_heartbeat` | 04 |
| `eshm_error_string` and the error codes | 01, 05, 06 |
| `ESHM_ROLE_MASTER` / `SLAVE` | 01 |
| `ESHM_ROLE_AUTO` | 05 (`--auto`) |
| `disconnect_behavior`: IMMEDIATELY / ON_TIMEOUT / NEVER | 05 (`--behavior`) |
| `max_reconnect_attempts`, `reconnect_wait_ms`, `reconnect_retry_interval_ms` | 05 |
| `stale_threshold_ms`, `auto_cleanup`, `use_threads` | 04, 05 |
| `ESHM_MAX_DATA_SIZE`, `ESHM_HEARTBEAT_INTERVAL_MS` | 06 |
| `DataHandler` scalar + binary types | 02, 03 |
| `DataHandler` Event / FunctionCall / ImageFrame | 07 |
| CMake integration (`find_package`, submodule, FetchContent) | 09 |

## Four facts the examples exist to teach

1. **Start the master first.** A slave `eshm_init` fails immediately if the
   segment does not exist. Retry — every consumer here does. (01)
2. **A reader only sees writes made after its first read.** The read path
   baselines the channel's write counter on the first call, so anything
   written earlier is never delivered. (01)
3. **The channel holds one value per direction, not a queue.** A writer that
   outruns its reader overwrites unread values. That is what makes reads
   lock-free; when you need every byte, add a handshake. (06)
4. **`eshm_check_remote_alive()` does not mean "a peer is attached".** It means
   "not yet detected as stale", which is true on an empty channel. Use
   `slave_alive` / `master_alive` from `eshm_get_stats()` to ask whether anyone
   is there. (04)

## Why 07 is C++ only

`DataHandler` defines eight types. The five universal ASN.1 ones — INTEGER,
BOOLEAN, REAL, STRING, BINARY — are implemented in both codecs and cross the
language boundary freely. `EVENT`, `FUNCTION_CALL` and `IMAGE_FRAME` use custom
application tags implemented **only in C++**; the Python codec rejects them.
[07's README](07_rich_types/README.md) covers what to do when Python is on the
other end.

## Housekeeping

Channel names are POSIX shared memory objects under `/dev/shm/eshm_<name>`.
Examples use distinct defaults, but if you run several at once give them
explicit names, or clean up after a crash:

```bash
ls /dev/shm/eshm_*
rm -f /dev/shm/eshm_*
```

## See also

- [docs/QUICK_START.md](../docs/QUICK_START.md) — getting started
- [docs/INTEGRATION_GUIDE.md](../docs/INTEGRATION_GUIDE.md) — integrating ESHM
- [docs/MEMORY_LAYOUT.md](../docs/MEMORY_LAYOUT.md) — customising the layout
- [test/TEST.md](../test/TEST.md) — the test suite
- [py/README.md](../py/README.md) — Python bindings reference
