#!/usr/bin/env python3
"""Python side of example 10 - named triggers.

    python3 peer.py master [channel] [rounds]   # writes data, fires triggers
    python3 peer.py worker [channel]            # handles them

Pairs with the C++ programs in this directory in both directions:

    ./build/trigger_master demo   +   python3 peer.py worker demo
    python3 peer.py master demo   +   ./build/trigger_worker demo

The pattern:  write the data  ->  fire the trigger  ->  handler reads the data.

A trigger carries a name and nothing else. Python never touches the control
channel's shared memory - a C++ dispatcher thread inside libeshm owns it and
calls up into these handlers through a ctypes callback, so there is exactly one
implementation of the wire format.

The data channel is separate and ordinary: struct.Struct here, a plain struct
in C++, moved with the normal ESHM read/write API.
"""

import struct
import sys
import time
from pathlib import Path

try:
    from eshm import ESHM, ESHMRole
    from eshm.rpc import Rpc
except ImportError:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "py"))
    from eshm import ESHM, ESHMRole
    from rpc import Rpc

CHANNEL_DEFAULT = "demo"

# struct Reading { uint64_t sample; double temperature; char label[32]; }
# 8 + 8 + 32, and the two 8-byte fields are already aligned, so no padding.
READING = struct.Struct("<Qd32s")


def label_of(raw: bytes) -> str:
    return raw.split(b"\x00", 1)[0].decode("utf-8", "replace")


# ------------------------------------------------------------------- master
def master(channel: str, rounds: int) -> int:
    """Own both channels: write values, then trigger the worker."""
    state = {"replies": 0, "ready": False}

    with ESHM(channel, role=ESHMRole.MASTER) as data:
        rpc = Rpc(channel, role=ESHMRole.MASTER)

        @rpc.on_event("worker_ready")
        def worker_ready():
            state["ready"] = True
            print("   [event] worker says it is ready", flush=True)

        @rpc.on_call("done")
        def done():
            state["replies"] += 1
            print(f"   [call ] worker finished a round ({state['replies']} so far)",
                  flush=True)

        with rpc:
            print(f"master: data on '{channel}', triggers on '{channel}_ctl'")
            print("master: waiting for a worker...", flush=True)

            for _ in range(150):
                if state["ready"]:
                    break
                time.sleep(0.1)
            if not state["ready"]:
                print("master: no worker appeared", file=sys.stderr)
                return 1

            for i in range(rounds):
                # Data first, then the trigger. The handler reads whatever is
                # current when it runs, which is why no arguments are needed.
                temperature = 20.0 + (i % 10) * 0.5
                data.write(READING.pack(i, temperature, f"round-{i}".encode()))
                print(f"-> wrote sample {i} ({temperature:.1f} C), calling 'process'",
                      flush=True)

                rpc.call("process")
                time.sleep(0.3)

            # An event, not a call: nobody is required to be listening.
            rpc.emit("shutting_down")
            time.sleep(0.3)

            print(f"\nmaster: {state['replies']} round(s) acknowledged, "
                  f"{rpc.dispatched} trigger(s) dispatched here, "
                  f"{rpc.missed} coalesced away")
            return 0 if state["replies"] else 1


# ------------------------------------------------------------------- worker
def worker(channel: str) -> int:
    """Attach to both channels and let the dispatcher drive."""
    data = None
    for _ in range(100):
        try:
            data = ESHM(channel, role=ESHMRole.SLAVE, auto_cleanup=False,
                        max_reconnect_attempts=0, reconnect_wait_ms=0)
            break
        except RuntimeError:
            time.sleep(0.1)
    if data is None:
        print(f"worker: no data channel '{channel}' - is the master running?",
              file=sys.stderr)
        return 1

    state = {"processed": 0, "running": True}

    with data:
        rpc = Rpc(channel, role=ESHMRole.SLAVE)

        @rpc.on_call("process")
        def process():
            # The trigger said "go look" - so look. Non-blocking: the master
            # wrote the data before firing.
            raw = data.try_read(buffer_size=READING.size)
            if raw and len(raw) >= READING.size:
                sample, temperature, label = READING.unpack_from(raw)
                state["processed"] += 1
                print(f"<- [call ] process: sample {sample}, {temperature:.1f} C, "
                      f'label "{label_of(label)}"', flush=True)
            else:
                # A burst can coalesce - the channel holds one value per
                # direction. Level-triggered handlers tolerate that by design.
                print("<- [call ] process: nothing new on the data channel", flush=True)

            rpc.call("done")        # triggers can go the other way from a handler

        @rpc.on_event("shutting_down")
        def shutting_down():
            print(f"<- [event] master is shutting down after "
                  f"{state['processed']} round(s)", flush=True)
            state["running"] = False

        with rpc:
            print(f"worker: attached to '{channel}' and '{channel}_ctl', "
                  f"waiting for triggers", flush=True)

            # Prime the data channel BEFORE announcing readiness.
            #
            # A reader only sees writes made after its first read - the read
            # path baselines the channel's write counter on the first call.
            # Without this throwaway read the master's very first sample would
            # be consumed as the baseline and look like a lost message.
            data.try_read(buffer_size=READING.size)

            rpc.emit("worker_ready")

            while state["running"]:
                time.sleep(0.05)

            print(f"\nworker: processed {state['processed']} round(s), "
                  f"{rpc.missed} trigger(s) coalesced away")
            return 0 if state["processed"] else 1


def main(argv):
    if len(argv) < 2 or argv[1] not in ("master", "worker"):
        print(__doc__)
        return 2

    channel = argv[2] if len(argv) > 2 else CHANNEL_DEFAULT
    try:
        if argv[1] == "master":
            return master(channel, int(argv[3]) if len(argv) > 3 else 5)
        return worker(channel)
    except KeyboardInterrupt:
        print("\nstopped")
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
