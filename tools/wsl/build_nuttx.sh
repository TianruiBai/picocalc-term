#!/usr/bin/env bash
############################################################################
# tools/wsl/build_nuttx.sh
#
# Configure and build NuttX for PicoCalc RP2350B under WSL2 Ubuntu.
#
# Environment variables (all optional):
#   BOARD_CONFIG  — NuttX board:config  (default: picocalc-rp2350b:full)
#   JOBS          — parallel jobs       (default: nproc)
#   CLEAN         — set to 1 to force clean before build
#   SKIP_CONFIGURE — set to 1 to skip configure step (incremental rebuild)
#
# Usage:
#   ./tools/wsl/build_nuttx.sh                     # full configure+build
#   JOBS=4 ./tools/wsl/build_nuttx.sh              # limit parallelism
#   SKIP_CONFIGURE=1 ./tools/wsl/build_nuttx.sh    # incremental build
#   CLEAN=1 ./tools/wsl/build_nuttx.sh             # clean + configure + build
############################################################################
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
NUTTX_DIR="${ROOT_DIR}/nuttx"
APPS_DIR="${ROOT_DIR}/nuttx-apps"
BOARD_CONFIG="${BOARD_CONFIG:-picocalc-rp2350b:full}"
JOBS="${JOBS:-$(nproc)}"
CLEAN="${CLEAN:-0}"
SKIP_CONFIGURE="${SKIP_CONFIGURE:-0}"

# -----------------------------------------------------------------------
# Preflight checks
# -----------------------------------------------------------------------
if [[ ! -d "${NUTTX_DIR}" ]]; then
  echo "ERROR: NuttX directory not found: ${NUTTX_DIR}" >&2
  exit 1
fi

if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
  echo "ERROR: arm-none-eabi-gcc not in PATH." >&2
  echo "Run: ./tools/wsl/setup_nuttx_env.sh" >&2
  exit 1
fi

echo "==> PicoCalc NuttX Build"
echo "    Board:     ${BOARD_CONFIG}"
echo "    Jobs:      ${JOBS}"
echo "    Toolchain: $(arm-none-eabi-gcc --version | head -1)"
echo ""

cd "${NUTTX_DIR}"

# -----------------------------------------------------------------------
# Step 0: Clean stale Windows-generated dependency files
#
# Make.dep files from MSYS2/Windows builds contain paths like C:/Users/...
# which cause "multiple target patterns" errors under Linux make.
# Always purge them — they'll be regenerated correctly by the Linux build.
# -----------------------------------------------------------------------
echo "==> Cleaning stale dependency files"
STALE_COUNT=0
for dir in "${NUTTX_DIR}" "${APPS_DIR}"; do
  if [[ -d "${dir}" ]]; then
    while IFS= read -r -d '' f; do
      # Only delete if file contains Windows drive paths (e.g. C:/)
      if grep -q '[A-Z]:/' "$f" 2>/dev/null; then
        rm -f "$f"
        STALE_COUNT=$((STALE_COUNT + 1))
      fi
    done < <(find "${dir}" -type f -name 'Make.dep' -print0 2>/dev/null)
  fi
done
echo "    Removed ${STALE_COUNT} stale Make.dep files with Windows paths"

# -----------------------------------------------------------------------
# Step 1: Optional full clean
# -----------------------------------------------------------------------
if [[ "${CLEAN}" == "1" ]]; then
  echo "==> make distclean"
  make distclean 2>/dev/null || true
  SKIP_CONFIGURE=0  # force re-configure after distclean
fi

# -----------------------------------------------------------------------
# Step 2: Configure (unless skipped)
# -----------------------------------------------------------------------
if [[ "${SKIP_CONFIGURE}" != "1" ]]; then
  echo "==> configure ${BOARD_CONFIG}"
  ./tools/configure.sh -l "${BOARD_CONFIG}"
  echo "==> make olddefconfig"
  make olddefconfig
else
  echo "==> Skipping configure (SKIP_CONFIGURE=1)"
fi

# -----------------------------------------------------------------------
# Step 3: Build
# -----------------------------------------------------------------------
echo "==> make -j${JOBS}"
make -j"${JOBS}"

# -----------------------------------------------------------------------
# Done
# -----------------------------------------------------------------------
echo ""
if [[ -f nuttx.uf2 ]]; then
  echo "==> BUILD SUCCESS"
  ls -lh nuttx.uf2
  echo ""
  echo "Flash: copy nuttx.uf2 to the RP2350 USB mass-storage drive"
else
  echo "==> BUILD FAILED — nuttx.uf2 not generated" >&2
  exit 1
fi