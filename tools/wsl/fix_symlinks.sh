#!/bin/bash
# Fix all NuttX symlinks to use WSL-native paths
set -ex
cd /home/polar/picocalc-term/nuttx

# Remove old symlinks
rm -f Make.defs
rm -f arch/arm/src/chip
rm -f arch/arm/src/board
rm -f drivers/platform
rm -f include/arch/chip
rm -f include/arch/board

# Recreate with correct WSL-native paths
ln -s /home/polar/picocalc-term/boards/arm/rp23xx/picocalc-rp2350b/scripts/Make.defs Make.defs
ln -s /home/polar/picocalc-term/nuttx/arch/arm/src/rp23xx arch/arm/src/chip
ln -s /home/polar/picocalc-term/nuttx/boards/arm/rp23xx/common arch/arm/src/board
ln -s /home/polar/picocalc-term/nuttx/drivers/dummy drivers/platform
ln -s /home/polar/picocalc-term/nuttx/arch/arm/include/rp23xx include/arch/chip
ln -s /home/polar/picocalc-term/boards/arm/rp23xx/picocalc-rp2350b/include include/arch/board

echo "=== Symlinks fixed ==="
ls -la Make.defs arch/arm/src/chip arch/arm/src/board drivers/platform include/arch/chip include/arch/board
