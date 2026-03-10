# PicoCalc-Term: Detailed Implementation Plan

This document expands each development phase from the [Project Plan](Project%20Plan.md) into concrete, actionable tasks with technical specifics, file paths, NuttX configuration, dependencies, and estimated effort.

---

## Project Directory Structure

```
picocalc-term/
├── docs/                           # Documentation
│   ├── Project Plan.md
│   ├── Detailed Implementation Plan.md
│   ├── App Framework API.md
│   └── PCV Format Spec.md
│
├── nuttx/                          # NuttX source (git submodule)
├── apps/                           # NuttX apps (git submodule)
│
├── boards/                         # Custom board definition
│   └── arm/rp23xx/picocalc-rp2350b/
│       ├── include/
│       │   └── board.h             # Pin definitions, clock config
│       ├── src/
│       │   ├── rp23xx_boot.c       # Board init sequence
│       │   ├── rp23xx_bringup.c    # Peripheral registration
│       │   ├── rp23xx_spi.c        # SPI bus init (display + SD)
│       │   ├── rp23xx_i2c.c        # I2C bus init (keyboard + BL)
│       │   ├── rp23xx_lcd.c        # ST7365P framebuffer driver
│       │   ├── rp23xx_keyboard.c   # I2C keyboard input driver
│       │   ├── rp23xx_psram.c      # PSRAM heap setup
│       │   ├── rp23xx_audio.c      # PWM audio driver
│       │   ├── rp23xx_sdcard.c     # SD card SPI + FAT32 mount
│       │   └── rp23xx_appinit.c    # Application startup
│       ├── configs/
│       │   ├── nsh/defconfig        # Minimal NuttShell config
│       │   ├── lvgl/defconfig       # LVGL + display + input config
│       │   └── full/defconfig       # Full OS config (all apps)
│       ├── scripts/
│       │   └── flash.ld            # Linker script (16MB flash)
│       ├── Kconfig
│       └── Make.defs
│
├── pcterm/                         # PicoCalc-Term OS layer
│   ├── include/
│   │   ├── pcterm/app.h            # App framework API
│   │   ├── pcterm/appstate.h       # App state save/restore API
│   │   ├── pcterm/package.h        # Package manager API
│   │   ├── pcterm/launcher.h       # Launcher interface
│   │   ├── pcterm/statusbar.h      # Status bar widget
│   │   ├── pcterm/terminal.h       # VT100 terminal emulator widget
│   │   ├── pcterm/config.h         # System settings API
│   │   └── pcterm/hostname.h       # Hostname management
│   ├── src/
│   │   ├── app_framework.c         # App lifecycle, ELF loader
│   │   ├── app_state.c             # State save/restore to PSRAM+SD
│   │   ├── package_manager.c       # Manifest parser, registry
│   │   ├── launcher.c              # Home screen / app grid
│   │   ├── statusbar.c             # Status bar (hostname, wifi, batt, clock)
│   │   ├── terminal_widget.c       # Shared VT100/ANSI terminal renderer
│   │   ├── config.c                # Settings persistence (JSON)
│   │   ├── hostname.c              # Hostname load/set from SD
│   │   └── main.c                  # OS entry point, boot sequence
│   └── Makefile
│
├── apps/                           # Built-in applications
│   ├── settings/
│   │   ├── settings_main.c
│   │   ├── settings_wifi.c
│   │   ├── settings_display.c
│   │   ├── settings_keyboard.c
│   │   ├── settings_audio.c
│   │   ├── settings_storage.c
│   │   ├── settings_system.c
│   │   ├── settings_packages.c
│   │   └── Makefile
│   ├── pcedit/
│   │   ├── pcedit_main.c           # Entry + state restore
│   │   ├── pcedit_buffer.c         # Gap buffer in PSRAM
│   │   ├── pcedit_vi.c             # vi mode state machine
│   │   ├── pcedit_render.c         # LVGL text rendering
│   │   ├── pcedit_file.c           # File I/O
│   │   └── Makefile
│   ├── pccsv/
│   │   ├── pccsv_main.c
│   │   ├── pccsv_parser.c          # RFC 4180 CSV parser
│   │   ├── pccsv_table.c           # LVGL table widget driver
│   │   ├── pccsv_edit.c            # Cell editing logic
│   │   └── Makefile
│   ├── pcaudio/
│   │   ├── pcaudio_main.c
│   │   ├── pcaudio_decoder.c       # minimp3 + WAV decoder
│   │   ├── pcaudio_player.c        # Playback engine (Core 1)
│   │   ├── pcaudio_ui.c            # LVGL player interface
│   │   ├── pcaudio_playlist.c      # m3u + directory playlist
│   │   └── Makefile
│   ├── pcvideo/
│   │   ├── pcvideo_main.c
│   │   ├── pcvideo_pcv.c           # .pcv format parser
│   │   ├── pcvideo_playback.c      # Frame + audio pipeline
│   │   ├── pcvideo_ui.c            # LVGL controls overlay
│   │   └── Makefile
│   ├── pcterm/
│   │   ├── pcterm_main.c           # Local terminal app
│   │   ├── pcterm_nsh.c            # NuttShell integration
│   │   └── Makefile
│   ├── pcssh/
│   │   ├── pcssh_main.c
│   │   ├── pcssh_client.c          # wolfSSH session management
│   │   ├── pcssh_scp.c             # SCP file transfer
│   │   ├── pcssh_sftp.c            # SFTP file browser
│   │   ├── pcssh_connections.c     # Saved connections manager
│   │   └── Makefile
│   └── pcweb/
│       ├── pcweb_main.c
│       ├── pcweb_http.c            # HTTP/HTTPS client
│       ├── pcweb_html.c            # HTML subset parser
│       ├── pcweb_render.c          # Text-mode page renderer
│       ├── pcweb_nav.c             # Navigation (history, bookmarks)
│       └── Makefile
│
├── tools/                          # Host-side tools
│   ├── pcv-convert/                # Video converter (Python/FFmpeg)
│   │   ├── pcv_convert.py
│   │   └── README.md
│   └── pcpkg-create/               # Package builder
│       ├── pcpkg_create.py
│       └── README.md
│
├── sdk/                            # Third-party app SDK
│   ├── template/                   # App template project
│   │   ├── main.c
│   │   ├── manifest.json
│   │   ├── icon.bin
│   │   └── Makefile
│   └── README.md
│
├── sdcard/                         # Default SD card image contents
│   ├── apps/
│   │   └── registry.json           # Empty initial registry
│   ├── documents/
│   ├── music/
│   ├── video/
│   ├── ssh/
│   └── etc/
│       ├── hostname                # Default: "picocalc"
│       ├── settings.json           # Default settings
│       ├── bookmarks.json          # Default bookmarks
│       └── appstate/               # Empty, created by OS
│
├── CMakeLists.txt                  # Top-level build orchestration
├── Makefile                        # Convenience wrapper
└── README.md
```

---

## Phase 1: Board Bring-up & Core OS

**Goal:** Boot NuttShell on the RP2350B-Plus-W with UART console, validate all bus peripherals, configure PSRAM heap.

**Duration estimate:** 2-3 weeks

### 1.1 Create NuttX Board Definition

**Base:** Clone from `nuttx/boards/arm/rp23xx/raspberrypi-pico-2/`

**Files to create/modify:**

| File | Purpose |
|---|---|
| `boards/arm/rp23xx/picocalc-rp2350b/include/board.h` | Pin definitions, clock config, PSRAM config |
| `boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_bringup.c` | Peripheral registration sequence |
| `boards/arm/rp23xx/picocalc-rp2350b/configs/nsh/defconfig` | Minimal boot config |
| `boards/arm/rp23xx/picocalc-rp2350b/Kconfig` | Board-specific Kconfig |
| `boards/arm/rp23xx/picocalc-rp2350b/scripts/flash.ld` | 16MB flash linker script |

