# 02 - Structured data across the language boundary

Example [01](../01_hello_channel/) moves bytes. Real payloads need types and
field names, and both ends need to agree without sharing a struct definition.
ESHM ships an ASN.1 DER codec for exactly that: `DataHandler` in C++, a wire
compatible pure-Python implementation in `py/data_handler.py`.

A **sender** (master) encodes one record per tick; a **receiver** (slave)
decodes it, prints the typed values, and acknowledges with an encoded record of
its own. Both sides exist in C++ and Python, and both directions are covered.

## The five types that cross the boundary

| ASN.1 | C++ | Python | Built with |
|---|---|---|---|
| INTEGER | `int64_t` | `int` | `createInteger` / `create_integer` |
| REAL | `double` | `float` | `createReal` / `create_real` |
| BOOLEAN | `bool` | `bool` | `createBoolean` / `create_boolean` |
| UTF8_STRING | `std::string` | `str` | `createString` / `create_string` |
| OCTET_STRING | `std::vector<uint8_t>` | `bytes` | `createBinary` / `create_binary` |

`DataHandler` also carries `Event`, `FunctionCall` and `ImageFrame`. Those are
C++ only — the Python codec does not implement those tags — so they get their
own example, [07](../07_rich_types/).

## Build

```bash
cmake -S . -B build && cmake --build build     # standalone
```

From the repo root the binaries land in `build/examples/02_structured_data/`.

## Run — both directions

```bash
# C++ encodes, Python decodes
./build/structured_sender demo            # terminal 1
python3 peer.py receive demo              # terminal 2

# Python encodes, C++ decodes
python3 peer.py send demo                 # terminal 1
./build/structured_receiver demo          # terminal 2
```

Both programs take an optional trailing count (`0` = run until Ctrl-C), and the
sender waits for a receiver to attach before it starts, so a bounded run is not
a race.

Or run both directions unattended:

```bash
./run_interop.sh                # uses ../../build; pass a build dir to override
```

```
=== C++ sender -> Python receiver ===
sender: channel 'interop_cpp_py' is live, waiting for a receiver...
receiver: attached to 'interop_cpp_py' (Ctrl-C to stop)
sender: receiver attached (Ctrl-C to stop)
-> #0 temperature=20.00 enabled=true (106 bytes on the wire)
<- #0 temperature=20.00 enabled=True source="C++ sender" checksum=00deadbeef
   <- ack #0 from "Python receiver"
--- C++ sender -> Python receiver ok
```

## How the encoding works

Each buffer is one DER SEQUENCE holding three SEQUENCEs — types, keys, then
values:

```
SEQUENCE {
  SEQUENCE { INTEGER 0, INTEGER 2, INTEGER 1, INTEGER 3, INTEGER 4 }   -- types
  SEQUENCE { "counter", "temperature", "enabled", "source", "checksum" } -- keys
  SEQUENCE { 42, 23.5, TRUE, "C++ sender", 00:de:ad:be:ef }            -- values
}
```

Keys travel with the data, so the receiver reconstructs a dictionary without
knowing the sender's layout — which is what makes the C++ and Python sides
interchangeable.

`REAL` is the one place implementations usually disagree. Both of these use
ISO 6093 NR3: a `0x03` marker byte followed by IEEE 754 binary64, big-endian.
`23.5` encodes as `09 09 03 40 37 80 00 00 00 00 00`.

## Doing the codec work in C++ from Python

The pure-Python codec here is readable and dependency free, but every field
costs Python bytecode. When the record is large or the rate is high, call the
C++ codec through the ABI-stable C API instead and Python never touches the
DER — that is example [03](../03_c_api/), and example [08](../08_benchmark/)
measures the difference.
