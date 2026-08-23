# 12 - GPU VRAM sharing: C++ writes, Python reads, zero copy

A C++ producer allocates a block of NVIDIA VRAM and publishes it by name; a
Python consumer maps the *same physical VRAM* into its own process and reads
it as a `cupy.ndarray` - no host round trip, no `cudaMemcpy` on the read
side. Requires an NVIDIA GPU, a CUDA toolkit ESHM was built against
(`include/eshm_cuda.h`), and `cupy` in the Python environment.

```
producer.cpp                          peer.py
  eshm_cuda_create("gpu_frame")         EshmCudaBuffer.attach("gpu_frame")
  cudaMemcpy(devptr, host, size)        buf.as_cupy(shape, dtype)   <- zero copy
  cudaDeviceSynchronize()
  eshm_rpc_call("frame_ready")   --->   @rpc.on_call("frame_ready")
```

## Why this needs two mechanisms, not one

`eshm_cuda` only answers "where is the memory" (a `CUdeviceptr`, mapped into
both processes). It does not answer "is it safe to read yet" - there is no
cross-process CUDA stream ordering here, only the mapping. That question is
answered the same way every other ESHM example answers it: an
[`eshm_rpc`](../10_triggers/) trigger fired **after** the producer's
`cudaDeviceSynchronize()`, over an ordinary host-memory control channel.
GPU buffer for the payload, host channel for "go look" - same split as every
other example, just with the payload moved into VRAM.

## Build and run

Needs `ESHM_ENABLE_CUDA` (default `AUTO`, on if a CUDA toolkit is found):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)
```

```bash
# Terminal 1 - start the consumer first; it retries the attach internally
python3 peer.py consume gpu_frame

