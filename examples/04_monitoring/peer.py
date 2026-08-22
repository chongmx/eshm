#!/usr/bin/env python3
"""Python side of example 04 - monitoring a live channel.

    python3 peer.py generate [channel]   # master: creates the channel, makes traffic
    python3 peer.py watch    [channel]   # slave:  attaches and reports statistics

Pairs with ./channel_monitor in either direction:

    ./build/channel_monitor generate demo   +   python3 peer.py watch demo
    python3 peer.py generate demo           +   ./build/channel_monitor watch demo

Demonstrates: get_stats() and every field it returns, get_role(),
is_remote_alive() and what it actually means.
"""

import sys
import time
from pathlib import Path

try:
    from eshm import ESHM, ESHMRole
except ImportError:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "py"))
    from eshm import ESHM, ESHMRole

CHANNEL_DEFAULT = "monitored"
BUFFER = 4096

HEADER = (f"{'#':<4} {'role':<6} | {'m.beat':<8} {'s.beat':<8} | "
          f"{'m.pid':<6} {'s.pid':<6} | {'m2s w/r':<9} {'s2m w/r':<9} | peer")


def print_stats(conn, sample: int) -> None:
    stats = conn.get_stats()

    # is_remote_alive() reports whether the peer has been detected as STALE -
    # it is True before anyone has ever attached. The *_alive flags below are
    # set on attach and cleared on destroy, so they answer "is a peer there".
    remote_alive = conn.is_remote_alive()

    if sample % 10 == 0:
        print("\n" + HEADER)
        print("-" * 92)

    if stats["slave_alive"]:
        peer = "slave"
    elif remote_alive:
        peer = "(not stale)"
    else:
        peer = "gone"
    if stats["master_alive"]:
        peer = "master " + peer

    print(f"{sample:<4} {conn.get_role().name:<6} | "
          f"{stats['master_heartbeat']:<8} {stats['slave_heartbeat']:<8} | "
          f"{stats['master_pid']:<6} {stats['slave_pid']:<6} | "
          f"{stats['m2s_write_count']:>4}/{stats['m2s_read_count']:<4} "
          f"{stats['s2m_write_count']:>4}/{stats['s2m_read_count']:<4} | {peer}")

    # The deltas are the useful liveness signal: a heartbeat that stops
    # advancing between samples is a peer that has stopped, whatever the alive
    # flags still say. get_stats() resets them each call.
    print(f"     heartbeat delta since last sample: "
          f"master +{stats['master_heartbeat_delta']}, "
          f"slave +{stats['slave_heartbeat_delta']} "
          f"(stale threshold {stats['stale_threshold']})", flush=True)


def watch(channel: str) -> int:
    conn = None
    for _ in range(50):
        try:
            conn = ESHM(channel, role=ESHMRole.SLAVE,
                        auto_cleanup=False,
                        max_reconnect_attempts=0,
                        reconnect_wait_ms=0)
            break
        except RuntimeError:
            time.sleep(0.1)

    if conn is None:
        print(f"monitor: no channel '{channel}' - is a generator running?", file=sys.stderr)
        return 1

    print(f"monitor: watching '{channel}' (Ctrl-C to stop)")
    sample = 0
    with conn:
        while True:
            # Drain the channel so the read counters move too.
            while conn.try_read(buffer_size=BUFFER) is not None:
                pass

            print_stats(conn, sample)
            sample += 1
            time.sleep(0.5)


def generate(channel: str) -> int:
    with ESHM(channel, role=ESHMRole.MASTER) as conn:
        print(f"generator: channel '{channel}' is live (Ctrl-C to stop)")
        i = 0
        while True:
            conn.write(f"tick {i}".encode() + b"\x00")
            if i % 20 == 0:
                print(f"generator: {i + 1} message(s) written", flush=True)
            i += 1
            time.sleep(0.05)


def main(argv):
    if len(argv) < 2 or argv[1] not in ("watch", "generate"):
        print(__doc__)
        return 2

    channel = argv[2] if len(argv) > 2 else CHANNEL_DEFAULT
    try:
        return watch(channel) if argv[1] == "watch" else generate(channel)
    except KeyboardInterrupt:
        print("\nstopped")
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
