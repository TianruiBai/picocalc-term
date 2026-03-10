#!/bin/bash
cd /home/polar/picocalc-term

FILES=(
  pcterm/src/config.c
  pcterm/src/package_manager.c
  pcterm/src/app_state.c
  pcterm/src/boot_splash.c
  pcterm/src/statusbar.c
  pcterm/src/launcher.c
  pcterm/src/hostname.c
  pcterm/src/app_framework.c
  pcterm/src/link_stubs.c
  pcterm/src/lv_port_indev.c
  pcterm/src/terminal_widget.c
  pcterm/src/lv_port_disp.c
)

for f in "${FILES[@]}"; do
  if [ -f "$f" ]; then
    sed -i \
      -e 's/"CONFIG: /"config: /g' \
      -e 's/"PKG: /"pkg: /g' \
      -e 's/"APPSTATE: /"appstate: /g' \
      -e 's/"SPLASH: /"splash: /g' \
      -e 's/"STATUSBAR: /"statusbar: /g' \
      -e 's/"LAUNCHER: /"launcher: /g' \
      -e 's/"HOSTNAME: /"hostname: /g' \
      -e 's/"APP: /"app: /g' \
      -e 's/"STUB: /"stub: /g' \
      -e 's/"INDEV: /"indev: /g' \
      -e 's/"TERM: /"term: /g' \
      -e 's/"DISP: /"disp: /g' \
      "$f"
    echo "Updated: $f"
  else
    echo "Not found: $f"
  fi
done

echo "DONE"
