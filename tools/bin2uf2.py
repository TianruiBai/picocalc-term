#!/usr/bin/env python3
"""Convert a raw binary file to UF2 format for RP2350 (ARM-S).

Usage: python bin2uf2.py input.bin output.uf2 [base_address] [family_id]

Defaults:
    base_address = 0x10000000 (RP2350 XIP flash)
    family_id    = 0xe48bff59 (RP2350 ARM-S)
"""

import struct
import sys
import os

UF2_MAGIC_START0 = 0x0A324655  # "UF2\n"
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END    = 0x0AB16F30
UF2_FLAG_FAMILY  = 0x00002000

PAYLOAD_SIZE = 256  # bytes per UF2 block

# RP2350 ARM-S family ID
RP2350_FAMILY_ID = 0xE48BFF59


def convert(input_path, output_path, base_addr=0x10000000, family_id=RP2350_FAMILY_ID):
    with open(input_path, "rb") as f:
        data = f.read()

    num_blocks = (len(data) + PAYLOAD_SIZE - 1) // PAYLOAD_SIZE

    blocks = []
    for i in range(num_blocks):
        offset = i * PAYLOAD_SIZE
        chunk = data[offset:offset + PAYLOAD_SIZE]
        # Pad to 256 bytes if last chunk is short
        chunk = chunk.ljust(PAYLOAD_SIZE, b"\x00")

        # Build the 512-byte UF2 block
        # 32 bytes header + 476 bytes (256 data + 220 padding) + 4 bytes footer
        header = struct.pack(
            "<IIIIIIII",
            UF2_MAGIC_START0,
            UF2_MAGIC_START1,
            UF2_FLAG_FAMILY,       # flags
            base_addr + offset,     # target address
            PAYLOAD_SIZE,           # payload size
            i,                      # block number
            num_blocks,             # total blocks
            family_id,              # family ID
        )
        # Data payload + padding to fill 476 bytes
        payload = chunk + b"\x00" * (476 - PAYLOAD_SIZE)
        footer = struct.pack("<I", UF2_MAGIC_END)
        blocks.append(header + payload + footer)

    with open(output_path, "wb") as f:
        for block in blocks:
            f.write(block)

    return len(data), num_blocks


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]
    base_addr = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x10000000
    family_id = int(sys.argv[4], 0) if len(sys.argv) > 4 else RP2350_FAMILY_ID

    if not os.path.exists(input_path):
        print(f"Error: {input_path} not found")
        sys.exit(1)

    data_size, num_blocks = convert(input_path, output_path, base_addr, family_id)
    uf2_size = num_blocks * 512
    print(f"Converted {data_size} bytes -> {output_path}")
    print(f"  {num_blocks} UF2 blocks, {uf2_size} bytes")
    print(f"  Base address: 0x{base_addr:08X}")
    print(f"  Family ID: 0x{family_id:08X} (RP2350 ARM-S)")


if __name__ == "__main__":
    main()
