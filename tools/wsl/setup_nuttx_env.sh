#!/usr/bin/env bash
############################################################################
# tools/wsl/setup_nuttx_env.sh
#
# Install all NuttX build prerequisites on Ubuntu Linux.
# Targets: RP2350B (ARM Cortex-M33), arm-none-eabi-gcc 13.x
#
# Usage:  chmod +x tools/wsl/setup_nuttx_env.sh
#         ./tools/wsl/setup_nuttx_env.sh
############################################################################
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

echo "==> Installing system packages"

# Some dev containers ship a broken Yarn apt source that blocks apt update.
# Disable it if present so dependency installation remains deterministic.
if [[ -f /etc/apt/sources.list.d/yarn.list ]]; then
  echo "==> Disabling /etc/apt/sources.list.d/yarn.list (known GPG issue in some containers)"
  sudo mv /etc/apt/sources.list.d/yarn.list /etc/apt/sources.list.d/yarn.list.disabled
fi

sudo apt-get update -qq
sudo apt-get install -y -qq \
  build-essential \
  git \
  make \
  cmake \
  ninja-build \
  ccache \
  gperf \
  flex \
  bison \
  pkg-config \
  libncurses5-dev \
  libncursesw5-dev \
  libffi-dev \
  libssl-dev \
  python3 \
  python3-pip \
  python3-venv \
  python3-setuptools \
  python3-wheel \
  python3-yaml \
  kconfig-frontends \
  genromfs \
  u-boot-tools \
  automake \
  autoconf \
  libtool \
  texinfo \
  patchelf \
  gawk \
  perl \
  gettext \
  unzip \
  curl \
  wget \
  xz-utils \
  file

echo "==> Installing Python packages (kconfiglib, pyelftools)"
python3 -m pip install --user -U pip 2>/dev/null || true
python3 -m pip install --user kconfiglib pyelftools

if ! command -v picotool >/dev/null 2>&1; then
  echo "==> picotool not found, installing it (required for UF2 generation)"
  bash "${ROOT_DIR}/tools/wsl/install_picotool.sh"
else
  echo "==> picotool already in PATH: $(picotool version | head -1)"
fi

# -----------------------------------------------------------------------
# ARM GNU Toolchain — required for RP2350B (Cortex-M33)
# -----------------------------------------------------------------------
ARM_GCC_VER="13.3.rel1"
ARM_GCC_TAR="arm-gnu-toolchain-${ARM_GCC_VER}-x86_64-arm-none-eabi.tar.xz"
ARM_GCC_URL="https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_GCC_VER}/binrel/${ARM_GCC_TAR}"
ARM_GCC_DIR="/opt/arm-gnu-toolchain-${ARM_GCC_VER}"

if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
  echo "==> arm-none-eabi-gcc already in PATH: $(arm-none-eabi-gcc --version | head -1)"
else
  if [[ -x "${ARM_GCC_DIR}/bin/arm-none-eabi-gcc" ]]; then
    echo "==> Toolchain already installed at ${ARM_GCC_DIR}"
  else
    echo "==> Downloading ARM GNU Toolchain ${ARM_GCC_VER} (for Cortex-M33 / RP2350)"
    cd /tmp
    wget -q --show-progress "${ARM_GCC_URL}" -O "${ARM_GCC_TAR}"
    echo "==> Extracting to ${ARM_GCC_DIR}"
    sudo mkdir -p "${ARM_GCC_DIR}"
    sudo tar xf "${ARM_GCC_TAR}" --strip-components=1 -C "${ARM_GCC_DIR}"
    rm -f "${ARM_GCC_TAR}"
  fi

  # Add to PATH for current session and persist in .bashrc
  export PATH="${ARM_GCC_DIR}/bin:${PATH}"
  if ! grep -q "${ARM_GCC_DIR}/bin" ~/.bashrc 2>/dev/null; then
    echo "export PATH=\"${ARM_GCC_DIR}/bin:\${PATH}\"" >> ~/.bashrc
    echo "==> Added toolchain to ~/.bashrc"
  fi
fi

echo ""
echo "==> Verifying toolchain"
arm-none-eabi-gcc --version | head -1
echo "  Target arch support:"
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -E -x c /dev/null -o /dev/null 2>/dev/null \
  && echo "  ✓ Cortex-M33 (RP2350B) supported" \
  || echo "  ✗ WARNING: Cortex-M33 not supported by this toolchain"

echo ""
echo "[OK] Linux NuttX build environment ready for PicoCalc RP2350B."
echo ""
echo "Next steps:"
echo "  cd $(pwd)"
echo "  make setup       # fetch/sync NuttX trees"
echo "  make configure   # one-time configure"
echo "  make build       # build nuttx.uf2"