#!/usr/bin/env python3
"""The Python half of the robot loop: read the newest state and frames, "infer",
publish an action chunk.

    python3 policy.py [--hz 20] [--infer-ms 25] [--cameras 2] [--seconds 5]
                      [--channel robot]

Pairs with ./robot_sim.

The point of the shape: the policy runs far slower than the control loop and
does not try to keep up. It reads whatever is newest at the instant it starts,
infers, and publishes a chunk of future actions. State samples it never read
were not lost - they were superseded before it could have acted on them, which
is exactly what a latest-value channel gives you for free.

What gets measured here is *staleness*: how old the state and frames were at
the moment the policy consumed them. That, plus the closed-loop figure the
robot side prints, is what decides whether a policy can drive a real arm.
"""

import argparse
import statistics
import struct
import sys
import time
from pathlib import Path

try:
    from eshm import ESHM, ESHMRole
except ImportError:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "py"))
    from eshm import ESHM, ESHMRole

JOINTS = 7
HORIZON = 16
FRAME_MAGIC = 0x524643

# struct RobotState { u64 seq, stamp_ns; double pos[7], vel[7]; u64 flags; }
STATE = struct.Struct(f"<2Q{2 * JOINTS}dQ")
# struct ActionChunk { u64 seq, stamp_ns, state_seq, state_stamp_ns;
#                      u32 horizon, pad; float actions[16][7]; }
ACTION = struct.Struct(f"<4Q2I{HORIZON * JOINTS}f")
# struct FrameHeader { u32 magic, cam_id; u64 seq, stamp_ns;
#                      u32 width, height, channels, bytes; }
FRAME = struct.Struct("<2I2Q4I")


def pct(values, p):
    if not values:
        return 0.0
    s = sorted(values)
    return s[min(int(p * (len(s) - 1)), len(s) - 1)]


