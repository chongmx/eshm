# ESHM v1.2.0 Release Notes

## Overview

v1.2.0 adds **GPU VRAM sharing**: one process allocates a block of NVIDIA
device memory and publishes it by name; any number of others map the *same
physical VRAM* into their own address space, zero copy. A C++ producer can
write a tensor with `cudaMemcpy` and a Python consumer can read it back as a
`cupy.ndarray` without either side's data ever touching host RAM.

It ships as a separate optional library, `libeshm_cuda.so`, built only when
a CUDA toolkit is found at configure time. Machines without a GPU build the
rest of ESHM exactly as before, and - as of this release - the built library
itself installs and loads on a machine with no GPU either: it never links
`libcuda.so`, so there is nothing for the dynamic linker to reject at
process-start time even without a driver present.

## Upgrading from 1.1.0

| | Changed? | What it means |
|---|---|---|
| **C ABI** | No | New library and symbols only; `SOVERSION` stays 1 on every existing library. Anything that merely links against ESHM keeps working without recompilation. |
| **Shared-memory protocol** | No | `ESHM_VERSION` stays 3. `eshm_cuda`'s own wire format (the fd handoff) is versioned independently and has never shipped before now. |
| **Default behaviour** | No | `ESHM_ENABLE_CUDA` defaults to `AUTO`; nothing changes for a build with no CUDA toolkit available. |
| **Packaging** | Additive | `libeshm_cuda.so` + `eshm_cuda.h` ride in the existing `libeshm1`/`libeshm-dev` packages - no new `.deb` to install, and `libeshm1`'s `Depends:` line is unchanged. |

## GPU VRAM sharing

```c
#include <eshm_cuda.h>

EshmCudaConfig config = eshm_cuda_default_config("frame", 640 * 480 * 4);
EshmCudaBuffer* buf = eshm_cuda_create(&config);   // allocates VRAM, publishes it

void* devptr; size_t size;
eshm_cuda_get_ptr(buf, &devptr, &size);
cudaMemcpy(devptr, host_frame, size, cudaMemcpyHostToDevice);
cudaDeviceSynchronize();          // eshm_cuda maps memory, not stream order -
                                   // sync before signalling "ready" yourself
```

```python
from eshm.eshm_cuda import EshmCudaBuffer

buf = EshmCudaBuffer.attach("frame")
arr = buf.as_cupy(shape=(480, 640, 4), dtype="uint8")   # zero-copy cupy.ndarray
```

### Mechanism

The CUDA driver's VMM API - `cuMemCreate` + `cuMemExportToShareableHandle`
with a POSIX file descriptor - not the legacy `cudaIpcGetMemHandle`, which is
unreliable under WSL2. The fd is handed to attachers over an
abstract-namespace `AF_UNIX` socket (`SCM_RIGHTS`): a raw fd number is
meaningless across processes, and POSIX shared memory cannot carry a live
one, so this is a second, small IPC primitive alongside ESHM's usual
channels, scoped to that one job.

Readiness signalling is deliberately *not* built in - `eshm_cuda` answers
"where is the memory", not "is it safe to read yet". Use an
[`eshm_rpc`](examples/10_triggers/) trigger over an ordinary host channel
after your stream has synced, same pattern as everything else in this
library.

### Verified, not asserted

- **Real cross-process sharing**: two independently-launched processes (not
  fork children), separate CUDA contexts, one writes a byte pattern into
  VRAM, the other maps the same physical memory and reads it back
  byte-exact - on WSL2, where this specifically was in question.
- **numpy/cupy version independence**: the same Python script, unmodified,
  reading C++-written VRAM correctly in two separate conda environments on
  opposite sides of the numpy 1.x/2.x C-ABI break (numpy 1.26.4 + cupy
  13.6.0, and numpy 2.5.2 + cupy 14.2.0). `__cuda_array_interface__` is a
  plain-Python-value protocol (an int pointer, a shape tuple, a dtype
  string), not a numpy/cupy ABI - that is what makes this work rather than a
  coincidence.
