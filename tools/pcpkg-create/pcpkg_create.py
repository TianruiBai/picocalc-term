#!/usr/bin/env python3
"""
pcpkg-create — Create PicoCalc package (.pcpkg) archives.

A .pcpkg file is a simple archive containing:
  - manifest.json   (package metadata)
  - app.elf         (ARM ELF binary)
  - icon.bin        (32x32 RGB565, optional)
  - assets/         (additional files, optional)

Usage:
    python pcpkg_create.py <app_dir> -o <output.pcpkg>

The app_dir must contain at least manifest.json and app.elf.
"""

import argparse
import json
import os
import struct
import sys
from pathlib import Path


PCPKG_MAGIC = b"PCPK"
PCPKG_VERSION = 1


def validate_manifest(manifest_path):
    """Validate manifest.json and return parsed data."""
    with open(manifest_path, "r") as f:
        manifest = json.load(f)

    required = ["name", "version", "author", "description", "category"]
    for field in required:
        if field not in manifest:
            print(f"ERROR: Missing required field '{field}' in manifest.json")
            sys.exit(1)

    name = manifest["name"]
    if not name.isalnum() and not all(c.isalnum() or c == '_' for c in name):
        print(f"ERROR: Package name must be alphanumeric (got '{name}')")
        sys.exit(1)

    if len(name) > 32:
        print(f"ERROR: Package name too long (max 32 chars)")
        sys.exit(1)

    return manifest


def create_package(app_dir, output_path):
    """Create a .pcpkg archive from the app directory."""
    app_dir = Path(app_dir)

    # Validate required files
    manifest_path = app_dir / "manifest.json"
    elf_path = app_dir / "app.elf"

    if not manifest_path.exists():
        print(f"ERROR: {manifest_path} not found")
        sys.exit(1)

    if not elf_path.exists():
        print(f"ERROR: {elf_path} not found")
        sys.exit(1)

    manifest = validate_manifest(manifest_path)
    print(f"Package: {manifest['name']} v{manifest['version']}")
    print(f"Author:  {manifest['author']}")

    # Collect all files to include
    files = []

    # manifest.json (always first)
    files.append(("manifest.json", manifest_path))

    # app.elf
    files.append(("app.elf", elf_path))

    # icon.bin (optional)
    icon_path = app_dir / "icon.bin"
    if icon_path.exists():
        files.append(("icon.bin", icon_path))

    # assets/ directory (optional)
    assets_dir = app_dir / "assets"
    if assets_dir.is_dir():
        for asset in sorted(assets_dir.rglob("*")):
            if asset.is_file():
                rel = asset.relative_to(app_dir)
                files.append((str(rel).replace("\\", "/"), asset))

    # Write .pcpkg
    with open(output_path, "wb") as f:
        # Header: magic(4) + version(2) + file_count(2) + reserved(8)
        f.write(PCPKG_MAGIC)
        f.write(struct.pack("<HH", PCPKG_VERSION, len(files)))
        f.write(b"\x00" * 8)  # reserved

        # File table
        file_data_start = 16 + (len(files) * 264)  # header + entries
        offset = file_data_start

        entries = []
        for name, path in files:
            size = path.stat().st_size
            # Entry: name(256) + offset(4) + size(4)
            name_bytes = name.encode("utf-8")[:255]
            entry = struct.pack("<256sII",
                                name_bytes, offset, size)
            f.write(entry)
            entries.append((path, size))
            offset += size

        # File data
        total_size = 0
        for path, size in entries:
            with open(path, "rb") as src:
                data = src.read()
                f.write(data)
                total_size += len(data)

    pkg_size = os.path.getsize(output_path)
    print(f"Created: {output_path} ({pkg_size:,} bytes)")
    print(f"  {len(files)} files, {total_size:,} bytes payload")


def main():
    parser = argparse.ArgumentParser(
        description="Create PicoCalc package (.pcpkg)")
    parser.add_argument("app_dir", help="App directory with manifest.json + app.elf")
    parser.add_argument("-o", "--output", required=True, help="Output .pcpkg path")
    args = parser.parse_args()

    create_package(args.app_dir, args.output)


if __name__ == "__main__":
    main()
