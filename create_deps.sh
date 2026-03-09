#!/bin/bash
############################################################################
# create_deps.sh (DEPRECATED)
#
# This script was used during MSYS2/Windows builds to create empty
# Make.dep stubs. It is no longer needed under WSL/Linux builds where
# dependency generation works natively.
#
# If you see "multiple target patterns" errors, run instead:
#   make clean-deps
# or:
#   find nuttx/ nuttx-apps/ -name 'Make.dep' -delete
############################################################################
echo "NOTE: This script is deprecated. Use 'make clean-deps' instead."
echo "      Dependency generation now works natively under WSL/Linux builds."

echo "Dependency stubs created"
echo "NuttX .depend count: $(find . -name '.depend' | wc -l)"
echo "Apps .depend count: $(find /c/Users/weyst/Documents/picocalc-term/nuttx-apps -name '.depend' | wc -l)"
