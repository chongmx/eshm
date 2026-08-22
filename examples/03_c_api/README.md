# 03 - The ABI-stable C API

`shm_protocol::DataHandler` is a C++ class whose interface uses `std::string`,
`std::vector` and `std::variant`. That means it cannot be called from C, from
ctypes, or safely across compiler and standard-library versions. Two headers
expose the same functionality through plain C with opaque handles, and that is
the surface every non-C++ consumer should bind to — including the Python
bindings shipped with ESHM.

The two programs here are compiled as **C, not C++**, which is the point: if
they link and run, the surface really is C-callable.

| Header | Library | Entry points |
|---|---|---|
| [`eshm_data_api.h`](../../include/eshm_data_api.h) | `libeshm` | `eshm_write_data` (encode + write), `eshm_data_free_value`, `eshm_data_get_last_error` |
| [`eshm.h`](../../include/eshm.h) | `libeshm` | `eshm_read_data` (read + decode), `eshm_free_value` |
| [`data_handler_c_api.h`](../../include/data_handler_c_api.h) | `libeshm_data` | `dh_create`, `dh_encode`, `dh_decode`, `dh_free_value`, `dh_destroy` — the codec alone, no channel |

## Value representation

Values are passed as `void*`; the type tag says what the pointer points at.
This table is the contract, and both `c_writer.c` and the Python bindings obey it:

| DataType | Value is | C | Python (`ESHMData`) |
|---|---|---|---|
| `INTEGER` (0) | `int64_t*` | `&(int64_t){42}` | `int` |
| `BOOLEAN` (1) | `bool*` | `&(bool){true}` | `bool` |
| `REAL` (2) | `double*` | `&(double){23.5}` | `float` |
| `STRING` (3) | `char*` | `"text"` | `str` |
| `BINARY` (4) | `struct { uint8_t* data; size_t len; }*` | `&(struct binary_value){buf, n}` | `bytes` |

Decoded values are heap allocated. Release every one:

```c
for (i = 0; i < item_count; ++i) eshm_free_value(values[i], types[i]);
```

Watch the argument order — `eshm_free_value(value, type)` and
`eshm_data_free_value(type, value)` take theirs the other way round.

## Build

```bash
cmake -S . -B build && cmake --build build     # standalone
```

or without CMake — note it is `cc`, not `c++`:

```bash
cc -std=c11 c_writer.c -o c_writer -leshm -lpthread -lrt
```

## Run — both directions

```bash
# C encodes, Python decodes
./build/c_writer demo                     # terminal 1
python3 peer.py read demo                 # terminal 2

# Python encodes, C decodes
python3 peer.py write demo                # terminal 1
./build/c_reader demo                     # terminal 2
```

```
c_reader: attached to 'demo' (Ctrl-C to stop)
<- record 1 (5 items)
  counter      INTEGER  0
  temperature  REAL     20.00
  enabled      BOOLEAN  true
  source       STRING   "Python writer"
  checksum     BINARY   00deadbeef (5 bytes)
```

## Why bind here instead of to the codec in Python

Example [02](../02_structured_data/) encodes DER in Python — readable,
dependency free, and it costs Python bytecode per field. `ESHMData` in
[`py/eshm_data.py`](../../py/eshm_data.py) instead calls `eshm_write_data()` and
`eshm_read_data()`, so a record crosses the FFI boundary once as finished
values rather than field by field. Same wire bytes either way; the two are
interchangeable on the network. Example [08](../08_benchmark/) measures the
difference.

## Waiting for a peer

Both programs wait for a reader before they start counting, and they use the
`slave_alive` flag from `eshm_get_stats()` to do it — **not**
`eshm_check_remote_alive()`. That call answers "has the peer been detected as
stale", which is false before anyone has ever attached, so on an empty channel
it reports the peer as alive. `slave_alive` is set by the slave in
`eshm_init()` and cleared in `eshm_destroy()`, which is the question actually
being asked. Example [04](../04_monitoring/) covers the rest of the stats.
