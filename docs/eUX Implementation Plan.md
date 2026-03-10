# eUX OS — Implementation Plan

**Version:** 1.0
**Status:** Active
**Supersedes:** `Detailed Implementation Plan.md` (PicoCalc-Term)

This document is the master checklist for building eUX OS. Each phase has concrete deliverables, file paths, and verification steps. Items marked with existing status carry forward from the PicoCalc-Term work where applicable.

---

## Phase 0: Foundation — Repo Structure & Build Pipeline

**Goal:** Restructure the repository for eUX, establish the ROMFS build pipeline, and create the root filesystem skeleton that gets baked into firmware.

### 0.1 Repository Restructure

Rename and reorganize to reflect eUX naming and Unix-like structure.

| Task | Details |
|---|---|
| Rename project references | `picocalc-term` → `eux-os` in user-facing strings, README, docs |
| Keep board name | `picocalc-rp2350b` (board is hardware, not the OS) |
| Keep app source names | `pcedit`, `pccsv`, etc. — well-established identifiers |

**New/modified directories:**

```
eux-os/  (repo root — or keep picocalc-term for git continuity)
├── rootfs/                     ← NEW: Source tree for ROMFS root filesystem
│   ├── etc/
│   │   ├── init.d/
│   │   │   └── rcS             ← Init script
│   │   ├── fstab               ← Mount table
│   │   ├── hostname            ← Default hostname
│   │   ├── profile             ← Shell environment
│   │   ├── passwd              ← User database
│   │   ├── motd                ← Login banner
│   │   └── eux/
│   │       ├── system.conf     ← System defaults
│   │       └── apps.conf       ← Built-in app registry
│   ├── usr/
│   │   └── share/
│   │       └── doc/
│   │           └── eux/
│   │               └── welcome.txt
│   └── ...  (other dirs created by build script, symlinks for /bin etc.)
├── boards/                     ← EXISTING: Board BSP (unchanged)
├── pcterm/                     ← EXISTING: Core OS framework (rename to eux/ later)
├── apps/                       ← EXISTING: Built-in applications
├── nuttx/                      ← EXISTING: NuttX kernel source
├── nuttx-apps/                 ← EXISTING: NuttX apps source
├── tools/                      ← EXISTING: Build + convert tools
│   ├── mkrootfs.sh             ← NEW: Generates rootfs/ → romfs.img
│   └── ...
├── Makefile                    ← MODIFIED: Add rootfs + romfs targets
└── docs/
    ├── eUX Project Plan.md     ← NEW (this project)
    └── eUX Implementation Plan.md ← NEW (this document)
```

#### Checklist

- [x] Create `rootfs/` directory tree with all subdirectories
- [x] Create `rootfs/etc/init.d/rcS` — init script
- [x] Create `rootfs/etc/fstab` — filesystem mount table
- [x] Create `rootfs/etc/hostname` — default hostname `eux`
- [x] Create `rootfs/etc/profile` — shell PATH and PS1
- [x] Create `rootfs/etc/passwd` — default user entry
- [x] Create `rootfs/etc/motd` — login message
- [x] Create `rootfs/etc/eux/system.conf` — display/audio/keyboard defaults
- [x] Create `rootfs/etc/eux/apps.conf` — built-in app registry (JSON)
- [x] Create `rootfs/usr/share/doc/eux/welcome.txt`
- [ ] Update `README.md` — reflect eUX branding and build instructions
- [x] Verify existing source directories (`boards/`, `pcterm/`, `apps/`) unchanged

---

### 0.2 ROMFS Build Pipeline

NuttX supports `CONFIG_NSH_ROMFSETC` which mounts a ROMFS image at `/etc` during boot. We use this mechanism, then extend mounting in the init script.

**Key files:**

| File | Purpose |
|---|---|
| `tools/mkrootfs.sh` | Script: populate `build/rootfs/`, run `genromfs`, output `build/romfs.img` |
| `nuttx-apps/examples/pcterm/romfs_data.c` | Generated C file with ROMFS image as byte array |
| `Makefile` (top-level) | New targets: `rootfs`, `romfs` |

**ROMFS generation flow:**

```bash
# tools/mkrootfs.sh
#!/bin/bash
# 1. Copy rootfs/ template to build/rootfs/
# 2. Generate genromfs image
# 3. Convert to C header (xxd or bin2c)
mkdir -p build/rootfs
cp -r rootfs/* build/rootfs/
genromfs -f build/romfs.img -d build/rootfs/ -V "eUX"
xxd -i build/romfs.img > build/romfs_data.h
```

**NuttX config for ROMFS:**

```makefile
# Enable NSH ROMFS /etc
CONFIG_NSH_ROMFSETC=y
CONFIG_NSH_ROMFSRC=y
CONFIG_NSH_INITSCRIPT="init.d/rcS"
CONFIG_NSH_ROMFSMOUNTPT="/etc"
CONFIG_NSH_ROMFSDEVNO=0
CONFIG_NSH_ROMFSSECTSIZE=64

# Enable ROMFS filesystem
CONFIG_FS_ROMFS=y
```

#### Checklist

- [x] Install `genromfs` tool (Ubuntu: `sudo apt install genromfs`)
- [x] Create `tools/mkrootfs.sh` — generates ROMFS image from `rootfs/`
- [x] Add `rootfs` and `romfs` targets to top-level `Makefile`
- [x] Integrate ROMFS image into NuttX build (linked C array or appended binary)
- [x] Enable `CONFIG_NSH_ROMFSETC=y` in defconfig
- [ ] Verify: boot → NuttX mounts `/etc` from ROMFS → `cat /etc/hostname` works
- [ ] Verify: `/etc/init.d/rcS` executes at startup

---

### 0.3 LittleFS Writable Partition

Flash partition for persistent writable storage at `/data`.

**NuttX config:**

```makefile
CONFIG_FS_LITTLEFS=y
CONFIG_MTD=y
CONFIG_MTD_PARTITION=y
CONFIG_RP23XX_FLASH_MTD=y
```

**Flash partitioning (in board BSP):**

```c
/* rp23xx_bringup.c — partition flash for LittleFS */
/* Firmware occupies first ~3MB, LittleFS gets the rest */
#define LITTLEFS_OFFSET  (3 * 1024 * 1024)  /* 3 MB */
#define LITTLEFS_SIZE    (12 * 1024 * 1024)  /* 12 MB */
```

