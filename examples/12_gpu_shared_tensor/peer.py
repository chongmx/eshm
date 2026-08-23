#!/usr/bin/env python3
"""Python side of example 12 - GPU VRAM sharing.

    python3 peer.py consume [name]

Pairs with ./build/gpu_tensor_producer [name] [rounds].

The producer allocates VRAM once (eshm_cuda_create), writes a float32
"tensor" into it with cudaMemcpy, syncs the stream, then fires the
"frame_ready" trigger over an ordinary ESHM control channel - the same
pattern as example 10, except the payload the trigger announces lives in
VRAM instead of a host channel.

This process never copies the tensor: EshmCudaBuffer.attach() maps the same
physical VRAM the producer wrote to, and as_cupy() wraps it as a cupy.ndarray
with zero copy, using nothing but __cuda_array_interface__ - a plain-value
protocol (an int pointer, a shape tuple, a dtype string), not a numpy/cupy
ABI. That's what makes this script version-independent: it runs unchanged
whichever numpy/cupy release happens to be installed in this environment.
"""

import sys
import time
from pathlib import Path

try:
    from eshm import ESHMRole
    from eshm.rpc import Rpc
    from eshm.eshm_cuda import EshmCudaBuffer, ESHM_CUDA_TIMEOUT_INFINITE
except ImportError:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "py"))
    from eshm import ESHMRole
    from rpc import Rpc
    from eshm_cuda import EshmCudaBuffer, ESHM_CUDA_TIMEOUT_INFINITE

NAME_DEFAULT = "gpu_frame"
N = 256  # must match producer.cpp


def consume(name: str) -> int:
    import cupy as cp
    import numpy as np

    print(f"consumer: waiting for a GPU producer under '{name}'...", flush=True)
    buf = EshmCudaBuffer.attach(name, timeout_ms=ESHM_CUDA_TIMEOUT_INFINITE)
    print(f"consumer: mapped {buf.size} bytes on device {buf.device} "
          f"(generation {buf.generation}); numpy={np.__version__} cupy={cp.__version__}",
          flush=True)

    arr = buf.as_cupy(shape=(N,), dtype="float32")  # zero-copy, built once

    state = {"frames": 0, "running": True}
    rpc = Rpc(name, role=ESHMRole.SLAVE)

    @rpc.on_call("frame_ready")
    def frame_ready():
        # arr always reflects current VRAM contents - no re-attach needed,
        # the trigger only says "go look". But eshm_cuda synchronizes the
        # MAPPING, not the DATA: it has no idea when a write is in flight, so
        # three separate reads off `arr` (a mean, then arr[0], then arr[-1])
        # can each land on a different moment of the producer's next
        # cudaMemcpy and print an internally-inconsistent frame. One
        # snapshot read keeps what we print self-consistent; it does not by
        # itself guarantee the snapshot is a *complete* round (see this
        # example's README for the double-buffering fix a producer/consumer
        # pair should use in production).
        snapshot = cp.asnumpy(arr)
        state["frames"] += 1
        print(f"<- frame {state['frames']}: mean={snapshot.mean():.1f} "
              f"first={snapshot[0]:.0f} last={snapshot[-1]:.0f}", flush=True)

    @rpc.on_event("shutting_down")
    def shutting_down():
        print(f"<- producer is shutting down after {state['frames']} frame(s)", flush=True)
        state["running"] = False

    with rpc:
        while state["running"]:
            time.sleep(0.05)

    buf.close()
    print(f"\nconsumer: {state['frames']} frame(s) seen, {rpc.missed} coalesced away")
    return 0 if state["frames"] else 1


def main(argv):
    if len(argv) < 2 or argv[1] != "consume":
        print(__doc__)
        return 2
    name = argv[2] if len(argv) > 2 else NAME_DEFAULT
    try:
        return consume(name)
    except KeyboardInterrupt:
        print("\nstopped")
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