**Key pin definitions for `board.h`:**

```c
/* ST7365P Display (SPI1) */
#define PICOCALC_LCD_SPI_PORT     1
#define PICOCALC_LCD_SCK          10
#define PICOCALC_LCD_MOSI         11
#define PICOCALC_LCD_MISO         12
#define PICOCALC_LCD_CS           13
#define PICOCALC_LCD_DC           14
#define PICOCALC_LCD_RST          15
#define PICOCALC_LCD_SPI_FREQ     25000000  /* 25 MHz */

/* I2C Keyboard (STM32 south-bridge) */
#define PICOCALC_KB_I2C_PORT      0
#define PICOCALC_KB_SDA           /* TBD from schematic */
#define PICOCALC_KB_SCL           /* TBD from schematic */
#define PICOCALC_KB_I2C_FREQ      400000    /* 400 kHz */
#define PICOCALC_KB_I2C_ADDR      0x1F      /* STM32 south-bridge addr (from PicoCalc code) */

/* SD Card (SPI0) */
#define PICOCALC_SD_SPI_PORT      0
#define PICOCALC_SD_SCK           /* TBD */
#define PICOCALC_SD_MOSI          /* TBD */
#define PICOCALC_SD_MISO          /* TBD */
#define PICOCALC_SD_CS            /* TBD */

/* Audio PWM */
#define PICOCALC_AUDIO_PWM_L      /* TBD */
#define PICOCALC_AUDIO_PWM_R      /* TBD */

/* CYW43439 Wi-Fi/BT (shared SPI/PIO) */
/* Uses RP2350 Wireless Module 2 — pins managed by pico-sdk/cyw43 driver */
```

**Critical NuttX Kconfig options for `nsh/defconfig`:**

```makefile
# Architecture
CONFIG_ARCH="arm"
CONFIG_ARCH_CHIP="rp23xx"
CONFIG_ARCH_BOARD="picocalc-rp2350b"
CONFIG_ARM_TOOLCHAIN_GNU_EABI=y
CONFIG_RP23XX_FLASH_SIZE=16777216

# PSRAM
CONFIG_RP23XX_PSRAM=y
CONFIG_RP23XX_PSRAM_SIZE=8388608
CONFIG_RP23XX_PSRAM_HEAP_USER=y

# UART Console
CONFIG_RP23XX_UART0=y
CONFIG_UART0_SERIAL_CONSOLE=y
CONFIG_UART0_BAUD=115200

# NuttShell
CONFIG_NSH_LIBRARY=y
CONFIG_SYSTEM_NSH=y
CONFIG_NSH_ARCHINIT=y

# File systems
CONFIG_FS_FAT=y
CONFIG_FS_LITTLEFS=y
CONFIG_FS_PROCFS=y

# SPI
CONFIG_RP23XX_SPI0=y
CONFIG_RP23XX_SPI1=y

# I2C
CONFIG_RP23XX_I2C0=y

# DMA
CONFIG_RP23XX_DMA=y
```

### 1.2 Validate Peripherals

| Test | Method | Pass Criteria |
|---|---|---|
| UART console | Connect USB-UART, boot, see nsh prompt | `nsh> ` appears, commands work |
| GPIO | Toggle test pin, measure with scope | Pin toggles at expected rate |
| SPI1 loopback | Wire MOSI→MISO, send test pattern | Read matches write |
| I2C scan | `i2ctool scan` or custom scan code | Detects STM32 at expected addr |
| PSRAM | Write/read patterns to PSRAM heap | All 8MB accessible, no corruption |
| Flash | Read chip ID, write/read LittleFS | 16MB confirmed, FS mount OK |

### 1.3 Hostname Support

```c
/* In rp23xx_bringup.c or main.c */
static void load_hostname(void)
{
    int fd = open("/mnt/sd/etc/hostname", O_RDONLY);
    if (fd >= 0) {
        char buf[64];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = '\0';
            /* Strip trailing newline */
            char *nl = strchr(buf, '\n');
            if (nl) *nl = '\0';
            sethostname(buf, strlen(buf));
            return;
        }
    }
    sethostname("picocalc", 8);  /* Default */
}
```

### 1.4 Deliverables

- [ ] Board boots to `nsh> ` prompt over UART
- [ ] `free` command shows ~520KB SRAM + ~8MB PSRAM
- [ ] `ls /dev` shows spi0, spi1, i2c0 devices
- [ ] `hostname` command returns `picocalc`

---

## Phase 2: Display & Framebuffer

**Goal:** ST7365P displays graphics via NuttX framebuffer `/dev/fb0`, DMA-accelerated.

**Duration estimate:** 2-3 weeks

### 2.1 ST7365P Driver Architecture

```
┌──────────────────────────────────────┐
│        NuttX Framebuffer API         │
│  fb_open / fb_mmap / fb_ioctl       │
├──────────────────────────────────────┤
│        rp23xx_lcd.c                  │
│  fb_videoinfo / fb_planeinfo        │
│  fb_putarea (partial update)        │
├──────────────────────────────────────┤
│        SPI1 + DMA                    │
│  25 MHz, 8-bit frames               │
│  DC pin toggle for cmd vs data      │
├──────────────────────────────────────┤
│        ST7365P (ILI9488-compat)     │
│  CASET/RASET/RAMWR commands         │
└──────────────────────────────────────┘
```

**Key driver file: `rp23xx_lcd.c`**

```c
/* ST7365P initialization sequence (adapted from PicoCalc lcdspi driver) */
static const struct lcd_init_cmd_s g_st7365p_init[] = {
    { 0x01, NULL, 0, 150 },           /* Software Reset, 150ms delay */
    { 0x11, NULL, 0, 120 },           /* Sleep Out, 120ms delay */
    { 0x36, "\x48", 1, 0 },           /* Memory Access Control (BGR, row/col) */
    { 0x3A, "\x55", 1, 0 },           /* Pixel Format: 16-bit RGB565 */
    { 0x2A, "\x00\x00\x01\x3F", 4, 0 }, /* Column Address Set: 0-319 */
    { 0x2B, "\x00\x00\x01\x3F", 4, 0 }, /* Row Address Set: 0-319 */
    { 0x29, NULL, 0, 20 },            /* Display ON */
    { 0x2C, NULL, 0, 0 },             /* Memory Write start */
};

/* Framebuffer ops */
static struct fb_vtable_s g_fbops = {
    .getvideoinfo = st7365p_getvideoinfo,   /* 320x320, RGB565 */
    .getplaneinfo = st7365p_getplaneinfo,   /* stride = 640 bytes */
    .putarea      = st7365p_putarea,        /* DMA SPI write region */
};
```

**DMA flush pipeline:**

```
CPU renders to SRAM draw buffer (20KB, 32 lines)
  │
  ▼
DMA channel copies draw buffer → SPI1 TX FIFO
  │  (CPU starts rendering next 32 lines concurrently)
  ▼
SPI1 clocks data out at 25 MHz → ST7365P RAMWR
  │
  ▼
Display updates visible region
```

**Bandwidth calculation:**
- SPI1 @ 25 MHz = 25 Mbit/s = 3.125 MB/s
- Full frame: 320 × 320 × 2 = 200 KB
- Full frame time: 200KB / 3.125 MB/s = 64 ms → **~15.6 FPS max**
- Partial update (1/10): 20KB → 6.4 ms → 156 partial updates/s

### 2.2 NuttX Kconfig Additions

