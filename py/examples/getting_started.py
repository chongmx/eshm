#!/usr/bin/env python3
"""Publisher/consumer example using the installed python3-eshm package.

    python3 getting_started.py publish [channel]   # master: creates the channel
    python3 getting_started.py consume [channel]   # slave: attaches, acknowledges

Either side pairs with the C++ example in examples/getting_started - the shared
memory does not care which language is on the other end.
"""

import sys
import time
from pathlib import Path

try:
    # Installed python3-eshm (/usr/lib/python3/dist-packages/eshm/)
    from eshm import ESHM, ESHMRole
except ImportError:
    # Running from the source tree: py/ holds the bindings
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from eshm import ESHM, ESHMRole

CHANNEL_DEFAULT = "sensor"
BUFFER = 4096            # ESHM_MAX_DATA_SIZE


def publish(channel: str) -> int:
    """Master role: create the channel and send a reading twice a second."""
    with ESHM(channel, role=ESHMRole.MASTER) as conn:
        print(f"publisher: channel '{channel}' is live (Ctrl-C to stop)")
        i = 0
        while True:
            i += 1
            conn.write(f"reading {i} temperature={20.0 + (i % 10) * 0.5:.1f}".encode() + b"\0")
            print(f"-> reading {i}", flush=True)

            # try_read never blocks; None means nobody answered yet.
            reply = conn.try_read(buffer_size=BUFFER)
            if reply:
                print(f"   <- {reply.rstrip(chr(0).encode()).decode()}", flush=True)

            time.sleep(0.4)


def consume(channel: str) -> int:
    """Slave role: attach to an existing channel and acknowledge each reading.

    ESHM(role=SLAVE) raises if the segment does not exist yet, so retry until
    the publisher shows up.
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
            print(f"<- {data.rstrip(chr(0).encode()).decode()}", flush=True)
            conn.write(f"ack {count}".encode() + b"\0")


def main(argv: list[str]) -> int:
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
