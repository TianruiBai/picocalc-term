#!/usr/bin/env python3
"""
pcv-convert — Convert video files to PicoCalc Video (.pcv) format.

Requires: FFmpeg (in PATH), Pillow, NumPy

Usage:
    python pcv_convert.py input.mp4 -o output.pcv [options]

Options:
    --fps N           Target frame rate (default: 12)
    --width N         Target width (default: 320)
    --height N        Target height (default: 320)
    --rle             Enable RLE compression
    --no-audio        Omit audio track
    --audio-rate N    Audio sample rate (default: 11025)
    --audio-bits N    Audio bits per sample: 8 or 16 (default: 8)
    --audio-channels N  1=mono, 2=stereo (default: 1)
    --fit WxH         Fit within dimensions, letterbox with --bg
    --bg 0xRRGG       Background color for letterboxing (RGB565 hex)
    --index           Generate seek index table
    --loop            Set loop flag in header
"""

import argparse
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    import numpy as np
    from PIL import Image
except ImportError:
    print("ERROR: Requires 'Pillow' and 'numpy'. Install with:")
    print("  pip install Pillow numpy")
    sys.exit(1)


# PCV format constants
PCV_MAGIC = b"PCV1"
PCV_HEADER_SIZE = 32
PCV_FLAG_RLE   = 1 << 0
PCV_FLAG_AUDIO = 1 << 1
PCV_FLAG_INDEX = 1 << 2
PCV_FLAG_LOOP  = 1 << 3


def rgb888_to_rgb565_bgr(r, g, b):
    """Convert RGB888 to RGB565 with BGR byte order (ST7365P native)."""
    r5 = (r >> 3) & 0x1F
    g6 = (g >> 2) & 0x3F
    b5 = (b >> 3) & 0x1F
    # RGB565: RRRRRGGG_GGGBBBBB
    pixel = (r5 << 11) | (g6 << 5) | b5
    return pixel


def frame_to_rgb565(img, width, height):
    """Convert PIL Image to RGB565 little-endian byte array."""
    img = img.convert("RGB").resize((width, height), Image.LANCZOS)
    pixels = np.array(img, dtype=np.uint16)

    r = (pixels[:, :, 0] >> 3).astype(np.uint16)
    g = (pixels[:, :, 1] >> 2).astype(np.uint16)
    b = (pixels[:, :, 2] >> 3).astype(np.uint16)

    rgb565 = (r << 11) | (g << 5) | b
    return rgb565.astype("<u2").tobytes()


def rle_encode(data):
    """RLE encode RGB565 pixel data.

    Format per packet:
      bit7=0: run of identical pixels. bits6-0 = count (1-128), then 2 bytes pixel
      bit7=1: literal pixels. bits6-0 = count (1-128), then count*2 bytes
    """
    pixels = np.frombuffer(data, dtype="<u2")
    result = bytearray()
    i = 0
    n = len(pixels)

    while i < n:
        # Check for a run of identical pixels
        run_start = i
        while i < n - 1 and pixels[i] == pixels[i + 1] and (i - run_start) < 127:
            i += 1
        run_len = i - run_start + 1

        if run_len >= 3:
            # Encode as run
            result.append((run_len - 1) & 0x7F)
            result.extend(int(pixels[run_start]).to_bytes(2, "little"))
            i = run_start + run_len
        else:
            # Encode as literals
            lit_start = run_start
            i = run_start
            while i < n and (i - lit_start) < 128:
                if i < n - 2 and pixels[i] == pixels[i + 1] == pixels[i + 2]:
                    break
                i += 1
            lit_len = i - lit_start
            if lit_len > 0:
                result.append(0x80 | ((lit_len - 1) & 0x7F))
                result.extend(pixels[lit_start:i].astype("<u2").tobytes())

    return bytes(result)


def extract_frames(input_path, fps, tmpdir):
    """Use FFmpeg to extract frames as PNG."""
    pattern = os.path.join(tmpdir, "frame_%06d.png")
    cmd = [
        "ffmpeg", "-i", str(input_path),
        "-vf", f"fps={fps}",
        "-q:v", "1",
        pattern,
        "-y", "-loglevel", "warning"
    ]
    subprocess.run(cmd, check=True)
    frames = sorted(Path(tmpdir).glob("frame_*.png"))
    return frames


def extract_audio(input_path, rate, bits, channels, fps, total_frames, tmpdir):
    """Use FFmpeg to extract raw PCM audio."""
    fmt = "u8" if bits == 8 else "s16le"
    audio_path = os.path.join(tmpdir, "audio.raw")
    cmd = [
        "ffmpeg", "-i", str(input_path),
        "-f", fmt,
        "-ar", str(rate),
        "-ac", str(channels),
        audio_path,
        "-y", "-loglevel", "warning"
    ]
    subprocess.run(cmd, check=True)

    with open(audio_path, "rb") as f:
        audio_data = f.read()

    # Split into per-frame chunks
    bytes_per_sample = 1 if bits == 8 else 2
    samples_per_frame = rate // fps
    chunk_size = samples_per_frame * channels * bytes_per_sample

    chunks = []
    for i in range(total_frames):
        offset = i * chunk_size
        chunk = audio_data[offset:offset + chunk_size]
        if len(chunk) < chunk_size:
            chunk = chunk + b"\x00" * (chunk_size - len(chunk))
        chunks.append(chunk)

    return chunks


