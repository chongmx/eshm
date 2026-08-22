#!/usr/bin/env python3
"""Python side of example 06 - payloads larger than the channel.

    python3 peer.py send    [channel] [--width W] [--height H] [--frames N]
    python3 peer.py receive [channel] [--frames N]

Pairs with the C++ programs in this directory in either direction:

    ./build/frame_sender demo   +   python3 peer.py receive demo
    python3 peer.py send demo   +   ./build/frame_receiver demo

Same wire format as frame_protocol.h: a header, then stop-and-wait chunks, each
acknowledged before the next goes out. struct.Struct below matches the C layout
field for field - all little-endian, no padding needed because every field is
4 or 8 bytes and the 8-byte ones are already aligned.
"""

import os
import struct
import sys
import time
from pathlib import Path

try:
    from eshm import ESHM, ESHMRole
except ImportError:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "py"))
    from eshm import ESHM, ESHMRole

CHANNEL_DEFAULT = "frames"
# ESHM_MAX_DATA_SIZE is baked into the C++ side at compile time. Both ends must
# agree on the chunk size, so override this when the library was configured with
# a different -DESHM_MAX_DATA_SIZE:  ESHM_MAX_DATA_SIZE=8388608 python3 peer.py ...
MAX_DATA_SIZE = int(os.environ.get("ESHM_MAX_DATA_SIZE", 4096))

# Same values as frame_protocol.h. Both sides pack them as little-endian
# uint32, so these have to match the C constants numerically.
MAGIC_HEADER = 0x46524D48     # FRAME_MAGIC_HEADER
MAGIC_CHUNK = 0x46524D43      # FRAME_MAGIC_CHUNK
MAGIC_ACK = 0x46524D41        # FRAME_MAGIC_ACK

# struct FrameHeader { u32 magic, frame_id, width, height, channels, chunk_count;
#                      u64 total_bytes, checksum; }
HEADER = struct.Struct("<6I2Q")
# struct ChunkHeader { u32 magic, frame_id, index, bytes; }
CHUNK = struct.Struct("<4I")
# struct FrameAck { u32 magic, frame_id; i32 index; }
ACK = struct.Struct("<2Ii")

CHUNK_PAYLOAD = MAX_DATA_SIZE - CHUNK.size


def checksum(data: bytes) -> int:
    """Additive and order independent, identical to frame_checksum() in C."""
    return sum(data) & 0xFFFFFFFFFFFFFFFF


def parse(argv):
    channel, opts, i = CHANNEL_DEFAULT, {}, 0
    while i < len(argv):
        if argv[i].startswith("--"):
            opts[argv[i][2:]] = argv[i + 1]
            i += 2
        else:
            channel = argv[i]
            i += 1
    return channel, opts


# --------------------------------------------------------------------- sender

def await_ack(conn, frame_id: int, index: int, timeout_s: float = 5.0) -> bool:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            reply = conn.read(buffer_size=ACK.size, timeout_ms=50)
        except (TimeoutError, RuntimeError):
            continue
        if len(reply) >= ACK.size:
            magic, fid, idx = ACK.unpack_from(reply)
            if magic == MAGIC_ACK and fid == frame_id and idx == index:
                return True
    return False


def send_frame(conn, frame_id: int, width: int, height: int, channels: int,
               pixels: bytes) -> bool:
    total = len(pixels)
    chunks = (total + CHUNK_PAYLOAD - 1) // CHUNK_PAYLOAD

    conn.write(HEADER.pack(MAGIC_HEADER, frame_id, width, height, channels,
                           chunks, total, checksum(pixels)))
    if not await_ack(conn, frame_id, -1):
        print(f"sender: no ack for frame {frame_id} header", file=sys.stderr)
        return False

    for i in range(chunks):
        offset = i * CHUNK_PAYLOAD
        payload = pixels[offset:offset + CHUNK_PAYLOAD]
        conn.write(CHUNK.pack(MAGIC_CHUNK, frame_id, i, len(payload)) + payload)
        if not await_ack(conn, frame_id, i):
            print(f"sender: no ack for frame {frame_id} chunk {i}", file=sys.stderr)
            return False
    return True


