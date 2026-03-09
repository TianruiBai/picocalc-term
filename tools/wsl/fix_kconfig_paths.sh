#!/bin/bash
# Fix stale Windows paths in auto-generated Kconfig files
set -e

OLD_PATH="/c/Users/weyst/Documents/picocalc-term"
NEW_PATH="/home/polar/picocalc-term"

echo "Finding Kconfig files with stale Windows paths..."
files=$(grep -rl "$OLD_PATH" /home/polar/picocalc-term/nuttx-apps/ --include="Kconfig" 2>/dev/null || true)

if [ -z "$files" ]; then
    echo "No stale paths found."
    exit 0
fi

for f in $files; do
    echo "  Fixing: $f"
    sed -i "s|$OLD_PATH|$NEW_PATH|g" "$f"
done

# Also check nuttx/ directory
files2=$(grep -rl "$OLD_PATH" /home/polar/picocalc-term/nuttx/ --include="Kconfig" 2>/dev/null || true)
for f in $files2; do
    echo "  Fixing: $f"
    sed -i "s|$OLD_PATH|$NEW_PATH|g" "$f"
done

echo "Done. Verifying no stale paths remain..."
remaining=$(grep -rl "$OLD_PATH" /home/polar/picocalc-term/nuttx-apps/ --include="Kconfig" 2>/dev/null | wc -l)
echo "Remaining files with stale paths: $remaining"
