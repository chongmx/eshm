#!/usr/bin/env python3
"""Python side of example 08 - throughput and the cost of each codec path.

    python3 bench.py drive [channel] [--seconds S] [--size N] [--poll]
    python3 bench.py echo  [channel] [--poll]
    python3 bench.py codec [--records N]     # no channel: encode/decode only

Pairs with ./bench in either direction:

    ./build/bench drive demo   +   python3 bench.py echo demo
    python3 bench.py drive demo   +   ./build/bench echo demo

The `codec` mode answers a different question - not how fast the channel is,
but what the two DataHandler bindings cost per record:

    pure Python   py/data_handler.py   readable, no C++ call, per-field bytecode
    native        py/eshm_data.py      one ctypes call, DER built in C++
"""

import os
import struct
import sys
import time
from pathlib import Path

try:
    from eshm import ESHM, ESHMRole, ESHMWakeupMode, DataHandler
    from eshm.eshm_data import ESHMData, DataItem, DataType
except ImportError:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "py"))
    from eshm import ESHM, ESHMRole, ESHMWakeupMode
    from data_handler import DataHandler
    from eshm_data import ESHMData, DataItem, DataType

CHANNEL_DEFAULT = "bench"
MAX_DATA_SIZE = int(os.environ.get("ESHM_MAX_DATA_SIZE", 4096))


FLAGS = {"poll"}          # options that take no value


def parse(argv):
    channel, opts, i = CHANNEL_DEFAULT, {}, 0
    while i < len(argv):
        if argv[i].startswith("--"):
            name = argv[i][2:]
            if name in FLAGS:
                opts[name] = True
                i += 1
            else:
                opts[name] = argv[i + 1]
                i += 2
        else:
            channel = argv[i]
            i += 1
    return channel, opts


def drive(channel: str, opts) -> int:
    seconds = float(opts.get("seconds", 5))
    size = min(int(opts.get("size", 256)), MAX_DATA_SIZE)

    with ESHM(channel, role=ESHMRole.MASTER) as conn:
        if opts.get("poll"):
            conn.wakeup_mode = ESHMWakeupMode.POLL
        print(f"bench: channel '{channel}' is live, waiting for an echo peer... "
              f"(wakeup: {conn.wakeup_mode.name})", flush=True)
        for _ in range(300):
            if conn.get_stats()["slave_alive"]:
                break
            time.sleep(0.1)
        print(f"bench: {size} byte payload, {seconds:.0f} s\n")

        payload = bytearray(b"\xa5" * size)
        round_trips = dropped = 0
        start = time.monotonic()
        deadline = start + seconds

        while time.monotonic() < deadline:
            struct.pack_into("<Q", payload, 0, round_trips)
            conn.write(bytes(payload))
            try:
                conn.read(buffer_size=MAX_DATA_SIZE, timeout_ms=1000)
            except (TimeoutError, RuntimeError):
                dropped += 1
                continue
            round_trips += 1

        elapsed = time.monotonic() - start
        print(f"round trips   {round_trips}")
        print(f"elapsed       {elapsed:.2f} s")
        print(f"rate          {round_trips / elapsed:.0f} round trips/s")
        print(f"latency       {elapsed / max(round_trips, 1) * 1e6:.1f} us per round trip")
        print(f"throughput    {2 * round_trips * size / elapsed / 1e6:.1f} MB/s "
              f"(payload only, both directions)")
        if dropped:
            print(f"timeouts      {dropped}")
    return 0


def echo(channel: str, opts) -> int:
    conn = None
    for _ in range(300):
        try:
            conn = ESHM(channel, role=ESHMRole.SLAVE, auto_cleanup=False,
                        max_reconnect_attempts=0, reconnect_wait_ms=0)
            break
        except RuntimeError:
            time.sleep(0.1)
    if conn is None:
        print(f"bench: no channel '{channel}' - is a driver running?", file=sys.stderr)
        return 1

    if opts.get("poll"):
        conn.wakeup_mode = ESHMWakeupMode.POLL
    print(f"bench: echoing on '{channel}' (Ctrl-C to stop) "
          f"(wakeup: {conn.wakeup_mode.name})", flush=True)
    echoed = 0
    seen_driver = False

    with conn:
        while True:
            try:
                data = conn.read(buffer_size=MAX_DATA_SIZE, timeout_ms=200)
            except (TimeoutError, RuntimeError):
                if seen_driver and not conn.is_remote_alive():
                    break
                continue
            seen_driver = True
            conn.write(data)
            echoed += 1

    print(f"\nbench: echoed {echoed} message(s)")
    return 0


def codec(channel: str, opts) -> int:
    """Encode + decode the same record both ways, with no channel involved."""
    records = int(opts.get("records", 20000))
    fields = [
        ("counter", DataType.INTEGER, 42),
        ("temperature", DataType.REAL, 23.5),
        ("enabled", DataType.BOOLEAN, True),
        ("source", DataType.STRING, "benchmark"),
    ]

    print(f"codec: {records} records, 4 fields each\n")

    # --- pure Python: py/data_handler.py --------------------------------
    handler = DataHandler()
    items = [
        DataHandler.create_integer("counter", 42),
        DataHandler.create_real("temperature", 23.5),
        DataHandler.create_boolean("enabled", True),
        DataHandler.create_string("source", "benchmark"),
    ]
    start = time.monotonic()
    for _ in range(records):
        handler.decode_data_buffer(handler.encode_data_buffer(items))
    pure = time.monotonic() - start

    # --- native: the C++ codec over a channel, via eshm_write_data ------
    # ESHMData binds encode+write together, so measure it the honest way: a
    # real channel with a peer that never reads. The write cost dominates
    # nothing here - the DER work is what differs.
    native_items = [DataItem(dtype, key, value) for key, dtype, value in fields]
    with ESHMData(channel + "_codec", role=ESHMRole.MASTER) as conn:
        start = time.monotonic()
        for _ in range(records):
            conn.write_data(native_items)
        native = time.monotonic() - start

    print(f"pure Python encode+decode   {pure:.2f} s   "
          f"{records / pure:>9,.0f} records/s   {pure / records * 1e6:6.1f} us each")
    print(f"native encode+write         {native:.2f} s   "
          f"{records / native:>9,.0f} records/s   {native / records * 1e6:6.1f} us each")
    print(f"\nnative is {pure / native:.1f}x the pure-Python encode+decode rate")
    print("(different work - the native figure includes the channel write and\n"
          " excludes the decode - but it is the comparison that matters when\n"
          " choosing which binding to send with)")
    return 0


def main(argv):
    if len(argv) < 2 or argv[1] not in ("drive", "echo", "codec"):
        print(__doc__)
        return 2

    channel, opts = parse(argv[2:])
    try:
        return {"drive": drive, "echo": echo, "codec": codec}[argv[1]](channel, opts)
    except KeyboardInterrupt:
        print("\nstopped")
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
