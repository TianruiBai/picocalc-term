# PicoCalc Package Format Specification (`.pcpkg`)

**Version:** 1.0  
**Status:** Draft  
**Last Updated:** 2026  

---

## 1. Overview

The `.pcpkg` format is a simple binary archive for distributing third-party applications on the PicoCalc-Term platform. It is designed for minimal parsing overhead on an MCU with limited resources (RP2350B, 520KB SRAM + 8MB PSRAM).

A package contains:
- A binary header for quick validation
- A file table for random access to contents
- Raw file data (manifest, ELF binary, icon, assets)

Packages are installed to SD card at `/mnt/sd/apps/<name>/` and tracked in `/mnt/sd/apps/registry.json`.

---

## 2. File Layout

```
┌──────────────────────────────────────────┐
│ Header (16 bytes)                        │
├──────────────────────────────────────────┤
│ File Table Entry 0 (264 bytes)           │
│ File Table Entry 1 (264 bytes)           │
│ ...                                      │
│ File Table Entry N-1 (264 bytes)         │
├──────────────────────────────────────────┤
│ File Data 0                              │
│ File Data 1                              │
│ ...                                      │
│ File Data N-1                            │
└──────────────────────────────────────────┘
```

**Total header + table size:** `16 + (264 × file_count)` bytes  
**File data starts at:** offset `16 + (264 × file_count)`

---

## 3. Header (16 bytes)

| Offset | Size | Type     | Field         | Description |
|--------|------|----------|---------------|-------------|
| 0      | 4    | char[4]  | `magic`       | Magic bytes: `"PCPK"` (0x50 0x43 0x50 0x4B) |
| 4      | 2    | uint16   | `version`     | Format version (currently `1`) |
| 6      | 2    | uint16   | `file_count`  | Number of files in the archive |
| 8      | 4    | uint32   | `total_size`  | Total archive size in bytes |
| 12     | 4    | uint32   | `flags`       | Reserved flags (must be 0) |

All multi-byte integers are **little-endian** (native RP2350B / ARM Cortex-M33 byte order).

### C Structure

```c
typedef struct __attribute__((packed)) pcpkg_header_s
{
    char     magic[4];       /* "PCPK" */
    uint16_t version;        /* 1 */
    uint16_t file_count;     /* N */
    uint32_t total_size;     /* Archive size (bytes) */
    uint32_t flags;          /* Reserved, must be 0 */
} pcpkg_header_t;

_Static_assert(sizeof(pcpkg_header_t) == 16, "header size");
```

### Validation Rules

1. `magic` must equal `"PCPK"` exactly
2. `version` must be `1`
3. `file_count` must be ≥ 1 (at least `manifest.json`)
4. `total_size` must match the actual file size on disk
5. `flags` must be 0 (reject unknown flags)

---

## 4. File Table Entry (264 bytes)

Each entry describes one file in the archive:

| Offset | Size | Type      | Field    | Description |
|--------|------|-----------|----------|-------------|
| 0      | 256  | char[256] | `name`   | NUL-terminated relative path (e.g., `"manifest.json"`, `"assets/font.bin"`) |
| 256    | 4    | uint32    | `offset` | Byte offset from start of archive to file data |
| 260    | 4    | uint32    | `size`   | File size in bytes |

### C Structure

```c
typedef struct __attribute__((packed)) pcpkg_file_entry_s
{
    char     name[256];
    uint32_t offset;
    uint32_t size;
} pcpkg_file_entry_t;

_Static_assert(sizeof(pcpkg_file_entry_t) == 264, "entry size");
```

### Path Rules

- Paths are relative to the package root (no leading `/`)
- Forward slashes only (e.g., `assets/data.bin`)
- No `..` components allowed
- Maximum path length: 255 characters (plus NUL terminator)
- Case-sensitive (FAT32 will normalize, but the format itself is case-sensitive)

---

## 5. File Data

File data immediately follows the file table. Each file's data is stored at the offset specified in its table entry. Files are stored contiguously with no padding or alignment requirements.

**Offset constraint:** Every `offset` value must satisfy:
```
offset >= 16 + (264 × file_count)
offset + size <= total_size
```

---

## 6. Required Files

Every valid `.pcpkg` must contain at minimum:

### 6.1 `manifest.json`

Package metadata in JSON format:

```json
{
    "name": "calculator",
    "version": "1.0.0",
    "display_name": "Calculator",
    "author": "developer_name",
    "description": "A simple calculator app",
    "category": "utility",
    "entry": "app.elf",
    "icon": "icon.bin",
    "min_ram": 32768,
    "requires": ["audio"],
    "flags": 0
}
```

