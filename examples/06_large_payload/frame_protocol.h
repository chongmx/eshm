// Wire format shared by frame_sender.cpp, frame_receiver.cpp and peer.py.
//
// The channel holds exactly one value per direction: a write overwrites
// whatever the reader has not collected yet. That is what makes ESHM fast, and
// it means a payload larger than ESHM_MAX_DATA_SIZE cannot simply be written
// in pieces - the reader would miss most of them.
//
// So this is a stop-and-wait protocol: the sender writes one chunk and waits
// for the receiver to acknowledge it before writing the next. Slower than a
// single write, but it moves a frame of any size over a channel of any size,
// and it never loses a byte.
//
//   sender                          receiver
//   ------                          --------
//   HEADER (frame_id, bytes, n) --> ACK(-1)
//   CHUNK 0                     --> ACK(0)
//   CHUNK 1                     --> ACK(1)
//   ...                             ...
//   CHUNK n-1                   --> ACK(n-1)
//
// When the whole frame fits in one chunk the handshake is still there, but it
// costs one round trip - see the README for the numbers.

#ifndef ESHM_EXAMPLE_FRAME_PROTOCOL_H
#define ESHM_EXAMPLE_FRAME_PROTOCOL_H

#include <eshm.h>
#include <stdint.h>

#define FRAME_MAGIC_HEADER 0x46524d48u   /* "FRMH" */
#define FRAME_MAGIC_CHUNK  0x46524d43u   /* "FRMC" */
#define FRAME_MAGIC_ACK    0x46524d41u   /* "FRMA" */

/* Announces a frame. Sent alone, before any chunk. */
struct FrameHeader {
    uint32_t magic;          /* FRAME_MAGIC_HEADER */
    uint32_t frame_id;
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint32_t chunk_count;
    uint64_t total_bytes;
    uint64_t checksum;       /* additive, so both languages compute it alike */
};

/* One slice of pixel data. Followed immediately by `bytes` payload bytes. */
struct ChunkHeader {
    uint32_t magic;          /* FRAME_MAGIC_CHUNK */
    uint32_t frame_id;
    uint32_t index;
    uint32_t bytes;
};

/* The receiver's reply. index -1 acknowledges the frame header. */
struct FrameAck {
    uint32_t magic;          /* FRAME_MAGIC_ACK */
    uint32_t frame_id;
    int32_t  index;
};

/* Payload bytes that fit alongside a ChunkHeader in one channel write. */
#define FRAME_CHUNK_PAYLOAD (ESHM_MAX_DATA_SIZE - (int)sizeof(struct ChunkHeader))

/* Cheap, order-independent, and identical in C and Python. Not a real digest -
 * it is here to prove the bytes survived the trip, not to resist tampering. */
static inline uint64_t frame_checksum(const uint8_t* data, uint64_t len) {
    uint64_t sum = 0;
    for (uint64_t i = 0; i < len; ++i) sum += data[i];
    return sum;
}

#endif /* ESHM_EXAMPLE_FRAME_PROTOCOL_H */
