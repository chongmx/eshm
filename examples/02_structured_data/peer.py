#!/usr/bin/env python3
"""Python side of example 02 - structured records over ASN.1 DER.

    python3 peer.py send    [channel] [count]   # master: encodes and writes
    python3 peer.py receive [channel] [count]   # slave: decodes and acknowledges
                                                #        (count 0 = until the
                                                #         sender leaves)

Pairs with the C++ programs in this directory in both directions:

    ./build/structured_sender demo   +   python3 peer.py receive demo
    python3 peer.py send demo        +   ./build/structured_receiver demo

The pure-Python codec in py/data_handler.py is wire compatible with the C++
DataHandler, so the same five types survive the crossing:

    INTEGER -> int      BOOLEAN -> bool    REAL -> float
    STRING  -> str      BINARY  -> bytes
"""

import math
import sys
import time
from pathlib import Path

try:
    from eshm import ESHM, ESHMRole, DataHandler
except ImportError:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "py"))
    from eshm import ESHM, ESHMRole
    from data_handler import DataHandler

CHANNEL_DEFAULT = "structured"
BUFFER = 4096            # ESHM_MAX_DATA_SIZE


def wait_for_peer(conn, seconds: int) -> bool:
    """Block until a peer attaches, so a bounded run is not a race.

    A reader only sees writes made after its first read (see
    ../01_hello_channel), so there is nothing to gain by sending into an
    empty channel.

    Note which call this uses. is_remote_alive() answers "has the peer been
    detected as stale", which is false before anyone has ever attached - so it
    reports True on an empty channel. The slave_alive flag in get_stats() is
    the one that means a peer is actually there.
    """
    for _ in range(seconds * 10):
        if conn.get_stats()["slave_alive"]:
            return True
        time.sleep(0.1)
    return False


def send(channel: str, count: int) -> int:
    """Master role: encode one record per tick and write it to the channel."""
    handler = DataHandler()

    with ESHM(channel, role=ESHMRole.MASTER) as conn:
        print(f"sender: channel '{channel}' is live, waiting for a receiver...",
              flush=True)
        if not wait_for_peer(conn, 15):
            print("sender: no receiver attached", file=sys.stderr)
            return 1
        print("sender: receiver attached (Ctrl-C to stop)")
        i = 0
        while count == 0 or i < count:
            temperature = 20.0 + 5.0 * math.sin(i * 0.1)
            checksum = bytes([i & 0xFF, 0xDE, 0xAD, 0xBE, 0xEF])

            payload = handler.encode_data_buffer([
                DataHandler.create_integer("counter", i),
                DataHandler.create_real("temperature", temperature),
                DataHandler.create_boolean("enabled", i % 2 == 0),
                DataHandler.create_string("source", "Python sender"),
                DataHandler.create_binary("checksum", checksum),
            ])
            conn.write(payload)

            if i % 10 == 0:
                print(f"-> #{i} temperature={temperature:.2f} "
                      f"enabled={i % 2 == 0} ({len(payload)} bytes on the wire)", flush=True)

            reply = conn.try_read(buffer_size=BUFFER)
            if reply:
                try:
                    values = DataHandler.extract_simple_values(
                        handler.decode_data_buffer(reply))
                    print(f"   <- ack #{values['ack']} from \"{values['source']}\"", flush=True)
                except Exception as exc:                      # noqa: BLE001 - example
                    print(f"sender: could not decode reply: {exc}", file=sys.stderr)

            i += 1
            time.sleep(0.01)

    print("\nsender: closing channel")
    return 0


def receive(channel: str, count: int) -> int:
    """Slave role: decode every record and acknowledge in the same encoding."""
    handler = DataHandler()

    conn = None
    for _ in range(50):                      # ~5 seconds
        try:
            conn = ESHM(channel, role=ESHMRole.SLAVE,
                        auto_cleanup=False,
                        max_reconnect_attempts=0,
                        reconnect_wait_ms=0)
            break
        except RuntimeError:
            time.sleep(0.1)

    if conn is None:
        print(f"receiver: no channel '{channel}' - is the sender running?", file=sys.stderr)
        return 1

    print(f"receiver: attached to '{channel}' (Ctrl-C to stop)")
    received = decode_errors = 0
    highest_counter = -1
    seen_sender = False

    with conn:
        while count == 0 or received < count:
            try:
                raw = conn.read(buffer_size=BUFFER, timeout_ms=200)
            except TimeoutError:
                # Nothing new this round. Once the sender has been seen, a
                # stale peer means it has exited - stop rather than spin.
                if seen_sender and not conn.is_remote_alive():
                    print("receiver: sender went away")
                    break
                continue
            seen_sender = True

            try:
                values = DataHandler.extract_simple_values(handler.decode_data_buffer(raw))
            except Exception as exc:                          # noqa: BLE001 - example
                decode_errors += 1
                if decode_errors < 10:
                    print(f"receiver: decode error: {exc}", file=sys.stderr)
                continue

            received += 1
            counter = values["counter"]
            highest_counter = counter
            if counter % 10 == 0:
                print(f"<- #{counter} temperature={values['temperature']:.2f} "
                      f"enabled={values['enabled']} source=\"{values['source']}\" "
                      f"checksum={values['checksum'].hex()}", flush=True)

            conn.write(handler.encode_data_buffer([
                DataHandler.create_integer("ack", counter),
                DataHandler.create_string("source", "Python receiver"),
            ]))

    # The channel holds one value per direction: if the sender outruns the
    # reader, intermediate records are overwritten rather than queued. Seeing
    # fewer records than were sent is expected; decode_errors is what has to
    # stay at zero.
    print(f"\nreceiver: {received} record(s) decoded, highest counter "
          f"{highest_counter}, {decode_errors} decode error(s)")
    return 0 if (received > 0 and decode_errors == 0) else 1


def main(argv):
    if len(argv) < 2 or argv[1] not in ("send", "receive"):
        print(__doc__)
        return 2

    channel = argv[2] if len(argv) > 2 else CHANNEL_DEFAULT
    try:
        if argv[1] == "send":
            return send(channel, int(argv[3]) if len(argv) > 3 else 0)
        return receive(channel, int(argv[3]) if len(argv) > 3 else 0)
    except KeyboardInterrupt:
        print("\nstopped")
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
