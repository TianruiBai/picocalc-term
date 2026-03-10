# eUX OS — Project Plan

**Version:** 1.0
**Status:** Active
**Supersedes:** `Project Plan.md` (PicoCalc-Term)

---

## 1. Vision

**eUX OS** is an embedded Unix-like operating system built on the Apache NuttX RTOS kernel. It presents a complete, self-contained Unix environment — with a proper root filesystem hierarchy, init system, login shell, device files, and POSIX userland — all flashed as a single firmware image to a microcontroller.

The first target platform is the **ClockworkPi PicoCalc** handheld (with Waveshare RP2350B-Plus-W), but the architecture is designed to be portable to other NuttX-supported boards.

### Design Principles

| # | Principle | Description |
|---|-----------|-------------|
| 1 | **Everything is a file** | Devices, processes, config — exposed through VFS. |
| 2 | **Firmware IS the system disk** | The `.uf2` image contains the NuttX kernel + a ROMFS root filesystem. Flash it, and you have a complete OS. |
| 3 | **Unix filesystem hierarchy** | `/bin`, `/etc`, `/dev`, `/home`, `/tmp`, `/var`, `/proc` — familiar to any Unix user. |
| 4 | **POSIX-first** | NuttX provides POSIX.1 compliance. `open()`/`read()`/`write()`/`close()`, pthreads, signals, pipes — all standard. |
| 5 | **Layered separation** | Kernel → Board BSP → System Services → Shell/GUI → Applications. Each layer has clear boundaries. |
| 6 | **Read-only system, writable user data** | System files in ROMFS (immutable). User config/data on SD card and flash LittleFS (mutable). |
| 7 | **Minimal but complete** | Ship only what's needed, but make it feel like a real Unix box. |

---

## 2. Architecture Overview

```
┌──────────────────────────────────────────────────────────────────┐
│                         User Applications                         │
│  ┌────────┐┌────────┐┌────────┐┌────────┐┌────────┐┌──────────┐ │
│  │ pcedit ││ pccsv  ││pcaudio ││pcvideo ││ pcweb  ││ 3rd-party│ │
│  │ editor ││ spread ││ player ││ player ││browser ││  (ELF)   │ │
│  └───┬────┘└───┬────┘└───┬────┘└───┬────┘└───┬────┘└────┬─────┘ │
│  ┌───┴─────────┴─────────┴─────────┴─────────┴──────────┴─────┐ │
│  │    /usr/bin — User applications (built-in + installable)    │ │
│  └─────────────────────────┬───────────────────────────────────┘ │
│  ┌─────────────────────────┴───────────────────────────────────┐ │
│  │    System Services Layer                                     │ │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌───────────────┐  │ │
│  │  │ Display  │ │  Audio   │ │ Network  │ │  App Manager  │  │ │
│  │  │ Server   │ │ Service  │ │ Service  │ │  (pkg/launch) │  │ │
│  │  └──────────┘ └──────────┘ └──────────┘ └───────────────┘  │ │
│  └─────────────────────────┬───────────────────────────────────┘ │
│  ┌─────────────────────────┴───────────────────────────────────┐ │
│  │    /bin, /sbin — System Shell & Core Utilities               │ │
│  │  sh (nsh) │ vi │ lua │ mount │ ifconfig │ ps │ kill │ ...   │ │
│  └─────────────────────────┬───────────────────────────────────┘ │
│  ┌─────────────────────────┴───────────────────────────────────┐ │
│  │    LVGL Graphics + Virtual Console + GUI Compositor          │ │
│  └─────────────────────────┬───────────────────────────────────┘ │
│  ┌─────────────────────────┴───────────────────────────────────┐ │
│  │    /etc — Init System │ /proc — Process Info │ VFS           │ │
│  └─────────────────────────┬───────────────────────────────────┘ │
├════════════════════════════╪═════════════════════════════════════╡
│                     NuttX Kernel                                  │
│  ┌──────────┐ ┌────────────┐ ┌──────────┐ ┌────────────────┐    │
│  │Scheduler │ │   VFS +    │ │  TCP/IP  │ │  Audio Subsys  │    │
│  │ POSIX    │ │ ROMFS/FAT  │ │  Stack   │ │                │    │
│  │ pthreads │ │ LittleFS   │ │(BSD sock)│ │                │    │
│  └────┬─────┘ └────────────┘ └────┬─────┘ └────────────────┘    │
│  ┌────┴───────────────────────────┴──────────────────────────┐   │
│  │              Board Support Package (BSP)                   │   │
│  │  SPI(LCD) │ I2C(KB) │ QSPI1(PSRAM/XIP) │ SPI/PIO(SD) │ PWM │   │
│  │  CYW43439(WiFi/BT) │ UART(console) │ RTC │ GPIO          │   │
│  └────────────────────────────────────────────────────────────┘   │
├══════════════════════════════════════════════════════════════════╡
│  RP2350B  │  520KB SRAM  │  16MB Flash  │  8MB PSRAM  │  SD     │
└──────────────────────────────────────────────────────────────────┘
```

