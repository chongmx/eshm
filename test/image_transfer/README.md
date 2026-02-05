# 4K Image Transfer Test

This test demonstrates ESHM's ability to transfer large data (4K resolution images) using custom memory layout configuration.

## Image Specifications

- **Resolution:** 3840 × 2160 (4K)
- **Color depth:** 32-bit RGBA
- **Size per image:** 33,177,600 bytes (~31.6 MB)
- **Number of images:** 4
- **Test patterns:**
  - Image 0: Red gradient
  - Image 1: Green gradient
  - Image 2: Blue checkerboard
  - Image 3: RGB mixed pattern

## Building

The default ESHM_MAX_DATA_SIZE (4096 bytes) is too small for 4K images. You must rebuild with a larger size:

```bash
cd /path/to/testESHM
rm -rf build && mkdir build && cd build

# Configure with 34MB channel size (to fit 4K image + header)
cmake -DESHM_MAX_DATA_SIZE=33554432 ..

# Build
make
```

## Running the Test

**Terminal 1 - Sender:**
```bash
./test/image_transfer/image_sender
```

**Terminal 2 - Receiver:**
```bash
./test/image_transfer/image_receiver
```

## Expected Output

**Sender:**
```
4K Image Sender - Testing ESHM with large data
================================================
Image specs: 3840x2160, 4 bytes/pixel
Size per image: 33177600 bytes (31.64 MB)

ESHM initialized as master. Waiting for receiver...

Generating image 0...
Sending image 0 (checksum: 0x...)...
✓ Sent image 0 in 45.123 ms (701.23 MB/s)

Receiver: ACK: Image 0 received and verified
...
```

**Receiver:**
```
4K Image Receiver - Testing ESHM with large data
=================================================

Received image 0:
  Dimensions: 3840x2160
  Size: 33177624 bytes
  Timestamp: ...
  ✓ Checksum valid (0x...)
  ✓ Image pattern verified

All 4 images received successfully!
```

## What This Tests

1. **Large data transfers** - Each image is ~32 MB
2. **Custom ESHM_MAX_DATA_SIZE** - Demonstrates configurable memory layout
3. **Data integrity** - Checksums verify no corruption
4. **Pattern verification** - Samples random pixels to verify correct data
5. **Throughput measurement** - Reports transfer speed in MB/s

## Memory Layout

With `ESHM_MAX_DATA_SIZE=33554432`:

- Channel size: ~33.5 MB each
- Total shared memory: ~67 MB (2 channels + header)
- Sufficient for one 4K 32-bit image per transfer

## Performance Notes

Since ESHM uses shared memory (not network), transfers are essentially memory copies:
- **Expected throughput:** 500+ MB/s (depends on CPU and memory speed)
- **Latency:** Single-digit milliseconds for 32MB transfer
- **Zero network overhead** - Direct memory access

## Limitations

- Both sender and receiver must be built with the same `ESHM_MAX_DATA_SIZE`
- Transfers one image at a time (4 sequential transfers for 4 images)
- Large shared memory allocation (~67 MB total)

## Alternative Approaches

For transferring multiple images simultaneously, you could:

1. **Chunk the data** - Split each image into smaller chunks
2. **Use multiple channels** - Create separate ESHM instances for each image
3. **Compress** - Apply compression before transfer (reduces size but adds CPU overhead)

This test uses the simplest approach: single full-image transfers with verification.