| Field          | Type     | Required | Description |
|----------------|----------|----------|-------------|
| `name`         | string   | Yes      | Package identifier (alphanumeric + hyphens, max 31 chars) |
| `version`      | string   | Yes      | Semantic version (e.g., `"1.2.3"`) |
| `display_name` | string   | Yes      | Human-readable name for launcher (max 31 chars) |
| `author`       | string   | Yes      | Author/publisher name |
| `description`  | string   | No       | Short description (max 255 chars) |
| `category`     | string   | No       | One of: `utility`, `game`, `productivity`, `network`, `media`, `system` |
| `entry`        | string   | Yes      | Filename of the ELF binary within the package |
| `icon`         | string   | No       | Filename of the 32×32 RGB565 icon (2,048 bytes) |
| `min_ram`      | integer  | No       | Minimum PSRAM bytes required (default: 0) |
| `requires`     | string[] | No       | Hardware capabilities: `"wifi"`, `"audio"`, `"bluetooth"` |
| `flags`        | integer  | No       | App flags bitmask (see §6.1.1) |

#### 6.1.1 App Flags

| Bit | Name               | Description |
|-----|--------------------|-------------|
| 0   | `BACKGROUND`       | App can run in background (e.g., audio player) |
| 1   | `STATEFUL`         | App supports save/restore state |
| 2   | `NETWORK`          | App requires network access |
| 3   | `FULL_SCREEN`      | App hides the status bar |
| 4-31| Reserved           | Must be 0 |

### 6.2 `app.elf`

An ARM Cortex-M33 ELF binary compiled with the PicoCalc SDK. Requirements:

- **Architecture:** ARM Thumb-2 (Cortex-M33)
- **ABI:** Hard float (if using FPU) or soft float
- **Position:** Must be position-independent (PIC/PIE) or relocatable
- **Entry symbol:** `pc_app_main` (matches `pc_app_main_t` typedef)
- **Linked against:** PicoCalc SDK stubs (syscall-based API)
- **Max size:** 2 MB (flash/SD constraint)

### 6.3 `icon.bin` (optional)

Raw 32×32 pixel icon in RGB565 format:

- **Size:** Exactly 2,048 bytes (32 × 32 × 2)
- **Pixel order:** Row-major, top-left to bottom-right
- **Byte order:** Little-endian RGB565

If omitted, the launcher displays a default icon.

---

## 7. Installation Process

The package manager performs these steps when installing a `.pcpkg`:

```
1. Open and read 16-byte header
2. Validate magic ("PCPK"), version (1), file_count (≥1)
3. Read file table (264 × file_count bytes)
4. Find "manifest.json" in file table
5. Read and parse manifest JSON
6. Validate manifest fields:
   a. "name" is valid identifier
   b. "entry" file exists in file table
   c. "min_ram" ≤ available PSRAM
7. Create directory: /mnt/sd/apps/<name>/
8. Extract all files:
   a. For each file table entry:
      - Create subdirectories as needed (mkdir -p)
      - Seek to entry.offset in archive
      - Read entry.size bytes → write to /mnt/sd/apps/<name>/<entry.name>
9. Update registry.json:
   a. Add entry with name, version, install_path, installed_size, timestamp
   b. Write registry atomically (write temp → rename)
10. Optionally delete the original .pcpkg file
```

### Error Handling

| Condition | Action |
|-----------|--------|
| Invalid magic/version | Reject with `PC_ERR_INVAL` |
| File too small for header + table | Reject |
| manifest.json not found | Reject with `PC_ERR_NOENT` |
| Invalid manifest JSON | Reject with `PC_ERR_INVAL` |
| Package already installed (same name) | Reject (user must uninstall first) |
| Insufficient PSRAM for min_ram | Reject with `PC_ERR_NOMEM` |
| SD card write failure | Roll back (delete partial directory), return `PC_ERR_IO` |
| File path contains `..` | Reject (path traversal protection) |

---

## 8. Uninstallation

```
1. Look up package by name in registry.json
2. Recursively delete /mnt/sd/apps/<name>/
3. Remove entry from registry.json
4. If app state exists in /mnt/sd/etc/appstate/<name>.bin, delete it
```

---

## 9. Registry Format (`registry.json`)

```json
{
    "version": 1,
    "packages": [
        {
            "name": "calculator",
            "version": "1.0.0",
            "install_path": "/mnt/sd/apps/calculator",
            "installed_size": 45312,
            "install_timestamp": 1709899200
        },
        {
            "name": "mygame",
            "version": "0.2.1",
            "install_path": "/mnt/sd/apps/mygame",
            "installed_size": 128000,
            "install_timestamp": 1710000000
        }
    ]
}
```