---

## 3. Firmware Image Composition

The key innovation: **the firmware `.uf2` is a self-contained OS disk.**

```
┌─────────────────────────────────────────────────────┐
│              16 MB Flash Layout                      │
├─────────────────────────────────────────────────────┤
│  0x10000000  RP2350 boot header (256 bytes)          │
├─────────────────────────────────────────────────────┤
│  0x10000100  NuttX kernel + BSP + linked apps        │
│              (~1.5–2.5 MB)                           │
├─────────────────────────────────────────────────────┤
│  Appended    ROMFS system image                      │
│              (root filesystem: /bin, /etc, /usr, …)  │
│              (~256 KB–1 MB)                          │
├─────────────────────────────────────────────────────┤
│  Partition   LittleFS writable partition             │
│              (/data — persistent writable storage)   │
│              (~4–12 MB)                              │
├─────────────────────────────────────────────────────┤
│  End         Wi-Fi firmware blobs (if stored here)   │
│              (~512 KB)                               │
└─────────────────────────────────────────────────────┘
```

### Build Pipeline

```
Source tree                    Build artifacts              Firmware
─────────────                  ────────────────             ─────────
boards/ + nuttx/          →    nuttx (ELF)            ─┐
                                                        ├──→  nuttx.uf2
rootfs/                   →    romfs.img (genromfs)   ─┘
  ├── bin/
  ├── etc/
  │   ├── init.d/rcS
  │   ├── fstab
  │   ├── hostname
  │   ├── profile
  │   └── eux/
  ├── usr/share/
  └── ...
```

The build system:
1. Compiles NuttX kernel + BSP + all built-in apps into `nuttx` ELF
2. Generates a ROMFS image from `rootfs/` directory using `genromfs`
3. Links the ROMFS image into the firmware binary (as a C array or appended section)
4. Converts to `.uf2` for flashing

---

## 4. Root Filesystem Hierarchy

