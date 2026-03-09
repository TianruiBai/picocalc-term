# PicoCalc Third-Party App SDK

Everything you need to build apps for PicoCalc-Term.

## Quick Start

```bash
# 1. Copy the template
cp -r template/ my_app/
cd my_app/

# 2. Edit manifest.json with your app info
# 3. Write your app in main.c
# 4. Build
make

# 5. Package
make package

# 6. Install: copy my_app.pcpkg to the SD card's /apps/ folder
```

## Prerequisites

- **ARM GNU Toolchain** — `arm-none-eabi-gcc` 13+
- **PicoCalc-Term source tree** — for headers
- **Python 3.8+** — for packaging tool

## API Overview

Your app gets a 320×300 pixel LVGL screen. The top 20 pixels are the system status bar.

```c
#include <pcterm/app.h>
#include <lvgl/lvgl.h>

int main(int argc, char *argv[]) {
    lv_obj_t *screen = pc_app_get_screen();
    
    // Create LVGL widgets on screen
    lv_obj_t *btn = lv_btn_create(screen);
    // ...
    
    pc_app_exit(0);  // Done
    return 0;
}
```

### Key APIs

| Function | Description |
|---|---|
| `pc_app_get_screen()` | Get LVGL root container |
| `pc_app_exit(code)` | Quit (discard state) |
| `pc_app_yield()` | Save state & return to launcher |
| `pc_app_psram_alloc(size)` | Allocate PSRAM memory |
| `pc_app_psram_free(ptr)` | Free PSRAM memory |
| `pc_app_get_hostname()` | Get device hostname |
| `pc_audio_play(path)` | Play audio file |

See [App Framework API](../docs/App%20Framework%20API.md) for complete reference.

## File Structure

```
my_app/
├── manifest.json      # Package metadata (required)
├── main.c             # App source (required → builds to app.elf)
├── Makefile           # Build rules
├── icon.bin           # 32×32 RGB565 LE icon (optional, 2048 bytes)
└── assets/            # Extra files bundled in package (optional)
```

## Tips

- **PSRAM** is your friend — use `pc_app_psram_alloc()` for buffers >1KB
- **SRAM** is limited (~520KB shared with kernel) — keep stack usage low
- Test with the `lvgl` NuttX config before trying the `full` config
- The LVGL font `lv_font_unscii_8` is ideal for terminal/text-heavy UIs
