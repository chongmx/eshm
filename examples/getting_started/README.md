# Getting started with ESHM

Two small programs that show the whole lifecycle: a **publisher** (master role,
creates the channel) and a **consumer** (slave role, attaches and acknowledges).

## Build against an installed ESHM

```bash
sudo apt install ./libeshm1_1.0.0_amd64.deb ./libeshm-dev_1.0.0_amd64.deb
cmake -S . -B build && cmake --build build
```

or without CMake:

```bash
g++ -std=c++17 publisher.cpp -o publisher -leshm -lpthread -lrt
```

## Run

```bash
./build/getting_started_publisher demo     # terminal 1
./build/getting_started_consumer  demo     # terminal 2
```

The Python version in [../../py/examples/getting_started.py](../../py/examples/getting_started.py)
speaks the same channel, so any pairing works:

```bash
./build/getting_started_publisher demo   +   python3 ../../py/examples/getting_started.py consume demo
python3 ../../py/examples/getting_started.py publish demo   +   ./build/getting_started_consumer demo
```

## The two rules that bite everyone

1. **Start the master (publisher) first.** `eshm_init` with `ESHM_ROLE_SLAVE`
   fails immediately if the segment does not exist yet — `consumer.cpp` retries
   for ~5 s, and your code should too.
2. **A reader only sees writes made after its first read.** The read path starts
   tracking the channel's write counter on the first call, so a message written
   before the peer's first read is never delivered. Periodic publishers are
   unaffected; request/response code needs a handshake or a retry.

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