```makefile
# Framebuffer
CONFIG_VIDEO_FB=y
CONFIG_LCD=y
CONFIG_LCD_FRAMEBUFFER=y
CONFIG_LCD_MAXCONTRAST=100

# Board-specific LCD
CONFIG_RP23XX_LCD_ST7365P=y
CONFIG_RP23XX_LCD_ST7365P_WIDTH=320
CONFIG_RP23XX_LCD_ST7365P_HEIGHT=320
CONFIG_RP23XX_LCD_ST7365P_BPP=16
CONFIG_RP23XX_LCD_ST7365P_SPI_FREQ=25000000
```

### 2.3 Test Plan

| Test | Method | Pass Criteria |
|---|---|---|
| Init sequence | Boot, check display powers on | No white/garbage screen |
| Solid fill | Write solid color to fb | Uniform color fills display |
| Gradient | Write horizontal gradient | Smooth gradient, no tearing |
| Partial update | Update 32-line strip | Only strip changes, rest stable |
| DMA throughput | Measure FPS of full fills | ≥14 FPS continuous |
| Color accuracy | Display RGB test pattern | Colors match expected (BGR swap) |

### 2.4 Deliverables

- [ ] `/dev/fb0` device appears in NuttX
- [ ] `fb_putarea()` writes pixels to screen with DMA
- [ ] Full-screen fill achieves ≥14 FPS
- [ ] No visual artifacts or tearing during partial updates

---

## Phase 3: Input, LVGL & Launcher

**Goal:** Keyboard input works through LVGL, launcher displays app grid with status bar.

**Duration estimate:** 3-4 weeks

### 3.1 I2C Keyboard Driver

The PicoCalc keyboard uses an STM32 south-bridge connected via I2C. Key behavior from existing code:

```c
/* Keyboard I2C protocol (from PicoCalc reference code) */
#define KB_I2C_ADDR    0x1F
#define KB_REG_KEY     0x01    /* Read: returns last keypress scancode */
#define KB_REG_BL      0x05    /* Write: keyboard backlight level 0-255 */

/* Read a keypress (non-blocking, returns 0 if no key) */
static int keyboard_read(FAR struct input_lowerhalf_s *lower,
                          FAR struct input_event_s *event)
{
    uint8_t reg = KB_REG_KEY;
    uint8_t scancode = 0;

    /* I2C write register addr, then read 1 byte */
    i2c_writeread(priv->i2c, &priv->config, &reg, 1, &scancode, 1);

    if (scancode == 0) return -EAGAIN; /* No key */

    event->type = EV_KEY;
    event->code = scancode_to_lvgl_key(scancode);
    event->value = 1;  /* KEY_PRESS */
    return OK;
}
```

**LVGL keypad input mapping:**

| PicoCalc Key | LVGL Key | Function |
|---|---|---|
| Arrow Up/Down/Left/Right | `LV_KEY_UP/DOWN/LEFT/RIGHT` | Navigation |
| Enter | `LV_KEY_ENTER` | Select/confirm |
| Esc | `LV_KEY_ESC` | Back/cancel |
| Tab | `LV_KEY_NEXT` | Focus next widget |
| Backspace | `LV_KEY_BACKSPACE` | Delete char |
| A-Z, 0-9, symbols | `LV_KEY_...` / char codes | Text input |
| Fn+Home | Custom handler | Return to launcher |
| Fn+Space | Custom handler | Audio play/pause |
| Fn+W | Custom handler | Wi-Fi toggle |

### 3.2 LVGL Integration

**NuttX config additions:**

```makefile
CONFIG_GRAPHICS_LVGL=y
CONFIG_LV_MEM_SIZE_KILOBYTES=48
CONFIG_LV_COLOR_DEPTH=16
CONFIG_LV_HOR_RES=320
CONFIG_LV_VER_RES=320
CONFIG_LV_DPI_DEF=160
CONFIG_LV_USE_LOG=y
CONFIG_LV_TICK_PERIOD=5
CONFIG_LV_FONT_MONTSERRAT_12=y
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_LV_FONT_UNSCII_8=y

# Input
CONFIG_INPUT=y
CONFIG_INPUT_KEYBOARD=y
CONFIG_LV_USE_KEYBOARD=y
```

**LVGL thread model:**

```c
/* Core 0: LVGL task — runs at ~30 Hz */
static void *lvgl_thread(void *arg)
{
    while (1) {
        uint32_t ms = lv_timer_handler();
        usleep(ms * 1000);
    }
}

/* All app UI manipulations post to LVGL via lv_msg or direct API calls
 * from the same thread context (cooperative) */
```

### 3.3 Status Bar Widget

```
┌──────────────────────────────────────────────────────────────┐
│ picocalc │ ▁▂▃ WiFi │ ████ 87% │                    14:32 │
└──────────────────────────────────────────────────────────────┘
│  hostname  │ signal   │ battery  │                   clock  │
```

**Implementation: `statusbar.c`**

```c
typedef struct {
    lv_obj_t *bar;          /* Container, 320x20px, top of screen */
    lv_obj_t *lbl_host;     /* Hostname label */
    lv_obj_t *lbl_wifi;     /* Wi-Fi icon + SSID */
    lv_obj_t *lbl_battery;  /* Battery % + bar */
    lv_obj_t *lbl_clock;    /* HH:MM */
} statusbar_t;

void statusbar_create(lv_obj_t *parent);
void statusbar_update(void);  /* Called every 1s by timer */
```

### 3.4 Launcher / Home Screen

```
┌──────────────────────────────────────┐  ← Status bar (20px)
├──────────────────────────────────────┤
│  ┌──────┐  ┌──────┐  ┌──────┐       │
│  │ ⚙️   │  │ 📝  │  │ 📊  │       │  Row 1
│  │ Set- │  │ pc-  │  │ pc-  │       │
│  │ tings│  │ edit │  │ csv  │       │
│  └──────┘  └──────┘  └──────┘       │
│  ┌──────┐  ┌──────┐  ┌──────┐       │
│  │ 🎵   │  │ 🎬  │  │ >_   │       │  Row 2
│  │ pc-  │  │ pc-  │  │ pc-  │       │
│  │audio │  │video │  │ term │       │
│  └──────┘  └──────┘  └──────┘       │
│  ┌──────┐  ┌──────┐                 │
│  │ 🔒   │  │ 🌐  │                 │  Row 3
│  │ pc-  │  │ pc-  │                 │
│  │ ssh  │  │ web  │                 │
│  └──────┘  └──────┘                 │
└──────────────────────────────────────┘
```

**Layout: 3-column grid, 80x80px icons + 16px label text, scrollable if > 9 apps.**

### 3.5 Deliverables

- [ ] Keyboard scancodes read via I2C, mapped to LVGL events
- [ ] LVGL displays launcher screen with app icons
- [ ] Status bar shows hostname, clock (Wi-Fi and battery placeholder)
- [ ] Arrow keys navigate grid, Enter selects an app stub
- [ ] Fn+Home returns to launcher from any screen
- [ ] Keyboard and screen backlight controllable

---

## Phase 4: Storage, Settings & Package System

**Goal:** SD card mounted, settings app functional, package manager can install/launch ELF apps.

**Duration estimate:** 3-4 weeks

### 4.1 SD Card & Directory Structure

```c
/* In rp23xx_bringup.c */
static void mount_sdcard(void)
{
    /* Register SPI-based MMCSD driver */
    mmcsd_spislotinitialize(0, 0, spi);  /* Minor 0, SPI bus 0 */

    /* Mount FAT32 */
    mount("/dev/mmcsd0", "/mnt/sd", "vfat", 0, NULL);

    /* Ensure directory structure exists */
    mkdir("/mnt/sd/apps", 0755);
    mkdir("/mnt/sd/documents", 0755);
    mkdir("/mnt/sd/music", 0755);
    mkdir("/mnt/sd/video", 0755);
    mkdir("/mnt/sd/ssh", 0755);
    mkdir("/mnt/sd/etc", 0755);
    mkdir("/mnt/sd/etc/appstate", 0755);
}
```

