# ESHM Python Wrapper

Python bindings for the Enhanced Shared Memory (ESHM) library - a high-performance IPC library with master-slave architecture and automatic reconnection.

## Features

- **Pythonic API**: Simple, intuitive Python interface
- **Context Manager Support**: Use with `with` statement for automatic cleanup
- **Type Hints**: Full type annotation support
- **Error Handling**: Python exceptions for error conditions
- **All ESHM Features**: Full access to reconnection, statistics, and monitoring

## Installation

### 1. Build the Shared Library

```bash
cd py
./build_shared_lib.sh
```

This creates `build/libeshm.so` which the Python wrapper uses.

### 2. Verify Installation

```python
import sys
sys.path.insert(0, 'py')  # Add py directory to path
from eshm import ESHM, ESHMRole

# Test it
with ESHM("test", role=ESHMRole.MASTER) as eshm:
    print(f"Initialized as {eshm.get_role().name}")
```

## Quick Start

### Basic Master-Slave Communication

**Master Process:**
```python
from eshm import ESHM, ESHMRole

with ESHM("eshm1", role=ESHMRole.MASTER) as eshm:  # Default SHM name
    # Send message with null terminator for C++ compatibility
    eshm.write((b"Hello, World!" + b'\0')

    # Or using string with null terminator
    message = "Hello from Python\0"
    eshm.write(message.encode('utf-8'))

    # Try to read response (non-blocking)
    response = eshm.try_read()
    if response:
        # Strip null terminators when displaying
        print(f"Received: {response.decode('utf-8').rstrip(chr(0))}")
```

**Slave Process:**
```python
from eshm import ESHM, ESHMRole

with ESHM("eshm1", role=ESHMRole.SLAVE) as eshm:
    # Read message (default 1000ms timeout)
    data = eshm.read()
    # Strip null terminators and garbage bytes
    message = data.decode('utf-8').rstrip('\0')
    print(f"Received: {message}")

    # Send response with null terminator
    eshm.write(b"ACK!\0")
```

**Important**: Always include null terminators (`\0`) when communicating with C++ processes!

### Custom Timeout

```python
# Read with custom timeout (500ms)
data = eshm.read(timeout_ms=500)

# Non-blocking read (0ms timeout)
data = eshm.try_read()  # Returns None if no data
```

### Unlimited Reconnection

```python
# Slave will retry indefinitely when master crashes
eshm = ESHM("my_shm",
            role=ESHMRole.SLAVE,
            max_reconnect_attempts=0,  # 0 = unlimited
            reconnect_retry_interval_ms=100)  # Retry every 100ms
```

## API Reference

### Class: ESHM

```python
ESHM(shm_name: str,
     role: ESHMRole = ESHMRole.AUTO,
     disconnect_behavior: ESHMDisconnectBehavior = ESHMDisconnectBehavior.ON_TIMEOUT,
     stale_threshold_ms: int = 100,
     reconnect_wait_ms: int = 5000,
     reconnect_retry_interval_ms: int = 100,
     max_reconnect_attempts: int = 50,
     auto_cleanup: bool = True,
     use_threads: bool = True)
```

#### Methods

**Communication:**
- `write(data: bytes) -> None` - Write data to shared memory
- `read(buffer_size: int = 4096, timeout_ms: Optional[int] = None) -> bytes` - Read data (default 1000ms timeout)
- `try_read(buffer_size: int = 4096) -> Optional[bytes]` - Non-blocking read, returns None if no data

**Monitoring:**
- `get_stats() -> dict` - Get statistics (heartbeat, PIDs, message counts)
- `get_role() -> ESHMRole` - Get current role (MASTER or SLAVE)
- `is_remote_alive() -> bool` - Check if remote endpoint is alive

**Lifecycle:**
- `close()` - Close handle (automatic with context manager)

### Enums

```python
class ESHMRole(IntEnum):
    MASTER = 0
    SLAVE = 1
    AUTO = 2

class ESHMError(IntEnum):
    SUCCESS = 0
    TIMEOUT = -10
    MASTER_STALE = -11
    NO_DATA = -9
    # ... (see eshm.py for full list)

class ESHMDisconnectBehavior(IntEnum):
    IMMEDIATELY = 0
    ON_TIMEOUT = 1
    NEVER = 2
```

## Wakeup mode

Blocking reads park on a futex and are woken by the peer's write - no CPU while
idle. Opt out per handle:

```python
from eshm import ESHM, ESHMRole, ESHMWakeupMode, TIMEOUT_INFINITE

with ESHM("channel", role=ESHMRole.SLAVE) as conn:
    print(conn.wakeup_mode)                 # ESHMWakeupMode.PUSH (default)
    data = conn.read(timeout_ms=TIMEOUT_INFINITE)   # wait until data arrives
    conn.wakeup_mode = ESHMWakeupMode.POLL  # back to the older poll loop
```

`timeout_ms=0` means *do not wait* - the opposite of what `0` means in the
constructor arguments, where it means *unlimited*.

## Named triggers

`eshm.rpc.Rpc` lets a peer run one of your functions by name. No arguments, no
return value: write your data, then fire the trigger, and the handler reads
current state.

