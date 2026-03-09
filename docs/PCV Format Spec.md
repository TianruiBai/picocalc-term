# PicoCalc Video Format (.pcv) Specification

**Version:** PCV1
**Status:** Draft

---

## Overview

`.pcv` (PicoCalc Video) is a simple, MCU-optimized video container format designed for playback on the PicoCalc's 320×320 ST7365P display. It prioritizes:

1. **Zero-copy streaming** — frames are read sequentially, no seeking required during playback
2. **No complex decoding** — no DCT, no motion compensation, no entropy coding
3. **Guaranteed real-time** — frame size is bounded, ensuring SPI bandwidth is never exceeded
4. **Interleaved audio** — audio samples are packed with each frame for natural A/V sync

---

## File Structure

```
┌────────────────────────────────┐
│         File Header            │  32 bytes, fixed
├────────────────────────────────┤
│       Frame Chunk 0            │  Variable size
├────────────────────────────────┤
│       Frame Chunk 1            │
├────────────────────────────────┤
│       Frame Chunk 2            │
├────────────────────────────────┤
│           ...                  │
├────────────────────────────────┤
│       Frame Chunk N-1          │
├────────────────────────────────┤
│    Index Table (optional)      │  For seeking support
└────────────────────────────────┘
```

---

## File Header (32 bytes)

| Offset | Size | Type | Field | Description |
|---|---|---|---|---|
| 0 | 4 | char[4] | `magic` | `"PCV1"` (0x50, 0x43, 0x56, 0x31) |
| 4 | 2 | uint16_le | `width` | Frame width in pixels (e.g. 320) |
| 6 | 2 | uint16_le | `height` | Frame height in pixels (e.g. 320) |
| 8 | 1 | uint8 | `fps` | Target frames per second (e.g. 12) |
| 9 | 2 | uint16_le | `audio_rate` | Audio sample rate in Hz (e.g. 8000, 11025, 22050), 0 = no audio |
| 11 | 1 | uint8 | `audio_bits` | Bits per sample (8 or 16), 0 = no audio |
| 12 | 1 | uint8 | `audio_channels` | 1 = mono, 2 = stereo (typically 1) |
| 13 | 4 | uint32_le | `total_frames` | Total number of frame chunks in file |
| 17 | 1 | uint8 | `flags` | Bitfield (see below) |
| 18 | 4 | uint32_le | `index_offset` | File offset to index table (0 = no index) |
| 22 | 10 | reserved | — | Zero-padded, reserved for future use |

### Flags Bitfield

| Bit | Name | Description |
|---|---|---|
| 0 | `PCV_FLAG_RLE` | Frames use RLE compression (else raw RGB565) |
| 1 | `PCV_FLAG_AUDIO` | File contains interleaved audio |
| 2 | `PCV_FLAG_INDEX` | Index table present at end of file |
| 3 | `PCV_FLAG_LOOP` | Player should loop playback |
| 4-7 | Reserved | Must be 0 |

---

## Frame Chunk (Variable Size)

Each frame chunk contains one video frame and its corresponding audio data.

| Offset | Size | Type | Field | Description |
|---|---|---|---|---|
| 0 | 4 | uint32_le | `frame_size` | Size of pixel data in bytes |
| 4 | `frame_size` | uint8[] | `pixel_data` | RGB565 pixel data (raw or RLE) |
| 4+frame_size | 2 | uint16_le | `audio_size` | Size of audio data in bytes (0 if no audio) |
| 6+frame_size | `audio_size` | uint8[] | `audio_data` | PCM audio samples |

### Pixel Data — Raw Mode (FLAG_RLE = 0)

Uncompressed RGB565 pixels, stored **left-to-right, top-to-bottom**.

- Size: `width × height × 2` bytes
- For 320×320: 204,800 bytes per frame
- Pixel format: 16-bit RGB565, little-endian, BGR ordering (matching ST7365P native format)

```
Byte layout per pixel:
  [byte 0] = GGGBBBBB  (G[2:0], B[4:0])
  [byte 1] = RRRRRGGG  (R[4:0], G[5:3])
```

### Pixel Data — RLE Mode (FLAG_RLE = 1)

Simple run-length encoding on 16-bit pixels:

```
RLE packet format:
  [count_byte] [pixel_hi] [pixel_lo]

  count_byte:
    Bit 7 = 0: Run of identical pixels. Bits 6-0 = count (1-128)
                Followed by 2 bytes = the pixel value, repeated count times
    Bit 7 = 1: Literal pixels. Bits 6-0 = count (1-128)
                Followed by count × 2 bytes = literal pixel data
```

Worst case (all literal): `frame_size ≈ width × height × 2 × (129/128)` — nearly no overhead.
Best case (solid color): `frame_size = 3` bytes per frame.
Typical (UI/animation): **30-70% compression** depending on content.

### Audio Data

- Format: Raw PCM, unsigned 8-bit or signed 16-bit little-endian
- Sample rate: as specified in header (`audio_rate`)
- Channels: as specified in header (`audio_channels`), typically mono
- Samples per frame: `audio_rate / fps` (e.g. 8000/12 = 666 samples)
- Size per frame (8-bit mono): `audio_rate / fps` bytes (e.g. 666 bytes)
- Size per frame (16-bit mono): `audio_rate / fps × 2` bytes

---

## Index Table (Optional)

