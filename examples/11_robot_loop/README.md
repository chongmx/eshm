# 11 - Robot loop: a policy-in-the-loop benchmark

The scenario ESHM is built for. C++ drives the robot and the cameras; Python
runs the policy:

```
C++ (robot)                              Python (policy)
-----------                              ---------------
state     ---- 100 Hz .. 1 kHz ------>   read newest
camera 0  ---- ~30 fps          ----->   read newest
camera 1  ---- ~30 fps          ----->   read newest
          <--- action chunk, 10-30 Hz    infer
```

Every stream is latest-value, which is what ESHM's channel *is*. A policy
running at 20 Hz does not want all 1000 state samples per second — it wants the
newest one at the instant it starts inferring. Samples it never read were not
lost; they were superseded before it could have acted on them.

## Channel layout

| Channel | Direction | Carries |
|---|---|---|
| `robot` | both | state (master→slave) **and** action chunks (slave→master) |
| `robot_cam0`, `robot_cam1`, … | master→slave | one camera each |

The control loop needs only **one** channel: a channel is bidirectional, so
state goes out on master→slave and actions come back on slave→master. Cameras
get a channel each so a 900 KB frame never overwrites a 136-byte state sample.

## Build

Camera frames need a channel big enough to hold one, which is a compile-time
constant, so the benchmark wants its own build:

```bash
cmake -S . -B build-robot -DCMAKE_BUILD_TYPE=Release -DESHM_MAX_DATA_SIZE=4194304
cmake --build build-robot -j$(nproc)
```

4 MB fits 1280×720×3 (2.76 MB). For 640×480×3 only, 2 MB is enough. `robot_sim`
refuses to start with a channel too small and tells you the value to use.

## Run

```bash
export PYTHONPATH=$PWD/py ESHM_LIB=$PWD/build-robot/libeshm.so

./build-robot/examples/11_robot_loop/robot_sim --rate 1000 --cameras 2   # terminal 1
python3 examples/11_robot_loop/policy.py --hz 20 --infer-ms 25           # terminal 2
```

Or sweep the whole matrix and get one table:

```bash
./examples/11_robot_loop/run_bench.sh build-robot 4
```

## Measured

One machine (WSL2 on a laptop), 4 s per case, Release build. **Absolute numbers
will not transfer to your hardware — the ratios and the shape will.** A tuned
bare-metal box with an isolated core will do considerably better on jitter.

`loop` is the full closed loop — state written → policy read → inferred →
action read back — so it *includes* the simulated inference time.

### Control rate, no cameras

| Case | ctrl Hz | jitter p50 | jitter p99 | loop p50 | state age p50 |
|---|---|---|---|---|---|
| 25 Hz | 25.0 | 95 µs | 574 µs | 45.97 ms | 19.80 ms |
| 100 Hz | 100.0 | 91 µs | 584 µs | 32.99 ms | 7.39 ms |
| 500 Hz | 500.0 | 95 µs | 563 µs | 27.00 ms | 1.49 ms |
| 1 kHz | 1000.0 | 96 µs | 578 µs | 26.21 ms | 0.84 ms |
| 1 kHz `--spin` | 1000.0 | **23 µs** | 556 µs | 25.93 ms | 0.49 ms |

**State age tracks the control period**, as it must: at 25 Hz a policy reads
state that is on average half a 40 ms period old. If your policy needs fresh
state, raise the control rate — that is the lever, not the transport.

### Camera load, 1 kHz control

| Cameras | Bandwidth | ctrl Hz | jitter p99 | loop p50 |
|---|---|---|---|---|
| 1 × 640×480 | 27.6 MB/s | 1000.0 | 568 µs | 26.26 ms |
| 2 × 640×480 | 55.3 MB/s | 1000.0 | 555 µs | 26.81 ms |
| 4 × 640×480 | 110.6 MB/s | 998.5 | 621 µs | 27.11 ms |
| 2 × 1280×720 | **165.9 MB/s** | **975.0** | **1274 µs** | 27.95 ms |