def send(channel: str, opts) -> int:
    width = int(opts.get("width", 640))
    height = int(opts.get("height", 480))
    channels = int(opts.get("channels", 4))
    frames = int(opts.get("frames", 10))

    frame_bytes = width * height * channels
    chunks = (frame_bytes + CHUNK_PAYLOAD - 1) // CHUNK_PAYLOAD
    print(f"sender: {width}x{height}x{channels} = {frame_bytes / 1048576:.2f} MB per frame")
    print(f"        channel holds {MAX_DATA_SIZE} bytes, so {chunks} chunk(s) "
          f"of up to {CHUNK_PAYLOAD} bytes")

    with ESHM(channel, role=ESHMRole.MASTER) as conn:
        print(f"sender: channel '{channel}' is live, waiting for a receiver...", flush=True)
        for _ in range(150):
            if conn.get_stats()["slave_alive"]:
                break
            time.sleep(0.1)
        print("sender: receiver attached\n")

        pixels = bytearray(i & 0xFF for i in range(frame_bytes))
        sent = 0
        start = time.monotonic()

        for i in range(frames):
            pixels[0] = i & 0xFF                     # make each frame distinct
            frame_start = time.monotonic()
            if not send_frame(conn, i, width, height, channels, bytes(pixels)):
                break
            ms = (time.monotonic() - frame_start) * 1000
            sent += 1
            print(f"-> frame {i} in {ms:.1f} ms ({frame_bytes / (ms / 1000) / 1e9:.2f} GB/s)",
                  flush=True)

        elapsed = time.monotonic() - start
        print(f"\nsender: {sent} frame(s), {sent * frame_bytes / 1048576:.2f} MB "
              f"in {elapsed:.2f} s ({sent * frame_bytes / elapsed / 1e9:.2f} GB/s, "
              f"{sent / elapsed:.1f} fps)")
    return 0 if sent else 1


# ------------------------------------------------------------------- receiver

def receive(channel: str, opts) -> int:
    want = int(opts.get("frames", 0))

    conn = None
    for _ in range(100):
        try:
            conn = ESHM(channel, role=ESHMRole.SLAVE, auto_cleanup=False,
                        max_reconnect_attempts=0, reconnect_wait_ms=0)
            break
        except RuntimeError:
            time.sleep(0.1)
    if conn is None:
        print(f"receiver: no channel '{channel}' - is the sender running?", file=sys.stderr)
        return 1

    print(f"receiver: attached to '{channel}' (Ctrl-C to stop)\n")
    good = bad = 0
    frame = None
    header = None
    next_chunk = 0
    seen_sender = False
    frame_start = 0.0

    with conn:
        while want == 0 or good + bad < want:
            try:
                message = conn.read(buffer_size=MAX_DATA_SIZE, timeout_ms=200)
            except (TimeoutError, RuntimeError):
                if seen_sender and not conn.is_remote_alive():
                    print("receiver: sender went away")
                    break
                continue
            seen_sender = True

            if len(message) < 4:
                continue
            magic = struct.unpack_from("<I", message)[0]

            if magic == MAGIC_HEADER and len(message) >= HEADER.size:
                (_, frame_id, width, height, channels,
                 chunk_count, total_bytes, expected) = HEADER.unpack_from(message)
                header = (frame_id, chunk_count, total_bytes, expected)
                frame = bytearray(total_bytes)
                next_chunk = 0
                frame_start = time.monotonic()
                print(f"<- frame {frame_id}: {width}x{height}x{channels}, "
                      f"{total_bytes / 1048576:.2f} MB in {chunk_count} chunk(s)", flush=True)
                conn.write(ACK.pack(MAGIC_ACK, frame_id, -1))
                continue

            if magic == MAGIC_CHUNK and header and len(message) >= CHUNK.size:
                _, frame_id, index, nbytes = CHUNK.unpack_from(message)
                if frame_id != header[0]:
                    continue
                if index != next_chunk:
                    conn.write(ACK.pack(MAGIC_ACK, frame_id, index))
                    continue

                offset = index * CHUNK_PAYLOAD
                frame[offset:offset + nbytes] = message[CHUNK.size:CHUNK.size + nbytes]
                next_chunk += 1
                conn.write(ACK.pack(MAGIC_ACK, frame_id, index))

                if next_chunk == header[1]:
                    ms = (time.monotonic() - frame_start) * 1000
                    ok = checksum(bytes(frame)) == header[3]
                    good, bad = (good + 1, bad) if ok else (good, bad + 1)
                    print(f"   frame {frame_id} "
                          f"{'verified' if ok else 'CHECKSUM MISMATCH'} in {ms:.1f} ms "
                          f"({len(frame) / (ms / 1000) / 1e9:.2f} GB/s)", flush=True)
                    header = None

    print(f"\nreceiver: {good} frame(s) verified, {bad} corrupted")
    return 0 if (good > 0 and bad == 0) else 1


def main(argv):
    if len(argv) < 2 or argv[1] not in ("send", "receive"):
        print(__doc__)
        return 2

    channel, opts = parse(argv[2:])
    try:
        return send(channel, opts) if argv[1] == "send" else receive(channel, opts)
    except KeyboardInterrupt:
        print("\nstopped")
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
