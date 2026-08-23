#!/usr/bin/env python3
"""Frame streaming throughput into Python - the consumer side of a camera rig.

    python3 frame_drain.py [--width W] [--height H] [--channels C]
                           [--seconds S] [--channel NAME]

Pairs with `./frame_bench send`. Where frame_bench recv measures the C++ -> C++
ceiling, this measures what Python can actually pull out, which is the number
that matters when the policy is the one consuming frames.

Every read copies the whole frame into a Python bytes object: eshm_read_ex is
all or nothing and will not hand back a prefix. That copy is included here.
"""

import argparse
import struct
import sys
import time
from pathlib import Path

try:
    from eshm import ESHM, ESHMRole
except ImportError:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "py"))
    from eshm import ESHM, ESHMRole

FRAME_MAGIC = 0x524643
# struct FrameHeader { u32 magic, cam_id; u64 seq, stamp_ns; u32 w, h, ch, bytes; }
FRAME = struct.Struct("<2I2Q4I")


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
    ap.add_argument("--channel", default="framebench")
    args = ap.parse_args()

    frame_bytes = args.width * args.height * args.channels
    buf = FRAME.size + frame_bytes

    conn = None
    for _ in range(200):
        try:
            conn = ESHM(args.channel, role=ESHMRole.SLAVE, auto_cleanup=False,
                        max_reconnect_attempts=0, reconnect_wait_ms=0)
            break
        except RuntimeError:
            time.sleep(0.1)
    if conn is None:
        print(f"drain: no channel '{args.channel}'", file=sys.stderr)
        return 1

    conn.try_read(buffer_size=buf)          # prime the read baseline
    print(f"drain: attached, {frame_bytes / 1e6:.2f} MB/frame", flush=True)

    t0 = time.monotonic()
    end = t0 + args.seconds + 1.0
    frames = 0
    first_seq = last_seq = 0
    read_ms, age_ms = [], []
    seen = False

    with conn:
        while time.monotonic() < end:
            r0 = time.monotonic_ns()
            raw = conn.try_read(buffer_size=buf)
            if not raw or len(raw) < FRAME.size:
                if seen and not conn.is_remote_alive():
                    break
                continue
            r1 = time.monotonic_ns()

            magic, _cam, fseq, stamp = FRAME.unpack_from(raw)[:4]
            if magic != FRAME_MAGIC:
                continue
            seen = True
            frames += 1
            if not first_seq:
                first_seq = fseq
            last_seq = fseq
            read_ms.append((r1 - r0) / 1e6)
            age_ms.append((r1 - stamp) / 1e6)

    elapsed = time.monotonic() - t0
    mbs = frames * (FRAME.size + frame_bytes) / elapsed / 1e6
    span = last_seq - first_seq + 1 if last_seq >= first_seq else 0
    missed = span - frames if span > frames else 0

    print()
    print("=== drain (Python) ===")
    print(f"  frames read     {frames} of {span} published while listening")
    print(f"  DELIVERED RATE  {mbs:.0f} MB/s  ({mbs * 8 / 1000:.2f} Gbps)")
    if span:
        print(f"  dropped         {100.0 * missed / span:.1f}%  ({missed} superseded)")
    print(f"  per read        p50 {pct(read_ms, 0.50):.2f} ms   p99 {pct(read_ms, 0.99):.2f} ms")
    if age_ms:
        print(f"  frame age       p50 {pct(age_ms, 0.50):.2f} ms   p99 {pct(age_ms, 0.99):.2f} ms")
    return 0 if frames else 1


if __name__ == "__main__":
    sys.exit(main())
