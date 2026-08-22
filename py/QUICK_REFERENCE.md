# ESHM Python Quick Reference

## Installation

```bash
cd py && ./build_shared_lib.sh
```

## Basic Usage

### Master
```python
from eshm import ESHM, ESHMRole

with ESHM("my_shm", role=ESHMRole.MASTER) as eshm:
    eshm.write(b"Hello!")
    response = eshm.try_read()
```

### Slave
```python
from eshm import ESHM, ESHMRole

with ESHM("my_shm", role=ESHMRole.SLAVE) as eshm:
    data = eshm.read()  # Blocks with 1000ms timeout
    eshm.write(b"ACK")
```

## Read Methods

```python
# Simple read (1000ms timeout, returns bytes)
data = eshm.read()

# Custom timeout
data = eshm.read(timeout_ms=500)

# Non-blocking (returns None if no data)
data = eshm.try_read()
```

## Reconnection

```python
# Unlimited retries
ESHM("my_shm",
     role=ESHMRole.SLAVE,
     max_reconnect_attempts=0,
     reconnect_retry_interval_ms=100)
```

## Statistics

```python
stats = eshm.get_stats()
print(f"Master PID: {stats['master_pid']}")
print(f"Writes: {stats['m2s_write_count']}")
```

## Error Handling

```python
try:
    data = eshm.read()
except TimeoutError:
    print("Timed out")
except RuntimeError as e:
    print(f"Error: {e}")
```

## Complete Example

```python
#!/usr/bin/env python3
from eshm import ESHM, ESHMRole
import sys

def master():
    with ESHM("demo", role=ESHMRole.MASTER) as eshm:
        eshm.write(b"Hello from Python!")
        print(eshm.try_read())

def slave():
    with ESHM("demo", role=ESHMRole.SLAVE) as eshm:
        print(eshm.read())
        eshm.write(b"Response from Python!")

if __name__ == "__main__":
    master() if len(sys.argv) > 1 and sys.argv[1] == "master" else slave()
```

## Run Examples

```bash
# Simple
python3 examples/01_hello_channel/peer.py publish
python3 examples/01_hello_channel/peer.py consume

# Advanced (JSON, stats)
python3 examples/04_monitoring/peer.py generate
python3 examples/04_monitoring/peer.py watch

# Reconnection demo
python3 examples/05_reconnection/peer.py consume --attempts 0 --wait 0
python3 examples/05_reconnection/peer.py publish

# Performance benchmarks
python3 py/tests/performance/benchmark_slave.py eshm1 1000
python3 py/tests/performance/benchmark_master.py eshm1
```
