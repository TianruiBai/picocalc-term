# pcv-convert

Converts standard video files to the PicoCalc Video (.pcv) format for playback on the PicoCalc handheld terminal.

## Requirements

- **Python 3.8+**
- **FFmpeg** (must be in PATH)
- **Pillow** (`pip install Pillow`)
- **NumPy** (`pip install numpy`)

## Installation

```bash
pip install Pillow numpy
```

Ensure FFmpeg is installed and accessible:
```bash
ffmpeg -version
```

## Usage

### Basic conversion
```bash
python pcv_convert.py input.mp4 -o output.pcv
```

### With RLE compression and custom settings
```bash
python pcv_convert.py input.mp4 -o output.pcv \
    --fps 12 \
    --width 320 --height 320 \
    --rle \
    --audio-rate 11025 \
    --audio-bits 8 \
    --index
```

### No audio
```bash
python pcv_convert.py input.mp4 -o output.pcv --no-audio --fps 15
```

### Looping video
```bash
python pcv_convert.py animation.mp4 -o loop.pcv --rle --loop
```

## Options

| Option | Default | Description |
|---|---|---|
| `--fps N` | 12 | Target frame rate |
| `--width N` | 320 | Target width in pixels |
| `--height N` | 320 | Target height in pixels |
| `--rle` | off | Enable RLE compression (30-70% smaller) |
| `--no-audio` | off | Omit audio track |
| `--audio-rate N` | 11025 | Audio sample rate (Hz) |
| `--audio-bits N` | 8 | Bits per sample (8 or 16) |
| `--audio-channels N` | 1 | 1=mono, 2=stereo |
| `--index` | off | Add seek index table |
| `--loop` | off | Set loop flag in header |

## Output Size Estimates

For 320×320 @ 12 FPS:

| Mode | Per Frame | Per Minute | Per 5 Min |
|---|---|---|---|
| Raw, no audio | 200 KB | 140 MB | 703 MB |
| RLE (~50%), no audio | 100 KB | 70 MB | 351 MB |
| RLE, 11kHz 8-bit audio | 101 KB | 71 MB | 356 MB |

For SD card playback, RLE compression is strongly recommended.

## See Also

- [PCV Format Spec](../../docs/PCV%20Format%20Spec.md) — Full format specification