def attach(name, **kwargs):
    for _ in range(150):
        try:
            return ESHM(name, role=ESHMRole.SLAVE, auto_cleanup=False,
                        max_reconnect_attempts=0, reconnect_wait_ms=0, **kwargs)
        except RuntimeError:
            time.sleep(0.1)
    return None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--hz", type=float, default=20.0, help="policy rate (default 20)")
    ap.add_argument("--infer-ms", type=float, default=25.0,
                    help="simulated inference cost in ms (default 25)")
    ap.add_argument("--cameras", type=int, default=2)
    ap.add_argument("--width", type=int, default=640)
    ap.add_argument("--height", type=int, default=480)
    ap.add_argument("--channels", type=int, default=3)
    ap.add_argument("--seconds", type=float, default=5.0)
    ap.add_argument("--channel", default="robot")
    args = ap.parse_args(argv)

    ctrl = attach(args.channel)
    if ctrl is None:
        print(f"policy: no channel '{args.channel}' - is robot_sim running?",
              file=sys.stderr)
        return 1

    cams = []
    for c in range(args.cameras):
        h = attach(f"{args.channel}_cam{c}")
        if h is None:
            print(f"policy: no camera channel {c}", file=sys.stderr)
            return 1
        cams.append(h)

    # A read is all or nothing: eshm_read_ex refuses a buffer smaller than the
    # message, so wanting only the header is not an option - the whole frame is
    # copied into Python. That is what a real policy does anyway (the pixels are
    # the point), and it means this benchmark includes that copy.
    frame_bytes = args.width * args.height * args.channels
    frame_buf = FRAME.size + frame_bytes

    # Prime every channel before the robot starts publishing, so the first
    # sample on each is delivered rather than consumed as the read baseline.
    ctrl.try_read(buffer_size=STATE.size)
    for h in cams:
        h.try_read(buffer_size=frame_buf)

    print(f"policy: {args.hz:g} Hz, {args.infer_ms:g} ms simulated inference, "
          f"{len(cams)} camera(s) {args.width}x{args.height}x{args.channels} "
          f"({frame_bytes / 1e6:.2f} MB/frame)", flush=True)

    period = 1.0 / args.hz
    t0 = time.monotonic()
    deadline = t0 + args.seconds
    next_tick = t0

    state_age, frame_age, frame_copy_ms = [], [], []
    frame_bytes_read = [0]
    cycles = misses = 0
    frames_seen = [0] * len(cams)
    last_state_seq = 0
    state_gaps = 0
    seq = 0

    while time.monotonic() < deadline:
        now = time.monotonic()
        if now < next_tick:
            time.sleep(min(next_tick - now, 0.002))
            continue
        next_tick += period
        if next_tick < now:
            next_tick = now + period
        cycles += 1

        # --- read the newest state ---------------------------------------
        raw = ctrl.try_read(buffer_size=STATE.size)
        if not raw or len(raw) < STATE.size:
            misses += 1
            continue
        fields = STATE.unpack_from(raw)
        s_seq, s_stamp = fields[0], fields[1]
        joints = fields[2:2 + JOINTS]

        read_ns = time.monotonic_ns()
        state_age.append((read_ns - s_stamp) / 1e6)      # ms
        if last_state_seq and s_seq > last_state_seq + 1:
            state_gaps += s_seq - last_state_seq - 1
        last_state_seq = s_seq

        # --- read the newest frame from each camera -----------------------
        for i, h in enumerate(cams):
            t_read = time.monotonic_ns()
            raw_f = h.try_read(buffer_size=frame_buf)
            if raw_f and len(raw_f) >= FRAME.size:
                magic, _cam, _fseq, f_stamp = FRAME.unpack_from(raw_f)[:4]
                if magic == FRAME_MAGIC:
                    frames_seen[i] += 1
                    done = time.monotonic_ns()
                    frame_age.append((done - f_stamp) / 1e6)
                    frame_copy_ms.append((done - t_read) / 1e6)
                    frame_bytes_read[0] += len(raw_f)

        # --- "inference" ---------------------------------------------------
        # A real policy burns this in torch; we just wait, so the measured
        # closed-loop figure reflects transport plus a realistic think time.
        if args.infer_ms > 0:
            time.sleep(args.infer_ms / 1000.0)

        # --- publish the action chunk -------------------------------------
        seq += 1
        actions = []
        for step in range(HORIZON):
            for j in range(JOINTS):
                actions.append(float(joints[j]) * 0.5 + step * 0.001)
        ctrl.write(ACTION.pack(seq, time.monotonic_ns(), s_seq, s_stamp,
                               HORIZON, 0, *actions))

    elapsed = time.monotonic() - t0

    print()
    print("=== policy side ===")
    print(f"  cycles        {cycles} in {elapsed:.2f} s  ({cycles / elapsed:.1f} Hz)")
    if misses:
        print(f"  no-state      {misses} cycle(s) found nothing new")
    if state_age:
        print(f"  STATE AGE     p50 {pct(state_age, 0.50):.2f} ms   "
              f"p99 {pct(state_age, 0.99):.2f} ms   max {max(state_age):.2f} ms")
        print(f"                (how stale the state was when the policy read it)")
    print(f"  state skipped {state_gaps} sample(s) superseded between reads "
          f"- expected, and not loss")
    for i, n in enumerate(frames_seen):
        print(f"  camera {i}      {n} frame(s) read ({n / elapsed:.1f} fps)")
    if frame_age:
        print(f"  FRAME AGE     p50 {pct(frame_age, 0.50):.2f} ms   "
              f"p99 {pct(frame_age, 0.99):.2f} ms")
        print(f"  frame copy    p50 {pct(frame_copy_ms, 0.50):.2f} ms   "
              f"p99 {pct(frame_copy_ms, 0.99):.2f} ms  "
              f"({frame_bytes_read[0] / 1e6 / elapsed:.0f} MB/s into Python)")

    ctrl.close()
    for h in cams:
        h.close()
    return 0 if cycles else 1


if __name__ == "__main__":
    sys.exit(main())
