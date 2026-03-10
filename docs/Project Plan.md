# PicoCalc-Term: A Handheld Terminal OS for PicoCalc

> **ARCHIVED** — This document is superseded by [eUX Project Plan.md](eUX%20Project%20Plan.md).
> The project has been renamed from PicoCalc-Term to **eUX OS** (Embedded Unix).
> Key changes: PSRAM is now XIP-mapped (not PIO-driven), entry point is `eux_init`,
> runit service manager, ROMFS root filesystem. See the new docs for current info.

## Project Overview

Build a lightweight, handheld terminal operating system with a small GUI for the [ClockworkPi PicoCalc](https://www.clockworkpi.com/picocalc), replacing the stock Pico 2 W with a **Waveshare RP2350B-Plus-W** for expanded capabilities.

### Goals

- **GUI-based OS** — not a plain text terminal, but a graphical desktop environment (LVGL) with a launcher, status bar, and fullscreen applications — like a real OS on a tiny device
- **Package System** — applications are packed into installable packages on the SD card; third-party apps can be developed, distributed, and installed just like a normal computer
- **System Settings** — centralized configuration for Wi-Fi, display, keyboard, audio, hostname, and system info
- **Office Suite** — text editor (vi-style) and table/spreadsheet editor (CSV read/write)
- **Entertainment** — audio player (MP3/WAV), video player (custom MCU-optimized format from SD card)
- **Local Terminal** — built-in terminal emulator with vi editor, running NuttShell
- **Remote SSH Terminal** — wireless SSH client with SCP file transfer and SFTP file management
- **Web Browser** — textual web browser for browsing the web on a 320x320 screen
- **Hostname & Network Identity** — the device has a configurable hostname, visible on the network and in the shell prompt

## Hardware Platform

### ClockworkPi PicoCalc (v2.0 Mainboard)

| Component | Specification |
|---|---|
| Screen | 4-inch IPS, 320x320, SPI interface, **ST7365P** controller (ILI9488-compatible) |
| Keyboard | 67-key QWERTY, I2C interface (STM32 south-bridge) |
| Storage | SD card slot |
| PSRAM | 8MB onboard (CPI v2.0 mainboard) |
| Audio | Dual PWM speakers + 3.5mm phone jack |
| Power | 18650 Li-ion battery holder + charge/discharge management |
| Backlight | Keyboard & screen backlight managed by STM32 |

### Core Module: Waveshare RP2350B-Plus-W (replacing Pico 2 W)

| Feature | Pico 2 W (stock) | RP2350B-Plus-W |
|---|---|---|
| MCU | RP2350A | **RP2350B** |
| Architecture | Dual Cortex-M33 / Dual Hazard3 RISC-V @ 150MHz | Same |
| SRAM | 520KB | 520KB |
| Flash | 4MB | **16MB** |
| GPIO | 26 | **41** |
| PIO State Machines | 12 (3 PIO blocks) | 12 (3 PIO blocks) |
| ADC | 3 channels (12-bit) | **6 channels** (12-bit) |
| PWM | 16 channels | **22 channels** |
| HSTX | N/A | **1x HSTX** |
| Wi-Fi | 802.11n (CYW43439) | Wi-Fi 4 (Radio Module 2, CYW43439) |
| Bluetooth | 5.2 | 5.2 |
| USB | Micro USB | **Type-C** |
| PSRAM | N/A | **Reserved solder pads** |

**Key upgrade benefits:** 4x flash (16MB), more GPIOs (41 vs 26), more ADC/PWM channels, HSTX port, USB-C, and PSRAM expansion pads.

### Display: ST7365P Controller

The PicoCalc uses a 4-inch 320x320 IPS panel driven by the **ST7365P** controller, which is ILI9488-command-compatible. The existing PicoCalc codebase references it as `ILI9488` in the driver layer.

| Parameter | Value |
|---|---|
| Resolution | 320 x 320 pixels |
| Color Depth | 16-bit RGB565 (native 24-bit, pixel-packed to 16-bit over SPI) |
| Interface | SPI1 @ 25 MHz |
| SPI SCK | GPIO 10 |
| SPI TX (MOSI) | GPIO 11 |
| SPI RX (MISO) | GPIO 12 |
| CS | GPIO 13 |
| DC (Data/Command) | GPIO 14 |
| RST (Reset) | GPIO 15 |
| Pixel Format | BGR ordering |
| Spec Sheet | [ST7365P_SPEC_V1.0.pdf](https://github.com/clockworkpi/PicoCalc/blob/master/ST7365P_SPEC_V1.0.pdf) |

**Framebuffer strategy:** At 320x320x2 bytes (RGB565), a full framebuffer is **200KB** — too large for 520KB SRAM alone. The approach:

1. **Partial framebuffer in SRAM** — allocate 1/10 screen buffer (~20KB) for LVGL draw buffer, flush via SPI DMA
2. **Full framebuffer in PSRAM** (optional) — the 8MB PSRAM on the CPI v2.0 mainboard can hold multiple full frames; use for double-buffering or compositing when GUI needs it
3. **DMA-accelerated SPI flush** — use RP2350 DMA to send framebuffer lines to ST7365P while CPU continues rendering next lines

Existing reference code:
- [picocalc_helloworld/lcdspi/](https://github.com/clockworkpi/PicoCalc/tree/master/Code/picocalc_helloworld/lcdspi) — bare-metal SPI display driver
- [picocalc_lvgl_graphics_demo/](https://github.com/clockworkpi/PicoCalc/tree/master/Code/picocalc_lvgl_graphics_demo) — LVGL v8.3 port with ILI9488 display and I2C keyboard input

---

## RTOS Selection: NuttX vs Zephyr

### Comparison Matrix

| Criteria | NuttX | Zephyr |
|---|---|---|
| **RP2350 Support Status** | Experimental / WIP | Maintained |
| **Supported Boards** | Pico 2, XIAO RP2350 | Pico 2, RP2350-Zero, + more |
| **RP2350B-Plus-W Board Def** | Not available (custom needed) | Not available (custom needed) |
| **Architecture** | ARM Cortex-M33 + RISC-V | ARM Cortex-M33 + RISC-V |
| **POSIX Compliance** | Full POSIX.1 | Partial (POSIX subsystem) |
| **Built-in Shell** | NuttShell (nsh) - full Unix-like shell | Zephyr Shell - command-based |
| **VFS / Filesystem** | Full VFS (FAT, littlefs, NFS, etc.) | File system subsystem (FAT, littlefs) |
| **Networking Stack** | Mature TCP/IP (BSD sockets) | Full stack (BSD sockets API) |
| **CYW43439 Wi-Fi** | Less documented for RP2350 | Documented (blob fetch via `west`) |
| **USB Status (RP2350)** | Experimental (data corruption) | Device controller supported |
| **PSRAM Support** | 3 heap modes (single/user/separate) | Not documented for RP2350 |
| **RAM Footprint** | Smaller (~30-60KB kernel) | Larger (~50-100KB kernel) |
| **I2C Driver** | Working | Working |
| **SPI Driver** | Working | Working |
| **PWM Driver** | Working | Working |
| **PIO Driver** | Working | Working |
| **DMA Driver** | Working | Working |
| **Build System** | make / CMake | west + CMake + Kconfig + Devicetree |
| **License** | Apache 2.0 | Apache 2.0 |
| **Community Size** | Moderate (Apache project) | Large (Linux Foundation) |

### Analysis for a Terminal OS

#### NuttX Strengths

1. **NuttShell (nsh) is purpose-built for this.** It provides a real Unix-like shell with pipes, redirection, scripting, environment variables, job control, and built-in commands (`ls`, `cat`, `cp`, `mount`, `ifconfig`, `ps`, etc.). This *is* a terminal OS out of the box.

2. **Full POSIX compliance.** Standard C library (`stdio`, `stdlib`, `unistd`, `pthread`), file descriptors, `open()`/`read()`/`write()`/`close()`, `fork()`-like task creation. Porting Unix CLI tools is straightforward.

3. **VFS architecture.** Mount SD card as `/mnt/sd`, access serial as `/dev/ttyS0`, treat everything as a file - exactly what a terminal OS expects.

4. **PSRAM heap modes are well-documented.** Three options (single heap, user/kernel split, separate heap) give fine-grained control over 8MB PSRAM on PicoCalc mainboard.

5. **Smaller footprint.** Leaves more of the 520KB SRAM for applications and buffers (important for terminal emulation + display framebuffer).

6. **Simpler build system.** `make` based, lower barrier to iterate quickly.

#### NuttX Weaknesses

1. **"Experimental" RP2350 port.** Explicitly marked work-in-progress.
2. **USB has data corruption issues** on RP2350.
3. **CYW43439 Wi-Fi driver is less mature** for this platform.
4. **Smaller community** = fewer RP2350-specific examples and troubleshooting.

#### Zephyr Strengths

1. **More mature RP2350 support.** "Maintained" status, more tested peripheral drivers.
2. **Better Wi-Fi out of box.** CYW43439 firmware blob process is documented, Wi-Fi shell sample works on Pico 2W.
3. **Devicetree-based hardware abstraction.** Clean way to define custom board (RP2350B-Plus-W).
4. **Huge ecosystem.** More drivers, more middleware (Bluetooth, LwM2M, MQTT, etc.).
5. **USB device controller** working on RP2350.

#### Zephyr Weaknesses

1. **Shell is command-oriented, not Unix-like.** No pipes, no file redirection, no scripting. Building a "terminal OS" requires significant custom work on top.
2. **No POSIX-native design.** The POSIX layer is a compatibility shim, not the core API.
3. **Larger kernel footprint.** Less free RAM for applications.
4. **Complex build system.** west + CMake + Kconfig + Devicetree has a steep learning curve.
5. **No documented PSRAM support for RP2350.**

### Recommendation: **NuttX**

For a **terminal operating system**, NuttX is the clear choice:

- NuttShell gives you 80% of the terminal OS for free - it's a real Unix shell.
- POSIX compliance means the programming model is `open()`/`read()`/`write()` on file descriptors, which is exactly how a terminal multiplexer, text editor, or SSH client expects to work.
- The VFS means SD card, serial ports, network sockets, and device drivers all appear as files in a unified namespace.
- Smaller footprint matters when you have 520KB SRAM + 8MB PSRAM and need to run a display framebuffer, keyboard input, and network stack simultaneously.

The "experimental" status is a manageable risk: GPIO, UART, I2C, SPI, DMA, PWM, PIO, and PSRAM all work. The main gaps (USB data corruption, Wi-Fi immaturity) are areas that will improve, and for a terminal OS the primary I/O is the built-in screen + keyboard, not USB.

If Wi-Fi connectivity is a day-one hard requirement, consider **starting with Zephyr** for Wi-Fi bring-up, then evaluate migrating to NuttX once its RP2350 Wi-Fi matures. But for the core terminal experience, NuttX is architecturally superior.

---

## Architecture (NuttX-based)

```
┌──────────────────────────────────────────────────────────────┐
│                    Installable Packages                       │
│  ┌────────┐┌────────┐┌────────┐┌────────┐┌────────┐┌──────┐ │
│  │Settings││ pcedit ││ pccsv  ││pcaudio ││pcvideo ││pcweb │ │
│  │  App   ││vi/text ││CSV/tbl ││MP3/WAV ││MCU vid ││browse│ │
│  └───┬────┘└───┬────┘└───┬────┘└───┬────┘└───┬────┘└──┬───┘ │
│  ┌───┴────┐┌───┴─────────┴─────────┴─────────┴────────┴───┐  │
│  │pcfiles ││  pcterm (local terminal, vi) │ pcssh (SSH)   │  │
│  │ files  ││  + NuttShell console         │ + SCP/SFTP    │  │
│  └───┬────┘└─────────────┬────────────────────────────────┘  │
│  ┌───┴───────────────────┴────────────────────────────────┐  │
│  │    App Framework + Package Manager + App Store         │  │
│  │  App lifecycle │ ELF loader │ Catalog fetch │ Events   │  │
│  └───────────────────┬────────────────────────────────────┘  │
│  ┌───────────────────┴────────────────────────────────────┐  │
│  │           GUI Layer (LVGL v8/v9) + Window Manager      │  │
│  │  Launcher │ Status Bar │ App Screens │ Notifications   │  │
│  └───────────────────┬────────────────────────────────────┘  │
│  ┌───────────────────┴────────────────────────────────────┐  │
│  │           NuttShell (nsh) + VFS + Hostname              │  │
│  │  /dev/fb0  /dev/input0  /mnt/sd  /dev/audio  /etc/*    │  │
│  └───────────────────┬────────────────────────────────────┘  │
├──────────────────────┼───────────────────────────────────────┤
│                    NuttX Kernel                               │
│  ┌─────────┐ ┌──────┴─────┐ ┌──────────┐ ┌──────────┐       │
│  │Framebuf │ │ Scheduler  │ │  TCP/IP  │ │  Audio   │       │
│  │ Driver  │ │ POSIX/pthd │ │  Stack   │ │  Subsys  │       │
│  └────┬────┘ └────────────┘ └────┬─────┘ └────┬─────┘       │
│  ┌────┴──────────────────────────┴────────────┴───────────┐  │
│  │                Hardware Drivers                         │  │
│  │  SPI1(ST7365P) │ I2C(KB+BL) │ SPI(SD) │ CYW43439(W+B) │  │
│  └────────────────┴────────────┴─────────┴────────────────┘  │
├──────────────────────────────────────────────────────────────┤
│  RP2350B (520KB SRAM) + CPI v2.0 (8MB PSRAM) + 16MB Flash   │
└──────────────────────────────────────────────────────────────┘
```

### Memory Map

```
┌─────────────────────────────────────────┐
│         520KB Internal SRAM             │
│  ┌────────────────────────────────────┐ │
│  │ NuttX Kernel + Drivers   (~60KB)  │ │
│  │ LVGL Draw Buffer (1/10)  (~20KB)  │ │
│  │ LVGL Core + Widgets      (~48KB)  │ │
│  │ TCP/IP Stack             (~40KB)  │ │
│  │ App Stack/Heap          (~350KB)  │ │
│  └────────────────────────────────────┘ │
├─────────────────────────────────────────┤
│          8MB External PSRAM             │
│  ┌────────────────────────────────────┐ │
│  │ Full Framebuffer 320x320  (200KB) │ │
│  │ Audio Decode Buffer       (64KB)  │ │
│  │ Video Frame Buffer       (200KB)  │ │
│  │ Text Editor Buffer       (512KB)  │ │
│  │ CSV Data Buffer            (1MB)  │ │
│  │ SSH Session Buffer       (256KB)  │ │
│  │ Available Heap           (~5.7MB) │ │
│  └────────────────────────────────────┘ │
├─────────────────────────────────────────┤
│          16MB External Flash            │
│  ┌────────────────────────────────────┐ │
│  │ NuttX Firmware + Builtin  (~2MB)  │ │
│  │ LVGL Fonts & Assets      (~1MB)   │ │
│  │ Wi-Fi Firmware Blobs     (~0.5MB) │ │
│  │ Available / LittleFS     (~12MB)  │ │
│  └────────────────────────────────────┘ │
├─────────────────────────────────────────┤
│          SD Card (user storage)         │
│  ┌────────────────────────────────────┐ │
│  │ /mnt/sd/                           │ │
│  │   apps/            (packages)     │ │
│  │   documents/       (text, csv)    │ │
│  │   music/           (mp3, wav)     │ │
│  │   video/           (pcv files)    │ │
│  │   ssh/             (keys, known)  │ │
│  │   etc/             (config)       │ │
│  └────────────────────────────────────┘ │
└─────────────────────────────────────────┘
```

## Key Interfaces to Implement

| Interface | Bus | NuttX Device | Details |
|---|---|---|---|
| Display (ILI9488) | SPI1 @ 25MHz | `/dev/fb0` | 320x320 IPS, RGB666 over SPI (3 bytes/pixel), MADCTL=0x48, inversion ON, DMA flush |
| STM32 South Bridge | I2C0 @ 0x1F | `/dev/sb0` | STM32F103R8T6: keyboard FIFO, AXP2101 battery, LCD/KB backlight, PA enable, HP detect, power control |
| Keyboard (67-key) | via South Bridge | `/dev/input0` | 2-byte FIFO protocol [state, keycode], capslock/numlock tracking |
| SD Card (SPI mode) | SPI | `/mnt/sd` | FAT32 filesystem, app data, audio files, documents |
| SD Card (PIO SDIO) | PIO1 1-bit SDIO | `/mnt/sd` | Optional CONFIG_PICOCALC_PIO_SDIO: CLK=GP18, CMD=GP19, DAT0=GP16, 25MHz |
| PSRAM (8MB) | PIO0 SPI | heap | PIO-driven SPI: CS=GP21, SCK=GP22, MOSI=GP3, MISO=GP2, bump allocator + free list |
| Wi-Fi (CYW43439) | SPI/PIO | `wlan0` | TCP/IP networking for SSH, HTTP |
| Audio (PWM) | PWM Slice 5 | `/dev/audio0` | GP26 left, GP27 right, MP3/WAV decode |
| LCD Backlight | via South Bridge | — | SB_REG_BKL (0x05), 0-255 brightness via PA8 PWM |
| KB Backlight | via South Bridge | — | SB_REG_BK2 (0x0A), 0-255 brightness via PC8 PWM |
| Battery | via South Bridge | — | SB_REG_BAT (0x0B), AXP2101 PMIC: percent + charging status |

### GPIO Pin Assignment (RP2350B-Plus-W)

| GPIO | Function | Notes |
|---|---|---|
| GP0 | UART0 TX | Debug console (115200 baud) |
| GP1 | UART0 RX | Debug console |
| GP2 | PSRAM MISO / SIO0 | PIO0 SM0 input |
| GP3 | PSRAM MOSI / SIO1 | PIO0 SM0 output |
| GP4 | PSRAM SIO2 | QSPI mode only |
| GP5 | PSRAM SIO3 | QSPI mode only |
| GP6 | I2C0 SDA | South bridge communication |
| GP7 | I2C0 SCL | South bridge communication |
| GP10 | SPI1 SCK | LCD SPI clock |
| GP11 | SPI1 TX (MOSI) | LCD data out |
| GP12 | SPI1 RX (MISO) | LCD data in (unused) |
| GP13 | LCD CS | Chip select |
| GP14 | LCD DC | Data/Command |
| GP15 | LCD RST | Reset |
| GP16 | SPI0 MISO / SDIO DAT0 | SD card data in (SPI) or DAT0 (PIO1) |
| GP17 | SD CS | SPI mode chip select |
| GP18 | SPI0 SCK / SDIO CLK | SD card clock (SPI or PIO1 sideset) |
| GP19 | SPI0 MOSI / SDIO CMD | SD card data out (SPI) or CMD (PIO1) |
| GP20 | (available) | — |
| GP21 | PSRAM CS | PIO0 SM0 sideset base |
| GP22 | PSRAM SCK | PIO0 SM0 sideset base+1 |
| GP23 | CYW43 WL_ON | Wi-Fi module power |
| GP24 | CYW43 SPI_D | Wi-Fi data |
| GP25 | CYW43 WL_CS | Wi-Fi chip select |
| GP26 | Audio Left | PWM slice 5, channel A |
| GP27 | Audio Right | PWM slice 5, channel B |
| GP28 | (available / ADC2) | — |
| GP29 | CYW43 SPI_CLK | Wi-Fi SPI clock |

## Package System

Applications are distributed as **packages** — self-contained bundles that live on the SD card and can be installed, updated, or removed like apps on a normal computer.

### Package Format (`.pcpkg`)

A package is a simple archive (tar or custom flat format) containing:

```
myapp.pcpkg
├── manifest.json       # metadata + entry point
├── app.elf             # NuttX-loadable ELF binary (or built-in name)
├── icon.bin            # 32x32 RGB565 icon (2KB)
├── assets/             # optional: fonts, images, data files
└── README.md           # optional: description
```

### manifest.json

```json
{
  "name": "myapp",
  "version": "1.0.0",
  "display_name": "My Application",
  "author": "developer",
  "entry": "app.elf",
  "icon": "icon.bin",
  "category": "utility",
  "requires": ["wifi", "audio"],
  "min_ram": 65536
}
```

### Package Manager

| Feature | Details |
|---|---|
| Install | Copy `.pcpkg` to `/mnt/sd/apps/`, package manager extracts and registers |
| Registry | `/mnt/sd/apps/registry.json` — list of installed packages |
| Launch | Launcher reads registry, displays icons, executes entry ELF |
| Remove | Delete package directory + unregister |
| Update | Replace package directory, bump version in registry |
| Built-in apps | System apps (Settings, Terminal, etc.) are built into firmware, not removable |
| Third-party | Developers build against a PicoCalc SDK (NuttX app + LVGL), produce `.pcpkg` |

### App Lifecycle

```
┌─────────┐   launch    ┌─────────┐   app_main()   ┌─────────┐
│Launcher │ ──────────→ │ Load ELF│ ────────────→  │ Running │
│(home)   │             │ + init  │                │  App    │
└─────────┘             └─────────┘                └────┬────┘
     ▲                                                  │
     │                                        exit / Fn+Home
     │                                                  │
     │                                                  ▼
     │    ┌──────────────────────────────────────────────────┐
     │    │              Save App State                      │
     │    │  Serialize UI state + working data to PSRAM      │
     │    │  Store as /mnt/sd/etc/appstate/<appname>.state   │
     │    └──────────────────────┬───────────────────────────┘
     │                           │
     │                           ▼
     │                    ┌──────────┐
     └────────────────────│ Launcher │
                          │ (return) │
                          └──────────┘

 On re-launch:
┌─────────┐  has state?  ┌──────────┐   restore()   ┌─────────┐
│Launcher │ ───yes──────→│ Load ELF │ ────────────→ │ Resumed │
│         │              │+ restore │               │  App    │
│         │ ───no───────→│ + fresh  │ ─app_main()─→│  (new)  │
└─────────┘              └──────────┘               └─────────┘
```

Apps run fullscreen. **Only one app is active at a time** due to the RP2350's resource constraints (520KB SRAM + 8MB PSRAM). When an app is backgrounded, its state is **saved** rather than discarded:

#### State Save/Restore Model

| Aspect | Details |
|---|---|
| Trigger | User presses `Fn+Home` or app calls `pc_app_yield()` |
| Save location | PSRAM buffer + persisted to `/mnt/sd/etc/appstate/<name>.state` |
| What is saved | App-defined state blob (cursor position, open file path, scroll offset, unsaved edits, playback position, URL, etc.) |
| State size limit | ~256KB per app (PSRAM budget) |
| Restore | On next launch, app receives saved state and resumes where it left off |
| Cold exit | `Ctrl+Q` or app's quit command = **discard state** (clean exit, no resume) |
| Stale state | If app version changes, state file is discarded (version mismatch) |
| API | Apps implement `pc_app_save(buf, len)` and `pc_app_restore(buf, len)` callbacks |

#### Per-App Save Examples

| App | Saved State |
|---|---|
| `pcedit` | File path, cursor line/col, scroll offset, unsaved buffer (if modified) |
| `pccsv` | File path, selected cell row/col, scroll position |
| `pcaudio` | Current track, playlist, playback position, volume |
| `pcvideo` | File path, frame position, paused/playing |
| `pcterm` | Scrollback buffer, current working directory, command history |
| `pcssh` | Connection info (host/user), reconnect on restore (session itself cannot persist) |
| `pcweb` | Current URL, scroll position, back/forward stack, form data |
| `settings` | Last viewed settings section |

Essential OS services that remain running in the background regardless:

| Service | Runs on | Purpose |
|---|---|---|
| LVGL tick + status bar | Core 0 | Display refresh, clock, battery, Wi-Fi indicator |
| Audio playback thread | Core 1 | Background music continues across app switches |
| Wi-Fi stack | Core 1 | Maintains connection, DHCP lease |
| Keyboard driver | Core 0 | Global hotkeys (Fn+Home = launcher) |

When the user exits an app via `Fn+Home`, the app's state is **saved to PSRAM and SD card**, then its memory is freed. On re-launch, the app restores from saved state and resumes where it left off. A hard quit (`Ctrl+Q`) discards the state for a fresh start next time.

---

## Applications

### Built-in System Apps

#### 1. Launcher / Home Screen

The desktop — first thing you see after boot.

| Feature | Details |
|---|---|
| UI | LVGL grid of app icons (from registry + built-ins) |
| Navigation | Arrow keys to select, Enter to launch, Esc to deselect |
| Status bar | Hostname, Wi-Fi status, battery level, clock |
| Quick actions | Fn+W = Wi-Fi toggle, Fn+B = brightness, Fn+V = volume |

#### 2. System Settings (`settings`)

Centralized configuration for the entire OS.

| Section | Settings |
|---|---|
| **Network** | Wi-Fi scan/connect/disconnect, saved networks, IP info |
| **Identity** | Hostname (editable, stored in `/mnt/sd/etc/hostname`) |
| **Display** | Brightness (backlight level), screen timeout |
| **Keyboard** | Backlight on/off/brightness, key repeat rate |
| **Audio** | Volume level, speaker enable/disable |
| **Storage** | SD card usage, format, eject |
| **System** | Firmware version, uptime, free RAM/PSRAM, reboot, about |
| **Packages** | List installed, remove, storage used per app |

Config is persisted in `/mnt/sd/etc/settings.json`.

### Office Suite

#### 3. Text Editor (`pcedit`)

A full vi/vim-style text editor with plugin ecosystem support.

| Feature | Details |
|---|---|
| Modes | **10 vi modes** — Normal, Insert, Visual, Visual-Line, Visual-Block, Command, Search-Forward, Search-Backward, Replace, Operator-Pending |
| Display | LVGL canvas with syntax highlighting, monospace font (6x12 → 53x24 chars) |
| Buffer | Gap buffer in PSRAM via `pcedit_buffer.c` (edit files up to ~4MB) |
| Multiple Buffers | Up to 8 open files, `:bn`/`:bp`/`:bd`/`:ls` buffer commands |
| File I/O | POSIX `open()`/`read()`/`write()` on `/mnt/sd/...` via `pcedit_file.c` |
| Encoding | UTF-8 |
| Status bar | Mode, filename, line/col, modified flag, recording indicator |
| **Vi Commands** | Full vi command set including: |
| | `:w` save, `:q` quit, `:wq`, `:q!`, `:e file`, `:w file` |
| | `:set option=value` (tabstop, shiftwidth, expandtab, number, etc.) |
| | `:%s/old/new/g` search and replace with regex |
| | `:marks`, `:reg`, `:jumps` — show marks, registers, jump list |
| | `:syntax on/off`, `:colorscheme`, `:noh` |
| | `:!cmd` shell commands, `:source file`, `:lua code` |
| | `:sp`/`:vs` splits, `:bn`/`:bp` buffers |
| **Navigation** | `h/j/k/l`, `w/b/e/W/B/E` word motions, `0/^/$` line, `gg/G` file |
| | `H/M/L` screen, `{/}` paragraph, `f/F/t/T/;/,` char find |
| | `Ctrl-D/U` half page, `Ctrl-F/B` full page scroll |
| | `Ctrl-O/Ctrl-I` jump list back/forward (50 entries) |
| **Operators** | `d` delete, `c` change, `y` yank, `>/<` indent, `gq` format |
| | `gU`/`gu` case change, `dd`/`cc`/`yy` line operators |
| | `D`/`C`/`Y`/`x`/`X`/`r`/`J`/`p`/`P` editing commands |
| **Registers** | 36 registers: `a-z` (named), `0-9` (numbered with shift) |
| | `"` default, `+` clipboard, `_` black hole, `/` search |
| **Marks** | 52 marks: `a-z` (local), `A-Z` (global/file), `'` last jump |
| **Macros** | Record: `q{reg}`, Play: `@{reg}`, up to 512 keys per macro |
| **Search** | `/` forward, `?` backward, `n/N` next/prev, `*/#` word search |
| | Regex support: `.`, `^`, `$`, `*`, `+`, `?`, `[abc]`, `\d`, `\w`, `\s` |
| | Incremental search, hlsearch match highlighting |
| **Syntax** | Built-in highlighting for C/C++, Python, Lua, Shell, Makefile, Markdown, JSON |
| | Token-based lexer: keywords, types, strings, comments, numbers, preprocessor |
| | Color scheme support via colorscheme_t struct |
| **Undo/Redo** | `u` undo, `Ctrl-R` redo (200-level linear history) |
| **Visual Mode** | `v` char, `V` line, `Ctrl-V` block selection |
| | Operators on selection: `d`/`c`/`y`/`>`/`<`/`u`/`U`/`~` |
| **Plugin System** | Lua scripting engine (via CONFIG_INTERPRETER_LUA) |
| | Hook system: on_open, on_save, on_key, on_mode_change, on_cmd, etc. |
| | Key mapping: `vim.keymap.set(mode, lhs, rhs, opts)` |
| | Autocommands: event/pattern/command with *.ext matching |
| | Plugin directory: `/sdcard/pcedit/plugins/<name>/init.lua` |
| | User config: `/sdcard/pcedit/init.lua` |
| | Editor options via `:set` with vim abbreviations (ts, sw, et, nu, etc.) |
| | vim.api: buf_get_lines, buf_set_lines, get_cursor, set_cursor, command |
| | Graceful fallback when Lua not available |
| **Source Files** | `pcedit_main.c` — App entry, integration, key dispatch, undo, file ops |
| | `pcedit_vi.c` — Full vi mode engine (~1400 lines) |
| | `pcedit_buffer.c` — Gap buffer with PSRAM allocation |
| | `pcedit_render.c` — Canvas rendering with syntax colors |
| | `pcedit_syntax.c` — Syntax highlighting for 7 languages |
| | `pcedit_search.c` — Search/replace with regex |
| | `pcedit_plugin.c` — Lua plugin ecosystem (~600 lines) |
| | `pcedit_file.c` — File I/O operations |

#### 4. Table/CSV Editor (`pccsv`)

Spreadsheet-like CSV editor.

| Feature | Details |
|---|---|
| Display | LVGL table widget (scrollable grid with headers) |
| Format | RFC 4180 CSV (comma-delimited, quoted strings) |
| Buffer | CSV parsed into row/col arrays in PSRAM (up to ~1MB data) |
| Editing | Arrow keys navigate cells, Enter to edit cell, Tab to next cell |
| Operations | Insert/delete row/col, sort by column, search across cells |
| File I/O | Read/write `.csv` files on SD card |
| Limits | ~10,000 rows practical limit at 320x320 display |
| New files | Create blank table, define columns, export to `.csv` |

### Entertainment

#### 5. Audio Player (`pcaudio`)

SD card audio playback through the PicoCalc's dual PWM speakers.

| Feature | Details |
|---|---|
| Formats | WAV (PCM 8/16-bit), MP3 (software decode via minimp3/libhelix) |
| Output | PWM audio via NuttX audio subsystem |
| Decode buffer | 64KB in PSRAM for streaming decode |
| UI | LVGL: file browser, play/pause/stop, progress bar, volume slider |
| Playlist | Load `.m3u` or auto-playlist from directory |
| Background | Audio continues playing when switching to other apps (Core 1) |
| Controls | Global hotkeys: Fn+Space = play/pause, Fn+→ = next, Fn+← = prev |

Reference: [PicoCalc MP3Player](https://github.com/clockworkpi/PicoCalc/tree/master/Code/MP3Player)

#### 6. Video Player (`pcvideo`)

Play pre-converted video files from the SD card on the 320x320 display.

| Feature | Details |
|---|---|
| Format | **`.pcv`** (PicoCalc Video) — custom MCU-optimized container |
| Video codec | Raw RGB565 frames or RLE-compressed RGB565, 320x320 or smaller |
| Audio track | Interleaved PCM 8-bit mono @ 8-22kHz |
| Frame rate | 10-15 FPS (SPI bandwidth limited) |
| Container | Simple sequential: `[header][frame+audio][frame+audio]...` |
| Decode | No complex codec — just read frame, DMA to display, simultaneously play audio |
| UI | Play/pause, seek (if keyframes indexed), progress bar |
| Converter | Offline PC tool: `pcv-convert input.mp4 -o output.pcv --fps 12 --res 320x320` |

**`.pcv` File Format:**

```
Header (32 bytes):
  magic:      "PCV1" (4 bytes)
  width:      u16
  height:     u16
  fps:        u8
  audio_rate: u16 (Hz)
  audio_bits: u8 (8 or 16)
  frames:     u32 (total frame count)
  flags:      u8 (0x01=RLE, 0x02=has_audio)
  reserved:   padding to 32 bytes

Frame Chunk (variable):
  frame_size: u32 (bytes of pixel data)
  pixel_data: [frame_size bytes] (RGB565 raw or RLE)
  audio_size: u16 (bytes of audio for this frame)
  audio_data: [audio_size bytes] (PCM)
```

The key insight: no complex codec (no H.264, no MJPEG decompression). Just pre-rendered frames at the target resolution, optionally RLE-compressed. The RP2350 reads frames sequentially from SD → PSRAM → DMA to display. Simple, predictable, guaranteed real-time.

### Terminals

#### 7. Local Terminal (`pcterm`)

Built-in terminal emulator running NuttShell locally on the device.

| Feature | Details |
|---|---|
| Shell | NuttShell (nsh) — full Unix-like shell |
| Display | VT100/ANSI terminal emulation on LVGL canvas |
| Font | Monospace 6x12 → ~53 cols x 26 rows |
| Editor | **vi** available as built-in command (NuttX `CONFIG_SYSTEM_VI`) |
| Commands | `ls`, `cat`, `cp`, `mv`, `rm`, `mkdir`, `mount`, `ps`, `kill`, `ifconfig`, `ping`, etc. |
| Scrollback | Ring buffer in PSRAM (~64KB, ~2000 lines) |
| Job control | Background tasks with `&`, `Ctrl+C` to interrupt |
| Prompt | `hostname:/mnt/sd$ ` — shows configured hostname |

#### 8. Remote SSH Terminal (`pcssh`)

Wireless SSH client with file transfer capabilities.

| Feature | Details |
|---|---|
| Protocol | SSH-2 (wolfSSH or dropbear) |
| Transport | Wi-Fi (CYW43439) → TCP socket → SSH |
| Display | VT100/ANSI terminal emulation, same renderer as `pcterm` |
| Font | Monospace 6x12 → ~53x26 chars |
| Auth | Password and SSH key-based (keys in `/mnt/sd/ssh/`) |
| SCP | `scp user@host:/remote/file /mnt/sd/local/file` — file copy over SSH |
| SFTP | Interactive SFTP file browser — browse remote files, download/upload |
| Sessions | Saved connections in `/mnt/sd/ssh/connections.json` |
| Known hosts | `/mnt/sd/ssh/known_hosts` — host key verification |
| Scrollback | Ring buffer in PSRAM (~64KB) |
| Logging | Optional session log to `/mnt/sd/documents/ssh_log_*.txt` |

### Web

#### 9. Web Browser (`pcweb`)

A text + image web browser for browsing the web on the 320x320 display.

| Feature | Details |
|---|---|
| Rendering | **Mixed text + image** — HTML parsed and rendered as styled text with inline images |
| Protocol | HTTP/1.1 and HTTPS (TLS via wolfSSL / mbedTLS) |
| HTML | Subset parser: headings, paragraphs, lists, links, tables, `<pre>`, `<img>`, basic forms |
| CSS | Minimal: bold, italic, colors where feasible on 16-bit display |
| JavaScript | **None** — too resource-intensive for RP2350 |
| **Images** | BMP decoder (RGB24/RGB32 uncompressed, auto-scaling to fit 308×240) |
| | PNG decoder (8-bit RGB/RGBA, zlib inflate or stored blocks, alpha compositing) |
| | Auto-detect format by file signature (BMP: `BM`, PNG: `\x89PNG`) |
| | Images decoded to LVGL canvas widgets (RGB565) inline with text |
| | Falls back to `[img: alt text]` placeholder on decode failure |
| | Max 16 images per page, URL resolution (absolute/relative/full) |
| | Direct image URL navigation (displays image fullscreen) |
| Navigation | Arrow keys scroll, Tab cycles links, Enter follows link, Backspace = back |
| Address bar | Top bar with URL input (keyboard entry) |
| Bookmarks | Saved in `/mnt/sd/etc/bookmarks.json` |
| History | Session history (back/forward stack), persisted across sessions |
| Downloads | Save files to `/mnt/sd/documents/` |
| Font | Monospace 6x12 for body, proportional for headings if available |
| Memory | HTML DOM + render buffer in PSRAM (~1-2MB per page) |
| TLS | wolfSSL or mbedTLS for HTTPS (shared with SSH library if wolfSSL) |
| User-Agent | `PicoCalc-pcweb/1.0` |
| **Source Files** | `pcweb_main.c` — App entry, navigation, URL handling |
| | `pcweb_http.c` — HTTP client with Accept: image/* |
| | `pcweb_html.c` — HTML parser with `<img>` src/alt extraction |
| | `pcweb_render.c` — Mixed text + image LVGL container renderer |
| | `pcweb_image.c` — BMP/PNG decoder to RGB565 LVGL canvas |

**Design rationale:** A full graphical browser (Chromium, WebKit) is impossible on an MCU. Instead, `pcweb` is closer to [w3m](https://w3m.sourceforge.net/) — a text-oriented browser with inline image support that renders HTML content in a readable, navigable form. This is useful for documentation sites, wikis, forums, weather, news, and simple web apps.

---

#### 10. File Explorer (`pcfiles`)

A built-in file manager for browsing, previewing, and managing files on SD card (`/mnt/sd`) and flash (`/data`).

| Feature | Details |
|---|---|
| Navigation | Arrow keys scroll, Enter opens dir/previews file, Backspace goes up |
| File list | Sorted listing with icons, file sizes, directory indicators |
| Root switching | Quick-swap between `/mnt/sd` (SD card) and `/data` (flash) |
| Preview | Text files previewed inline (first 1KB: .txt, .c, .h, .json, .csv, .md, etc.) |
| Copy/Move | Clipboard model: Copy (c), Cut (x), Paste (v) — works across filesystems |
| Delete | Recursive delete for files and directories |
| Create dir | Create new folders from the UI |
| Hidden files | Toggle to show/hide dotfiles (.hidden) |
| Sorting | Name, size, date — ascending/descending; directories always listed first |
| Memory | Entry cache for up to 256 files per directory |

**Source files:** `apps/pcfiles/pcfiles_main.c` (UI + navigation), `apps/pcfiles/pcfiles_ops.c` (file operations).

**UI layout (320×300):**
```
[Path bar: /mnt/sd/documents              ] ← cyan text, scrollable
[📁 ..                                     ]
[📁 music/                                 ]
[📁 projects/                              ]
[📄 readme.txt                     2.1KB   ]
[📄 notes.csv                      840B    ]
[Status: 2 dirs, 2 files (2.9KB)           ]
[Cp] [Cut] [Paste] [Del] [Dir] [Swap]       ← action buttons
```

---

### Enhanced Package Manager & App Store

The package manager has been extended with a **general app loader** and **app store** capabilities.

#### General App Loader

The `app_framework_launch()` function now serves as a **universal app loader** that handles:

1. **Built-in apps** — Compiled into firmware, launched directly from the app registry (setjmp/longjmp lifecycle)
2. **Third-party ELF apps** — Installed `.pcpkg` packages loaded via NuttX `load_module()`/`exec_module()` ELF binary format loader
3. **Automatic fallback** — If an app name is not found in built-in registry, the framework automatically tries the package manager's ELF loader

The launcher grid displays **both** built-in and installed third-party apps. Third-party app info is cached from the package registry so they appear in the grid with `LV_SYMBOL_FILE` icons.

#### App Store (Remote Repository)

Users can browse and install apps from a remote repository over Wi-Fi.

| Feature | Details |
|---|---|
| Catalog URL | `https://picocalc.dev/repo/catalog.json` (configurable) |
| Catalog format | JSON with `packages[]` array listing name, version, author, description, category, download_url, size_bytes, min_ram |
| Catalog cache | Cached to `/mnt/sd/apps/catalog.json` for offline browsing |
| Download | HTTP(S) GET of `.pcpkg` file to staging directory, then install |
| Staging | Downloaded files stored in `/mnt/sd/apps/.staging/` before install to prevent corruption |
| Validation | Magic bytes (`PCPK`) checked before install; corrupt downloads are discarded |
| Update check | `pcpkg_update_available()` compares installed vs catalog version |
| UI | Settings → Packages tab now has two sub-tabs: "Installed" and "App Store" |

#### Install Workflows

**Sideload from SD card:**
1. Copy `.pcpkg` files to `/mnt/sd/apps/` via USB mass storage or SCP
2. Go to Settings → Packages → "Scan SD" button
3. All `.pcpkg` files are installed and renamed to `.pcpkg.installed` to prevent re-install

**Download from App Store:**
1. Go to Settings → Packages → "App Store" tab
2. Press "Refresh Catalog" to fetch the latest app list (requires Wi-Fi)
3. Browse available apps; tap an app to download and install
4. Installed apps appear immediately in the launcher grid

**New package.h API additions:**
```c
int  pcpkg_fetch_catalog(const char *repo_url, pcpkg_catalog_t *catalog);
int  pcpkg_load_cached_catalog(pcpkg_catalog_t *catalog);
int  pcpkg_download_and_install(const char *url, const char *name);
int  pcpkg_scan_and_install_sd(void);
bool pcpkg_update_available(const char *name, const pcpkg_catalog_t *catalog);
```

---

## Hostname & Network Identity

The device has a configurable hostname, making it identifiable on the network and in the shell.

| Feature | Details |
|---|---|
| Default hostname | `picocalc` |
| Config file | `/mnt/sd/etc/hostname` (plain text, single line) |
| Applied at boot | NuttX `sethostname()` called during init |
| Shell prompt | `picocalc:/mnt/sd$ ` (or `user@picocalc:~$ `) |
| Network | Hostname sent in DHCP requests, used in mDNS (if implemented) |
| Change via | Settings app → Identity → Hostname, or `hostname <newname>` in terminal |
| SSH display | Shows as banner when SSH connects: `Welcome to picocalc` |

---

## GUI Framework

### LVGL on NuttX Framebuffer

The GUI is built on [LVGL](https://lvgl.io/) running on top of the NuttX framebuffer driver (`/dev/fb0`).

| Parameter | Value |
|---|---|
| LVGL Version | v8.3+ (proven on PicoCalc) or v9.x |
| Color Depth | 16-bit RGB565 |
| Resolution | 320 x 320 |
| Draw Buffer | 1/10 screen (~20KB) in SRAM, DMA flush to ST7365P |
| Memory Pool | 48KB LVGL heap (in SRAM), large data in PSRAM |
| Input Device | I2C keyboard mapped to LVGL keypad input driver |
| Font | Built-in Montserrat 12/14px, custom monospace for terminal/vi |
| Tick Source | NuttX system timer |
| Thread Model | LVGL runs in dedicated `lv_task` thread, apps post events |

### Window Manager

Not a traditional desktop WM — apps run **fullscreen** with a shared status bar.

| Component | Details |
|---|---|
| Status bar | Top 20px: hostname, Wi-Fi icon, battery %, clock |
| App area | Remaining 300x320 pixels for the active app |
| Switching | Press `Fn+Home` → return to launcher |
| Background | Audio player thread continues during app switch |
| Notifications | Toast-style popups for Wi-Fi connect/disconnect, low battery |

**Why LVGL:**
- Already ported and tested on PicoCalc hardware ([picocalc_lvgl_graphics_demo](https://github.com/clockworkpi/PicoCalc/tree/master/Code/picocalc_lvgl_graphics_demo))
- NuttX has native LVGL integration (`CONFIG_GRAPHICS_LVGL`)
- Provides textarea, table, keyboard, file browser, and chart widgets out-of-the-box
- Small footprint (~48KB RAM, ~128KB flash for core + used widgets)
- Active community, well-documented C API

---

## Development Phases

### Phase 1: Board Bring-up & Core OS
- [ ] Create NuttX board definition for RP2350B-Plus-W (based on `raspberrypi-pico-2`)
- [ ] Boot NuttShell over UART
- [ ] Validate GPIO, I2C, SPI peripherals
- [ ] Configure PSRAM heap (8MB, `RP23XX_PSRAM_HEAP_USER` mode)
- [ ] Set up hostname support (`sethostname()` at boot, `/mnt/sd/etc/hostname`)

### Phase 2: Display & Framebuffer
- [ ] Port ST7365P SPI driver to NuttX framebuffer interface (`/dev/fb0`)
- [ ] Adapt from existing [lcdspi](https://github.com/clockworkpi/PicoCalc/tree/master/Code/picocalc_helloworld/lcdspi) driver
- [ ] DMA-accelerated SPI flush (partial buffer, 1/10 screen lines)
- [ ] Verify 25MHz SPI, GPIO 10-15 pin mapping on RP2350B-Plus-W

### Phase 3: Input, LVGL & Launcher
- [ ] I2C keyboard driver (STM32 south-bridge protocol)
- [ ] Map keyboard scancodes to LVGL input device
- [ ] Integrate LVGL v8.3+ on NuttX (`CONFIG_GRAPHICS_LVGL`)
- [ ] Status bar component (hostname, Wi-Fi, battery, clock)
- [ ] Launcher / home screen with app grid (reads package registry)
- [ ] Backlight control (screen + keyboard) via I2C

### Phase 4: Storage, Settings & Package System
- [ ] SD card SPI driver + FAT32 mount at `/mnt/sd`
- [ ] SD card directory structure (`apps/`, `documents/`, `music/`, `video/`, `ssh/`, `etc/`)
- [ ] Settings app — Wi-Fi, hostname, display, keyboard, audio, storage, system info
- [ ] Config persistence (`/mnt/sd/etc/settings.json`, `/mnt/sd/etc/hostname`)
- [ ] Package manager — manifest parser, registry, install/remove
- [ ] App lifecycle — launch ELF from package, return to launcher on exit
- [ ] App state save/restore — `pc_app_save()`/`pc_app_restore()` API, PSRAM + SD persistence
- [ ] Per-app state directory (`/mnt/sd/etc/appstate/`)

### Phase 5: Local Terminal & vi
- [ ] VT100/ANSI terminal emulator widget in LVGL (shared by `pcterm` + `pcssh`)
- [ ] Local terminal app (`pcterm`) — NuttShell on LVGL terminal display
- [ ] Enable NuttX built-in vi (`CONFIG_SYSTEM_VI`)
- [ ] Shell prompt with hostname display
- [ ] Scrollback buffer in PSRAM

### Phase 6: Office Suite
- [ ] Text editor (`pcedit`) — vi-style modes (Normal/Insert/Command)
- [ ] vi keybindings: `hjkl`, `dd`, `yy`, `p`, `:w`, `:q`, `/search`
- [ ] CSV parser (RFC 4180 compliant)
- [ ] Table editor (`pccsv`) — LVGL table widget + cell navigation/editing
- [ ] Save/load files on SD card

### Phase 7: Audio & Video
- [ ] PWM audio driver (NuttX audio subsystem)
- [ ] MP3 software decoder (minimp3) + WAV PCM playback
- [ ] Audio player app (`pcaudio`) — file browser, playback controls, background play
- [ ] `.pcv` video format specification + header parser
- [ ] Video player app (`pcvideo`) — frame-sequential playback with interleaved audio
- [ ] Offline PC converter tool (`pcv-convert`) — FFmpeg-based MP4→PCV transcoder

### Phase 8: Networking & SSH
- [ ] CYW43439 Wi-Fi bring-up (driver + firmware blobs)
- [ ] TCP/IP stack configuration (BSD sockets)
- [ ] Hostname in DHCP requests
- [ ] Wi-Fi manager in Settings (scan, connect, save credentials)
- [ ] SSH client (`pcssh`) — wolfSSH or dropbear, terminal emulation
- [ ] SCP file transfer (`scp` command in SSH session)
- [ ] SFTP interactive file browser (browse remote, download/upload)
- [ ] Saved connections + known_hosts (`/mnt/sd/ssh/`)
- [ ] Basic network tools (ping, wget/curl, DNS lookup)
- [ ] TLS library integration (wolfSSL or mbedTLS) for HTTPS
- [ ] Web browser (`pcweb`) — HTML subset parser, text-mode renderer
- [ ] Bookmarks, history, downloads to SD card

### Phase 9: Polish & Distribution
- [ ] Power management (sleep modes, battery status on status bar)
- [ ] Global hotkeys (Fn+Space audio, Fn+Home launcher, Fn+W Wi-Fi)
- [ ] Notification toasts (Wi-Fi events, low battery)
- [ ] Bluetooth serial console (optional)
- [ ] OTA firmware update via Wi-Fi
- [ ] PicoCalc SDK documentation for third-party package development
- [ ] Sample third-party app template
- [ ] Build system + CI for firmware + packages

## Build & Flash

```bash
# Clone NuttX
git clone https://github.com/apache/nuttx.git nuttx
git clone https://github.com/apache/nuttx-apps.git nuttx-apps

# Configure for PicoCalc (custom board, once created)
cd nuttx
make distclean
./tools/configure.sh picocalc-rp2350b:nsh

# Build
make -j$(nproc)

# Flash via UF2 (hold BOOT + reset)
cp nuttx.uf2 /path/to/RPI-RP2/
```

## References

### PicoCalc Hardware & Code
- [ClockworkPi PicoCalc](https://www.clockworkpi.com/picocalc)
- [ClockworkPi GitHub — PicoCalc](https://github.com/clockworkpi/PicoCalc)
- [CPI v2.0 Mainboard Schematic](https://github.com/clockworkpi/PicoCalc/blob/master/clockwork_Mainboard_V2.0_Schematic.pdf)
- [ST7365P Spec Sheet](https://github.com/clockworkpi/PicoCalc/blob/master/ST7365P_SPEC_V1.0.pdf)
- [PicoCalc Hello World (SPI LCD + I2C KB + PSRAM)](https://github.com/clockworkpi/PicoCalc/tree/master/Code/picocalc_helloworld)
- [PicoCalc LVGL Graphics Demo](https://github.com/clockworkpi/PicoCalc/tree/master/Code/picocalc_lvgl_graphics_demo)
- [PicoCalc MP3 Player](https://github.com/clockworkpi/PicoCalc/tree/master/Code/MP3Player)
- [Picoware Community Firmware](https://github.com/jblanked/Picoware)

### Core Module
- [Waveshare RP2350B-Plus-W Wiki](https://www.waveshare.com/wiki/RP2350B-Plus-W)
- [Waveshare RP2350B-Plus-W Schematic](https://files.waveshare.com/wiki/RP2350B-Plus-W/RP2350B-Plus-W.pdf)
- [RP2350 Datasheet](https://files.waveshare.com/wiki/common/Rp2350-datasheet.pdf)

### RTOS
- [NuttX RP2350 Platform Docs](https://nuttx.apache.org/docs/latest/platforms/arm/rp23xx/index.html)
- [Zephyr Pico 2 Board Docs](https://docs.zephyrproject.org/latest/boards/raspberrypi/rpi_pico2/doc/index.html)
- [NuttX LVGL Integration](https://nuttx.apache.org/docs/latest/components/drivers/special/lcd.html)

### GUI & Libraries
- [LVGL Documentation](https://docs.lvgl.io/)
- [minimp3 — Lightweight MP3 Decoder](https://github.com/lieff/minimp3)
- [wolfSSH — Embedded SSH Library](https://www.wolfssl.com/products/wolfssh/)
- [dropbear — Lightweight SSH Client/Server](https://matt.ucc.asn.au/dropbear/dropbear.html)
- [FFmpeg — Source for pcv-convert tool](https://ffmpeg.org/)
- [wolfSSL — Embedded TLS Library](https://www.wolfssl.com/products/wolfssl/)
- [Lynx — Text Web Browser (inspiration)](https://lynx.invisible-island.net/)
- [w3m — Text-based Web Browser (inspiration)](https://w3m.sourceforge.net/)