| Field               | Type    | Description |
|---------------------|---------|-------------|
| `version`           | integer | Registry format version (1) |
| `packages`          | array   | List of installed packages |
| `packages[].name`   | string  | Package identifier |
| `packages[].version`| string  | Installed version |
| `packages[].install_path` | string | Absolute path to install directory |
| `packages[].installed_size` | integer | Total extracted size in bytes |
| `packages[].install_timestamp` | integer | Unix timestamp of installation |

---

## 10. Creating Packages

### Using the `pcpkg_create.py` tool:

```bash
python tools/pcpkg-create/pcpkg_create.py \
    --manifest myapp/manifest.json \
    --files myapp/app.elf myapp/icon.bin myapp/assets/ \
    --output myapp.pcpkg
```

### Manual creation (Python pseudocode):

```python
import struct

files = {
    "manifest.json": manifest_bytes,
    "app.elf": elf_bytes,
    "icon.bin": icon_bytes,
}

header_size = 16
entry_size = 264
table_size = entry_size * len(files)
data_start = header_size + table_size

# Build file table
offset = data_start
entries = []
for name, data in files.items():
    entry = name.encode().ljust(256, b'\x00')
    entry += struct.pack('<II', offset, len(data))
    entries.append(entry)
    offset += len(data)

total_size = offset

# Write archive
with open("output.pcpkg", "wb") as f:
    # Header
    f.write(b'PCPK')
    f.write(struct.pack('<HH', 1, len(files)))
    f.write(struct.pack('<II', total_size, 0))
    
    # File table
    for entry in entries:
        f.write(entry)
    
    # File data
    for data in files.values():
        f.write(data)
```

---

## 11. SDK Integration

Third-party apps link against the PicoCalc SDK, which provides:

| API Function             | Description |
|--------------------------|-------------|
| `pc_app_exit(code)`      | Exit app and return to launcher |
| `pc_app_yield()`         | Save state and return to launcher |
| `pc_app_get_screen()`    | Get LVGL app area (320×300) |
| `pc_app_psram_alloc(sz)` | Allocate from PSRAM heap |
| `pc_app_psram_free(ptr)` | Free PSRAM allocation |
| `pc_app_get_hostname()`  | Get device hostname |

App registration:

```c
#include "pcterm/app.h"

static int my_app_main(int argc, char *argv[]) {
    lv_obj_t *scr = pc_app_get_screen();
    /* ... build UI ... */
    return 0;
}

PC_APP_REGISTER(myapp, "My App", "1.0.0", 32768,
                PC_APP_FLAG_STATEFUL,
                my_app_main, my_save, my_restore);
```

Cross-compile with the SDK Makefile:

```makefile
TOOLCHAIN = arm-none-eabi-
CC = $(TOOLCHAIN)gcc
CFLAGS = -mcpu=cortex-m33 -mthumb -Os -fPIC
LDFLAGS = -nostartfiles -shared -T sdk.ld
```

---

## 12. Size Limits

| Resource         | Limit    | Rationale |
|------------------|----------|-----------|
| Package archive  | 4 MB     | SD card + extraction time |
| ELF binary       | 2 MB     | Flash/PSRAM constraint |
| Icon             | 2,048 B  | Fixed 32×32 RGB565 |
| File count       | 65,535   | uint16 file_count field |
| Path length      | 255 chars| FAT32 compatibility |
| Package name     | 31 chars | Registry field width |
| Installed total  | ~8 MB    | SD card practical limit per package |

---

## 13. Future Extensions

Reserved for format version 2+:

- **Compression:** Per-file LZ4 or zlib compression (flag bit in file_entry)
- **Signatures:** Ed25519 package signing for trusted sources
- **Dependencies:** Package dependency resolution
- **Delta updates:** Binary diff patches for version upgrades
- **Multi-arch:** Support for both Cortex-M33 and Hazard3 RISC-V binaries

---

## Appendix A: Byte Diagram

```
Offset 0x00:  50 43 50 4B  01 00 03 00  XX XX XX XX  00 00 00 00
              P  C  P  K   ver=1 cnt=3  total_size   flags=0

Offset 0x10:  6D 61 6E 69  66 65 73 74  2E 6A 73 6F  6E 00 00 00
              m  a  n  i   f  e  s  t   .  j  s  o   n  \0  ...
              ... (256 bytes padded name) ...
Offset 0x110: XX XX XX XX  XX XX XX XX
              file_offset  file_size

Offset 0x118: 61 70 70 2E  65 6C 66 00  00 00 00 00  ...
              a  p  p  .   e  l  f  \0  ...
              ... (next entry) ...
```