#### Checklist

- [x] Configure flash MTD driver with partition table
- [x] Create LittleFS partition starting after firmware region
- [x] Auto-format LittleFS on first boot if unformatted
- [x] Mount at `/flash` during init (via `rp23xx_bringup.c`)
- [ ] Verify: `echo "test" > /flash/test.txt && cat /flash/test.txt` works
- [ ] Verify: data persists across reboots

---

### 0.4 Root Filesystem Content

#### `/etc/init.d/rcS`

```sh
# eUX OS — System initialization script
# Executed by NSH at boot via CONFIG_NSH_ROMFSETC
#
# By the time this runs, rp23xx_bringup.c has already:
#   - Mounted LittleFS at /flash (writable internal flash)
#   - Mounted SD card at /mnt/sd (if card present)
#   - Mounted tmpfs at /tmp, procfs at /proc, binfs at /bin
#   - Created /flash/{sbin,lib,etc,home,usr,var} + symlinks

mkdir -p /tmp/run
mkdir -p /flash/etc/eux
mkdir -p /flash/etc/wifi
mkdir -p /flash/etc/sv

# Copy default runit service templates from ROMFS to flash (first boot)
for svc in display audio network syslog; do
  if [ ! -d /flash/etc/sv/$svc ]; then
    mkdir -p /flash/etc/sv/$svc
    if [ -f /etc/sv/$svc/run ]; then
      cp /etc/sv/$svc/run /flash/etc/sv/$svc/run
    fi
  fi
done

# Ensure SD card user directories
if [ -d /mnt/sd ]; then
  mkdir -p /mnt/sd/home/user/documents
  mkdir -p /mnt/sd/home/user/music
  mkdir -p /mnt/sd/home/user/video
  mkdir -p /mnt/sd/home/user/pictures
  mkdir -p /mnt/sd/home/user/downloads
  mkdir -p /mnt/sd/apps
  mkdir -p /mnt/sd/ssh
fi

# Load hostname from writable flash
if [ -f /flash/etc/hostname ]; then
  hostname -F /flash/etc/hostname
fi

# Shell environment
export PATH=/bin:/sbin:/usr/bin
export HOME=/mnt/sd/home/user
export TERM=vt100
export PS1='\h:\w\$ '
export EDITOR=vi

# Source user profile from writable flash
if [ -f /flash/etc/profile ]; then
  . /flash/etc/profile
fi

# Start runit service supervision
if [ -d /flash/etc/sv ]; then
  runsvdir /flash/etc/sv &
fi

cat /etc/motd 2>/dev/null
```

#### `/etc/fstab`

```
# eUX OS filesystem table (reference — mounts done by rp23xx_bringup.c)
# <device>        <mount>     <type>      <options>
/dev/flash0       /flash      littlefs    defaults
/dev/mmcsd0       /mnt/sd     vfat        defaults
none              /tmp        tmpfs       defaults
none              /proc       procfs      defaults
none              /bin        binfs       defaults
```

#### `/etc/hostname`

```
eux
```

#### `/etc/profile`

```sh
# eUX OS shell profile
export PATH=/bin:/sbin:/usr/bin
export HOME=/mnt/sd/home/user
export TERM=vt100
export EDITOR=vi
export PS1='\h:\w\$ '

# Source user overrides from writable flash
if [ -f /flash/etc/profile ]; then
  . /flash/etc/profile
fi
```

#### `/etc/passwd`

```
root:x:0:0:System Administrator:/tmp:/bin/sh
user:x:1000:1000:Default User:/mnt/sd/home/user:/bin/sh
```

#### `/etc/motd`

```
  ___  _   _ __  __
 / _ \| | | |\ \/ /
|  __/| |_| | >  <    eUX OS v0.1
 \___| \___/ /_/\_\   NuttX / RP2350B

Type 'help' for available commands.
```

#### `/etc/eux/system.conf`

```json
{
  "version": 1,
  "display": {
    "brightness": 80,
    "timeout_sec": 120
  },
  "keyboard": {
    "backlight": 50,
    "repeat_rate_ms": 30,
    "repeat_delay_ms": 500
  },
  "audio": {
    "volume": 70,
    "speaker_enabled": true,
    "key_click": false
  },
  "power": {
    "profile": "standard",
    "sleep_timeout_sec": 300
  }
}
```

#### `/etc/eux/apps.conf`

```json
{
  "builtin": [
    {"id": "settings",  "name": "Settings",      "icon": "settings.bin",  "category": "system"},
    {"id": "pcfiles",   "name": "Files",          "icon": "files.bin",     "category": "system"},
    {"id": "pcterm",    "name": "Terminal",        "icon": "terminal.bin",  "category": "system"},
    {"id": "pcedit",    "name": "Text Editor",    "icon": "editor.bin",    "category": "office"},
    {"id": "pccsv",     "name": "Spreadsheet",    "icon": "sheet.bin",     "category": "office"},
    {"id": "pcaudio",   "name": "Music Player",   "icon": "audio.bin",     "category": "media"},
    {"id": "pcvideo",   "name": "Video Player",   "icon": "video.bin",     "category": "media"},
    {"id": "pcssh",     "name": "SSH Client",     "icon": "ssh.bin",       "category": "network"},
    {"id": "pcweb",     "name": "Web Browser",    "icon": "web.bin",       "category": "network"}
  ]
}
```

#### Checklist (rootfs content)

- [x] Write `rootfs/etc/init.d/rcS` with mount + service start logic
- [x] Write `rootfs/etc/fstab` with mount table
- [x] Write `rootfs/etc/hostname` with default `eux`
- [x] Write `rootfs/etc/profile` with PATH, PS1, HOME
- [x] Write `rootfs/etc/passwd` with root + user entries
- [x] Write `rootfs/etc/motd` with eUX ASCII banner
- [x] Write `rootfs/etc/eux/system.conf` with default settings JSON
- [x] Write `rootfs/etc/eux/apps.conf` with built-in app registry JSON
- [x] Write `rootfs/usr/share/doc/eux/welcome.txt`
- [ ] Verify ROMFS image contains all files (mount and inspect)

---

## Phase 1: Kernel & Board BSP

