#!/bin/bash
# Fix config and run olddefconfig with proper PATH
set -e
cd /home/polar/picocalc-term/nuttx

# Ensure PATH prefers Linux tools over Windows/MSYS2
export PATH="/home/polar/.local/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"

# Remove duplicate HOST_LINUX entries, keep just one
grep -v "CONFIG_HOST_LINUX" .config > .config.tmp || true
echo "CONFIG_HOST_LINUX=y" >> .config.tmp
mv .config.tmp .config

echo "Running olddefconfig with clean PATH..."
echo "menuconfig at: $(command -v menuconfig)"
echo "olddefconfig at: $(command -v olddefconfig)"

make olddefconfig 2>&1

echo ""
echo "Config expanded successfully!"
echo "Total .config lines: $(wc -l < .config)"
echo ""
echo "Key settings:"
grep -E "HO