### 4.2 Settings Persistence

**Config file: `/mnt/sd/etc/settings.json`**

```json
{
    "version": 1,
    "network": {
        "wifi_enabled": true,
        "saved_networks": [
            { "ssid": "MyNetwork", "psk": "..." }
        ]
    },
    "identity": {
        "hostname": "picocalc"
    },
    "display": {
        "brightness": 80,
        "timeout_sec": 120
    },
    "keyboard": {
        "backlight": 50,
        "repeat_rate": 30
    },
    "audio": {
        "volume": 70,
        "speaker_enabled": true
    }
}
```

**JSON library:** Use cJSON (lightweight, ~16KB code, already available in NuttX apps). Enable with `CONFIG_NETUTILS_CJSON=y`.

### 4.3 Package Manager Implementation

**Registry file: `/mnt/sd/apps/registry.json`**

```json
{
    "version": 1,
    "packages": [
        {
            "name": "calculator",
            "version": "1.0.0",
            "display_name": "Calculator",
            "entry": "/mnt/sd/apps/calculator/app.elf",
            "icon": "/mnt/sd/apps/calculator/icon.bin",
            "category": "utility",
            "installed_at": "2026-03-08T12:00:00Z"
        }
    ]
}
```

**Package install flow:**

```
1. User copies myapp.pcpkg to /mnt/sd/apps/
2. Settings → Packages → "Install from file"
3. Package manager:
   a. Detect .pcpkg files in /mnt/sd/apps/
   b. Extract (untar) to /mnt/sd/apps/myapp/
   c. Parse manifest.json, validate fields
   d. Check min_ram vs available PSRAM
   e. Add entry to registry.json
   f. Delete original .pcpkg (or keep)
4. Launcher refreshes app grid
```

### 4.4 App Framework API

```c
/* pcterm/app.h — Every app implements this interface */

typedef struct pc_app_info_s {
    const char *name;           /* "pcedit" */
    const char *display_name;   /* "Text Editor" */
    const char *version;        /* "1.0.0" */
    uint32_t min_ram;           /* Minimum PSRAM bytes needed */
} pc_app_info_t;

/* App entry point (called by framework) */
typedef int (*pc_app_main_t)(int argc, char *argv[]);

/* State save/restore callbacks */
typedef int (*pc_app_save_t)(void *buf, size_t maxlen);
typedef int (*pc_app_restore_t)(const void *buf, size_t len);

/* Registration structure */
typedef struct pc_app_s {
    pc_app_info_t   info;
    pc_app_main_t   main;       /* App entry */
    pc_app_save_t   save;       /* Called on Fn+Home */
    pc_app_restore_t restore;   /* Called on re-launch if state exists */
} pc_app_t;

/* Framework functions apps can call */
void pc_app_exit(int code);                    /* Clean exit (discard state) */
void pc_app_yield(void);                       /* Save state + return to launcher */
lv_obj_t *pc_app_get_screen(void);             /* Get 300x320 LVGL app area */
void *pc_app_psram_alloc(size_t size);         /* Allocate from PSRAM heap */
void pc_app_psram_free(void *ptr);
const char *pc_app_get_hostname(void);
```

### 4.5 App State Save/Restore Flow

```
                Fn+Home pressed
                      │
                      ▼
            ┌─────────────────┐
            │ pc_app_yield()  │
            └────────┬────────┘
                     │
            ┌────────▼────────┐
            │ Call app->save  │
            │ (app serializes │
            │  state to buf)  │
            └────────┬────────┘
                     │
            ┌────────▼──────────────────┐
            │ Write buf to PSRAM cache  │
            │ + flush to SD:            │
            │ /mnt/sd/etc/appstate/     │
            │   pcedit.state            │
            └────────┬──────────────────┘
                     │
            ┌────────▼────────┐
            │ Free app memory │
            │ Destroy LVGL    │
            │ objects          │
            └────────┬────────┘
                     │
            ┌────────▼────────┐
            │ Show launcher   │
            └─────────────────┘


            Re-launch app
                  │
           ┌──────▼──────┐     ┌──────────────┐
           │ State file   │─yes─▶ Read .state  │
           │ exists?      │     │ into buf     │
           └──────┬───────┘     └──────┬───────┘
                  │no                   │
           ┌──────▼──────┐     ┌───────▼───────┐
           │ app->main() │     │ app->restore()│
           │ (fresh)     │     │ (resume)      │
           └─────────────┘     └───────────────┘
```

### 4.6 ELF Loading (Third-party Apps)

NuttX supports loadable ELF modules via `CONFIG_ELF=y`:

```c
/* Launch a third-party ELF from SD card */
static int launch_elf_app(const char *elf_path, int argc, char *argv[])
{
    struct binary_s bin;
    memset(&bin, 0, sizeof(bin));
    bin.filename = elf_path;
    bin.exports = NULL;  /* Or provide pcterm API symbol table */
    bin.nexports = 0;

    int ret = load_module(&bin);
    if (ret < 0) return ret;

    ret = exec_module(&bin);
    unload_module(&bin);
    return ret;
}
```

**NuttX config:**
```makefile
CONFIG_ELF=y
CONFIG_LIBC_EXECFUNCS=y
CONFIG_BINFMT_CONSTRUCTORS=y
CONFIG_SYMTAB_ORDEREDBYNAME=y
```

### 4.7 Deliverables

- [ ] SD card mounts at `/mnt/sd` with full directory structure
- [ ] Settings app reads/writes `settings.json`
- [ ] Package manager parses manifests, maintains registry
- [ ] Built-in apps launch from launcher and return on exit
- [ ] `pc_app_save/restore` API works — pcedit can save/restore cursor position
- [ ] Third-party ELF loads from `/mnt/sd/apps/*/app.elf`

---

## Phase 5: Local Terminal & vi

**Goal:** Working VT100 terminal emulator in LVGL, NuttShell integration, vi editor.

**Duration estimate:** 2-3 weeks

### 5.1 VT100 Terminal Emulator Widget

This is a **shared component** used by both `pcterm` (local) and `pcssh` (remote).

**Architecture:**

```
┌────────────────────────────────────────┐
│       LVGL Canvas (300 x 300 px)       │  ← App area minus status bar
│  ┌──────────────────────────────────┐  │
│  │  Terminal grid: 53 cols × 25 rows│  │  ← 6x12 monospace font
│  │                                  │  │
│  │  picocalc:/mnt/sd$ ls            │  │
│  │  documents/  music/  video/      │  │
│  │  picocalc:/mnt/sd$ _             │  │
│  │                                  │  │
│  └──────────────────────────────────┘  │
│  ┌──────────────────────────────────┐  │
│  │  [Scrollback: 2000 lines PSRAM] │  │
│  └──────────────────────────────────┘  │
└────────────────────────────────────────┘
```

**Data structures:**

```c
/* terminal_widget.h */

#define TERM_COLS       53
#define TERM_ROWS       25
#define TERM_SCROLLBACK 2000

typedef struct {
    /* Visible grid */
    struct {
        char     ch;
        uint8_t  fg;    /* 4-bit ANSI color index */
        uint8_t  bg;
        uint8_t  attr;  /* bold, underline, inverse */
    } cells[TERM_ROWS][TERM_COLS];

    /* State */
    int cursor_row, cursor_col;
    bool cursor_visible;
    int scroll_top, scroll_bottom;  /* Scroll region */

    /* Scrollback ring buffer (in PSRAM) */
    void *scrollback_buf;  /* 64KB PSRAM ring */
    int   scrollback_lines;
    int   scrollback_head;

    /* ANSI parser state machine */
    enum { STATE_NORMAL, STATE_ESC, STATE_CSI, STATE_OSC } parse_state;
    char  csi_buf[32];
    int   csi_len;

    /* LVGL rendering */
    lv_obj_t *canvas;
    lv_color_t *canvas_buf;  /* In PSRAM */
    bool dirty;
} terminal_t;

/* API */
terminal_t *terminal_create(lv_obj_t *parent);
void terminal_destroy(terminal_t *term);
void terminal_write(terminal_t *term, const char *data, size_t len);  /* Feed data from shell/SSH */
void terminal_keypress(terminal_t *term, uint32_t key);               /* Feed input to shell/SSH */
void terminal_render(terminal_t *term);                                /* Redraw dirty regions */
```