**Goal:** NuttX boots on RP2350B-Plus-W, all hardware peripherals validated, NuttShell accessible over UART.

**Status:** Largely complete from PicoCalc-Term work. Needs validation and cleanup.

### 1.1 Board Definition

**Path:** `boards/arm/rp23xx/picocalc-rp2350b/`

| File | Status | Notes |
|---|---|---|
| `include/board.h` | ✅ Done | 409 lines, all pin defs, south bridge registers |
| `src/rp23xx_boot.c` | ✅ Done | Early GPIO pin mux init |
| `src/rp23xx_bringup.c` | ✅ Done | Device registration sequence |
| `src/rp23xx_appinit.c` | ✅ Done | Board app init |
| `src/rp23xx_spi.c` | ✅ Done | SPI bus init |
| `src/rp23xx_lcd.c` | ✅ Done | ST7365P framebuffer driver |
| `src/rp23xx_keyboard.c` | ✅ Done | I2C keyboard via south bridge |
| `src/rp23xx_psram.c` | ✅ Done | XIP PSRAM driver (PSRAM on RP2350B-Plus-W module, XIP-mapped at 0x11000000) |
| `src/rp23xx_sdcard.c` | ✅ Done | SD card SPI + PIO SDIO |
| `src/rp23xx_audio.c` | ✅ Done | PWM audio driver |
| `src/rp23xx_southbridge.c` | ✅ Done | STM32 south bridge I2C |
| `src/rp23xx_clockmgr.c` | ✅ Done | Dynamic frequency scaling (5 profiles) |
| `src/rp23xx_sleep.c` | ✅ Done | Sleep/power management |
| `src/rp23xx_aonrtc.c` | ✅ Done | Always-on RTC |
| `src/rp23xx_flash_mtd.c` | ✅ Done | Flash MTD partition driver |
| `src/rp23xx_firmware.c` | ✅ Done | Firmware utility |
| `src/rp23xx_pio_sdio.c` | ✅ Done | PIO-based 1-bit SDIO |
| `configs/nsh/defconfig` | ✅ Done | Minimal NSH config |
| `configs/full/defconfig` | ✅ Done | Full OS config |
| `configs/lvgl/defconfig` | ❓ Untested | LVGL display test config |
| `scripts/memmap_default.ld` | ✅ Done | Linker script |
| `Kconfig` | ✅ Done | Board Kconfig |
| `Make.defs` | ✅ Done | Build definitions |

### 1.2 Defconfig Updates for eUX

The `full` defconfig needs adjustments for eUX:

```makefile
# === eUX OS Additions ===

# ROMFS root filesystem
CONFIG_NSH_ROMFSETC=y
CONFIG_NSH_ROMFSRC=y
CONFIG_NSH_INITSCRIPT="init.d/rcS"
CONFIG_NSH_ROMFSMOUNTPT="/etc"
CONFIG_FS_ROMFS=y

# LittleFS writable partition
CONFIG_FS_LITTLEFS=y

# Hostname support
CONFIG_NET_HOSTNAME=y
CONFIG_LIBC_HOSTNAME_MAX=32

# Init entry point
CONFIG_INIT_ENTRYPOINT="eux_init"

# Process filesystem
CONFIG_FS_PROCFS=y
CONFIG_FS_PROCFS_REGISTER=y

# Tmpfs
CONFIG_FS_TMPFS=y
```

#### Checklist

- [x] Board definition files complete and building
- [x] PSRAM XIP driver rewritten and validated (was PIO-driven, now XIP at 0x11000000)
- [x] Audio PWM slice mapping corrected
- [x] Boot code cleaned up (dead code removed)
- [x] Duplicate defines removed from board.h
- [x] Add ROMFS config to `full/defconfig`
- [x] Add LittleFS config to `full/defconfig`
- [x] Add tmpfs config to `full/defconfig`
- [x] Update `CONFIG_INIT_ENTRYPOINT` to `"eux_init"`
- [ ] Verify UART console boot to `nsh>` prompt
- [ ] Verify `free` shows SRAM + PSRAM
- [ ] Verify all devices in `/dev` (fb0, input0, audio0, mmcsd0, i2c0, spi0, spi1)
- [ ] Test dynamic frequency scaling via `clockset`

---

## Phase 2: Root Filesystem & Init System

**Goal:** Firmware boots with ROMFS root filesystem, executes init scripts, mounts writable partitions, presents a configured shell environment.

### 2.1 ROMFS Integration

The ROMFS image is linked into the NuttX firmware binary. On boot, NuttX mounts it at `/etc` (via `CONFIG_NSH_ROMFSETC`), and the init script sets up the rest.

**Integration method:** NuttX's `nsh_romfsetc` mechanism:

1. ROMFS image compiled as C array into firmware
2. Registered as a block device at boot
3. Mounted at `/etc`
4. NSH executes `/etc/init.d/rcS`

**Build integration:**

```makefile
# Makefile addition
rootfs:
	@echo "==> Generating ROMFS image"
	@./tools/mkrootfs.sh

build: rootfs
	# ... existing build steps (ROMFS is built before NuttX compile)
```

#### Checklist

- [x] Create `tools/mkrootfs.sh` script
- [x] Generate valid ROMFS image from `rootfs/`
- [x] Link ROMFS into firmware build
- [ ] Verify `ls /etc` shows all ROMFS files
- [ ] Verify `cat /etc/hostname` returns `eux`
- [ ] Verify `cat /etc/motd` prints eUX banner
- [ ] Verify `cat /etc/eux/system.conf` shows JSON config

### 2.2 Init System

The init entry point (`eux_init`) replaces `pcterm_main` as the system's PID 1.

**File:** `pcterm/src/init.c` (new, or refactored from `main.c`)

