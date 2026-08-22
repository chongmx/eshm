# 08 - Benchmark

What the channel actually delivers, and what each choice on the far end costs.

A **driver** (master) sends a message and waits for the echo before sending the
next, so the figure reported is complete **round trips** per second. One-way
write rates are much larger and much less useful — they measure how fast you
can overwrite a buffer nobody read.

## Build and run

```bash
cmake -S . -B build && cmake --build build
```

Start the echo side first or second — the driver waits for a peer either way.

```bash
# C++ driving a C++ echo — the ceiling
./build/bench echo demo                    # terminal 1
./build/bench drive demo --seconds 5       # terminal 2

# C++ driving Python
python3 bench.py echo demo                 # terminal 1
./build/bench drive demo --seconds 5       # terminal 2

# Python driving C++
./build/bench echo demo                    # terminal 1
python3 bench.py drive demo --seconds 5    # terminal 2
```

```
bench: 256 byte payload, 5 s

round trips   1362845
elapsed       5.00 s
rate          272569 round trips/s
latency       3.7 us per round trip
throughput    139.6 MB/s (payload only, both directions)
```

`--size N` changes the payload (capped at `ESHM_MAX_DATA_SIZE`). Larger
payloads trade round-trip rate for bandwidth; the crossover is usually a few
hundred bytes.

## Reading the numbers

The round-trip rate is dominated by whichever side is slower, so the three
pairings above tell you where the cost is:

| Pairing | Bounded by |
|---|---|
| C++ ↔ C++ | The channel itself: two memcpys and two sequence-lock round trips |
| C++ ↔ Python | The Python interpreter — one `ctypes` call per read and per write |
| Python ↔ Python | The same, on both ends |

Numbers vary by an order of magnitude across machines and kernel versions. Run
it on your own hardware before designing around a figure; the ratios between
the three pairings are the portable part, not the absolute values.

## Which Python codec to send with

```bash
python3 bench.py codec --records 20000
```

This mode opens no channel worth measuring — it compares the two DataHandler
bindings that examples [02](../02_structured_data/) and
[03](../03_c_api/) use:

| Binding | What happens per record |
|---|---|
| `py/data_handler.py` (pure Python) | DER built field by field in Python bytecode |
| `py/eshm_data.py` (native) | One `ctypes` call; DER built in C++ |

Both produce the same bytes, so the choice is purely about cost on the Python
side. Pure Python is dependency free and easy to read; the native path is what
you want once records get large or rates get high.

## Making it faster

- **Batch.** One 4 KB record beats sixteen 256-byte ones — the per-message cost
  is mostly fixed.
- **Do not echo.** This benchmark is deliberately request/response. A
  one-directional stream with a reader that takes the latest value skips the
  round trip entirely.
- **Size the channel to the payload** rather than chunking
  ([06](../06_large_payload/)).
- **Move the codec into C++** (`ESHMData`, [03](../03_c_api/)).
- **Check `use_threads`.** The heartbeat and monitor threads cost well under
  1% but are not free; `use_threads = false` removes them, at the cost of stale
  detection and therefore of automatic reconnection
  ([05](../05_reconnection/)).
