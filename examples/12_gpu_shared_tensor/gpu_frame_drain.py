#!/usr/bin/env python3
"""GPU VRAM frame-streaming throughput into Python.

    python3 gpu_frame_drain.py [--width W] [--height H] [--channels C]
                               [--seconds S] [--name NAME]

Pairs with `./gpu_frame_bench send`. Where `gpu_frame_bench recv` measures the
C++ -> C++ ceiling, this measures what Python can actually pull out - the
number that matters when Python is the one consuming frames (a policy, a
model, anything downstream).

Every trigger dispatch reads only the header (40 bytes) and an 8-byte footer
off the VRAM buffer with cupy, not the whole frame - the point of this
transport is that the pixels never have to cross into Python's process at
all unless something (a model, cv2, ...) actually needs to look at them.
header.seq == footer_seq is the torn-read check described in frame_format.h;
eshm_cuda maps memory, it does not lock it, and this is what makes that
visible instead of silently returning a mixed frame.
"""

import argparse
import struct
import sys
import time
from pathlib import Path

try:
    from eshm import ESHMRole
    from eshm.rpc import Rpc
    from eshm.eshm_cuda import EshmCudaBuffer
except ImportError:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "py"))
    from eshm import ESHMRole
    from rpc import Rpc
    from eshm_cuda import EshmCudaBuffer

GPU_FRAME_MAGIC = 0x47465243
# struct GpuFrameHeader { u32 magic, cam_id; u64 seq, stamp_ns; u32 w, h, ch, bytes; }
HEADER = struct.Struct("<2I2Q4I")
FOOTER = struct.Struct("<Q")


def pct(v, p):
    if not v:
        return 0.0
    s = sorted(v)
    return s[min(int(p * (len(s) - 1)), len(s) - 1)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--width", type=int, default=1280)
    ap.add_argument("--height", type=int, default=720)
    ap.add_argument("--channels", type=int, default=3)
    ap.add_argument("--seconds", type=float, default=3.0)
    ap.add_argument("--name", default="gpu_framebench")
    args = ap.parse_args()

    import cupy as cp

    frame_bytes = args.width * args.height * args.channels
    total_bytes = HEADER.size + frame_bytes + FOOTER.size

    buf = EshmCudaBuffer.attach(args.name, timeout_ms=20000)
    arr = buf.as_cupy(dtype="uint8")  # flat byte view, zero-copy, built once
    print(f"drain: attached (device {buf.device}), {frame_bytes / 1e6:.2f} MB/frame, "
          f"cupy={cp.__version__}", flush=True)

    stats = {
        "frames": 0, "torn": 0,
        "first_seq": 0, "last_seq": 0,
        "read_ms": [], "age_ms": [],
    }

    rpc = Rpc(args.name, role=ESHMRole.SLAVE)

    @rpc.on_call("frame_ready")
    def frame_ready():
        # Header and footer are NOT adjacent (the whole frame sits between
        # them), so this is two separate reads either way - but two separate
        # *Python-level* .get() calls each pay their own cupy/CUDA dispatch
        # and sync cost, opening a real gap between them for the producer's
        # next cudaMemcpy to land in. cp.concatenate does the gather on the
        # GPU as one op and .get() copies both pieces down in one transfer,
        # closing nearly all of that gap - this is what made the difference
        # between a ~1% and a ~60% torn rate at flat-out load while building
        # this benchmark. Fetching the header alone (ignoring the footer)
        # would look "faster" but silently hide every torn frame instead.
        r0 = time.monotonic_ns()
        combined = cp.concatenate((arr[:HEADER.size], arr[total_bytes - FOOTER.size:total_bytes])).get()
        r1 = time.monotonic_ns()
        header_bytes = combined[:HEADER.size].tobytes()
        footer_bytes = combined[HEADER.size:].tobytes()

        magic, _cam, seq, stamp_ns = HEADER.unpack_from(header_bytes)[:4]
        if magic != GPU_FRAME_MAGIC:
            return
        (footer_seq,) = FOOTER.unpack(footer_bytes)

        if footer_seq != seq:
            stats["torn"] += 1
            return

        stats["frames"] += 1
        if not stats["first_seq"]:
            stats["first_seq"] = seq
        stats["last_seq"] = seq
        stats["read_ms"].append((r1 - r0) / 1e6)
        if r1 > stamp_ns:
            stats["age_ms"].append((r1 - stamp_ns) / 1e6)

    with rpc:
        t0 = time.monotonic()
        end = t0 + args.seconds + 1.0
        while time.monotonic() < end:
            time.sleep(0.02)

        elapsed = time.monotonic() - t0
        frames = stats["frames"]
        mbs = frames * total_bytes / elapsed / 1e6
        span = (stats["last_seq"] - stats["first_seq"] + 1
                if stats["last_seq"] >= stats["first_seq"] else 0)
        torn = stats["torn"]

        print()
        print("=== drain (Python) ===")
        print(f"  frames read     {frames} of {span} published while listening")
        print(f"  DELIVERED RATE  {mbs:.0f} MB/s  ({mbs * 8 / 1000:.2f} Gbps)")
        if span:
            coalesced = max(span - frames - torn, 0)
            print(f"  coalesced       {100.0 * coalesced / span:.1f}%  "
                  f"({coalesced} superseded before a trigger dispatched)")
        print(f"  torn            {torn}  "
              f"({100.0 * torn / (frames + torn):.2f}% of dispatched, header/footer seq mismatch)"
              if (frames + torn) else "  torn            0")
        print(f"  per read        p50 {pct(stats['read_ms'], 0.50):.3f} ms   "
              f"p99 {pct(stats['read_ms'], 0.99):.3f} ms   (header+footer only, 48 B)")
        if stats["age_ms"]:
            print(f"  frame age       p50 {pct(stats['age_ms'], 0.50):.2f} ms   "
                  f"p99 {pct(stats['age_ms'], 0.99):.2f} ms")
        print(f"  rpc             {rpc.dispatched} dispatched, {rpc.missed} coalesced (peer's count)")

    buf.close()
    return 0 if frames else 1


if __name__ == "__main__":
    sys.exit(main())