```c
/* eux_init() — PID 1, the first userspace process */
int eux_init(int argc, char *argv[])
{
    /* Phase 1: Mount filesystems (executed by NSH ROMFS mechanism) */
    /* /etc already mounted by NSH romfsetc */

    /* Phase 2: Mount additional filesystems */
    mount_littlefs();   /* /data */
    mount_sdcard();     /* /mnt/sd */
    mount_tmpfs();      /* /tmp */

    /* Phase 3: Load configuration */
    load_hostname();    /* /data/etc/hostname → /etc/hostname */
    load_system_conf(); /* /data/etc/eux/system.conf overlay */

    /* Phase 4: Initialize hardware services */
    init_display();     /* LVGL + framebuffer */
    init_keyboard();    /* I2C keyboard + LVGL input driver */
    init_audio();       /* PWM audio service */

    /* Phase 5: Start network (if configured) */
    start_wifi();       /* Background: connect to saved network */

    /* Phase 6: Present user interface */
    start_launcher();   /* LVGL launcher (or NSH shell) */

    /* Event loop — never returns */
    while (1) {
        uint32_t ms = lv_timer_handler();
        usleep(ms * 1000);
    }
}
```

#### Checklist

- [x] Create `eux_init` entry point (refactored from `pcterm_main`)
- [x] Init script `/etc/init.d/rcS` executes on boot
- [x] LittleFS mounts at `/flash` (via `rp23xx_bringup.c`)
- [x] SD card mounts at `/mnt/sd` (via `rp23xx_bringup.c`)
- [x] tmpfs mounts at `/tmp` (via `rp23xx_bringup.c`)
- [x] Hostname loaded from flash (`/flash/etc/hostname`)
- [x] Shell environment set (PATH, PS1, HOME, EDITOR)
- [ ] `echo $PATH` returns `/bin:/sbin:/usr/bin`
- [ ] `pwd` in new shell starts at `$HOME`
- [x] runit service supervisor implemented (`pcterm/src/runit.c`)
- [x] Service directories created: `/etc/sv/display/run`, `/etc/sv/audio/run`, `/etc/sv/network/run`, `/etc/sv/syslog/run`
- [x] `sv` command available from NSH for service control (`sv start|stop|restart|status <name>`)
- [ ] Verify runit auto-restarts crashed services on target

### 2.3 Configuration Overlay System

**File:** `pcterm/src/config.c` (existing, needs overlay logic)

```c
/* Config overlay: check /flash/etc first, fall back to /mnt/sd/etc */
const char *config_resolve(const char *relpath)
{
    static char buf[128];

    /* Try writable flash override first */
    snprintf(buf, sizeof(buf), "/flash/etc/%s", relpath);
    if (access(buf, R_OK) == 0) return buf;

    /* Try SD card */
    snprintf(buf, sizeof(buf), "/mnt/sd/etc/%s", relpath);
    if (access(buf, R_OK) == 0) return buf;

    /* Fall back to flash path (will be created on first save) */
    snprintf(buf, sizeof(buf), "/flash/etc/%s", relpath);
    return buf;
}

/* Save config always goes to /flash/etc */
const char *config_save_path(const char *relpath)
{
    static char buf[128];
    snprintf(buf, sizeof(buf), "/flash/etc/%s", relpath);
    return buf;
}
```

#### Checklist

- [x] Implement `config_resolve()` overlay lookup (flash → SD → defaults)
- [x] Implement `config_save_path()` → always writes to `/flash/etc/`
- [x] Implement `config_home_dir()` → SD home or flash fallback
- [ ] Settings app reads defaults from compiled `pc_config_defaults()`
- [x] Settings changes persist to `/flash/etc/eux/settings.json`
- [x] Config survives reboot (stored on flash LittleFS)
- [x] Deleting `/flash/etc/` restores compiled defaults ("factory reset")

### 2.4 User & Login (Simplified)

eUX supports a simplified user model. No multi-user enforcement, but the structure is in place.

| Concept | Implementation |
|---|---|
| `/etc/passwd` | Static file in ROMFS with `root` and `user` entries |
| Login | Optional — auto-login to `user` if not configured otherwise |
| Home directory | `/mnt/sd/home/user/` (on SD card) |
| UID/GID | `root:0:0`, `user:1000:1000` |

#### Checklist

- [x] `/etc/passwd` present in ROMFS
- [x] `$HOME` set to `/mnt/sd/home/user` at login
- [ ] `whoami` returns `user` (or `root` if serial console)
- [x] Home directory auto-created on SD card at first boot

---

## Phase 3: Display & Input Subsystem

**Goal:** Framebuffer displays graphics, keyboard input works through LVGL, virtual console available.

**Status:** Display and keyboard drivers already implemented. Need integration with eUX init system.

### 3.1 Framebuffer Driver

**File:** `boards/.../src/rp23xx_lcd.c` (✅ Existing)

| Feature | Status |
|---|---|
| ST7365P init sequence | ✅ Done |
| SPI1 @ 25 MHz with DMA | ✅ Done |
| NuttX `fb_vtable_s` interface | ✅ Done |
| Dirty-rect partial flush | ✅ Done |
| PSRAM framebuffer allocation | ✅ Done |

### 3.2 Keyboard Driver

**File:** `boards/.../src/rp23xx_keyboard.c` (✅ Existing)

| Feature | Status |
|---|---|
| South bridge I2C protocol | ✅ Done |
| Scancode → LVGL key mapping | ✅ Done |
| Fn modifier key combinations | ✅ Done |
| NuttX `/dev/input0` interface | ✅ Done |

### 3.3 LVGL Integration

**Files:** `pcterm/src/lv_port_disp.c`, `pcterm/src/lv_port_indev.c` (✅ Existing)

| Feature | Status |
|---|---|
| LVGL display driver on `/dev/fb0` | ✅ Done |
| LVGL keypad input driver on `/dev/input0` | ✅ Done |
| 1/10 draw buffer (20 KB SRAM) | ✅ Done |
| LVGL thread model (30 Hz tick) | ✅ Done |

### 3.4 Virtual Console

**File:** `pcterm/src/vconsole.c` (✅ Existing)

For eUX, the virtual console becomes an important concept — it's the text-mode console that can run alongside or instead of the GUI.

#### Checklist

- [x] `/dev/fb0` device appears and works
- [x] Keyboard reads scancodes, maps to LVGL events
- [x] LVGL displays widgets on 320×320 screen
- [x] DMA-accelerated SPI flush working
- [ ] Virtual console integrated with eUX init (accessible via key combo)
- [ ] Display init happens in `eux_init` Phase 4 (not hardcoded)

---

## Phase 4: System Services

**Goal:** Status bar, audio service, clock/RTC, and Wi-Fi service running as background components.

### 4.1 Status Bar

