#!/usr/bin/env python3
"""Python side of example 01 - the core read/write API.

    python3 peer.py publish [channel]   # master: creates the channel
    python3 peer.py consume [channel]   # slave: attaches, acknowledges

Either side pairs with the C++ programs in this directory, so all four
combinations work:

    ./build/hello_publisher demo   +   python3 peer.py consume demo
    python3 peer.py publish demo   +   ./build/hello_consumer demo
    ./build/hello_publisher demo   +   ./build/hello_consumer demo
    python3 peer.py publish demo   +   python3 peer.py consume demo

Demonstrates: ESHM(role=MASTER/SLAVE), write, read(timeout_ms), try_read,
the context manager, and the NUL-termination convention C++ peers expect.
"""

import sys
import time
from pathlib import Path

try:
    # Installed python3-eshm (/usr/lib/python3/dist-packages/eshm/)
    from eshm import ESHM, ESHMRole
except ImportError:
    # Running from the source tree: py/ holds the bindings
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "py"))
    from eshm import ESHM, ESHMRole

CHANNEL_DEFAULT = "sensor"
BUFFER = 4096            # ESHM_MAX_DATA_SIZE
NUL = b"\x00"


def publish(channel: str) -> int:
    """Master role: create the channel and send a reading twice a second."""
    with ESHM(channel, role=ESHMRole.MASTER) as conn:
        print(f"publisher: channel '{channel}' is live (Ctrl-C to stop)")
        i = 0
        while True:
            i += 1
            # C++ peers read these as C strings, so terminate with NUL.
            reading = f"reading {i} temperature={20.0 + (i % 10) * 0.5:.1f}"
            conn.write(reading.encode() + NUL)
            print(f"-> {reading}", flush=True)

            # try_read never blocks; None means nobody answered yet.
            reply = conn.try_read(buffer_size=BUFFER)
            if reply:
                print("   <- " + reply.rstrip(NUL).decode(), flush=True)

            time.sleep(0.4)


def consume(channel: str) -> int:
    """Slave role: attach to an existing channel and acknowledge each reading.

    ESHM(role=SLAVE) raises if the segment does not exist yet, so retry until
    the publisher shows up - exactly what consumer.cpp does.
    """
    conn = None
    for _ in range(50):                      # ~5 seconds
        try:
            conn = ESHM(channel, role=ESHMRole.SLAVE,
                        auto_cleanup=False,          # the master owns the segment
                        max_reconnect_attempts=0,    # reconnect forever
                        reconnect_wait_ms=0)
            break
        except RuntimeError:
            time.sleep(0.1)

    if conn is None:
        print(f"consumer: no channel '{channel}' - is the publisher running?", file=sys.stderr)
        return 1

    print(f"consumer: attached to '{channel}' (Ctrl-C to stop)")
    count = 0
    with conn:
        while True:
            try:
                data = conn.read(buffer_size=BUFFER, timeout_ms=200)
            except TimeoutError:
                continue                      # nothing new this round

            count += 1
            print("<- " + data.rstrip(NUL).decode(), flush=True)
            conn.write(f"ack {count}".encode() + NUL)


def main(argv):
    if len(argv) < 2 or argv[1] not in ("publish", "consume"):
        print(__doc__)
        return 2

    channel = argv[2] if len(argv) > 2 else CHANNEL_DEFAULT
    try:
        return publish(channel) if argv[1] == "publish" else consume(channel)
    except KeyboardInterrupt:
        print("\nstopped")
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