**Supported ANSI escape sequences:**

| Sequence | Function |
|---|---|
| `ESC[nA/B/C/D` | Cursor up/down/forward/back |
| `ESC[n;mH` | Cursor position (row;col) |
| `ESC[2J` | Clear screen |
| `ESC[K` | Clear to end of line |
| `ESC[nm` | SGR: colors (30-37, 40-47), bold (1), reset (0) |
| `ESC[nS/T` | Scroll up/down |
| `ESC[?25h/l` | Show/hide cursor |
| `ESC[r` | Set scroll region |
| `\n`, `\r`, `\t`, `\b` | Standard control chars |

### 5.2 NuttShell Integration for `pcterm`

```c
/* pcterm_main.c */
int pcterm_main(int argc, char *argv[])
{
    terminal_t *term = terminal_create(pc_app_get_screen());

    /* Create a pseudo-terminal pair */
    int master_fd, slave_fd;
    openpty(&master_fd, &slave_fd, NULL, NULL, NULL);

    /* Fork NuttShell on the slave side */
    pid_t pid = task_create("nsh", 100, 4096, nsh_consolemain, NULL);
    /* Redirect nsh stdin/stdout/stderr to slave_fd */

    /* Main loop: shuttle data between terminal widget and pty */
    while (1) {
        /* Read from master_fd (nsh output) → terminal_write() */
        /* Read keyboard → terminal_keypress() → write to master_fd */
        /* terminal_render() on LVGL tick */
    }
}
```

### 5.3 vi Configuration

NuttX has a built-in vi implementation. Enable with:

```makefile
CONFIG_SYSTEM_VI=y
CONFIG_SYSTEM_VI_COLS=53
CONFIG_SYSTEM_VI_ROWS=25
```

vi runs inside `pcterm`'s terminal emulator — no special integration needed. User types `vi /mnt/sd/documents/file.txt` at the nsh prompt.

### 5.4 Deliverables

- [ ] Terminal widget renders 53x25 grid with ANSI color support
- [ ] NuttShell runs inside terminal, full interactivity
- [ ] vi opens and edits files within the terminal
- [ ] Scrollback works (Page Up/Down)
- [ ] Terminal state saves/restores (cwd, scrollback, history)

---

## Phase 6: Office Suite

**Goal:** vi-style text editor and CSV table editor as standalone LVGL apps.

**Duration estimate:** 3-4 weeks

### 6.1 Text Editor (`pcedit`) — Architecture

Unlike the vi inside `pcterm`, `pcedit` is a **native LVGL app** with its own rendering. This gives better UX (syntax highlighting potential, status bar, smoother scroll).

**Core data structure: Gap Buffer**

```c
/* pcedit_buffer.c — Gap buffer in PSRAM for efficient insert/delete */
typedef struct {
    char *buf;          /* PSRAM allocation, up to 512KB */
    size_t buf_size;    /* Total allocated */
    size_t gap_start;   /* Index where gap begins (= cursor position) */
    size_t gap_end;     /* Index where gap ends */
    size_t content_len; /* buf_size - gap_size = actual text length */
} gap_buffer_t;

gap_buffer_t *gap_buffer_create(size_t initial_size);  /* Alloc in PSRAM */
void gap_buffer_insert(gap_buffer_t *gb, char ch);
void gap_buffer_delete(gap_buffer_t *gb, int count);   /* Backspace */
void gap_buffer_move(gap_buffer_t *gb, int offset);    /* Move cursor */
char gap_buffer_get(gap_buffer_t *gb, size_t pos);     /* Read char at pos */
```

**vi mode state machine:**

```
         ┌──────────────────────────────────────┐
         │                                      │
    ┌────▼────┐    'i','a','o'    ┌──────────┐  │
    │ NORMAL  │ ─────────────────▶│  INSERT  │  │
    │  Mode   │◀─────────────────│  Mode    │  │
    └────┬────┘      Esc          └──────────┘  │
         │                                      │
         │ ':'                                  │
         │                                      │
    ┌────▼────┐                                 │
    │ COMMAND │─────── Enter (execute) ─────────┘
    │  Mode   │
    │ :w :q   │
    └─────────┘
```

### 6.2 CSV Editor (`pccsv`) — Architecture

```c
/* pccsv_parser.c — RFC 4180 compliant */
typedef struct {
    char ***cells;      /* cells[row][col] — string pointers */
    int num_rows;
    int num_cols;
    void *data_pool;    /* PSRAM pool for cell strings */
} csv_data_t;

csv_data_t *csv_parse_file(const char *path);
int csv_write_file(csv_data_t *data, const char *path);
void csv_insert_row(csv_data_t *data, int after_row);
void csv_delete_row(csv_data_t *data, int row);
void csv_set_cell(csv_data_t *data, int row, int col, const char *value);
```

**LVGL table widget usage:**

```c
/* pccsv_table.c */
void pccsv_create_table(lv_obj_t *parent, csv_data_t *data)
{
    lv_obj_t *table = lv_table_create(parent);
    lv_table_set_col_cnt(table, data->num_cols);
    lv_table_set_row_cnt(table, data->num_rows);

    for (int r = 0; r < data->num_rows; r++) {
        for (int c = 0; c < data->num_cols; c++) {
            lv_table_set_cell_value(table, r, c, data->cells[r][c]);
        }
    }

    /* Navigation: arrow keys move selection, Enter edits cell */
    lv_obj_add_event_cb(table, table_key_handler, LV_EVENT_KEY, data);
}
```

### 6.3 Deliverables

- [ ] `pcedit` opens files, vi keybindings work (hjkl, dd, yy, p, :w, :q)
- [ ] Gap buffer handles files up to 512KB without lag
- [ ] `pccsv` loads/saves CSV, navigates cells, edits values
- [ ] Both apps save/restore state on Fn+Home

---

## Phase 7: Audio & Video

**Goal:** Audio playback (MP3/WAV) with background play, video player with .pcv format.

**Duration estimate:** 3-4 weeks

### 7.1 Audio Pipeline

```
┌──────────┐    ┌───────────┐    ┌──────────┐    ┌──────────┐
│ SD Card  │───▶│ Decoder   │───▶│ Ring Buf │───▶│ PWM Out  │
│ (file)   │    │ minimp3   │    │ (PSRAM)  │    │ (DMA)    │
│          │    │ WAV parse │    │  64KB    │    │          │
└──────────┘    └───────────┘    └──────────┘    └──────────┘
                  Core 0            shared          Core 1
```

**Background playback model:**

```c
/* Runs on Core 1, continues across app switches */
static struct {
    volatile bool playing;
    volatile bool stop_requested;
    char current_file[256];
    uint32_t position_ms;
    uint8_t volume;         /* 0-100 */
    /* Ring buffer */
    uint8_t *ring_buf;      /* 64KB in PSRAM */
    volatile uint32_t read_pos;
    volatile uint32_t write_pos;
} g_audio;

/* Core 1 audio ISR: feeds PWM from ring buffer */
void audio_pwm_isr(void)
{
    /* Read samples from ring buffer, write to PWM compare registers */
}
```

### 7.2 Video Player Pipeline

