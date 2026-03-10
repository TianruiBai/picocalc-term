# PicoCalc-Term

A full-featured handheld terminal OS for the [ClockworkPi PicoCalc](https://www.clockworkpi.com/picocalc), powered by a **Waveshare RP2350B-Plus-W** board running **Apache NuttX RTOS** with **LVGL** GUI.

## Features

- **Full GUI OS** with status bar, app launcher, and window management
- **9 built-in applications**: Settings, Text Editor, CSV Editor, Audio Player, Video Player, Local Terminal, SSH Client, Web Browser
- **Package system** for third-party apps (`.pcpkg` format)
- **App state save/restore** — suspend and resume apps without losing work
- **Wi-Fi networking** via CYW43439 (Wi-Fi 4 + BT 5.2)
- **Dual-core architecture** — Core 0 for UI/apps, Core 1 for audio/background tasks

## Hardware

| Component | Specification |
|---|---|
| MCU | RP2350B (dual Cortex-M33 / Hazard3, 150 MHz) |
| SRAM | 520 KB |
| Flash | 16 MB |
| PSRAM | 8 MB (PicoCalc mainboard) |
| Display | 320×320 IPS LCD, ST7365P controller, SPI |
| Keyboard | 67-key QWERTY, STM32 south-bridge (I2C) |
| Audio | Dual PWM speakers + 3.5mm jack |
| Wireless | Wi-Fi 4 + BT 5.2 (CYW43439) |
| Storage | SD card (FAT32) |
| Battery | 18650 Li-ion |

## Documentation

- [Project Plan](docs/Project%20Plan.md) — Architecture, RTOS selection, app design
- [Detailed Implementation Plan](docs/Detailed%20Implementation%20Plan.md) — Phase-by-phase breakdown with code samples
- [App Framework API](docs/App%20Framework%20API.md) — API reference for app development
- [PCV Format Spec](docs/PCV%20Format%20Spec.md) — `.pcv` video container format

## Building

### Requirements

- **Linux environment** (GitHub Codespaces, local Linux, or WSL2 Ubuntu 22.04+)
- **ARM GNU Toolchain + picotool** — arm-none-eabi-gcc 13+ and UF2 tool (installed by `make setup`)
- Python 3.8+, CMake, make (installed by `make setup`)
- Target: **RP2350B** (ARM Cortex-M33, 150 MHz)

### Quick Start (Codespaces / Linux)

All commands run in a Linux terminal:

```bash
cd /workspaces/picocalc-term  # Codespaces path (or your local repo path)

# One-time setup: install toolchain + dependencies
make setup

# One-time: configure NuttX for PicoCalc RP2350B
make configure

# Build firmware
make build

# Check output
make uf2
```

The output is `nuttx/nuttx.uf2` — copy it to the RP2350 USB mass-storage drive to flash.

### Build Targets

```bash
make setup              # install ARM toolchain + build deps (one-time)
make fetch-nuttx        # clone apache/nuttx if missing
make fetch-nuttx-apps   # clone apache/nuttx-apps if missing
make sync-board         # copy custom board into nuttx tree
make configure          # configure NuttX board (one-time)
make build              # incremental build (recommended)
make build JOBS=4 V=1   # verbose, limit parallelism
make rebuild            # clean objects + build
make clean              # remove build artifacts
make distclean          # full reset (removes .config too)
make menuconfig         # interactive Kconfig editor
make fix-eol            # normalize CRLF→LF after Windows edits
make tips               # WSL performance tips
```

### Using build_nuttx.sh Directly

For more control, use the build script directly:

```bash
# Full configure + build
./tools/wsl/build_nuttx.sh

# Incremental rebuild (skip configure)
SKIP_CONFIGURE=1 ./tools/wsl/build_nuttx.sh

# Clean build with 4 jobs
CLEAN=1 JOBS=4 ./tools/wsl/build_nuttx.sh
```

### Troubleshooting

**`Make.dep:2: *** multiple target patterns. Stop.`**
Stale dependency files from a previous Windows/MSYS2 build contain `C:/` paths.
Fix: `make clean-deps` or `find nuttx/ nuttx-apps/ -name 'Make.dep' -delete`

**`/usr/bin/env: 'bash\r': No such file or directory`**
Windows CRLF line endings in shell scripts. Fix: `make fix-eol`

**`arm-none-eabi-gcc: not found`**
Toolchain not installed or not in PATH. Fix: `make setup` (or re-open terminal after setup)

**`kconfig-conf: command not found`**
Missing host dependency (`kconfig-frontends`). Fix: `make setup`

### Performance Tips

- **Best speed**: keep the source tree in WSL ext4 (`~/src/picocalc-term`) instead of `/mnt/c/...`
- Use `JOBS=6` to `JOBS=10` to balance CPU and I/O
- Keep builds incremental (`make build`); avoid `distclean` unless config changed
- If using Codespaces, prefer persistent volumes/caches for toolchains

## Project Structure

```
picocalc-term/
├── docs/           # Project documentation
├── boards/         # NuttX board definition (picocalc-rp2350b)
├── pcterm/         # OS framework layer (app framework, launcher, status bar)
├── apps/           # Built-in applications (9 apps)
├── tools/          # Host-side tools (pcv-convert, pcpkg-create)
├── sdk/            # Third-party app development SDK
└── sdcard/         # Default SD card image contents
```

## License

TBD