```
/                                 ← VFS root (kernel-managed)
├── bin/                          ← System commands (ROMFS symlinks to builtins)
│   ├── sh                        ← NuttShell
│   ├── ls, cat, cp, mv, rm      ← File utilities
│   ├── mkdir, rmdir, mount       ← Filesystem utilities
│   ├── ps, kill, sleep, time     ← Process utilities
│   ├── df, free, uname, uptime   ← System info
│   ├── vi                        ← Text editor (NuttX built-in)
│   ├── lua                       ← Lua 5.4 interpreter
│   ├── qjs                       ← QuickJS interpreter
│   ├── ping, ifconfig, route     ← Network utilities
│   ├── dmesg                     ← Kernel log viewer
│   ├── lscpu, lsi2c, lsspi       ← Hardware info
│   ├── screenfetch               ← System info display
│   ├── clockset                  ← CPU frequency control
│   └── hostname                  ← Hostname management
│
├── sbin/                         ← System administration (ROMFS)
│   ├── init                      ← Init process (PID 1 entry point)
│   ├── mkfatfs                   ← FAT filesystem formatter
│   └── reboot                    ← System reboot
│
├── etc/                          ← System configuration (ROMFS defaults)
│   ├── init.d/
│   │   └── rcS                   ← Boot init script
│   ├── fstab                     ← Filesystem mount table
│   ├── hostname                  ← Default hostname ("eux")
│   ├── profile                   ← Shell profile (PATH, PS1, aliases)
│   ├── passwd                    ← User database
│   ├── motd                      ← Message of the day
│   ├── eux/
│   │   ├── system.conf           ← System defaults (display, audio, keyboard)
│   │   └── apps.conf             ← Built-in app registry
│   └── wifi/
│       └── wpa_supplicant.conf   ← Wi-Fi defaults (template)
│
├── lib/                          ← System data (ROMFS)
│   └── eux/
│       ├── fonts/                ← System fonts (.bin)
│       └── icons/                ← System app icons (.bin)
│
├── usr/                          ← User programs (ROMFS)
│   ├── bin/                      ← Application binaries (built-in)
│   │   ├── settings              ← System settings app
│   │   ├── pcedit                ← Text editor (vi-style)
│   │   ├── pccsv                 ← CSV/spreadsheet editor
│   │   ├── pcaudio               ← Audio player
│   │   ├── pcvideo               ← Video player
│   │   ├── pcterm                ← Local terminal emulator
│   │   ├── pcssh                 ← SSH client
│   │   ├── pcweb                 ← Web browser
│   │   └── pcfiles               ← File explorer
│   └── share/
│       └── doc/                  ← Built-in documentation
│           └── eux/
│               └── welcome.txt
│
├── dev/                          ← Device files (kernel-populated at runtime)
│   ├── console                   ← UART serial console
│   ├── null                      ← /dev/null
│   ├── zero                      ← /dev/zero
│   ├── fb0                       ← Framebuffer (320×320 RGB565)
│   ├── input0                    ← Keyboard input device
│   ├── audio0                    ← Audio output (PWM)
│   ├── mmcsd0                    ← SD card block device
│   ├── mtd0                      ← Flash MTD partition
│   ├── i2c0                      ← I2C bus
│   ├── spi0, spi1                ← SPI buses
│   └── ttyS0                     ← Serial port
│
├── proc/                         ← Process info filesystem (procfs)
│   ├── [pid]/                    ← Per-process info
│   ├── cpuload                   ← CPU load
│   ├── meminfo                   ← Memory info
│   ├── uptime                    ← System uptime
│   └── version                   ← NuttX version
│
├── tmp/                          ← Temporary files (tmpfs, RAM-backed)
│
├── var/                          ← Variable runtime data
│   ├── log/                      ← System logs (writable flash or RAM)
│   └── run/                      ← PID files, sockets
│
├── data/                         ← Persistent writable storage (LittleFS on flash)
│   ├── etc/                      ← User config overrides
│   │   ├── hostname              ← User-set hostname (overrides /etc/hostname)
│   │   ├── profile               ← User shell customizations
│   │   ├── passwd                ← Modified user database
│   │   ├── eux/
│   │   │   └── system.conf       ← User-modified system settings
│   │   └── wifi/
│   │       └── wpa_supplicant.conf ← Saved Wi-Fi networks
│   └── cache/                    ← App caches, catalogs
│
├── home/                         ← User home directories
│   └── user/                     ← Default user home → /mnt/sd/home/user
│
└── mnt/                          ← Mount points
    └── sd/                       ← SD card (FAT32, user storage)
        ├── home/
        │   └── user/             ← User files
        │       ├── documents/
        │       ├── music/
        │       ├── video/
        │       ├── pictures/
        │       └── downloads/
        ├── apps/                 ← Installed third-party packages
        │   └── registry.json
        └── ssh/                  ← SSH keys and known_hosts
```