Four VGA streams at 110 MB/s cost essentially nothing. Two 720p streams at
166 MB/s **do** show up: the control rate slips to 975 Hz and jitter p99
doubles. See "if frames start to hurt" below.

### Policy rate, 1 kHz control, 2 cameras

| Policy | loop p50 | loop p99 | **transport** (loop − inference) |
|---|---|---|---|
| 10 Hz / 50 ms infer | 51.36 ms | 52.70 ms | ~1.4 ms |
| 20 Hz / 25 ms infer | 26.66 ms | 27.91 ms | ~1.7 ms |
| 30 Hz / 15 ms infer | 16.41 ms | 17.82 ms | ~1.4 ms |
| **30 Hz / 0 ms infer** | **1.36 ms** | **2.67 ms** | **1.36 ms** |

That last row is the headline number: with inference removed, a full
**state → policy → action → robot round trip costs about 1.4 ms**, p99 2.7 ms,
while 1 kHz control and 55 MB/s of video run alongside. Of that, roughly 0.5 ms
is state staleness and the rest is two Python-side reads plus a write.

**Your inference time will dominate.** At any realistic policy cost the
transport is 3-10% of the loop.

## Pace the publisher

Every number below uses a paced publisher (`--fps N`). An unpaced one
(`--fps 0`) does not measure a higher ceiling - it measures reader starvation:

| 640x480x3, 4 s | frames published | frames read | delivered |
|---|---|---|---|
| `--fps 1000` | 4 000 | 4 000 | 887 MB/s |
| `--fps 0` | 90 024 | **4** | 1 MB/s |

That is the seqlock working as designed. A reader retries whenever the writer
touches the buffer mid-copy, so a writer that never pauses can keep a
large-frame reader retrying almost indefinitely. Real cameras pace themselves;
if yours does not, pace the publisher or give each frame its own channel.

## Frame streaming ceiling

`robot_sim` measures a whole control loop. `frame_bench` isolates just the pixel
path, which is the question when sizing a multi-camera rig:

```bash
# 64 MB channels hold a 4K RGBA frame (33.2 MB)
cmake -S . -B build-big -DCMAKE_BUILD_TYPE=Release -DESHM_MAX_DATA_SIZE=67108864
cmake --build build-big -j$(nproc)

./build-big/examples/11_robot_loop/frame_bench recv --width 3840 --height 2160 --channels 4 &
./build-big/examples/11_robot_loop/frame_bench send --width 3840 --height 2160 --channels 4 --fps 30
```

### C++ → C++, paced publisher, zero drops

| Frame | MB/frame | fps | Delivered | |
|---|---|---|---|---|
| 640×480×3 | 0.92 | 1000 | 873 MB/s | 7.0 Gbps |
| 1920×1080×3 | 6.22 | 60 | 356 MB/s | 2.8 Gbps |
| 1920×1080×3 | 6.22 | 240 | 1385 MB/s | 11.1 Gbps |
| 3840×2160×4 | 33.18 | 30 | 942 MB/s | 7.5 Gbps |
| **3840×2160×4** | **33.18** | **45** | **1425 MB/s** | **11.4 Gbps** |

**Zero-drop ceiling is around 1.4 GB/s (11 Gbps)**, and it degrades gracefully
rather than falling over — 4K at 90 fps moved 2836 MB/s (22.7 Gbps) while still
delivering 99.6% of frames.

### C++ → Python, paced publisher, zero drops

Python is the consumer in a policy rig, so this is the number that usually binds:

| Frame | fps | Delivered | | per read |
|---|---|---|---|---|
| 640×480×3 | 240 | 208 MB/s | 1.7 Gbps | 0.58 ms |
| 1920×1080×3 | 60 | 350 MB/s | 2.8 Gbps | 4.70 ms |
| 1920×1080×3 | 120 | 703 MB/s | 5.6 Gbps | 7.31 ms |
| **3840×2160×4** | **30** | **931 MB/s** | **7.4 Gbps** | **25.7 ms** |