```python
from eshm import ESHMRole
from eshm.rpc import Rpc

rpc = Rpc("demo", role=ESHMRole.SLAVE)      # opens "demo_ctl"

@rpc.on_call("process")
def process():                              # exactly one handler per name
    ...

@rpc.on_event("shutting_down")
def shutting_down():                        # any number per name
    ...

with rpc:                                   # start() / stop()
    rpc.emit("ready")
```

Handlers run on a C++ dispatcher thread, not the main thread. See
[examples/10_triggers/](../examples/10_triggers/) for the full pattern and the
coalescing rules.

## Examples

Every example lives in the top-level [`examples/`](../examples/) directory,
next to the C++ program it pairs with — each numbered directory has a `peer.py`
that talks to the C++ side in both directions.

| Directory | Python entry point | Shows |
|---|---|---|
| [01_hello_channel](../examples/01_hello_channel/) | `peer.py publish\|consume` | `write`, `read`, `try_read`, roles, NUL handling |
| [02_structured_data](../examples/02_structured_data/) | `peer.py send\|receive` | `DataHandler`, the pure-Python DER codec |
| [03_c_api](../examples/03_c_api/) | `peer.py write\|read` | `ESHMData`, the codec done in C++ |
| [04_monitoring](../examples/04_monitoring/) | `peer.py generate\|watch` | `get_stats`, `get_role`, `is_remote_alive` |
| [05_reconnection](../examples/05_reconnection/) | `peer.py publish\|consume` | reconnect policy, `ESHMRole.AUTO` |
| [06_large_payload](../examples/06_large_payload/) | `peer.py send\|receive` | payloads larger than the channel |
| [08_benchmark](../examples/08_benchmark/) | `bench.py drive\|echo\|codec` | throughput, and which codec to use |
| [09_integration](../examples/09_integration/) | `peer.py master\|slave` | attaching to an integrated C++ app |
| [10_triggers](../examples/10_triggers/) | `peer.py master\|worker` | named calls and events across languages |

Run any of them against its C++ counterpart:

```bash
cmake -S . -B build && cmake --build build -j$(nproc)
export PYTHONPATH=$PWD/py ESHM_LIB=$PWD/build/libeshm.so

# Terminal 1                                        # Terminal 2
./build/examples/01_hello_channel/hello_publisher demo
python3 examples/01_hello_channel/peer.py consume demo
```

Or check every pairing at once:

```bash
./examples/run_all.sh
```

### Performance benchmarks

The example benchmark covers round-trip rate and the codec comparison:

```bash
python3 examples/08_benchmark/bench.py drive demo --seconds 5   # with ./bench echo
python3 examples/08_benchmark/bench.py codec --records 20000    # pure vs native codec
```

The older fixed-rate benchmark tools remain under `py/tests/performance/`:

```bash
# Terminal 1 - C++ benchmark master (unlimited rate)
./build/test/test_benchmark_master master eshm1

# Terminal 2 - Python slave benchmark (stats every 1000 messages)
python3 py/tests/performance/benchmark_slave.py eshm1 1000

# Or Python-to-Python
python3 py/tests/performance/benchmark_master.py eshm1
python3 py/tests/performance/benchmark_slave.py eshm1 1000
```

## Error Handling

The Python wrapper converts ESHM error codes to Python exceptions:

```python
try:
    data = eshm.read()
except TimeoutError:
    print("Read timed out")
except RuntimeError as e:
    print(f"Error: {e}")
```

## Statistics

Get detailed statistics about the shared memory:

```python
stats = eshm.get_stats()

print(f"Master PID: {stats['master_pid']}")
print(f"Slave PID: {stats['slave_pid']}")
print(f"Master alive: {stats['master_alive']}")
print(f"Slave alive: {stats['slave_alive']}")
print(f"Master heartbeat: {stats['master_heartbeat']}")
print(f"Slave heartbeat: {stats['slave_heartbeat']}")
print(f"Master->Slave writes: {stats['m2s_write_count']}")
print(f"Master->Slave reads: {stats['m2s_read_count']}")
print(f"Slave->Master writes: {stats['s2m_write_count']}")
print(f"Slave->Master reads: {stats['s2m_read_count']}")
```

## Performance

The Python wrapper has minimal overhead:
- Direct ctypes bindings to C library
- Zero-copy for binary data
- Lock-free reads in the underlying C implementation
- 1ms heartbeat updates

Actual performance (bidirectional with ACK responses):
- C++ Master ↔ C++ Slave: ~2.7M msg/sec
- C++ Master ↔ Python Slave: ~2,700-2,800 msg/sec
- Python Master ↔ Python Slave: ~2,000-2,400 msg/sec
- Perfect for high-performance IPC between Python processes or Python↔C/C++

## Requirements

- Python 3.6+
- Linux (System V shared memory)
- GCC with C++11 support
- pthread and rt libraries

## Thread Safety

The underlying C library is thread-safe. Multiple Python threads can safely use the same ESHM instance.

## License

This is a demonstration/educational project.