# Terminal 2
./build/examples/12_gpu_shared_tensor/gpu_tensor_producer gpu_frame 8
```

```
producer: 256 float32s (2097152 bytes) live in VRAM on device 0, generation 1
producer: buffer 'gpu_frame', triggers on 'gpu_frame_ctl'
-> round 0: wrote [0 .. 255] into VRAM, firing 'frame_ready'
...
```

```
consumer: mapped 2097152 bytes on device 0 (generation 1); numpy=2.5.2 cupy=14.2.0
<- frame 1: mean=127.5 first=0 last=255
...
<- producer is shutting down after 7 frame(s)
```

## Benchmark: how fast can video actually stream through VRAM?

`gpu_frame_bench.cpp` (C++ producer, also a C++ `recv` mode for a ceiling
number) and `gpu_frame_drain.py` (Python consumer) stream real camera-shaped
frames - a header, pixel bytes, a footer - and measure delivered rate, torn
rate, and per-frame latency. `./run_gpu_bench.sh [build_dir] [seconds]`
sweeps resolutions and pacing and prints one table; the numbers below are
from one manual run on the machine this feature was built on (WSL2, RTX 5060
Laptop, driver 596.36, CUDA 13.2) - run it on your own hardware before
sizing anything around these.

| Case | Send rate | Delivered | Torn | Age p50 / p99 |
|---|---|---|---|---|
| C++→C++, VGA, flat out | 4213-7470 fps | ~89-90% | 0.7% | 0.42 / 0.73 ms |
| C++→C++, 1080p, flat out | 936 fps | ~57% | **25.0%** | 2.16 / 3.32 ms |
| C++→C++, 1080p, 30 fps (paced) | 30 fps | 100% | 0% | 2.31 / 8.37 ms |
| C++→Python, VGA, flat out | 6299-7470 fps | 34-38% | 0.4-14.1%* | 0.26 / 0.45 ms |
| **C++→Python, 1080p, 30 fps (paced)** | 30 fps | **100%** | **0.4%** (1 frame) | **1.96 / 8.42 ms** |

\* See "the fix that mattered" below - this number moved a lot during
development and the methodology is the interesting part.

**Read this as two different questions.** "Flat out" (`--fps 0`, no pacing)
answers *what's the ceiling* - useful for capacity planning, not a mode any
real camera runs in. "Paced" at a real frame rate (30 fps here) answers *is
this safe for actual use*, and the answer at 1080p/30fps is unambiguously
yes: every frame delivered, one torn frame out of 235, sub-2ms p50 latency,
with only 48 bytes (a header + footer) crossing the host channel per frame
regardless of resolution - a 1080p frame is 6.2 MB and none of it touches
host RAM on the read side.

**Torn rate is a load phenomenon, not a baseline risk.** It jumps from
under 1% (VGA) to 25% (1080p) under flat-out saturation because the consumer's
whole-frame `cudaMemcpy` D2H (1.17 ms p50 at 1080p) takes long enough for the
producer's *next* write to land mid-read - and drops back to 0% the moment
the producer is paced at a realistic rate instead of writing as fast as
possible. If your real workload can genuinely saturate the transport (not
"processes video," but "writes faster than the consumer can drain
continuously"), see [the sharp edge](#the-sharp-edge-torn-reads) below for
the double-buffering fix - this benchmark deliberately does *not* apply it,
so the torn-rate column stays a true measurement of the unsynchronized case.

**The fix that mattered while measuring the Python numbers:** the first
version of `gpu_frame_drain.py` read the header and the footer as two
separate `cupy` `.get()` calls. That is itself a race - two independent
Python-level GPU reads with real wall-clock time between them - and it
showed a 63.8% torn rate at flat-out VGA that had nothing to do with
`eshm_cuda` and everything to do with benchmark methodology. Combining them
into one `cp.concatenate(...).get()` (one dispatch, one transfer) dropped it
to 14.1% at the same flat-out load - still higher than C++'s single
contiguous `cudaMemcpy`, which is the honest remaining gap between the two
languages' dispatch overhead, not a flaw in the transport. Paced load erases
the difference entirely (0.4% either way). Left in on purpose: a benchmark
that silently measures its own overhead is worse than no benchmark.

## The part that isn't obvious: numpy/cupy version independence

C++ never formats anything for a specific numpy or cupy version - it writes
raw `float32`s. `EshmCudaBuffer.cuda_array_interface()` describes them with
`__cuda_array_interface__`, which is a **plain Python dict** (an int pointer,
a shape tuple, a dtype string), not a numpy/cupy C struct. `as_cupy()` hands
that to `cupy.asarray()` and gets a real zero-copy `cupy.ndarray` back.
Because the interop surface is protocol-level rather than binary, this script
runs unmodified regardless of which numpy/cupy release is installed -
verified by running it unchanged in two conda environments on opposite sides
of the numpy 1.x/2.x C-ABI break (numpy 1.26.4 + cupy 13.6.0, and numpy 2.5.2
+ cupy 14.2.0), both reading the same C++-written VRAM correctly.

## The sharp edge: torn reads

`eshm_cuda` maps memory; it does not lock it. While building this example,
`peer.py` originally read `cp.mean(arr)`, `arr[0]`, and `arr[-1]` as three
separate calls, and once genuinely printed:

```
<- frame 1: mean=1127.5 first=2000 last=2255
```

`mean=1127.5` is round 1's data; `first=2000 last=2255` is round 2's - three
GPU reads, no synchronization between them, so the producer's next
`cudaMemcpy` landed in the middle. The fix here is `cp.asnumpy(arr)` **once**
per trigger and deriving every printed value from that single snapshot
(`frame_ready` in [peer.py](peer.py)) - it stops the three numbers from
disagreeing with each other, though it does not by itself guarantee the
snapshot is a *complete* round if a producer overlaps its next write with a
consumer's read closely enough.

For production use where read/write can genuinely overlap under load,
double-buffer: two VRAM regions (`eshm_cuda_create` twice, or a `size * 2`
buffer with two offsets), the producer alternates which half it writes and
announces *which half* in the trigger payload (piggyback it on the existing
control channel, same as `Reading` in [example 10](../10_triggers/)), and
never touches the half a consumer might still be reading. `eshm_cuda`'s
`generation` counter is reserved for the related case of a producer
reallocating its buffer entirely - not yet wired up to trigger a re-attach,
noted as follow-up work.

## GPU VRAM sharing, mechanically

`include/eshm_cuda.h` uses the CUDA driver's VMM API
(`cuMemCreate`/`cuMemExportToShareableHandle` with a POSIX file descriptor),
not the older `cudaIpcGetMemHandle`. That older API is unreliable under
WSL2; the VMM/fd path is the one documented to work there, and this example
was built and verified against a live producer/consumer pair on WSL2. The fd
itself is handed to attachers over an abstract-namespace `AF_UNIX` socket
(`SCM_RIGHTS`) - a raw fd number means nothing across processes, and POSIX
shared memory cannot carry a live one, so this is a second, small IPC
primitive alongside ESHM's usual channels, scoped to the one job of handing
over that fd.
