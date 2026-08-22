#!/usr/bin/env python3
"""Python side of example 09 - talking to an integrated C++ application.

    python3 peer.py slave  [channel]    # attach to integration_master
    python3 peer.py master [channel]    # be the master for integration_slave

The C++ programs here hardcode the channel name "demo_shm", so the default
matches. Run either pairing:

    ./build/integration_master   +   python3 peer.py slave
    python3 peer.py master       +   ./build/integration_slave

The point: a project that consumes ESHM through CMake gets a Python-reachable
channel for free. Install python3-eshm on the same machine and scripts, tests
and tooling can attach to your application without it exposing anything else.
"""

import sys
import time
from pathlib import Path

try:
    from eshm import ESHM, ESHMRole
except ImportError:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "py"))
    from eshm import ESHM, ESHMRole

CHANNEL_DEFAULT = "demo_shm"     # matches master.cpp / slave.cpp
NUL = b"\x00"


def slave(channel: str) -> int:
    conn = None
    for _ in range(100):
        try:
            conn = ESHM(channel, role=ESHMRole.SLAVE, auto_cleanup=False,
                        max_reconnect_attempts=0, reconnect_wait_ms=0)
            break
        except RuntimeError:
            time.sleep(0.1)
    if conn is None:
        print(f"peer: no channel '{channel}' - is integration_master running?",
              file=sys.stderr)
        return 1

    print(f"peer: attached to '{channel}' as SLAVE (Ctrl-C to stop)")
    count = 0
    with conn:
        while True:
            try:
                data = conn.read(buffer_size=4096, timeout_ms=500)
            except (TimeoutError, RuntimeError):
                continue
            count += 1
            print("<- " + data.rstrip(NUL).decode(errors="replace"), flush=True)
            conn.write(f"Reply #{count} from Python".encode() + NUL)


def master(channel: str) -> int:
    with ESHM(channel, role=ESHMRole.MASTER) as conn:
        print(f"peer: channel '{channel}' is live as MASTER (Ctrl-C to stop)")
        i = 0
        while True:
            conn.write(f"Message #{i} from Python master".encode() + NUL)
            reply = conn.try_read(buffer_size=4096)
            if reply:
                print("   <- " + reply.rstrip(NUL).decode(errors="replace"), flush=True)
            i += 1
            time.sleep(0.5)


def main(argv):
    if len(argv) < 2 or argv[1] not in ("slave", "master"):
        print(__doc__)
        return 2

    channel = argv[2] if len(argv) > 2 else CHANNEL_DEFAULT
    try:
        return slave(channel) if argv[1] == "slave" else master(channel)
    except KeyboardInterrupt:
        print("\nstopped")
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
