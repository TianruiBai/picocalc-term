# pcpkg-create

Creates PicoCalc package (`.pcpkg`) archives from compiled third-party apps.

## Requirements

- Python 3.8+

## Usage

```bash
python pcpkg_create.py <app_dir> -o <output.pcpkg>
```

### Example

```bash
# Build your app (produces app.elf)
make -C my_app/

# Create the package
python pcpkg_create.py my_app/ -o my_app.pcpkg
```

## App Directory Structure

```
my_app/
├── manifest.json      # Required: package metadata
├── app.elf            # Required: compiled ARM ELF binary
├── icon.bin           # Optional: 32×32 RGB565 icon (2048 bytes)
└── assets/            # Optional: additional files
    ├── help.txt
    └── data/
```

## manifest.json Format

```json
{
    "name": "myapp",
    "version": "1.0.0",
    "author": "Your Name",
    "description": "A short description of the app",
    "category": "entertainment",
    "min_ram": 65536,
    "requires_network": false
}
```

### Fields

| Field | Required | Description |
|---|---|---|
| `name` | Yes | Package ID (alphanumeric, max 32 chars) |
| `version` | Yes | Semantic version string |
| `author` | Yes | Author name |
| `description` | Yes | Short description |
| `category` | Yes | One of: system, office, entertainment, network, tools |
| `min_ram` | No | Minimum PSRAM bytes (default: 0) |
| `requires_network` | No | Requires Wi-Fi (default: false) |

## .pcpkg File Format

```
[Header: 16 bytes]
  Magic:       "PCPK" (4 bytes)
  Version:     uint16 (2 bytes)
  File count:  uint16 (2 bytes)
  Reserved:    8 bytes

[File Table: N × 264 bytes]
  Filename:    256 bytes (UTF-8, null-padded)
  Offset:      uint32 (4 bytes)
  Size:        uint32 (4 bytes)

[File Data]
  Raw file contents, concatenated
```

## Installation on PicoCalc

Copy the `.pcpkg` file to `/mnt/sd/apps/` on the SD card, then use:
- Settings → Packages → Install
- Or the package manager will auto-detect new `.pcpkg` files

## See Also

- [App Framework API](../../docs/App%20Framework%20API.md) — API reference
- [SDK Template](../../sdk/template/) — Starter project
