#!/bin/bash
# Install picotool for RP2350 UF2 generation
set -e
export PATH="${HOME}/.local/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"

if command -v picotool >/dev/null 2>&1; then
  echo "picotool already installed: $(picotool version | head -1)"
  exit 0
fi

PICOTOOL_DIR="/tmp/picotool_build"

# Clean any previous attempt
rm -rf "$PICOTOOL_DIR"
mkdir -p "$PICOTOOL_DIR"
cd "$PICOTOOL_DIR"

echo "==> Cloning picotool..."
git clone --depth 1 https://github.com/raspberrypi/picotool.git .

echo "==> Cloning pico-sdk (needed for picotool build)..."
git clone --depth 1 https://github.com/raspberrypi/pico-sdk.git /tmp/pico-sdk-for-picotool

echo "==> Building picotool..."
mkdir -p build
cd build
cmake .. \
  -DPICO_SDK_PATH=/tmp/pico-sdk-for-picotool \
  -DPICOTOOL_NO_LIBUSB=1 \
  -DCMAKE_INSTALL_PREFIX=/usr/local

make -j$(nproc)

echo "==> Installing picotool..."
sudo make install

echo "==> Verifying..."
picotool version
echo "Done!"
