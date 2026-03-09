#!/bin/bash
exec > /c/Users/weyst/Documents/picocalc-term/boardcheck.log 2>&1

cd /c/Users/weyst/Documents/picocalc-term/nuttx/arch/arm/src

echo "=== board symlink ==="
ls -la board

echo ""
echo "=== board/board symlink ==="
ls -la board/board 2>&1

echo ""
echo "=== Looking for rp23xx_bringup.c ==="
ls -la board/rp23xx_bringup.c 2>&1
ls -la board/board/rp23xx_bringup.c 2>&1

echo ""
echo "=== Find all rp23xx_bringup.c ==="
find board -name 'rp23xx_bringup.c' -type f 2>&1

echo ""
echo "=== head of board/board/rp23xx_bringup.c ==="
head -30 board/board/rp23xx_bringup.c 2>&1

echo "=== DONE ==="