Python sustains ~0.9 GB/s on 4K frames with **no drops** — every read copies the
whole frame into a `bytes` object at roughly 1.3 GB/s, and that copy is included.
At 4K/30 it eats 26 ms of a 33 ms budget, so that is close to Python's practical
limit; 1080p leaves ample headroom.

### The one way to get this badly wrong

**Do not publish flat out.** With the sender in a tight loop and no pacing:

| | sent | read | delivered | dropped |
|---|---|---|---|---|
| VGA, flat out | 57,787 | **19** | 6 MB/s | **100%** |

The writer pushes 17 GB/s of memcpy into the channel and continuously
invalidates the reader's in-progress copy — the sequence lock makes the reader
retry, and it almost never completes one. Publishing *faster* delivered
**3000× less data** than publishing at 1000 fps.

Real cameras are paced, so this does not arise in practice. But a "push frames
as fast as the GPU produces them" loop will deliver essentially nothing. Pace to
the rate the consumer needs; there is no benefit to going faster, and a large
cost.

## What this tells you about sizing

- **Control at 1 kHz is comfortable**, and stays comfortable with several VGA
  camera streams alongside.
- **Budget ~1.5 ms of transport** in a closed-loop latency calculation, plus
  half a control period of state staleness.
- **Jitter p99 is the OS, not ESHM.** ~570 µs here is WSL2's scheduler; the
  transport contribution is microseconds. `--spin` cuts median jitter 4× by not
  sleeping into the deadline, which is what a real high-rate loop does. For
  hard real-time you want an isolated core, `SCHED_FIFO`, and a tickless
  kernel — none of which are ESHM's business.
- **Watch total camera bandwidth**, not camera count. 110 MB/s was free
  alongside a 1 kHz control loop on one thread; 166 MB/s was not. The pixel path
  on its own goes far higher — see the ceiling above — so this limit is the
  shared thread, not the channel.
- **Pace your publisher.** Zero drops up to ~1.4 GB/s when paced; ~100% drops
  when unpaced, at any size.

## If frames start to hurt

In this example one thread writes state *and* memcpys every frame. At 166 MB/s
those memcpys land inside control periods and the rate slips. Three fixes, in
order of effort:

1. **Publish frames from their own thread.** The control loop then only ever
   touches 136-byte writes. Channels are independent, and `eshm_write` needs no
   lock against a reader — just don't have two threads writing the *same*
   channel.
2. **Send only what the policy consumes.** A policy at 20 Hz reading 30 fps
   streams discards a third of them. Publishing at the policy rate cuts
   bandwidth by that much for free.
3. **Downscale before publishing.** Policies rarely want full resolution; a
   224×224 crop is 0.15 MB against 2.76 MB for 720p.

## Reading the code

- [`robot_link.h`](robot_link.h) — the three structs, shared with `policy.py`
  via `struct.Struct`. Timestamps are `CLOCK_MONOTONIC` ns, which Python's
  `time.monotonic_ns()` reads too, so a stamp written by one process can be
  subtracted by the other and the difference is real.
- [`robot_sim.cpp`](robot_sim.cpp) — never blocks on the policy. Action reads
  are `timeout_ms = 0`, so a stalled policy cannot stall the robot.
- [`policy.py`](policy.py) — `try_read` everywhere; a cycle that finds nothing
  new skips rather than waits.
- [`frame_bench.cpp`](frame_bench.cpp) / [`frame_drain.py`](frame_drain.py) —
  the pixel path in isolation, `send`/`recv` modes, `--fps 0` for flat out.

Both sides **prime** their read channels before the other starts publishing, so
the first sample is delivered rather than consumed as the read baseline — see
[10_triggers](../10_triggers/) for why that matters.

A frame read copies the whole frame into Python: `eshm_read_ex` is all-or-
nothing and refuses a buffer smaller than the message, so you cannot peek at
just the header. That copy runs at about 3 GB/s and is included in every number
above.
