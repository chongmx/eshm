# 06 - Payloads larger than the channel

`ESHM_MAX_DATA_SIZE` is a compile-time constant (4096 bytes by default) that
fixes the size of each direction's buffer inside the shared segment. A single
`eshm_write` cannot exceed it. This example moves image frames of any size
anyway, and measures what that costs.

## Why you cannot just write in pieces

The channel holds exactly **one value per direction**. A write overwrites
whatever the reader has not collected yet — that is what makes reads lock-free
and sub-microsecond, and it means naively writing chunk after chunk loses
almost all of them.

So `frame_protocol.h` defines a stop-and-wait handshake: the sender writes one
chunk and waits for the receiver to acknowledge it before writing the next.

```
sender                          receiver
------                          --------
HEADER (frame_id, bytes, n) --> ACK(-1)
CHUNK 0                     --> ACK(0)
CHUNK 1                     --> ACK(1)
...                             ...
CHUNK n-1                   --> ACK(n-1)
```

Every byte arrives, in order, and the receiver verifies an additive checksum
over the reassembled frame.

## Two ways to size the channel

| | Default 4 KB channel | `-DESHM_MAX_DATA_SIZE=8388608` |
|---|---|---|
| 1.2 MB frame | ~300 chunks, ~300 round trips | 1 chunk, 1 round trip |
| Throughput | round-trip bound | memcpy bound |
| Segment size in `/dev/shm` | ~8.5 KB | ~16 MB |

Chunking is the portable path and works against any build. Sizing the channel
to the payload is dramatically faster and is what you want for video:

```bash
cmake -S . -B build-big -DESHM_MAX_DATA_SIZE=8388608    # 8 MB channels
cmake --build build-big
```

Both ends must agree. The C++ side gets the value at compile time; tell the
Python side with the environment variable:

```bash
ESHM_MAX_DATA_SIZE=8388608 python3 peer.py receive demo
```

Mismatched sizes show up as a hang waiting for an ack, or as
`ESHM_ERROR_BUFFER_TOO_SMALL` on the read.

## Build and run

```bash
cmake -S . -B build && cmake --build build
```

```bash
# C++ sends, Python receives
./build/frame_sender demo --width 640 --height 480 --frames 5
python3 peer.py receive demo

# Python sends, C++ receives
python3 peer.py send demo --width 640 --height 480 --frames 5
./build/frame_receiver demo
```

```
sender: 640x480x4 = 1.17 MB per frame
        channel holds 4096 bytes, so 301 chunk(s) of up to 4080 bytes
sender: receiver attached

-> frame 0 in 21.4 ms (0.06 GB/s)
```

```
<- frame 0: 640x480x4, 1.17 MB in 301 chunk(s)
   frame 0 verified in 21.3 ms (0.06 GB/s)
```

4K RGBA is `--width 3840 --height 2160` (33 MB per frame). On the default
build that is ~8000 chunks per frame; with an 8 MB channel it is 5.

## What this shows about the API

- `ESHM_MAX_DATA_SIZE` caps one write, not the total you can move.
- Raw `eshm_write` with your own struct framing is fine — ASN.1 (examples
  [02](../02_structured_data/) and [03](../03_c_api/)) is for
  self-describing records, not bulk pixels.
- The same C struct and Python `struct.Struct` format describe the same bytes.
  Every field here is 4 or 8 bytes and the 8-byte fields are already aligned,
  so no padding is needed and `<6I2Q` matches the C layout exactly. Add a
  `uint16_t` and that stops being true.
- A latest-value channel needs a handshake to become reliable. If you do not
  need reliability — live video where a dropped frame is better than a stalled
  one — skip the acks and let the reader take whatever is current.
