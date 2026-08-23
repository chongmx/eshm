# 01 - Hello channel

The core channel API, and the one example to read first. A **publisher**
(master role, creates the segment) sends a reading twice a second; a
**consumer** (slave role, attaches) prints it and acknowledges.

Both roles exist in C++ and in Python, and every pairing works — the shared
memory does not care which language is on the other end.

| Demonstrates | C++ | Python |
|---|---|---|
| Create a channel (master) | `eshm_init` + `ESHM_ROLE_MASTER` | `ESHM(name, role=MASTER)` |
| Attach to one (slave) | `eshm_init` + `ESHM_ROLE_SLAVE` | `ESHM(name, role=SLAVE)` |
| Send | `eshm_write` | `.write(bytes)` |
| Receive with timeout | `eshm_read_ex(..., 200)` | `.read(timeout_ms=200)` |
| Receive without blocking | `eshm_read_ex(..., 0)` | `.try_read()` |
| Wait until data arrives | `eshm_read_ex(..., ESHM_TIMEOUT_INFINITE)` | `.read(timeout_ms=TIMEOUT_INFINITE)` |
| Decode an error | `eshm_error_string` | exception message |
| Release | `eshm_destroy` | `with` block / `.close()` |

## Build

```bash
cmake -S . -B build && cmake --build build          # standalone, against installed ESHM
```

or, from the repo root, `cmake --build build` puts them in
`build/examples/01_hello_channel/`. Without CMake:

```bash
g++ -std=c++17 publisher.cpp -o hello_publisher -leshm -lpthread -lrt
```

## Run — C++ to Python

Start the master first, in either language.

```bash
# terminal 1                                # terminal 2
./build/hello_publisher demo                python3 peer.py consume demo
python3 peer.py publish demo                ./build/hello_consumer demo
./build/hello_publisher demo                ./build/hello_consumer demo
python3 peer.py publish demo                python3 peer.py consume demo
```

Expected output, C++ publisher with a Python consumer attached:

```
publisher: channel 'demo' is live (Ctrl-C to stop)
-> reading 1 temperature=20.5
   <- ack 1
-> reading 2 temperature=21.0
   <- ack 2
```

## `0` does not mean "wait forever"

In the read functions `timeout_ms = 0` means **do not wait at all** — try once
and return. That is the opposite of what `0` means in `ESHMConfig`, where
`reconnect_wait_ms = 0` and `max_reconnect_attempts = 0` mean *unlimited*.

| You want | Pass |
|---|---|
| Try once, never block | `0` |
| Wait up to N ms | `N` |
| Wait until data arrives | `ESHM_TIMEOUT_INFINITE` |

Blocking reads park on a futex and are woken by the peer's write, so waiting is
cheap — an idle reader uses no CPU. `eshm_set_wakeup_mode(handle,
ESHM_WAKEUP_POLL)` restores the older polling behaviour if you would rather
drive your own loop. See [08](../08_benchmark/) for what the difference costs.

## The two rules that bite everyone

1. **Start the master first.** `eshm_init` with `ESHM_ROLE_SLAVE` (and
   `ESHM(role=SLAVE)`) fails immediately if the segment does not exist yet.
   `consumer.cpp` and `peer.py consume` retry for ~5 s, and your code should
   too. Example [05](../05_reconnection/) covers surviving a master *restart*.
2. **A reader only sees writes made after its first read.** The read path
   starts tracking the channel's write counter on the first call, so a message
   written before the peer's first read is never delivered. Periodic publishers
   are unaffected; request/response code needs a handshake or a retry.

## Crossing the language boundary

Python `str` has no terminator; C++ `printf("%s")` and `std::string(buf)` want
one. Both sides here follow the same convention:

```python
conn.write(text.encode() + b"\x00")     # add NUL when sending to C++
text = data.rstrip(b"\x00").decode()    # strip NUL when receiving from C++
```

For anything richer than a string, use the ASN.1 codec instead of hand-rolling
a format — that is example [02](../02_structured_data/).

## Smallest useful program

```cpp
#include <eshm.h>

ESHMConfig config = eshm_default_config("my_channel");
config.role = ESHM_ROLE_MASTER;          // or ESHM_ROLE_SLAVE to attach
ESHMHandle* handle = eshm_init(&config);

eshm_write(handle, "hello", 6);

char buffer[ESHM_MAX_DATA_SIZE];
size_t n = 0;
if (eshm_read_ex(handle, buffer, sizeof(buffer), &n, 200) == ESHM_SUCCESS) { /* ... */ }

eshm_destroy(handle);
```

```python
from eshm import ESHM, ESHMRole

with ESHM("my_channel", role=ESHMRole.MASTER) as conn:
    conn.write(b"hello\x00")
    reply = conn.try_read()
```
