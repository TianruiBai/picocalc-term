#!/bin/bash
set -euo pipefail

cd /mnt/c/Users/weyst/Documents/picocalc-term
LOG="build19.log"

exec > >(tee "$LOG") 2>&1

echo "=== Build 19 ==="
echo "Date: $(date)"
echo "Toolchain: $(arm-none-eabi-gcc --version | head -1)"
echo ""

cd nuttx

# Step 1: Clean old MSYS build objects
echo "==> Step 1: make clean (remove old objects)"
make clean 2>/dev/null || true

# Step 2: Delete ALL stale Make.dep and .depend files
echo "==> Step 2: Purge all Make.dep / .depend files"
find . ../nuttx-apps -name 'Make.dep' -delete 2>/dev/null || true
find . ../nuttx-apps -name '.depend' -delete 2>/dev/null || true
echo "    Done"

# Step 3: Re-configure for Linux host
echo "==> Step 3: configure picocalc-rp2350b:full"
./tools/configure.sh -l picocalc-rp2350b:full

echo "==> Step 4: olddefconfig"
make olddefconfig

# Verify host config
echo "==> Config check:"
grep 'CONFIG_HOST' .config || true

# Step 5: Build
echo "==> Step 5: make -j4"
make -j4

# Result
echo ""
if [ -f nuttx.uf2 ]; then
    echo "==> BUILD SUCCESS"
    ls -lh nuttx.uf2
else
    echo "==> BUILD FAILED"
    exit 1
fi
