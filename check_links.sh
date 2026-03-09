#!/bin/bash
set -e
cd /c/Users/weyst/Documents/picocalc-term/nuttx

echo "=== Checking dirlinks ==="
ls -ld include/arch arch/arm/src/chip arch/arm/src/board 2>&1 || true
echo ""

echo "=== Looking for arm_exception.S ==="
find arch/arm/src -name 'arm_exception.S' 2>/dev/null || echo "NOT FOUND"
echo ""

echo "=== Checking chip link target ==="
readlink arch/arm/src/chip 2>/dev/null || echo "NO LINK"
ls arch/arm/src/chip/*.S 2>/dev/null | head -10 || echo "NO ASM FILES"
echo ""

echo "=== arm_exception in armv8-m ==="
find arch/arm/src/armv8-m -name '*exception*' 2>/dev/null || echo "not in armv8-m"
echo ""

echo "=== DONE ==="