If `PCV_FLAG_INDEX` is set, an index table at the end of the file enables seeking.

**Located at:** `index_offset` bytes from start of file.

| Offset | Size | Type | Field | Description |
|---|---|---|---|---|
| 0 | 4 | uint32_le | `num_entries` | Number of index entries |
| 4 | num×8 | struct[] | `entries` | Array of index entries |

**Index entry (8 bytes):**

| Offset | Size | Type | Field |
|---|---|---|---|
| 0 | 4 | uint32_le | `frame_number` |
| 4 | 4 | uint32_le | `file_offset` |

Index entries should be placed every **N frames** (e.g. every 12 frames = 1 entry per second at 12 FPS) for coarse seeking. For fine seeking, the player reads forward from the nearest index entry.

---

## Constraints & Limits

| Parameter | Limit | Rationale |
|---|---|---|
| Max resolution | 320 × 320 | Display is 320×320 pixels |
| Max FPS | 15 | SPI bandwidth: 3.125 MB/s ÷ 200KB/frame |
| Max frame size (raw) | 204,800 bytes | 320 × 320 × 2 |
| Max frame size (RLE) | ~210,000 bytes | Worst-case RLE overhead |
| Audio sample rate | 8,000 – 22,050 Hz | PWM output quality vs CPU load |
| Audio bits | 8 or 16 | 8-bit sufficient for speech/effects, 16-bit for music |
| Max file size | Limited by SD card | FAT32: up to 4GB per file |
| Playback duration | ~5 min at 320×320 12FPS raw | ~7.2 GB/hour — RLE reduces significantly |

---

## Bandwidth Budget (12 FPS, 320×320)

```
Per frame (raw, no audio):
  Read from SD:   200 KB @ 6.5 MB/s  = 30.8 ms
  DMA to display: 200 KB @ 3.125 MB/s = 64.0 ms
  Frame period:   83.3 ms (12 FPS)

  Pipeline overlap:
  ┌─────────────────────────────────────┐
  │ Time  │ SD Read     │ SPI DMA      │
  │ 0ms   │ Frame N+1   │ Frame N      │
  │ 31ms  │ (done)      │ (ongoing)    │
  │ 64ms  │             │ (done)       │
  │ 83ms  │ Frame N+2   │ Frame N+1    │  ← next frame starts
  └─────────────────────────────────────┘

  Effective: SD read overlaps with SPI DMA → fits in 83ms window ✓
```

With RLE compression (50% typical):
  - SD read: 100 KB @ 6.5 MB/s = 15.4 ms
  - RLE decode: ~2 ms (CPU)
  - More headroom for audio processing

---

## `pcv-convert` Usage

The offline PC tool converts standard video files to `.pcv`:

```bash
# Basic conversion
pcv-convert input.mp4 -o output.pcv

# Custom settings
pcv-convert input.mp4 -o output.pcv \
    --fps 12 \
    --width 320 --height 320 \
    --rle \
    --audio-rate 11025 \
    --audio-bits 8 \
    --audio-channels 1 \
    --index

# No audio
pcv-convert input.mp4 -o output.pcv --no-audio --fps 15

# Resize to fit (maintain aspect ratio, letterbox)
pcv-convert input.mp4 -o output.pcv --fit 320x320 --bg 0x0000
```

**Dependencies:** Python 3.8+, FFmpeg (in PATH), Pillow, NumPy.

**Process:**
1. FFmpeg extracts frames as raw RGB24 at target FPS
2. Each frame is resized/cropped to target resolution
3. RGB24 → RGB565 conversion (with BGR swap for ST7365P)
4. Optional RLE compression
5. FFmpeg extracts audio as raw PCM at target rate/bits
6. Frames and audio chunks are interleaved into `.pcv` container
7. Optional index table appended

---

## C Header

```c
/* pcv.h — PicoCalc Video format definitions */

#ifndef PCV_H
#define PCV_H

#include <stdint.h>

#define PCV_MAGIC       "PCV1"
#define PCV_HEADER_SIZE 32

#define PCV_FLAG_RLE    (1 << 0)
#define PCV_FLAG_AUDIO  (1 << 1)
#define PCV_FLAG_INDEX  (1 << 2)
#define PCV_FLAG_LOOP   (1 << 3)

typedef struct __attribute__((packed)) {
    char     magic[4];       /* "PCV1" */
    uint16_t width;
    uint16_t height;
    uint8_t  fps;
    uint16_t audio_rate;
    uint8_t  audio_bits;
    uint8_t  audio_channels;
    uint32_t total_frames;
    uint8_t  flags;
    uint32_t index_offset;
    uint8_t  reserved[10];
} pcv_header_t;

_Static_assert(sizeof(pcv_header_t) == PCV_HEADER_SIZE,
               "PCV header must be 32 bytes");

typedef struct __attribute__((packed)) {
    uint32_t frame_number;
    uint32_t file_offset;
} pcv_index_entry_t;

/* RLE decode one frame from src into dst (RGB565 pixel buffer) */
int pcv_rle_decode(const uint8_t *src, size_t src_len,
                   uint16_t *dst, size_t dst_pixels);

/* RLE encode one frame from src into dst */
int pcv_rle_encode(const uint16_t *src, size_t src_pixels,
                   uint8_t *dst, size_t dst_maxlen);

#endif /* PCV_H */
```
