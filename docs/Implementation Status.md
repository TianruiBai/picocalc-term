# PicoCalc-Term: Implementation Status & Roadmap

> **ARCHIVED** — This document tracks the original PicoCalc-Term sessions.
> The project has been renamed to **eUX OS**. For current status, see
> [eUX Implementation Plan.md](eUX%20Implementation%20Plan.md) and
> [Stub Audit & Locations.md](Stub%20Audit%20%26%20Locations.md).

**Last Updated:** Session 3 (TermOS Packaging Validation)

---

## Build Profiles

| Profile | Status | Flash | RAM | Description |
|---------|--------|-------|-----|-------------|
| `nsh` | **BUILDS** | 108 KB | 19 KB | Minimal NuttShell + diagnostics + minipkg/wifi/dmesg |
| `full` | **BUILDS** | 1.29 MB | 316 KB | Full TermOS launcher + built-in apps + CLI tools |
| `lvgl` | Untested | — | — | LVGL + display + input (no apps) |

Build command: `make configure BOARD_CONFIG=picocalc-rp2350b:full && make build`

---

## 1. Critical Boot Fixes (Completed)

### 1.1 Audio Init Freeze — FIXED

**Root Cause:** Initial mapping/slice mismatch during bring-up.

| Parameter | Was (broken) | Now (fixed) | Source |
|-----------|-------------|-------------|--------|
| `BOARD_AUDIO_PIN_LEFT` | GPIO 40 | GPIO 40 | `board.h` |
| `BOARD_AUDIO_PIN_RIGHT` | GPIO 41 | GPIO 41 | `board.h` |
| `AUDIO_PWM_SLICE` | 4 | 5 | `rp23xx_audio.c` |

Final hardware mapping for this project target is GPIO40 (L) and GPIO41 (R) as validated on RP2350B-Plus-W.

### 1.2 PSRAM Probe Failure — FIXED

**Root Cause:** Driver used 1-bit SPI PIO program. The PicoCalc v2.0 PSRAM requires 4-bit QSPI.

**Fix Applied:** Ran `tools/patch_psram_qspi.py` which converted `rp23xx_psram.c`:

- **Init sequence:** Bitbang SPI for Reset Enable (0x66) → Reset (0x99) → Enter QPI (0x35)
- **PIO program:** 11-instruction QSPI program with 4-bit I/O, CS+SCK sideset
- **Data path:** Nibble-based FIFO protocol for read/write through QSPI PIO SM
- **Commands:** `PSRAM_QCMD_WRITE=0x02`, `PSRAM_QCMD_FAST_READ=0xEB`
- **Bulk transfers:** Chunked with `QSPI_MAX_WRITE_DATA=123`, `QSPI_MAX_READ_DATA=120` bytes

### 1.3 Boot Code Cleanup — FIXED

`rp23xx_boot.c` had three dead-code issues:
- `#include "rp23xx_psram.h"` — file doesn't exist
- `CONFIG_RP23XX_PSRAM` guard — wrong symbol (should be `CONFIG_PICOCALC_PSRAM`)
- `rp23xx_psramconfig()` call — wrong function name (should be `rp23xx_psram_init()`)

All removed. PSRAM initialization correctly occurs in `rp23xx_bringup.c`.

### 1.4 Duplicate Defines — FIXED

`board.h` had duplicate `#define` for `SB_CFG_OVERFLOW_ON` and `SB_CFG_USE_MODS`. Duplicates removed.

---

## 2. Build System Fixes (Completed)

### 2.1 wolfSSL Makefile — FIXED

- **SSH→HTTPS:** Changed `git clone git@github.com:wolfSSL/...` to `https://github.com/wolfSSL/...` in `nuttx-apps/crypto/wolfssl/Makefile` (Codespaces has no SSH keys)
- **distclean target conflict:** Changed `distclean:` (single-colon) to `distclean::` (double-colon) to match the other branch

### 2.2 CONFIG_SPINLOCK — REMOVED

`CONFIG_SPINLOCK=y` requires `up_testset()` which is only implemented for SMP (multi-core NuttX). Removed from full defconfig since we run single-core.

### 2.3 Entry Point — INTEGRATED

`CONFIG_INIT_ENTRYPOINT` is `pcterm_main` in the full profile, and `CONFIG_EXAMPLES_PCTERM=y` is enabled.

