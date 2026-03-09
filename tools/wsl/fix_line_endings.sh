#!/usr/bin/env bash
############################################################################
# tools/wsl/fix_line_endings.sh
#
# Convert CRLF → LF for build scripts, makefiles, kconfig, and defconfig
# files. Safe to run multiple times. Run after editing from Windows tools.
############################################################################
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

if command -v git >/dev/null 2>&1 && git -C "${ROOT_DIR}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  git -C "${ROOT_DIR}" ls-files -z \
    '*.sh' '*.mk' 'Makefile' 'Kconfig' '*.cmake' 'defconfig' '.config' | while IFS= read -r -d '' file; do
      sed -i 's/\r$//' "${ROOT_DIR}/${file}"
    done
else
  find "${ROOT_DIR}" \
    -type f \
    \( -name "*.sh" -o -name "*.mk" -o -name "Makefile" -o -name "Kconfig" \
       -o -name "*.cmake" -o -name "defconfig" -o -name ".config" \) \
    -print0 | while IFS= read -r -d '' file; do
      sed -i 's/\r$//' "$file"
    done
fi

echo "[OK] Converted line endings to LF for build scripts/config files."