```
┌──────────┐    ┌───────────┐    ┌──────────┐    ┌──────────┐
│ SD Card  │───▶│ PCV Parse │───▶│ Frame    │───▶│ SPI DMA  │
│ .pcv file│    │ header +  │    │ Buffer   │    │ to LCD   │
│          │    │ frame read│    │ (PSRAM)  │    │          │
└──────────┘    └───────────┘    └──────────┘    └──────────┘
                                  200KB PSRAM      25 MHz SPI

                ┌───────────┐    ┌──────────┐
                │ Audio     │───▶│ PWM Out  │
                │ interleave│    │ (shared) │
                └───────────┘    └──────────┘
```

**Frame timing for 12 FPS:**
- Frame period: 83.3 ms
- SD read (200KB raw frame): ~25 ms @ 6.5 MB/s SD SPI
- SPI DMA to display: ~64 ms
- **Overlap:** Read next frame from SD while DMA sends current frame
- Audio: ~1.4KB of PCM per frame @ 8kHz mono 8-bit → read with frame, push to audio ring buffer

### 7.3 `pcv-convert` Tool (PC-side)

```python
#!/usr/bin/env python3
"""pcv_convert.py — Convert video files to PicoCalc .pcv format"""

import argparse, struct, subprocess, numpy as np
from PIL import Image

def convert(input_path, output_path, fps=12, width=320, height=320):
    # Use FFmpeg to extract frames as raw RGB
    # Convert each frame to RGB565
    # Extract audio as PCM 8-bit mono
    # Interleave into .pcv container
    pass

# Header: magic, w, h, fps, audio_rate, audio_bits, frame_count, flags
HEADER_FMT = '<4sHHBHBIB13x'  # 32 bytes total
```

### 7.4 Deliverables

- [ ] `pcaudio` plays MP3 and WAV files from SD card
- [ ] Audio continues in background when switching apps
- [ ] Volume controllable, playlist support
- [ ] `pcvideo` plays .pcv files at target FPS
- [ ] `pcv-convert` tool converts MP4 → .pcv on PC
- [ ] Audio + video synchronized during playback

---

## Phase 8: Networking & SSH

**Goal:** Wi-Fi connected, SSH client with SCP/SFTP, text web browser.

**Duration estimate:** 4-6 weeks (largest phase)

### 8.1 Wi-Fi Bring-up

**CYW43439 driver chain:**

```
NuttX wlan0 interface
    │
    ▼
cyw43 driver (ported from pico-sdk)
    │
    ▼
PIO-based SPI to CYW43439 radio module
    │
    ▼
CYW43439 firmware blob (loaded from flash)
```

**NuttX config:**
```makefile
CONFIG_NET=y
CONFIG_NET_TCP=y
CONFIG_NET_UDP=y
CONFIG_NET_ICMP=y
CONFIG_NETDB_RESOLVCONF=y
CONFIG_NSH_NETINIT=y
CONFIG_DRIVERS_WIRELESS=y
CONFIG_IEEE80211_CYW43439=y
CONFIG_NET_SOCKOPTS=y
CONFIG_NET_SOLINGER=y
CONFIG_NET_ETH_PKTSIZE=1518
```

### 8.2 SSH Client (wolfSSH)

```c
/* pcssh_client.c — wolfSSH session management */
#include <wolfssh/ssh.h>

typedef struct {
    WOLFSSH_CTX *ctx;
    WOLFSSH *ssh;
    int sock_fd;
    terminal_t *term;       /* Shared terminal widget */
    char host[128];
    char user[64];
    uint16_t port;
} ssh_session_t;

int ssh_connect(ssh_session_t *sess)
{
    /* 1. TCP connect to host:port */
    sess->sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    connect(sess->sock_fd, ...);

    /* 2. wolfSSH handshake */
    sess->ctx = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_CLIENT, NULL);
    sess->ssh = wolfSSH_new(sess->ctx);
    wolfSSH_set_fd(sess->ssh, sess->sock_fd);
    wolfSSH_SetUsername(sess->ssh, sess->user);

    /* 3. Auth (password or key) */
    wolfSSH_connect(sess->ssh);

    /* 4. Request PTY + shell channel */
    wolfSSH_ChannelOpenSession(sess->ssh);
    wolfSSH_ChannelSendRequest(sess->ssh, "pty-req", ...);
    wolfSSH_ChannelSendRequest(sess->ssh, "shell", ...);

    return OK;
}

/* Main loop: shuttle SSH ↔ terminal widget */
void ssh_loop(ssh_session_t *sess)
{
    while (1) {
        /* Read from SSH → terminal_write() */
        int n = wolfSSH_ChannelRead(sess->ssh, buf, sizeof(buf));
        if (n > 0) terminal_write(sess->term, buf, n);

        /* Read keyboard → wolfSSH_ChannelSend() */
        /* ... */
    }
}
```

### 8.3 SCP Implementation

```c
/* pcssh_scp.c — File transfer over SSH */
int scp_download(ssh_session_t *sess,
                 const char *remote_path,
                 const char *local_path)
{
    wolfSSH_ChannelSendRequest(sess->ssh, "exec",
        "scp -f remote_path", ...);
    /* SCP protocol: read file size, then stream data to local file */
    int fd = open(local_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    /* ... read from channel, write to fd ... */
    close(fd);
    return OK;
}
```

### 8.4 SFTP Browser

Interactive file browser showing remote filesystem:

```
┌──────────────────────────────────────┐
│ SFTP: user@server:/home/user/        │
├──────────────────────────────────────┤
│  📁 ..                               │
│  📁 documents/                       │
│  📁 projects/                        │
│  📄 readme.txt           1.2 KB      │
│  📄 data.csv            45.6 KB      │
│  📄 photo.jpg          230.0 KB      │
├──────────────────────────────────────┤
│ [Download]  [Upload]  [Delete]  [←]  │
└──────────────────────────────────────┘
```

Uses `wolfSSH_SFTP_*` API for directory listing, file get/put.

### 8.5 Web Browser (`pcweb`)

**HTML parser approach — state machine:**

```
┌──────────┐    ┌───────────┐    ┌───────────┐    ┌──────────┐
│  HTTP    │───▶│   HTML    │───▶│  Layout   │───▶│  LVGL    │
│  Client  │    │  Tokenize │    │  Engine   │    │  Render  │
│          │    │  + Parse  │    │ (text fmt)│    │          │
└──────────┘    └───────────┘    └───────────┘    └──────────┘
```

**Supported HTML elements:**

| Element | Rendering |
|---|---|
| `<h1>`-`<h6>` | Bold text, larger font for h1-h3 if available |
| `<p>` | Paragraph with blank line separator |
| `<a href="...">` | Highlighted text, Tab-navigable, Enter follows |
| `<ul>`, `<ol>`, `<li>` | Bulleted/numbered list |
| `<table>`, `<tr>`, `<td>` | ASCII table grid |
| `<pre>`, `<code>` | Monospace, preserve whitespace |
| `<b>`, `<strong>` | Bold attribute |
| `<i>`, `<em>` | Italic (or underline on monospace) |
| `<br>` | Line break |
| `<hr>` | Horizontal rule: `────────` |
| `<img>` | Placeholder `[img: alt text]` (optional: decode small images) |
| `<form>`, `<input>` | Basic text input + submit button |

**HTTP client (BSD sockets + wolfSSL):**

```c
int http_get(const char *url, char *response_buf, size_t maxlen)
{
    /* Parse URL → host, port, path */
    /* DNS resolve */
    /* TCP connect */
    /* If HTTPS: wolfSSL_new(), wolfSSL_connect() */
    /* Send: GET /path HTTP/1.1\r\nHost: ...\r\n\r\n */
    /* Recv response into PSRAM buffer */
    /* Parse headers, return body */
}
```

### 8.6 NuttX Config Additions