TermOS is built as a NuttX app in `nuttx-apps/examples/pcterm` and links core OS modules plus all built-in GUI apps from `apps/` and `pcterm/src/`.

---

## 3. Dynamic Frequency Scaling (Completed)

**File:** `rp23xx_clockmgr.c`

Five PLL SYS profiles via safe PLL reconfiguration (switch to XOSC → reprogram PLL → switch back):

| Profile | Frequency | FBDIV | PD1 | PD2 | VCO | Use Case |
|---------|-----------|-------|-----|-----|-----|----------|
| Standard | 150 MHz | 125 | 5 | 2 | 1500 MHz | Default operation |
| High Performance | 200 MHz | 100 | 3 | 2 | 1200 MHz | Heavy computation |
| Power Save | 100 MHz | 100 | 6 | 2 | 1200 MHz | Battery saving |
| Max Boost | 300 MHz | 150 | 3 | 2 | 1800 MHz | Burst workloads |
| Overclock | 400 MHz | 100 | 3 | 1 | 1200 MHz | Maximum performance* |

*400 MHz exceeds RP2350 rated spec (150 MHz). Use at user's own risk.

**API:**
```c
int rp23xx_set_power_profile(int profile);
uint32_t rp23xx_get_sys_freq_mhz(void);
int rp23xx_get_num_profiles(void);
const char *rp23xx_get_profile_name(int profile);
```

**CLI:** `clockset [standard|highperf|powersave|boost|overclock]`

---

## 4. System Commands (Completed)

All registered as NSH built-in commands in `nuttx-apps/system/`:

| Command | Description | Key Features |
|---------|-------------|-------------|
| `lscpu` | CPU/system info | Architecture, model, cores, FPU, memory, procfs data |
| `lsi2c` | I2C bus scanner | Scans /dev/i2c0..3, probes 0x03-0x77 (like i2cdetect) |
| `lsspi` | SPI bus info | Pin assignments, device list from Kconfig |
| `screenfetch` | System info display | NuttX ASCII art + system details |
| `clockset` | CPU frequency control | Interface to rp23xx_clockmgr profiles |

Also enabled from upstream NuttX:
- `i2c` — NuttX I2C tool (get/set/dump)
- `spi` — NuttX SPI tool

---

## 5. Scripting Language Support (Completed)

### 5.1 Decision: Lua + QuickJS (not CPython)

CPython requires `CONFIG_INTERPRETERS_CPYTHON_STACKSIZE=307200` (300 KB) — over half of the RP2350's 520 KB SRAM. Not viable for this platform.

| Interpreter | Config | Stack | Use Case |
|-------------|--------|-------|----------|
| **Lua 5.4** | `CONFIG_INTERPRETERS_LUA=y` | 32 KB | Lightweight scripting, app automation |
| **QuickJS** | `CONFIG_INTERPRETERS_QUICKJS=y` | 16 KB | ES2020 JavaScript, mini mode |

Lua core libraries enabled (`CONFIG_INTERPRETERS_LUA_CORELIBS=y`): math, io, os, string, table, coroutine.

### 5.2 Usage

```
nsh> lua
Lua 5.4.0  Copyright (C) 1994-2020 Lua.org, PUC-Rio
> print("Hello from PicoCalc!")

nsh> qjs
QuickJS - Type "\h" for help
qjs > console.log(2 + 2)
```

---

## 6. Multi-Threading (Completed)

POSIX pthread support enabled in both profiles:

| Config | Purpose |
|--------|---------|
| `CONFIG_PTHREAD_MUTEX_TYPES=y` | Normal, recursive, errorcheck mutex types |
| `CONFIG_PTHREAD_MUTEX_BOTH=y` | Both robust and non-robust mutexes |
| `CONFIG_PTHREAD_STACK_DEFAULT=4096` | Default thread stack |
| `CONFIG_CANCELLATION_POINTS=y` | POSIX cancellation points |
| `CONFIG_SCHED_HAVE_PARENT=y` | Parent-child process tracking |
| `CONFIG_SIG_DEFAULT=y` | Default signal handlers |
| `CONFIG_SCHED_CPULOAD=y` | CPU load monitoring |
| `CONFIG_SCHED_WAITPID=y` | waitpid() support |