**File:** `pcterm/src/statusbar.c` (✅ Existing)

```
┌──────────────────────────────────────────────────────────────┐
│ eux      │ ▁▂▃ WiFi │ ████ 87% │                     14:32 │
└──────────────────────────────────────────────────────────────┘
```

| Feature | Status |
|---|---|
| Hostname display | ✅ Done |
| Wi-Fi signal indicator | ✅ Done |
| Battery percentage | ✅ Done |
| Clock display | ✅ Done |
| 1-second refresh timer | ✅ Done |

#### Checklist

- [x] Status bar renders hostname, Wi-Fi, battery, clock
- [ ] Hostname reads from eUX config overlay (not hardcoded)
- [ ] Status bar uses eUX theming from `/etc/eux/system.conf`

### 4.2 Audio Service

**File:** `pcterm/src/audio_service.c` (✅ Existing)

| Feature | Status |
|---|---|
| Background playback thread | ✅ Done |
| minimp3 MP3 decoding | ✅ Done |
| WAV PCM playback | ✅ Done |
| PSRAM ring buffer (64 KB) | ✅ Done |
| Volume control | ✅ Done |
| play/pause/stop/next/prev | ✅ Done |

#### Checklist

- [x] Audio service functional
- [ ] Volume defaults loaded from `pc_config_defaults()`
- [ ] Volume changes saved to `/flash/etc/eux/settings.json`

### 4.3 Clock / RTC Service

**File:** `boards/.../src/rp23xx_aonrtc.c` (✅ Existing)

| Feature | Status |
|---|---|
| Always-on RTC driver | ✅ Done |
| Time sync via NTP | ✅ Done (`pcntp_main.c`) |

#### Checklist

- [x] RTC keeps time across sleep
- [x] NTP time sync available via `pcntp` command
- [ ] Auto NTP sync on Wi-Fi connect (in init script or network service)

### 4.4 Wi-Fi Service

**Files:** `nuttx-apps/system/pcssh/pcwifi_main.c` (✅ Existing)

| Feature | Status |
|---|---|
| Wi-Fi scan | ✅ Done |
| Wi-Fi connect (WPA2) | ✅ Done |
| DHCP client | ✅ Done (NuttX built-in) |
| Saved network credentials | ✅ Done |

#### Checklist

- [x] `pcwifi scan` lists networks
- [x] `pcwifi connect <ssid>` connects
- [ ] Wi-Fi config stored at `/flash/etc/wifi/wpa_supplicant.conf`
- [ ] Auto-connect to saved network on boot (if configured in settings)
- [ ] Hostname sent in DHCP requests

---

## Phase 5: Shell & Core Utilities

**Goal:** NSH presents a proper Unix shell experience with correct paths, environment, and utilities accessible at `/bin/`.

### 5.1 Shell Environment

NuttX's NSH already provides most Unix commands as builtins. In eUX, we ensure they feel like a proper Unix shell.

**Shell behavior:**

| Feature | NuttX NSH | eUX Additions |
|---|---|---|
| Prompt | `nsh>` | `eux:/mnt/sd/home/user$` (via `/etc/profile`) |
| PATH | N/A (builtins only) | `/bin:/sbin:/usr/bin` |
| HOME | N/A | `/mnt/sd/home/user` |
| `cd ~` | No | `$HOME` expansion |
| Tab completion | Partial | Files + commands |
| History | Yes (limited) | Persist to `/data/cache/.history` |
| Pipes | Yes | `ls | grep foo` |
| Redirect | Yes | `cmd > file`, `cmd >> file` |
| Background | Yes | `cmd &` |
| `$?` | Yes | Exit status |
| Scripts | Basic | `/etc/init.d/rcS`, user scripts |

### 5.2 Core Utilities Mapping

NuttX builtins that map to Unix `/bin/` commands:

| Unix Command | NuttX Builtin | Config Flag | Status |
|---|---|---|---|
| `ls` | `ls` | Built-in | ✅ |
| `cat` | `cat` | Built-in | ✅ |
| `cp` | `cp` | Built-in | ✅ |
| `mv` | `mv` | Built-in | ✅ |
| `rm` | `rm` | Built-in | ✅ |
| `mkdir` | `mkdir` | Built-in | ✅ |
| `rmdir` | `rmdir` | Built-in | ✅ |
| `mount` | `mount` | Built-in | ✅ |
| `umount` | `umount` | Built-in | ✅ |
| `df` | `df` | Built-in | ✅ |
| `free` | `free` | Built-in | ✅ |
| `ps` | `ps` | Built-in | ✅ |
| `kill` | `kill` | Built-in | ✅ |
| `sleep` | `sleep` | Built-in | ✅ |
| `echo` | `echo` | Built-in | ✅ |
| `test` | `test` | Built-in | ✅ |
| `uname` | `uname` | `CONFIG_SYSTEM_UNAME=y` | ✅ |
| `hostname` | `hostname` | Custom | ✅ |
| `vi` | `vi` | `CONFIG_SYSTEM_VI=y` | ✅ |
| `ping` | `ping` | `CONFIG_SYSTEM_PING=y` | ✅ |
| `ifconfig` | `ifconfig` | Built-in (net) | ✅ |
| `dmesg` | `dmesg` | Custom | ✅ |
| `lua` | `lua` | `CONFIG_INTERPRETERS_LUA=y` | ✅ |
| `qjs` | `qjs` | `CONFIG_INTERPRETERS_QUICKJS=y` | ✅ |
| `lscpu` | `lscpu` | Custom | ✅ |
| `lsi2c` | `lsi2c` | Custom | ✅ |
| `lsspi` | `lsspi` | Custom | ✅ |
| `screenfetch` | `screenfetch` | Custom | ✅ |
| `clockset` | `clockset` | Custom | ✅ |
| `pcwifi` | `pcwifi` | Custom | ✅ |
| `pcssh` | `pcssh` (CLI) | Custom | ✅ |
| `pcscp` | `pcscp` | Custom | ✅ |

### 5.3 `uname` Output

```
$ uname -a
eUX 0.1.0 eux NuttX 12.x.x picocalc-rp2350b armv8-m 2026-03-11
```

Customized via:
```makefile
CONFIG_SYSTEM_UNAME=y
CONFIG_LIBC_HOSTNAME_MAX=32
```