- **No CUDA-version or GPU-architecture lock-in**: `eshm_cuda.cpp` contains
  no device code at all - every call is host-side driver API. Checked
  against the CUDA 13.2 headers this was built with: of the sixteen driver
  functions it calls, only one (`cuDevicePrimaryCtxRelease`) has a versioned
  ABI symbol (`_v2`), stable since that function's introduction. One build
  runs against any driver new enough for VMM POSIX-fd sharing (R470+).

### Deployment: dlopen, not link-time linking

`libeshm_cuda.so` resolves `libcuda.so.1` with `dlopen()` on first use
instead of linking it at build time. Confirmed via `readelf -d`: zero
NVIDIA-related entries in its `NEEDED` list. Practical effect:

- The library installs, links, and loads on a machine with **no GPU at
  all** - only actually calling `eshm_cuda_create()`/`attach()` there fails,
  with a clear error (`eshm_cuda_get_last_error()`), instead of the whole
  process refusing to start over a missing `libcuda.so.1`.
- `scripts/export_deb.sh` needed no changes to fold `libeshm_cuda.so` into
  the existing `libeshm1`/`libeshm-dev` packages - `dpkg-shlibdeps` finds
  nothing NVIDIA-related to depend on. Added `--cuda AUTO|ON|OFF` for builds
  that should not compile the GPU code in at all.

## Benchmark: video-frame streaming

[`examples/12_gpu_shared_tensor/`](examples/12_gpu_shared_tensor/) ships
`gpu_frame_bench` (C++ producer, and a C++ `recv` mode for a ceiling number)
and `gpu_frame_drain.py` (Python consumer), streaming camera-shaped frames -
header, pixels, footer - and measuring delivered rate, a torn-read check,
and latency. Measured on one machine (WSL2, RTX 5060 Laptop) - run
`./run_gpu_bench.sh` on your own hardware before sizing anything around
these:

| Case | Delivered | Torn | Age p50 / p99 |
|---|---|---|---|
| C++→C++, 1080p, flat out (unpaced) | ~57% | **25.0%** | 2.16 / 3.32 ms |
| C++→C++, 1080p, 30 fps (paced) | 100% | 0% | 2.31 / 8.37 ms |
| **C++→Python, 1080p, 30 fps (paced)** | **100%** | **0.4%** | **1.96 / 8.42 ms** |

At a realistic camera rate, every frame arrives, sub-2ms p50 latency, with
only 48 bytes crossing the host channel per frame regardless of resolution -
a 1080p frame is 6.2 MB and none of it touches host RAM on the read side.

**Torn rate is a load phenomenon, not a baseline risk**: it only appears
under flat-out saturation (no real camera runs unpaced) and the mechanism is
exactly what the "Mechanism" section above implies - `eshm_cuda` maps
memory, it does not lock it. The example's README documents this in more
detail, including a benchmark-methodology bug caught along the way (reading
a header and a footer as two separate Python-level GPU reads is itself a
race, inflating an early torn-rate measurement to 63.8% before the fix).

## Examples

12 is now the numbered example count.

| # | Directory | Covers |
|---|---|---|
| 12 | `gpu_shared_tensor` | GPU VRAM sharing, zero-copy tensor exchange, frame-streaming benchmark |

(01-11 unchanged from 1.1.0 - see [RELEASE_NOTES_v1.1.md](RELEASE_NOTES_v1.1.md).)

## Verification

- 9/9 ctest tests pass, unchanged, after every change in this release
  (including the `dlopen` rewrite and the `GNUInstallDirs` ordering fix)
- Real C++↔C++ and C++↔Python VRAM sharing re-verified after the `dlopen`
  rewrite specifically, not just re-compiled
- `.deb` packages built and installed on the development machine;
  `scripts/check_install.sh` extended to report on the GPU module and
  confirms it end to end, including the installed (not build-tree)
  `libeshm_cuda.so.1`

## Requirements

Unchanged for the core library.

- Linux, CMake 3.10+, C++17 (GCC 7+/Clang 5+), pthread and rt
- Optional, for GPU VRAM sharing: an NVIDIA GPU, driver R470+, and a CUDA
  toolkit at build time only (`find_package(CUDAToolkit)`, CMake 3.17+) -
  nothing extra required on the machine that eventually runs it