```makefile
# wolfSSL (TLS)
CONFIG_CRYPTO=y
CONFIG_CRYPTO_WOLFSSL=y

# wolfSSH
CONFIG_NETUTILS_WOLFSSH=y

# DNS
CONFIG_NETDB_DNSCLIENT=y
CONFIG_NETDB_RESOLVCONF=y

# Network tools
CONFIG_SYSTEM_PING=y
CONFIG_SYSTEM_WGET=y
CONFIG_NSH_TELNET=y
```

### 8.7 Deliverables

- [ ] Wi-Fi connects to WPA2 networks, gets DHCP IP
- [ ] `ping` works from nsh
- [ ] `pcssh` connects to SSH server, interactive terminal
- [ ] SCP downloads/uploads files
- [ ] SFTP browser lists remote files, download/upload works
- [ ] `pcweb` loads HTTP/HTTPS pages, renders text content
- [ ] Link navigation, bookmarks, history, address bar
- [ ] Known hosts verification works

---

## Phase 9: Polish & Distribution

**Goal:** Production-ready OS with power management, global hotkeys, OTA updates, SDK.

**Duration estimate:** 2-3 weeks

### 9.1 Power Management

```c
/* Battery monitoring via ADC */
typedef struct {
    uint16_t voltage_mv;    /* Battery voltage */
    uint8_t  percent;       /* Estimated SOC */
    bool     charging;      /* Charge detect pin */
} battery_status_t;

/* Screen timeout */
static void screen_timeout_cb(lv_timer_t *timer) {
    /* Dim backlight → off after settings.display.timeout_sec */
    /* Any keypress wakes: set I2C backlight register */
}
```

### 9.2 Global Hotkeys

| Hotkey | Action | Implementation |
|---|---|---|
| `Fn+Home` | Return to launcher | `pc_app_yield()` in keyboard ISR |
| `Fn+Space` | Audio play/pause | Toggle `g_audio.playing` |
| `Fn+→` | Next track | Signal audio thread |
| `Fn+←` | Prev track | Signal audio thread |
| `Fn+W` | Wi-Fi toggle | Call Wi-Fi enable/disable |
| `Fn+B` | Brightness cycle | I2C backlight register |
| `Fn+V` | Volume cycle | Audio volume level |

Hotkeys are intercepted in the keyboard driver **before** reaching the active app.

### 9.3 OTA Firmware Update

```
1. Settings → System → Check for Update
2. HTTP GET https://releases.picocalc-term.org/latest.json
3. Compare version with current firmware
4. Download firmware.uf2 to /mnt/sd/firmware/
5. Reboot into bootloader (set magic value at SRAM address)
6. Bootloader writes UF2 to flash
```

### 9.4 SDK for Third-party Apps

```
sdk/
├── template/
│   ├── main.c              # Minimal app skeleton
│   ├── manifest.json       # Template manifest
│   ├── icon.bin            # Placeholder icon
│   ├── Makefile            # Builds against NuttX + pcterm headers
│   └── README.md           # "How to build your first app"
├── include/
│   └── pcterm/             # Symlink/copy of pcterm headers
├── lib/
│   └── libpcterm.a         # Prebuilt app framework library
└── docs/
    ├── API.md              # Full API reference
    ├── LVGL_Guide.md       # LVGL usage for PicoCalc apps
    └── Examples.md         # Code examples
```

**Template `main.c`:**

```c
#include <pcterm/app.h>
#include <lvgl.h>

static int app_save(void *buf, size_t maxlen)
{
    /* Save your app state to buf */
    return 0;  /* Return bytes written, 0 = no state */
}

static int app_restore(const void *buf, size_t len)
{
    /* Restore your app state from buf */
    return 0;
}

int main(int argc, char *argv[])
{
    lv_obj_t *screen = pc_app_get_screen();

    /* Create your UI */
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "Hello, PicoCalc!");
    lv_obj_center(label);

    /* Event loop (LVGL ticks handled by framework) */
    while (1) {
        usleep(50000);  /* 50ms */
    }

    pc_app_exit(0);
    return 0;
}
```

### 9.5 Deliverables

- [ ] Battery % and charge status on status bar
- [ ] Screen dims/sleeps on timeout, wakes on keypress
- [ ] All global hotkeys functional
- [ ] OTA update downloads and installs firmware
- [ ] SDK template app compiles and runs on device
- [ ] README and API docs complete
- [ ] CI builds firmware + packages

---

## Dependency Graph

```
Phase 1 ─── Board Bring-up
   │
   ▼
Phase 2 ─── Display & Framebuffer
   │
   ▼
Phase 3 ─── Input, LVGL & Launcher ──────────────────┐
   │                                                   │
   ▼                                                   │
Phase 4 ─── Storage, Settings & Package System         │
   │                                                   │
   ├──────────┬──────────┬─────────────────────────────┤
   ▼          ▼          ▼                             │
Phase 5    Phase 6    Phase 7                          │
Terminal   Office     Audio/Video                      │
   │          │          │                             │
   │          │          │         Phase 8 ◄───────────┘
   │          │          │         Networking & SSH
   │          │          │             │
   └──────────┴──────────┴─────────────┘
                    │
                    ▼
              Phase 9 ─── Polish & Distribution
```

**Key dependencies:**
- Phase 2 needs Phase 1 (SPI bus working)
- Phase 3 needs Phase 2 (framebuffer for LVGL)
- Phase 4 needs Phase 3 (LVGL for settings UI, launcher for packages)
- Phases 5/6/7 can proceed in **parallel** after Phase 4
- Phase 8 needs Phase 3 (LVGL) + Phase 5 (terminal widget) + Wi-Fi hardware
- Phase 9 needs all prior phases

---

## Risk Register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| NuttX RP2350 CYW43439 driver immature | High | High | Start Phase 8 early for Wi-Fi; fallback: raw pico-sdk cyw43 + NuttX shim |
| PSRAM access conflicts (DMA + CPU) | Medium | Medium | Use DMA channels carefully, PSRAM mutex for shared regions |
| SPI bandwidth limits video FPS | High | Low | Already planned for 10-15 FPS max; RLE helps |
| wolfSSH too large for flash | Medium | Medium | Fallback: dropbear (smaller); strip unused features |
| LVGL memory fragmentation | Medium | Medium | Fixed-size LVGL heap, PSRAM for large buffers |
| Third-party ELF loading fails | Medium | Low | Built-in apps work regardless; ELF is bonus feature |
| SD card SPI conflicts with display SPI | Low | High | Separate SPI buses (SPI0 for SD, SPI1 for display) |
| STM32 keyboard protocol undocumented | Medium | Medium | Reverse-engineer from PicoCalc reference code |
| 16MB flash too small for all features | Low | Medium | LittleFS compression, strip debug symbols, modular builds |

---

## Estimated Timeline

| Phase | Duration | Cumulative |
|---|---|---|
| Phase 1: Board Bring-up | 2-3 weeks | Week 3 |
| Phase 2: Display | 2-3 weeks | Week 6 |
| Phase 3: Input & LVGL | 3-4 weeks | Week 10 |
| Phase 4: Storage & Packages | 3-4 weeks | Week 14 |
| Phase 5-7: Apps (parallel) | 3-4 weeks | Week 18 |
| Phase 8: Networking | 4-6 weeks | Week 24 |
| Phase 9: Polish | 2-3 weeks | Week 27 |
| **Total** | **~6-7 months** | |

This assumes a single developer working part-time. With focused full-time work, this could compress to 3-4 months.

---

## Implementation Updates — Session 3

### Completed Features

#### 1. Launcher App Entry Fix
- **Problem**: LVGL v9 editing mode consumes `LV_KEY_ENTER` to toggle editing, never generating `LV_EVENT_CLICKED`
- **Fix**: Added `LV_EVENT_READY` handler in [pcterm/src/launcher.c](../pcterm/src/launcher.c) that calls `launcher_queue_selected()` and re-enables editing mode
- **Files**: `pcterm/src/launcher.c`