#### Checklist

- [x] NSH boots with all builtins available
- [x] vi editor works (`CONFIG_SYSTEM_VI=y`)
- [x] Lua and QuickJS interpreters available
- [x] Custom commands registered (lscpu, clockset, screenfetch, etc.)
- [ ] `/etc/profile` sources on login (PATH, PS1, HOME set)
- [ ] Shell prompt shows `<hostname>:<cwd>$ ` format
- [ ] `uname -a` output includes eUX version and platform
- [ ] Command history persists to `/flash/var/cache/.history`
- [ ] `which <cmd>` resolves command location

---

## Phase 6: Window Manager & App Framework

**Goal:** Launcher, app lifecycle, state save/restore, and package system working.

### 6.1 Launcher

**File:** `pcterm/src/launcher.c` (✅ Existing)

```
┌──────────────────────────────────────┐  ← Status bar (20px)
├──────────────────────────────────────┤
│  ┌──────┐  ┌──────┐  ┌──────┐       │
│  │ ⚙   │  │ 📁  │  │ >_   │       │  Row 1: System
│  │ Set- │  │ File │  │ Term │       │
│  │ tings│  │  s   │  │ inal │       │
│  └──────┘  └──────┘  └──────┘       │
│  ┌──────┐  ┌──────┐  ┌──────┐       │
│  │ 📝  │  │ 📊  │  │ 🎵  │       │  Row 2: Office+Media
│  │ Text │  │Spread│  │Music │       │
│  │Editor│  │Sheet │  │Player│       │
│  └──────┘  └──────┘  └──────┘       │
│  ┌──────┐  ┌──────┐  ┌──────┐       │
│  │ 🎬  │  │ 🔒  │  │ 🌐  │       │  Row 3: Media+Network
│  │Video │  │ SSH  │  │ Web  │       │
│  │Player│  │Client│  │Browse│       │
│  └──────┘  └──────┘  └──────┘       │
└──────────────────────────────────────┘
```

| Feature | Status |
|---|---|
| App grid with icons | ✅ Done |
| Arrow key navigation | ✅ Done |
| Enter to launch | ✅ Done |
| Fn+Home returns to launcher | ✅ Done |
| Built-in + third-party apps in grid | ✅ Done |

#### Checklist

- [x] Launcher displays 9 built-in apps + installed packages
- [x] Navigation and launch working
- [ ] App registry loaded from `/etc/eux/apps.conf` (built-ins) + SD registry (third-party)
- [ ] Launcher theming from system.conf

### 6.2 App Lifecycle Framework

**File:** `pcterm/src/app_framework.c` (✅ Existing)

| Feature | Status |
|---|---|
| App registration & launch | ✅ Done |
| setjmp/longjmp lifecycle | ✅ Done |
| State save (yield) | ✅ Done |
| State restore | ✅ Done |
| ELF loading for third-party | ✅ Done |
| PSRAM allocation per-app | ✅ Done |
| Event callback system | ✅ Done |

#### Checklist

- [x] Built-in apps launch and return correctly
- [x] State save/restore works (Fn+Home → resume)
- [x] Third-party ELF loading from SD card
- [ ] State saved to `/flash/etc/appstate/` (writable flash)
- [ ] App crash handler logs to `/flash/var/log/crash.log`

### 6.3 Package Manager

**File:** `pcterm/src/package_manager.c` (✅ Existing)

| Feature | Status |
|---|---|
| `.pcpkg` parsing (PCPK magic, file table) | ✅ Done |
| Install to `/mnt/sd/apps/<name>/` | ✅ Done |
| Registry management | ✅ Done |
| Uninstall | ✅ Done |
| Catalog fetch (app store) | ✅ Done |
| SD card scan & install | ✅ Done |

#### Checklist

- [x] Package install/uninstall functional
- [x] App store catalog fetch
- [ ] Package manager uses eUX config paths
- [ ] `pcminipkg` CLI command works for installing from shell

---

## Phase 7: Built-in Applications

**Goal:** All 9 built-in applications functional and polished.

### 7.1 Settings App (`settings`)

**Path:** `apps/settings/`

| Feature | Status |
|---|---|
| Wi-Fi settings (scan/connect) | ✅ Done |
| Display settings (brightness, timeout) | ✅ Done |
| Audio settings (volume, key click) | ✅ Done |
| Keyboard settings (backlight, repeat) | ✅ Done |
| Storage management (eject, usage) | ✅ Done |
| System info (hostname, reboot) | ✅ Done |
| Package management (install/remove) | ✅ Done |

#### Checklist

- [x] All settings sections functional
- [ ] Settings reads/writes via eUX config overlay system
- [ ] "Factory Reset" button → deletes `/flash/etc/eux/`
- [x] "About" section shows eUX version, NuttX version, platform

### 7.2 File Explorer (`pcfiles`)

**Path:** `apps/pcfiles/`

| Feature | Status |
|---|---|
| Directory listing | ✅ Done |
| Navigation (enter, back) | ✅ Done |
| File preview | ✅ Done |
| Copy/Move/Delete/Mkdir | ✅ Done |
| Path switching (SD ↔ flash) | ✅ Done |

#### Checklist

- [x] File operations working
- [x] Root navigation starts at `$HOME` (SD card user home)
- [x] Can browse `/etc`, `/flash`, `/mnt/sd`, `/proc`
- [ ] Read-only indicator for ROMFS paths

### 7.3 Text Editor (`pcedit`)

**Path:** `apps/pcedit/`

| Feature | Status |
|---|---|
| vi mode engine (Normal/Insert/Command) | ✅ Done |
| Gap buffer in PSRAM | ✅ Done |
| Syntax highlighting (7 languages) | ✅ Done |
| Search/replace with regex | ✅ Done |
| Multiple buffers | ✅ Done |
| Macro recording | ✅ Done |
| Lua plugin system | ✅ Done |
| State save/restore | ✅ Done |

**Remaining stubs:** _(All resolved)_