### Configuration Overlay Model

eUX uses a **ROM defaults + writable overlay** pattern:

```
Read order (first match wins):
  1. /data/etc/<file>        ← User-modified config (flash LittleFS, writable)
  2. /mnt/sd/etc/<file>      ← SD card config (optional, for portability)
  3. /etc/<file>             ← ROM defaults (ROMFS, read-only)
```

This means:
- Factory defaults always exist in ROMFS (can't be corrupted)
- User changes are stored in flash LittleFS (`/data/etc/`)
- SD card can carry portable config across devices
- "Factory reset" = delete `/data/etc/` → reverts to ROM defaults

---

## 5. Boot Sequence

```
Power On
  │
  ▼
RP2350B ROM bootloader
  │ (loads firmware from flash)
  ▼
NuttX kernel init
  │ ├── Scheduler init
  │ ├── VFS init
  │ ├── Device driver registration
  │ └── Memory (SRAM + PSRAM heap)
  ▼
Board bringup (rp23xx_bringup.c)
  │ ├── UART console → /dev/console
  │ ├── SPI buses → /dev/spi0, /dev/spi1
  │ ├── I2C bus → /dev/i2c0
  │ ├── LCD framebuffer → /dev/fb0
  │ ├── Keyboard input → /dev/input0
  │ ├── SD card → /dev/mmcsd0
  │ ├── Flash MTD → /dev/mtd0
  │ ├── PSRAM heap → mm_addregion()
  │ └── Audio PWM → /dev/audio0
  ▼
Filesystem mounts
  │ ├── Mount ROMFS at /etc (nsh_romfsetc)
  │ ├── Mount procfs at /proc
  │ ├── Mount tmpfs at /tmp
  │ ├── Mount LittleFS at /data (writable flash partition)
  │ └── Mount FAT32 at /mnt/sd (SD card)
  ▼
/etc/init.d/rcS executes
  │ ├── Load hostname  (from /data/etc/hostname or /etc/hostname)
  │ ├── Set PATH       (from /etc/profile)
  │ ├── Start services (display server, audio service)
  │ ├── Start Wi-Fi    (if configured)
  │ └── Print motd
  ▼
Login prompt  (or auto-login if single-user)
  │
  ▼
Shell (nsh) or GUI Launcher
```

### Init Script (`/etc/init.d/rcS`)

```sh
# eUX OS init script
# Mounts filesystems and starts system services

# Mount writable partitions (per /etc/fstab)
mount -t littlefs /dev/mtd0 /data
mount -t vfat /dev/mmcsd0 /mnt/sd
mount -t tmpfs none /tmp

# Ensure user directories exist on SD
mkdir -p /mnt/sd/home/user/documents
mkdir -p /mnt/sd/home/user/music
mkdir -p /mnt/sd/home/user/video
mkdir -p /mnt/sd/apps

# Load user hostname override
if [ -f /data/etc/hostname ]; then
  hostname -F /data/etc/hostname
fi

# Set shell prompt
export PS1='\h:\w\$ '
export PATH=/bin:/sbin:/usr/bin
export HOME=/mnt/sd/home/user

# Print message of the day
cat /etc/motd
```

---

## 6. Hardware Platform

### ClockworkPi PicoCalc (v2.0 Mainboard) + Waveshare RP2350B-Plus-W

| Component | Specification |
|---|---|
| MCU | RP2350B — dual Cortex-M33 / Hazard3 RISC-V @ 150 MHz |
| SRAM | 520 KB |
| Flash | 16 MB (system image + LittleFS partition) |
| PSRAM | 8 MB (on RP2350B-Plus-W module, XIP-mapped at 0x11000000) |
| Display | 320×320 IPS LCD, ST7365P (ILI9488-compat), SPI1 @ 25 MHz |
| Keyboard | 67-key QWERTY, STM32 south-bridge (I2C0 @ 0x1F) |
| Audio | Dual PWM speakers + 3.5 mm jack (GP40/GP41) |
| Wireless | Wi-Fi 4 + BT 5.2 (CYW43439) |
| Storage | SD card slot (SPI0 or PIO 1-bit SDIO) |
| Battery | 18650 Li-ion + AXP2101 PMIC (via south bridge) |
| Console | UART0 (GP0 TX, GP1 RX) @ 115200 baud |

### GPIO Pin Assignment

| GPIO | Function | Notes |
|---|---|---|
| GP0 | UART0 TX | Debug console |
| GP1 | UART0 RX | Debug console |
| GP2 | (on-module PSRAM QSPI1) | Not user-available |
| GP3 | (on-module PSRAM QSPI1) | Not user-available |
| GP4 | (on-module PSRAM QSPI1) | Not user-available |
| GP5 | (on-module PSRAM QSPI1) | Not user-available |
| GP6 | I2C0 SDA | South bridge |
| GP7 | I2C0 SCL | South bridge |
| GP8 | South bridge INT | Keyboard interrupt |
| GP10 | SPI1 SCK | LCD clock |
| GP11 | SPI1 TX (MOSI) | LCD data |
| GP12 | SPI1 RX (MISO) | LCD (unused) |
| GP13 | LCD CS | Chip select |
| GP14 | LCD DC | Data/Command |
| GP15 | LCD RST | Reset |
| GP16 | SD DAT0 / SPI0 MISO | SD card |
| GP17 | SD CS | SPI mode |
| GP18 | SD CLK / SPI0 SCK | SD card |
| GP19 | SD CMD / SPI0 MOSI | SD card |
| GP21 | (available) | Was PSRAM, now freed |
| GP22 | SD card detect | Active-low |
| GP23 | (available) | |
| GP24 | (available) | |
| GP25 | (available) | |
| GP26 | (available / ADC0) | |
| GP27 | (available / ADC1) | |
| GP29 | (available / ADC3) | |
| GP36 | CYW43 WL_ON | Wi-Fi power |
| GP37 | CYW43 SPI_D | Wi-Fi data |
| GP38 | CYW43 WL_CS | Wi-Fi CS |
| GP39 | CYW43 SPI_CLK | Wi-Fi clock |
| GP40 | Audio Left (PWM) | Slice 10 Ch A |
| GP41 | Audio Right (PWM) | Slice 10 Ch B |

### Memory Map

```
520 KB SRAM                              8 MB PSRAM
┌────────────────────────────┐           ┌────────────────────────────┐
│ NuttX Kernel      (~60 KB) │           │ Framebuffer 320×320 (200K) │
│ LVGL Core+Widgets (~48 KB) │           │ Audio Decode Buffer  (64K) │
│ LVGL Draw Buffer  (~20 KB) │           │ Video Frame Buffer  (200K) │
│ TCP/IP Stack      (~40 KB) │           │ Text Editor Buffer  (512K) │
│ App Stacks/Heap  (~350 KB) │           │ Terminal Scrollback  (64K) │
└────────────────────────────┘           │ App Heap / Cache   (~6.9M) │
                                         └────────────────────────────┘
16 MB Flash
┌────────────────────────────┐
│ Firmware + ROMFS   (~3 MB) │
│ LittleFS /data    (~12 MB) │
│ Wi-Fi FW blob     (~0.5 M) │
└────────────────────────────┘
```

---

## 7. System Services

### 7.1 Display Server

Owns the framebuffer (`/dev/fb0`) and LVGL event loop. All GUI output goes through this service.

| Aspect | Details |
|---|---|
| Thread | Dedicated LVGL thread on Core 0, ~30 Hz |
| Draw buffer | 1/10 screen (20 KB) in SRAM, DMA flush to LCD |
| Compositing | Status bar (20 px top) + app area (300 × 320 px) |
| Backend | LVGL v8.3+ on NuttX `fb_vtable_s` |

### 7.2 Audio Service

Background audio playback that survives app switches.

| Aspect | Details |
|---|---|
| Thread | Audio decode on Core 0, PWM ISR on Core 1 |
| Ring buffer | 64 KB in PSRAM |
| Formats | MP3 (minimp3), WAV (PCM 8/16-bit) |
| API | `pc_audio_play()`, `pause()`, `stop()`, `set_volume()` |

### 7.3 Network Service

Wi-Fi connection management with auto-reconnect.

| Aspect | Details |
|---|---|
| Driver | CYW43439 via PIO SPI |
| Stack | NuttX TCP/IP (BSD sockets) |
| Config | `/data/etc/wifi/wpa_supplicant.conf` |
| CLI | `pcwifi scan`, `pcwifi connect <ssid>` |

### 7.4 Init Service & runit Supervision

PID 1 — the first userspace process. System services are supervised by **runit** (https://smarden.org/runit/).

| Aspect | Details |
|---|---|
| Entry | `CONFIG_INIT_ENTRYPOINT="eux_init"` |
| Role | Mount filesystems, run `/etc/init.d/rcS`, start runit, launch shell/GUI |
| Config | `/etc/fstab` for mounts, `/etc/init.d/` for boot scripts |
| Supervisor | runit `runsvdir` scans `/etc/sv/` for service directories |
| Service dirs | `/etc/sv/display/run`, `/etc/sv/audio/run`, `/etc/sv/network/run`, `/etc/sv/syslog/run` |
| Control | `sv start|stop|restart|status <service>` from NSH shell |
| Auto-restart | runit automatically restarts crashed services |
| Runtime state | `/tmp/run/sv/<name>/stat` and `/tmp/run/sv/<name>/pid` |

---

## 8. Application Architecture

### Built-in Applications

Built-in apps are compiled into the firmware and registered as NuttX tasks. They appear at `/usr/bin/<name>` in the filesystem.

| App | Command | Description |
|---|---|---|
| Settings | `settings` | System configuration GUI |
| Text Editor | `pcedit` | vi-style text editor with syntax highlighting |
| Spreadsheet | `pccsv` | CSV table editor |
| Audio Player | `pcaudio` | MP3/WAV player with background playback |
| Video Player | `pcvideo` | .pcv format video player |
| Terminal | `pcterm` | Local NuttShell terminal emulator |
| SSH Client | `pcssh` | Remote SSH terminal + SCP/SFTP |
| Web Browser | `pcweb` | Text+image web browser |
| File Explorer | `pcfiles` | File manager for SD card and flash |

### Third-party Applications

Installed from `.pcpkg` packages to `/mnt/sd/apps/<name>/`. Loaded as ELF binaries via NuttX `exec()`.

### App Lifecycle

```
Launcher (home)
    │
    ├── select app ──→  Launch (load ELF or call builtin)
    │                       │
    │                       ▼
    │                   App Running (fullscreen, 300×320 area)
    │                       │
    │     ┌─────────────────┼─────────────────┐
    │     │                 │                 │
    │  Fn+Home          Ctrl+Q            App crashes
    │  (yield)          (exit)            (trapped)
    │     │                 │                 │
    │     ▼                 ▼                 ▼
    │  Save state       Discard state     Discard state
    │  to /data/        clean exit        log error
    │     │                 │                 │
    └─────┴─────────────────┴─────────────────┘
                    Return to Launcher
```

---

## 9. Package System

### Package Format (`.pcpkg`)

Binary archive containing manifest, ELF binary, icon, and assets. See `Package Format Spec.md` for full details.

### Package Locations

| Path | Purpose |
|---|---|
| `/mnt/sd/apps/<name>/` | Installed third-party apps |
| `/mnt/sd/apps/registry.json` | Package registry |
| `/mnt/sd/apps/.staging/` | Download staging area |

### Install Methods

1. **Sideload**: Copy `.pcpkg` to SD card → Settings → Packages → Install
2. **App Store**: Browse remote catalog → Download → Install (Wi-Fi required)
3. **CLI**: `pcminipkg install <file.pcpkg>`

---

## 10. Build System

```bash
# One-time setup
make setup                    # Install toolchain + deps

# Configure + build
make configure                # Apply defconfig + board files
make build                    # Compile kernel + apps + ROMFS → nuttx.uf2

# Build details
make rootfs                   # Generate ROMFS image from rootfs/
make build JOBS=4 V=1         # Verbose parallel build

# Development
make menuconfig               # Edit NuttX Kconfig
make rebuild                  # Clean + build
make flash                    # Copy .uf2 to RP2350 boot drive

# Profiles
make configure BOARD_CONFIG=picocalc-rp2350b:nsh    # Minimal shell
make configure BOARD_CONFIG=picocalc-rp2350b:full   # Complete eUX OS
```

### Build Artifacts

| File | Description |
|---|---|
| `nuttx/nuttx` | ELF binary (for debugging with GDB) |
| `nuttx/nuttx.uf2` | UF2 firmware image (flash to RP2350) |
| `build/romfs.img` | ROMFS root filesystem image |
| `build/rootfs/` | Generated rootfs directory (pre-genromfs) |

---

## 11. Development Phases (Summary)

| Phase | Name | Goal |
|---|---|---|
| 0 | **Foundation** | Repo restructure, ROMFS build pipeline, rootfs skeleton |
| 1 | **Kernel & BSP** | Boot to NuttShell over UART, all peripherals validated |
| 2 | **Root Filesystem** | ROMFS root in firmware, init system, fstab, login, `/etc/profile` |
| 3 | **Display & Input** | Framebuffer driver, keyboard driver, LVGL integration |
| 4 | **System Services** | Status bar, audio service, clock/RTC, virtual console |
| 5 | **Shell & Utilities** | NSH with Unix paths, core commands in `/bin`, vi, scripting |
| 6 | **Window Manager** | Launcher, app lifecycle, state save/restore, package system |
| 7 | **Applications** | All 9 built-in apps (pcedit, pccsv, pcaudio, etc.) |
| 8 | **Networking** | Wi-Fi, TCP/IP, SSH, web browser, NTP |
| 9 | **Polish** | Power management, global hotkeys, OTA, SDK, documentation |

See `eUX Implementation Plan.md` for detailed checklists per phase.

---

## 12. References

### Hardware
- [ClockworkPi PicoCalc](https://www.clockworkpi.com/picocalc)
- [PicoCalc GitHub](https://github.com/clockworkpi/PicoCalc)
- [CPI v2.0 Schematic](https://github.com/clockworkpi/PicoCalc/blob/master/clockwork_Mainboard_V2.0_Schematic.pdf)
- [ST7365P Spec](https://github.com/clockworkpi/PicoCalc/blob/master/ST7365P_SPEC_V1.0.pdf)
- [Waveshare RP2350B-Plus-W](https://www.waveshare.com/rp2350b-plus.htm)

### NuttX
- [Apache NuttX](https://nuttx.apache.org/)
- [NuttX ROMFS](https://nuttx.apache.org/docs/latest/components/filesystem/romfs.html)
- [NuttX NSH Startup](https://nuttx.apache.org/docs/latest/applications/nsh/nsh_startup.html)
- [NuttX ELF Loader](https://nuttx.apache.org/docs/latest/components/binfmt.html)

### Reference Code
- [PicoCalc Hello World (LCD+KB+PSRAM)](https://github.com/clockworkpi/PicoCalc/tree/master/Code/picocalc_helloworld)
- [PicoCalc LVGL Demo](https://github.com/clockworkpi/PicoCalc/tree/master/Code/picocalc_lvgl_graphics_demo)