#### 2. Status Bar Refinement
- Rewrote to use LVGL flex row layout (`LV_FLEX_FLOW_ROW`, `SPACE_BETWEEN`) for auto-spacing — no more text overlap
- Battery uses LVGL built-in `LV_SYMBOL_BATTERY_*` icons with 4 display styles: Icon+%, Icon Only, Text Bars, Percent Only
- Compact `HH:MM` clock format
- **Files**: `pcterm/src/statusbar.c`, `pcterm/include/pcterm/statusbar.h`

#### 3. Battery & Power Settings Tab
- New `settings_power.c` in Settings app with:
  - Battery display style dropdown (4 options)
  - Power profile dropdown (Standard 150MHz / High Performance 200MHz / Power Save 100MHz)
  - Backlight timeout dropdown (Never / 30s / 1m / 2m / 5m / 10m)
- Config persistence: `battery_style`, `power_profile`, `backlight_timeout` in `pc_config_t`
- **Files**: `apps/settings/settings_power.c`, `apps/settings/settings_main.c`, `pcterm/include/pcterm/config.h`, `pcterm/src/config.c`

#### 4. Core Frequency Profiles (RP2350 PLL Management)
- New `rp23xx_clockmgr.c` with 3 clock profiles:
  - Standard: 150 MHz (FBDIV=150, PD1=5, PD2=2)
  - High Performance: 200 MHz (FBDIV=200, PD1=5, PD2=2)
  - Power Save: 100 MHz (FBDIV=100, PD1=5, PD2=2)
- Glitchless clock switching: switches CLK_SYS to XOSC before PLL reconfig, waits for lock, switches back
- **Files**: `boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_clockmgr.c`, `boards/arm/rp23xx/picocalc-rp2350b/include/board.h`

#### 5. Backlight Timeout & Sleep
- New `rp23xx_sleep.c` with:
  - Activity tracking via `CLOCK_MONOTONIC`
  - Dimming at 75% of timeout, off at 100%
  - Sleep mode: backlight off + power-save clock profile
  - Wake on any key press
- Input driver calls `rp23xx_backlight_activity()` on key events
- Main loop calls `rp23xx_backlight_timer_tick()` every second
- **Files**: `boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_sleep.c`, `pcterm/src/lv_port_indev.c`, `nuttx-apps/examples/pcterm/pcterm_main.c`

#### 6. PSRAM Fixes
- **Coalescing bug**: Old code zeroed sizes without removing dead blocks from array, causing phantom blocks and memory leaks. Fixed with proper multi-pass merge + array compaction using element shifting
- **psram_available()**: Fixed to skip zero-size blocks
- **PSRAM_MAX_BLOCKS**: Increased from 256 to 512 for more allocation headroom
- **Files**: `boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_psram.c`

#### 7. Keyboard F1-F12 Mapping
- Added proper F-key code definitions: F1=0x81 through F10=0x90 (from STM32 south-bridge firmware)
- Removed incorrect legacy compat codes (0x81-0x8A as arrow keys)
- Defined custom LVGL key constants: `LV_KEY_F1` (0xF001) through `LV_KEY_F10` (0xF00A)
- Created shared header `pcterm/include/pcterm/keys.h` with F-key codes and VT100 escape sequences
- Added PGUP/PGDN mapping (0xD6/0xD7)
- **Files**: `pcterm/src/lv_port_indev.c`, `pcterm/include/pcterm/keys.h`

#### 8. NTP Command Fix + Time Command Enhancement
- **NTP Makefile Fix**: Removed duplicate PROGNAME entries for `time` and `ntp` that were registered under both `CONFIG_SYSTEM_PCSSH` and their own configs. Each command now compiles independently.
- **Time Command**: Full rewrite with Unix-like subcommands:
  - `time` — Unix-style output: "Wed Jun 18 14:30:45 UTC 2025"
  - `time -v` — Verbose with date, time, timezone, epoch, uptime
  - `time set YYYY-MM-DD HH:MM:SS` — Set date/time
  - `time epoch` — Print Unix epoch seconds
  - `time uptime` — System uptime (days, hours, minutes)
  - `time tz` / `time tz set <value>` — View/set timezone
  - `time date` — Date only
  - `time clock` — Time only
  - `time iso` — ISO 8601 format
- **Files**: `nuttx-apps/system/pcssh/pctime_main.c`, `nuttx-apps/system/pcssh/Makefile`

#### 9. minipkg Package Manager
- New CLI tool `minipkg` utilizing existing `pcpkg_*` infrastructure
- Commands: `list`, `info <name>`, `install <file.pcpkg>`, `remove <name>`, `search <query>`, `update`, `upgrade <name>`, `scan`, `catalog`, `refresh`
- Kconfig entry: `CONFIG_SYSTEM_PCMINIPKG` with configurable progname, priority, stack size
- Short aliases: `ls`, `i`, `rm`, `s`
- **Files**: `nuttx-apps/system/pcssh/pcminipkg_main.c`, `nuttx-apps/system/pcssh/Kconfig`, `nuttx-apps/system/pcssh/Makefile`

#### 10. Wireless CLI Tools
- **wifi command** (`pcwifi_main.c`): `status`, `scan`, `connect <ssid> [pass]`, `disconnect`, `ip`, `saved`, `autoconnect [on|off]`
  - Signal strength bars display, auth type info, auto-saves credentials to config
- **bt command** (`pcbt_main.c`): `status`, `on`, `off`, `scan`, `pair <addr>`, `unpair <addr>`, `devices`
  - Uses weak-linked stubs — will function when CYW43 BT driver is implemented
- Added `rp23xx_wifi_disconnect()` stub to `link_stubs.c`
- Kconfig entries: `CONFIG_SYSTEM_PCWIFI`, `CONFIG_SYSTEM_PCBT`
- **Files**: `nuttx-apps/system/pcssh/pcwifi_main.c`, `nuttx-apps/system/pcssh/pcbt_main.c`, `pcterm/src/link_stubs.c`

#### 11. Movable Status Bar
- Status bar position configurable: Top (default) or Bottom
- `statusbar_set_position()` re-layouts bar and app area immediately with animation
- Display settings tab now includes "Status Bar Position" dropdown
- Config persistence: `statusbar_position` in `pc_config_t`
- **Files**: `pcterm/include/pcterm/statusbar.h`, `pcterm/src/statusbar.c`, `pcterm/include/pcterm/config.h`, `pcterm/src/config.c`, `apps/settings/settings_display.c`, `nuttx-apps/examples/pcterm/pcterm_main.c`

### New Files Created This Session
| File | Purpose |
|---|---|
| `boards/.../src/rp23xx_clockmgr.c` | RP2350 PLL clock frequency management |
| `boards/.../src/rp23xx_sleep.c` | Backlight timeout + sleep controller |
| `apps/settings/settings_power.c` | Battery & Power settings UI tab |
| `pcterm/include/pcterm/keys.h` | Shared F-key and VT100 escape definitions |
| `nuttx-apps/system/pcssh/pcminipkg_main.c` | minipkg package manager CLI |
| `nuttx-apps/system/pcssh/pcwifi_main.c` | Wi-Fi CLI command |
| `nuttx-apps/system/pcssh/pcbt_main.c` | Bluetooth CLI command |

### defconfig Changes
```
CONFIG_SYSTEM_PCMINIPKG=y
CONFIG_SYSTEM_PCWIFI=y
CONFIG_SYSTEM_PCBT=y
```

### Board Make.defs Changes
```makefile
CSRCS += rp23xx_clockmgr.c
CSRCS += rp23xx_sleep.c
```