| Feature | Status | Priority |
|---|---|---|
| `d` operator-pending delete (dd, dw, d$) | ✅ Done | — |
| `u` undo / `Ctrl-R` redo | ✅ Done | — |
| `o`/`O` newline insertion | ✅ Done | — |
| `x`/`X` delete char | ✅ Done | — |
| `D` delete to EOL | ✅ Done | — |
| `Y`/`yy` yank line | ✅ Done | — |
| `p`/`P` put/paste | ✅ Done | — |
| `r` replace char | ✅ Done | — |
| `J` join lines | ✅ Done | — |
| `s`/`S`/`C` substitute/change | ✅ Done | — |
| `~` toggle case | ✅ Done | — |
| `m`/`'`/`` ` `` marks | ✅ Done | — |
| Insert `Ctrl-W`/`Ctrl-U` | ✅ Done | — |

#### Checklist

- [x] Core vi editing functional
- [x] Implement `d` delete operator (dd, dw, d$, etc.)
- [x] Implement `u` undo / `Ctrl-R` redo (200-level stack)
- [x] Implement `o`/`O` newline insert in gap buffer
- [ ] Default file save path follows `$HOME/documents/`

### 7.4 Spreadsheet (`pccsv`)

**Path:** `apps/pccsv/`

| Feature | Status |
|---|---|
| RFC 4180 CSV parser | ✅ Done |
| LVGL table widget | ✅ Done |
| Cell editing | ✅ Done |
| State save/restore | ✅ Done |

**Remaining:** _(All resolved)_

| Feature | Status | Priority |
|---|---|---|
| Visual cell selection highlight | ✅ Done | — |

#### Checklist

- [x] CSV load/save/edit working
- [x] Visual cell highlight for current selection (LVGL draw event)

### 7.5 Audio Player (`pcaudio`)

**Path:** `apps/pcaudio/`

| Feature | Status |
|---|---|
| MP3 decode (minimp3) | ✅ Done |
| WAV playback | ✅ Done |
| Playlist (m3u + directory) | ✅ Done |
| Background playback | ✅ Done |
| Playback UI (play/pause/progress/volume) | ✅ Done |
| State save/restore | ✅ Done |

#### Checklist

- [x] Audio player fully functional
- [x] Default music directory: `$HOME/music/`

### 7.6 Video Player (`pcvideo`)

**Path:** `apps/pcvideo/`

| Feature | Status |
|---|---|
| .pcv format parser | ✅ Done |
| Frame decode pipeline | ✅ Done |
| Interleaved audio playback | ✅ Done |
| Playback controls | ✅ Done |
| State save/restore | ✅ Done |

#### Checklist

- [x] Video player functional
- [x] Default video directory: `$HOME/video/`

### 7.7 Terminal Emulator (`pcterm`)

**Path:** `apps/pcterm/`

| Feature | Status |
|---|---|
| VT100/ANSI terminal emulator | ✅ Done |
| NuttShell integration via PTY | ✅ Done |
| PSRAM scrollback (2000 lines) | ✅ Done |
| State save/restore | ✅ Done |

**Remaining:**

| Feature | Status | Priority |
|---|---|---|
| `terminal_resize()` | STUB | Low (fixed display) |

#### Checklist

- [x] Terminal emulator with NuttShell
- [ ] Shell prompt in terminal matches eUX format (`eux:/path$`)
- [ ] Terminal respects `/etc/profile` environment

### 7.8 SSH Client (`pcssh`)

**Path:** `apps/pcssh/`

| Feature | Status |
|---|---|
| wolfSSH session management | ✅ Done |
| Terminal emulation (shared widget) | ✅ Done |
| SCP file transfer | ✅ Done |
| SFTP file browser | ✅ Done |
| Saved connections | ✅ Done |
| State save/restore | ✅ Done |

#### Checklist

- [x] SSH client functional
- [x] SSH connections stored at `/flash/etc/ssh/connections.json`
- [ ] SSH keys stored at `/mnt/sd/ssh/`
- [ ] Known hosts at `/mnt/sd/ssh/known_hosts`

### 7.9 Web Browser (`pcweb`)

**Path:** `apps/pcweb/`

| Feature | Status |
|---|---|
| HTTP/HTTPS client (wolfSSL) | ✅ Done |
| HTML subset parser | ✅ Done |
| Text+image rendering | ✅ Done |
| Link navigation | ✅ Done |
| Address bar, bookmarks, history | ✅ Done |
| BMP/PNG inline images | ✅ Done |
| State save/restore | ✅ Done |

#### Checklist

- [x] Web browser functional
- [x] Bookmarks stored at `/flash/home/user/.bookmarks.json`
- [ ] Downloads saved to `$HOME/downloads/`

---

## Phase 8: Networking

**Goal:** Complete network stack with Wi-Fi, SSH, web, and NTP integration.

### 8.1 Wi-Fi Stack

| Feature | Status |
|---|---|
| CYW43439 driver | ✅ Done |
| WPA2 connection | ✅ Done |
| DHCP client | ✅ Done |
| DNS resolution | ✅ Done |
| BSD sockets API | ✅ Done |

### 8.2 wolfSSL / wolfSSH

| Feature | Status |
|---|---|
| wolfSSL TLS library | ✅ Done |
| wolfSSH library | ✅ Done (cloned) |
| SSH key generation | ✅ Done |

### 8.3 Network Utilities

| Command | Status |
|---|---|
| `ping` | ✅ Done |
| `ifconfig` | ✅ Done |
| `pcwifi` | ✅ Done |
| `pcssh` (CLI) | ✅ Done |
| `pcscp` (CLI) | ✅ Done |
| `pcntp` | ✅ Done |

#### Checklist

- [x] Wi-Fi connects and gets IP
- [x] SSH/SCP/SFTP functional
- [x] HTTPS works (wolfSSL)
- [x] NTP time sync
- [ ] Wi-Fi credentials stored in `/flash/etc/wifi/wpa_supplicant.conf`
- [ ] Auto-NTP sync on network connect
- [ ] `hostname` sent in DHCP requests

---

## Phase 9: Polish & Distribution

**Goal:** Production-ready OS with power management, robust boot, and developer SDK.

### 9.1 Power Management

| Feature | Status |
|---|---|
| Battery monitoring (AXP2101 via south bridge) | ✅ Done |
| Screen timeout / dim | ✅ Done |
| Sleep mode | ✅ Done |
| Dynamic frequency scaling | ✅ Done |

#### Checklist

- [x] Battery percentage on status bar
- [x] Screen timeout configurable
- [ ] Low battery warning notification (< 10%)
- [ ] Clean shutdown on critical battery (< 5%)
- [ ] Power config from `pc_config_get()` settings

### 9.2 Global Hotkeys

| Hotkey | Action | Status |
|---|---|---|
| `Fn+Home` | Return to launcher | ✅ Done |
| `Fn+Space` | Audio play/pause | ✅ Done |
| `Fn+→` | Next track | ✅ Done |
| `Fn+←` | Previous track | ✅ Done |
| `Fn+W` | Wi-Fi toggle | ✅ Done |
| `Fn+B` | Brightness cycle | ✅ Done |
| `Fn+V` | Volume cycle | ✅ Done |

#### Checklist

- [x] All hotkeys functional
- [ ] Hotkey bindings configurable via `/data/etc/eux/keys.conf`

### 9.3 Boot Splash

**File:** `pcterm/src/boot_splash.c` (✅ Existing)

#### Checklist

- [x] Boot splash displays logo
- [x] Boot splash shows eUX branding

### 9.4 OTA Firmware Update

| Feature | Status |
|---|---|
| Firmware download via HTTP | Not started |
| Version check | Not started |
| UF2 write to flash | Not started |

#### Checklist

- [ ] Settings → System → Check for Update
- [ ] Download `.uf2` to `/mnt/sd/firmware/`
- [ ] Reboot into bootloader for flash

### 9.5 SDK & Documentation

| Deliverable | Status |
|---|---|
| App template project | ✅ Done (`sdk/template/`) |
| API header files | ✅ Done (`pcterm/include/`) |
| API documentation | ✅ Done (`docs/App Framework API.md`) |

#### Checklist

- [x] SDK template exists
- [ ] SDK documentation updated for eUX paths and conventions
- [ ] Sample third-party app builds and installs correctly
- [ ] Package format spec up to date

### 9.6 Robustness & Error Handling

#### Checklist

- [ ] Watchdog timer enabled (recovery from lockup)
- [ ] Filesystem corruption recovery (LittleFS auto-repair)
- [ ] Graceful fallback if SD card missing (boot to shell only)
- [ ] Kernel panic handler with diagnostic dump
- [ ] `dmesg` captures boot log for debugging

---

## Summary: Master Checklist

### Phase 0: Foundation
- [x] `rootfs/` directory tree created
- [x] All rootfs config files written (rcS, fstab, hostname, profile, passwd, motd, system.conf, apps.conf)
- [x] `tools/mkrootfs.sh` generates ROMFS image
- [x] ROMFS linked into firmware build
- [x] `genromfs` tool available
- [x] LittleFS flash partition configured
- [x] Build pipeline: `make rootfs` → `make build` → `nuttx.uf2` with embedded ROMFS

### Phase 1: Kernel & BSP
- [x] Board definition complete
- [x] All hardware drivers implemented
- [x] Defconfig updated with ROMFS + LittleFS + tmpfs configs
- [ ] UART console boot verified
- [ ] All `/dev` devices present

### Phase 2: Root Filesystem & Init
- [x] `eux_init` entry point created
- [x] ROMFS mounts at `/etc` on boot
- [x] Init script executes (`/etc/init.d/rcS`)
- [x] LittleFS at `/flash`, FAT32 at `/mnt/sd`, tmpfs at `/tmp`
- [x] Config overlay system (flash → SD → defaults)
- [x] Hostname loaded from `/flash/etc/hostname`
- [x] Shell environment configured (PATH, PS1, HOME)

### Phase 3: Display & Input
- [x] Framebuffer driver working
- [x] Keyboard driver working
- [x] LVGL integration complete
- [x] Display init in eUX init sequence

### Phase 4: System Services
- [x] Status bar functional
- [x] Audio service functional
- [x] RTC + NTP
- [x] Wi-Fi management
- [ ] All services use eUX config overlay for settings load/save

### Phase 5: Shell & Utilities
- [x] NSH with all builtins
- [x] vi editor, Lua, QuickJS
- [x] Custom commands (lscpu, clockset, etc.)
- [ ] Shell profile sourced on login
- [ ] Proper Unix-style prompt

### Phase 6: Window Manager
- [x] Launcher functional
- [x] App lifecycle (launch, yield, exit)
- [x] State save/restore
- [x] Package manager
- [ ] App registry from `/etc/eux/apps.conf`

### Phase 7: Applications
- [x] All 9 apps implemented (86/87 functions)
- [x] pcedit: All vi stubs resolved (d, u, o/O, x, X, D, Y, p, P, r, J, s, S, C, ~, m, marks, Ctrl-W, Ctrl-U)
- [x] pccsv: visual cell highlight implemented (LVGL draw event)
- [ ] pcterm: terminal_resize (1 stub, low priority — fixed 320×320 display)
- [x] All apps use eUX paths ($HOME, /flash, etc.)

### Phase 8: Networking
- [x] Wi-Fi, SSH, HTTPS, NTP all functional
- [ ] Credentials in `/flash/etc/wifi/`
- [ ] Auto-NTP on connect

### Phase 9: Polish
- [x] Power management, hotkeys, boot splash
- [x] eUX branding in boot splash, login, terminal, about screens
- [ ] OTA update system
- [ ] Robustness (watchdog, corruption recovery)

---

## Immediate Next Steps (Recommended Order)

1. ~~**Create `rootfs/` directory with all config files**~~ — ✅ Done
2. ~~**Build ROMFS pipeline** (`tools/mkrootfs.sh` + Makefile targets)~~ — ✅ Done
3. ~~**Update defconfig** — add ROMFS, LittleFS, tmpfs, init entry point~~ — ✅ Done
4. ~~**Refactor `pcterm_main` → `eux_init`**~~ — ✅ Done
5. ~~**Implement config overlay** — flash → SD → compiled defaults~~ — ✅ Done
6. **Build and verify** — boot with embedded ROMFS, see `/etc/hostname` = `eux`
7. ~~**Update paths** in all apps — use eUX config overlay and `$HOME` convention~~ — ✅ Done
8. ~~**Resolve remaining stubs** — pcedit vi operations (d, u, o/O)~~ — ✅ Done
9. ~~**Implement runit service supervisor**~~ — ✅ Done
10. ~~**eUX branding** — boot splash, login, terminal, about, User-Agent~~ — ✅ Done
