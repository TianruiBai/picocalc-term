#!/bin/bash
# Build NuttX with clean PATH (Linux tools only, no MSYS2)
set -e
cd /home/polar/picocalc-term/nuttx

# Ensure PATH prefers Linux tools over Windows/MSYS2
export PATH="/home/polar/.local/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"

echo "Toolchain: $(arm-none-eabi-gcc --version | head -1)"
echo "Building with $(nproc) jobs..."

make -j$(nproc) 2>&1
