#!/usr/bin/env python3
"""Python side of example 03 - the ABI-stable C API, reached through ctypes.

    python3 peer.py write [channel] [count]   # master: encode + write in C++
    python3 peer.py read  [channel] [count]   # slave:  read + decode in C++

Pairs with the C programs in this directory in both directions:

    ./build/c_writer demo   +   python3 peer.py read demo
    python3 peer.py write demo   +   ./build/c_reader demo

Example 02 encodes DER in Python. This one does not: ESHMData.write_data() and
.read_data() call eshm_write_data() / eshm_read_data(), the same two C entry
points c_writer.c and c_reader.c use, so the codec work happens in C++ and only
finished values cross the FFI boundary. Example 08 measures what that saves.
"""

import sys
import time
from pathlib import Path

try:
    from eshm.eshm_data import ESHMData, DataItem, DataType
    from eshm import ESHMRole
except ImportError:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "py"))
    from eshm_data import ESHMData, DataItem, DataType
    from eshm import ESHMRole

CHANNEL_DEFAULT = "c_api"

TYPE_NAMES = {
    DataType.INTEGER: "INTEGER",
    DataType.BOOLEAN: "BOOLEAN",
    DataType.REAL: "REAL",
    DataType.STRING: "STRING",
    DataType.BINARY: "BINARY",
}


def show(items) -> None:
    for item in items:
        value = item.value.hex() if isinstance(item.value, bytes) else item.value
        print(f"  {item.key:<12} {TYPE_NAMES.get(item.type, item.type):<8} {value}")


def write(channel: str, count: int) -> int:
    """Master role: hand values to C++ and let it encode them."""
    with ESHMData(channel, role=ESHMRole.MASTER) as conn:
        print(f"writer: channel '{channel}' is live, waiting for a reader...", flush=True)
        # slave_alive, not is_remote_alive(): the latter only reports whether a
        # peer has been detected as stale, so it is True on an empty channel.
        for _ in range(150):
            if conn.get_stats()["slave_alive"]:
                break
            time.sleep(0.1)
        print("writer: reader attached (Ctrl-C to stop)")

        n = 0
        while count == 0 or n < count:
            # Same five types as c_writer.c, same wire bytes.
            conn.write_data([
                DataItem(DataType.INTEGER, "counter", n),
                DataItem(DataType.REAL, "temperature", 20.0 + (n % 10) * 0.5),
                DataItem(DataType.BOOLEAN, "enabled", n % 2 == 0),
                DataItem(DataType.STRING, "source", "Python writer"),
                DataItem(DataType.BINARY, "checksum", bytes([n & 0xFF, 0xDE, 0xAD, 0xBE, 0xEF])),
            ])

            if n % 10 == 0:
                print(f"-> #{n}", flush=True)

            n += 1
            time.sleep(0.01)

    print("\nwriter: closing channel")
    return 0


def read(channel: str, count: int) -> int:
    """Slave role: let C++ decode, and get finished Python values back."""
    conn = None
    for _ in range(50):
        try:
            conn = ESHMData(channel, role=ESHMRole.SLAVE,
                            auto_cleanup=False,
                            max_reconnect_attempts=0,
                            reconnect_wait_ms=0)
            break
        except RuntimeError:
            time.sleep(0.1)

    if conn is None:
        print(f"reader: no channel '{channel}' - is the writer running?", file=sys.stderr)
        return 1

    print(f"reader: attached to '{channel}' (Ctrl-C to stop)")
    records = 0
    seen_writer = False

    with conn:
        while count == 0 or records < count:
            # read_data returns [] when nothing arrived before the timeout.
            items = conn.read_data(timeout_ms=200)
            if not items:
                if seen_writer and not conn.is_remote_alive():
                    print("reader: writer went away")
                    break
                continue

            seen_writer = True
            records += 1
            if records % 10 == 1:
                print(f"<- record {records} ({len(items)} items)")
                show(items)
                sys.stdout.flush()

    print(f"\nreader: {records} record(s)")
    return 0 if records > 0 else 1


def main(argv):
    if len(argv) < 2 or argv[1] not in ("write", "read"):
        print(__doc__)
        return 2

    channel = argv[2] if len(argv) > 2 else CHANNEL_DEFAULT
    count = int(argv[3]) if len(argv) > 3 else 0
    try:
        return write(channel, count) if argv[1] == "write" else read(channel, count)
    except KeyboardInterrupt:
        print("\nstopped")
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