---

## 7. Filesystem & procfs (Completed in Config)

| Config | Purpose |
|--------|---------|
| `CONFIG_FS_PROCFS=y` | /proc filesystem |
| `CONFIG_FS_PROCFS_REGISTER=y` | Custom procfs entries |
| `CONFIG_FS_FAT=y` | FAT16/FAT32 for SD card |
| `CONFIG_FAT_LFN=y` | Long filename support |
| `CONFIG_FS_ROMFS=y` | ROM filesystem |

SD card auto-mount is implemented in bringup (`/dev/mmcsd0` -> `/mnt/sd`).

---

## 8. Remaining Work

### 8.1 pcterm Application Integration (Completed)

TermOS is integrated through `nuttx-apps/examples/pcterm` and is included in full firmware builds.

Verified by:
- `CONFIG_EXAMPLES_PCTERM=y`
- `CONFIG_INIT_ENTRYPOINT="pcterm_main"`
- Linked app symbols: `g_settings_app`, `g_pcedit_app`, `g_pccsv_app`, `g_pcaudio_app`, `g_pcvideo_app`, `g_pcterm_local_app`, `g_pcterm_serial_app`, `g_pcssh_app`, `g_pcwireless_app`, `g_pcweb_app`, `g_pcfiles_app`

### 8.2 Built-in Applications (Packed Into Firmware)

The following built-in applications are linked into full firmware:
- **settings** — System settings UI
- **pcedit** — vi-style text editor
- **pccsv** — CSV spreadsheet viewer/editor
- **pcaudio** — MP3/WAV audio player
- **pcvideo** — Custom .pcv video player
- **pcfiles** — File manager
- **pcterm** — Local terminal (NSH)
- **pcssh** — SSH/SCP/SFTP client (needs wolfSSL)
- **pcweb** — Text-based web browser
- **pcwireless** — WiFi manager

Runtime feature completeness still varies by module, but all app descriptors are present in the final ELF image.

### 8.3 SD Card Auto-Mount (Completed)

Bring-up now mounts `/dev/mmcsd0` at `/mnt/sd` when available.

### 8.4 LVGL Profile Testing (Not Tested)

The `lvgl` defconfig has been updated but never build-tested.

### 8.5 WiFi Driver (Not Started)

The RP2350B-Plus-W has CYW43439 WiFi. NuttX has some RP2040 WiFi support but RP2350 WiFi driver integration is needed.

---

## File Inventory

### Modified Files

| File | Changes |
|------|---------|
| `boards/.../include/board.h` | Fixed audio pins (40→26, 41→27), removed duplicate defines |
| `boards/.../src/rp23xx_audio.c` | PWM slice 4→5 |
| `boards/.../src/rp23xx_boot.c` | Removed dead PSRAM code |
| `boards/.../src/rp23xx_psram.c` | Full SPI→QSPI conversion (11 patches) |
| `boards/.../src/rp23xx_clockmgr.c` | Added 300/400 MHz profiles, helper functions |
| `boards/.../configs/full/defconfig` | Threading, interpreters, commands, entry point fix |
| `boards/.../configs/nsh/defconfig` | System commands, threading, diagnostics |
| `nuttx-apps/crypto/wolfssl/Makefile` | SSH→HTTPS URLs, distclean fix |
| `tools/patch_psram_qspi.py` | Fixed hardcoded path |

### Created Files

| File | Purpose |
|------|---------|
| `nuttx-apps/system/lscpu/*` | CPU information command |
| `nuttx-apps/system/lsi2c/*` | I2C bus scanner command |
| `nuttx-apps/system/lsspi/*` | SPI bus information command |
| `nuttx-apps/system/screenfetch/*` | System info with ASCII art |
| `nuttx-apps/system/clockset/*` | CPU frequency control command |

---

## Build Quick Reference

```bash
# First-time setup
make setup

# Configure and build nsh (lightweight)
make configure BOARD_CONFIG=picocalc-rp2350b:nsh
make build

# Configure and build full (all features)
make configure BOARD_CONFIG=picocalc-rp2350b:full
make build

# Output
ls -la nuttx/nuttx.uf2

# Flash: hold BOOTSEL, plug USB, copy nuttx.uf2 to RPI-RP2 drive
```