def main():
    parser = argparse.ArgumentParser(description="Convert video to .pcv format")
    parser.add_argument("input", help="Input video file")
    parser.add_argument("-o", "--output", required=True, help="Output .pcv file")
    parser.add_argument("--fps", type=int, default=12)
    parser.add_argument("--width", type=int, default=320)
    parser.add_argument("--height", type=int, default=320)
    parser.add_argument("--rle", action="store_true", help="Enable RLE compression")
    parser.add_argument("--no-audio", action="store_true")
    parser.add_argument("--audio-rate", type=int, default=11025)
    parser.add_argument("--audio-bits", type=int, default=8, choices=[8, 16])
    parser.add_argument("--audio-channels", type=int, default=1, choices=[1, 2])
    parser.add_argument("--index", action="store_true", help="Add seek index")
    parser.add_argument("--loop", action="store_true", help="Set loop flag")
    args = parser.parse_args()

    print(f"Input:  {args.input}")
    print(f"Output: {args.output}")
    print(f"Target: {args.width}x{args.height} @ {args.fps} FPS")

    with tempfile.TemporaryDirectory() as tmpdir:
        # Extract frames
        print("Extracting frames...")
        frames = extract_frames(args.input, args.fps, tmpdir)
        total_frames = len(frames)
        print(f"  {total_frames} frames extracted")

        if total_frames == 0:
            print("ERROR: No frames extracted")
            sys.exit(1)

        # Extract audio
        audio_chunks = None
        has_audio = not args.no_audio
        if has_audio:
            print("Extracting audio...")
            try:
                audio_chunks = extract_audio(
                    args.input, args.audio_rate, args.audio_bits,
                    args.audio_channels, args.fps, total_frames, tmpdir
                )
                print(f"  {len(audio_chunks)} audio chunks")
            except subprocess.CalledProcessError:
                print("  No audio track found, disabling audio")
                has_audio = False

        # Build PCV file
        flags = 0
        if args.rle:   flags |= PCV_FLAG_RLE
        if has_audio:   flags |= PCV_FLAG_AUDIO
        if args.index:  flags |= PCV_FLAG_INDEX
        if args.loop:   flags |= PCV_FLAG_LOOP

        print(f"Writing .pcv (flags=0x{flags:02X})...")

        with open(args.output, "wb") as f:
            # Write header (placeholder — will rewrite index_offset later)
            header = struct.pack(
                "<4sHHBHBBIBI10s",
                PCV_MAGIC,
                args.width,
                args.height,
                args.fps,
                args.audio_rate if has_audio else 0,
                args.audio_bits if has_audio else 0,
                args.audio_channels if has_audio else 0,
                total_frames,
                flags,
                0,  # index_offset placeholder
                b"\x00" * 10
            )
            f.write(header)

            index_entries = []

            for i, frame_path in enumerate(frames):
                if i % 100 == 0:
                    print(f"  Frame {i}/{total_frames}...")

                # Record index every 1 second
                if args.index and i % args.fps == 0:
                    index_entries.append((i, f.tell()))

                # Convert frame to RGB565
                img = Image.open(frame_path)
                pixel_data = frame_to_rgb565(img, args.width, args.height)

                if args.rle:
                    pixel_data = rle_encode(pixel_data)

                # Write frame chunk
                f.write(struct.pack("<I", len(pixel_data)))
                f.write(pixel_data)

                # Write audio chunk
                if has_audio and audio_chunks:
                    audio_data = audio_chunks[i] if i < len(audio_chunks) else b""
                    f.write(struct.pack("<H", len(audio_data)))
                    f.write(audio_data)
                else:
                    f.write(struct.pack("<H", 0))

            # Write index table if requested
            index_offset = 0
            if args.index and index_entries:
                index_offset = f.tell()
                f.write(struct.pack("<I", len(index_entries)))
                for frame_num, file_offset in index_entries:
                    f.write(struct.pack("<II", frame_num, file_offset))

            # Rewrite header with correct index_offset
            if index_offset > 0:
                f.seek(18)  # offset of index_offset field
                f.write(struct.pack("<I", index_offset))

        file_size = os.path.getsize(args.output)
        print(f"Done! Output: {args.output} ({file_size:,} bytes)")
        print(f"  {total_frames} frames, "
              f"{'RLE' if args.rle else 'raw'}, "
              f"{'audio' if has_audio else 'no audio'}")


if __name__ == "__main__":
    main()
