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

round trips   6779605
elapsed       4.00 s
rate          1694894 round trips/s
latency       0.6 us per round trip
throughput    867.8 MB/s (payload only, both directions)
```

`--size N` changes the payload (capped at `ESHM_MAX_DATA_SIZE`). Larger
payloads trade round-trip rate for bandwidth; the crossover is usually a few
hundred bytes.

## Push wakeup vs polling

`--poll` switches both ends to `ESHM_WAKEUP_POLL`, the behaviour ESHM had
before 1.1.0, so the two can be compared back to back on the same machine:

```bash
./build/bench echo demo  --poll            # terminal 1
./build/bench drive demo --poll --seconds 4
```

Measured on one machine (WSL2), 256-byte payload:

| | Push (default) | Poll |
|---|---|---|
| Round trips/sec, C++ to C++ | **1,981,817** | 5,492 |
| Round trips/sec, C++ to Python | **205,882** | 5,235 |
| Per round trip, C++ to C++ | **0.5 µs** | 182.1 µs |

The gap is this wide because request/response is the worst case for polling:
the old read path slept 100 µs on *every* miss, and a round trip contains two
of them. Streaming, where the reader rarely misses, barely differs.

The flip side is CPU. Push spins briefly before parking, so a hot loop keeps a
core busy — that is the trade that buys the sub-microsecond round trip. An
*idle* reader parks and costs nothing:

| Over a 300 ms wait with no traffic | Push | Poll |
|---|---|---|
| CPU used | **0.5 ms** | 18.2 ms |
| Context switches | **6** | 1,702 |

So push is the right default for both hot and idle readers. `--poll` exists for
callers who want to own their own loop entirely.

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
