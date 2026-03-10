# eUX OS: Stub Audit & Implementation Locations

This document tracks all stub/placeholder implementations across the codebase for systematic completion.

**Last updated:** 2026-03-10

---

## Legend

| Status | Meaning |
|--------|---------|
| ✅ | Fully implemented |
| **PARTIAL** | Some logic works but key parts are missing |
| **STUB** | Function exists but does nothing useful |
| **MISSING** | Declared in header but no implementation exists |

---

## 1. Core Framework (`pcterm/src/`)

| # | Function | File | Line | Status | Description |
|---|----------|------|------|--------|-------------|
| 1.1 | `pc_app_exit()` | `app_framework.c` | ~232 | ✅ | setjmp/longjmp + state discard |
| 1.2 | `pc_app_yield()` | `app_framework.c` | ~248 | ✅ | Saves state + longjmp return |
| 1.3 | `boot_init_lvgl()` | `main.c` | ~93 | ✅ | lv_init + display/input driver |
| 1.4 | `status_bar_timer_cb()` | `main.c` | ~114 | ✅ | Real WiFi/battery/audio queries |
| 1.5 | `eux_init()` step 6 | `main.c` | ~189 | ✅ | app_framework_init() call |
| 1.6 | `terminal_resize()` | `terminal_widget.c` | ~983 | STUB | Low priority — fixed 320×320 display |
| 1.7 | `terminal_scroll()` | `terminal_widget.c` | ~949 | ✅ | Scrollback row mapping |
| 1.8 | `terminal_render()` bg | `terminal_widget.c` | ~880 | ✅ | Direct pixel bg fill + scrollback |
| 1.9 | `pcpkg_launch()` | `package_manager.c` | ~843 | ✅ | NuttX ELF load_module/exec_module |
| 1.10 | `pcpkg_install()` timestamp | `package_manager.c` | ~631 | ✅ | time() call for install_timestamp |

### Audio API (declared in `pcterm/include/pcterm/app.h`)

| # | Function | File | Status | Description |
|---|----------|------|--------|-------------|
| 1.11 | `pc_audio_play()` | `audio_service.c` | ✅ | Lazy init + decoder open + thread start |
| 1.12 | `pc_audio_pause()` | `audio_service.c` | ✅ | Sets PAUSED state |
| 1.13 | `pc_audio_resume()` | `audio_service.c` | ✅ | Sets PLAYING state |
| 1.14 | `pc_audio_stop()` | `audio_service.c` | ✅ | Thread join + decoder close |
| 1.15 | `pc_audio_next()` | `audio_service.c` | ✅ | Playlist index advance + play |
| 1.16 | `pc_audio_prev()` | `audio_service.c` | ✅ | Playlist index retreat + play |
| 1.17 | `pc_audio_set_volume()` | `audio_service.c` | ✅ | rp23xx_audio_set_volume delegate |
| 1.18 | `pc_audio_get_status()` | `audio_service.c` | ✅ | State/position/duration from decoder |
| 1.19 | `pc_app_register_event_cb()` | `app_framework.c` | ✅ | Event callback registration |
| 1.20 | `pc_app_get_system_info()` | `app_framework.c` | ✅ | System info struct |

---

## 2. Board Support (`boards/arm/rp23xx/picocalc-rp2350b/src/`)

| # | Function | File | Status | Description |
|---|----------|------|--------|-------------|
| 2.1 | `rp23xx_boardearlyinitialize()` | `rp23xx_boot.c` | ✅ | GPIO pin mux for SPI/I2C/UART/PWM |
| 2.2 | `rp23xx_audio_initialize()` | `rp23xx_audio.c` | ✅ | PWM slice 10, ISR, PSRAM ring buffer |
| 2.3 | `rp23xx_psram_init()` | `rp23xx_psram.c` | ✅ | QSPI probe + mm_initialize heap |
| 2.4 | `lcd_updatearea()` | `rp23xx_lcd.c` | ✅ | Dirty-rect partial SPI DMA flush |
| 2.5 | `rp23xx_lcd_initialize()` FB | `rp23xx_lcd.c` | ✅ | PSRAM framebuffer allocation |

---

## 3. Local Terminal (`apps/pcterm/`)

| # | Function | File | Status | Description |
|---|----------|------|--------|-------------|
| 3.1 | `pcterm_local_main()` | `pcterm_main.c` | ✅ | PTY + terminal + NuttShell spawn |
| 3.2 | `.save` / `.restore` | `pcterm_main.c` | ✅ | Scrollback/cursor serialization |
| 3.3 | `pcterm_nsh_start()` | `pcterm_nsh.c` | ✅ | task_create + dup2 for PTY |
| 3.4 | `pcterm_nsh_stop()` | `pcterm_nsh.c` | ✅ | SIGTERM + waitpid |

---

## 4. Text Editor (`apps/pcedit/`)

| # | Function | File | Status | Description |
|---|----------|------|--------|-------------|
| 4.1 | `pcedit_main()` | `pcedit_main.c` | ✅ | Canvas editor + vi state machine |
| 4.2 | `vi_normal_key()` 'd' / dd | `pcedit_main.c` | ✅ | Full delete: x, X, D, dd, cc operators |
| 4.3 | `vi_normal_key()` 'u' / Ctrl-R | `pcedit_main.c` | ✅ | Full undo/redo stack with linear history |
| 4.4 | `vi_normal_key()` 'o'/'O' | `pcedit_main.c` | ✅ | Newline insert + cursor + enter insert mode |
| 4.5 | `vi_normal_key()` 'p'/'P' | `pcedit_main.c` | ✅ | Put/paste from registers (linewise + charwise) |
| 4.6 | `vi_normal_key()` 'Y' / yy | `pcedit_main.c` | ✅ | Yank line into register |
| 4.7 | `vi_normal_key()` 'r' | `pcedit_main.c` | ✅ | Replace single char (two-key sequence) |
| 4.8 | `vi_normal_key()` 'J' | `pcedit_main.c` | ✅ | Join lines with space |
| 4.9 | `vi_normal_key()` 's'/'S'/'C' | `pcedit_main.c` | ✅ | Substitute char/line, change to EOL |
| 4.10 | `vi_normal_key()` '~' | `pcedit_main.c` | ✅ | Toggle case at cursor |
| 4.11 | `vi_normal_key()` 'm'/\'/\` | `pcedit_main.c` | ✅ | Set mark / jump to mark |
| 4.12 | `vi_insert_key()` Ctrl-W | `pcedit_main.c` | ✅ | Delete word backward in insert mode |
| 4.13 | `vi_insert_key()` Ctrl-U | `pcedit_main.c` | ✅ | Delete to line start in insert mode |

---

## 5. Spreadsheet (`apps/pccsv/`)

| # | Function | File | Status | Description |
|---|----------|------|--------|-------------|
| 5.1 | `pccsv_main()` | `pccsv_main.c` | ✅ | RFC 4180 parser + cell editing |
| 5.2 | `pccsv_save()` | `pccsv_main.c` | ✅ | CSV serialization |
| 5.3 | `pccsv_restore()` | `pccsv_main.c` | ✅ | CSV deserialization |
| 5.4 | `pccsv_table_move_selection()` | `pccsv_table.c` | ✅ | Cell selection with LVGL draw event highlight |

---

## 6. Audio Player (`apps/pcaudio/`)

| # | Function | File | Status | Description |
|---|----------|------|--------|-------------|
| 6.1 | `pcaudio_main()` | `pcaudio_main.c` | ✅ | Playlist scan, UI, keyboard, timer |
| 6.2 | `.save` / `.restore` | `pcaudio_main.c` | ✅ | Track + position + playlist state |
| 6.3 | `audio_decoder_read()` MP3 | `pcaudio_decoder.c` | ✅ | minimp3 frame-by-frame decode |
| 6.4 | `audio_decoder_seek()` MP3 | `pcaudio_decoder.c` | ✅ | Frame scan seek |
| 6.5 | `audio_decoder_open()` MP3 | `pcaudio_decoder.c` | ✅ | minimp3_init + file buf + first frame probe |
| 6.6 | `player_thread()` audio write | `pcaudio_player.c` | ✅ | rp23xx_audio_write() |

---

## 7. Video Player (`apps/pcvideo/`)

| # | Function | File | Status | Description |
|---|----------|------|--------|-------------|
| 7.1 | `pcvideo_main()` | `pcvideo_main.c` | ✅ | PCV decode pipeline + LVGL timer + keyboard |
| 7.2 | `.save` / `.restore` | `pcvideo_main.c` | ✅ | Frame position state |

---

## 8. SSH Client (`apps/pcssh/`)

| # | Function | File | Status | Description |
|---|----------|------|--------|-------------|
| 8.1 | `pcssh_main()` | `pcssh_main.c` | ✅ | Connection list + terminal + poll timer |
| 8.2 | `ssh_write_cb()` | `pcssh_main.c` | ✅ | wolfSSH_stream_send via ssh_send() |
| 8.3 | `.save` / `.restore` | `pcssh_main.c` | ✅ | Session state save/restore |
| 8.4 | `ssh_client_init()` | `pcssh_client.c` | ✅ | wolfSSH_Init() (guarded) |
| 8.5 | `ssh_connect()` | `pcssh_client.c` | ✅ | wolfSSH CTX+handshake (guarded) |
| 8.6 | `ssh_send()` | `pcssh_client.c` | ✅ | wolfSSH_stream_send (guarded) |
| 8.7 | `ssh_recv()` | `pcssh_client.c` | ✅ | wolfSSH_stream_read (guarded) |
| 8.8 | `ssh_disconnect()` | `pcssh_client.c` | ✅ | wolfSSH shutdown/free (guarded) |
| 8.9 | `ssh_client_cleanup()` | `pcssh_client.c` | ✅ | wolfSSH_Cleanup() (guarded) |
| 8.10 | `scp_download()` | `pcssh_scp.c` | ✅ | wolfSSH SCP receive (guarded) |
| 8.11 | `scp_upload()` | `pcssh_scp.c` | ✅ | wolfSSH SCP send (guarded) |
| 8.12 | `sftp_list_dir()` | `pcssh_sftp.c` | ✅ | wolfSSH SFTP ReadDir (guarded) |
| 8.13 | `sftp_download()` | `pcssh_sftp.c` | ✅ | wolfSSH SFTP Open+Read (guarded) |
| 8.14 | `sftp_upload()` | `pcssh_sftp.c` | ✅ | wolfSSH SFTP Open+Write (guarded) |

---

## 9. Web Browser (`apps/pcweb/`)

| # | Function | File | Status | Description |
|---|----------|------|--------|-------------|
| 9.1 | `pcweb_main()` | `pcweb_main.c` | ✅ | HTTP+HTML+render pipeline, nav, keyboard |
| 9.2 | `pcweb_restore()` | `pcweb_main.c` | ✅ | Re-fetches URL from saved state |
| 9.3 | `http_get()` HTTPS | `pcweb_http.c` | ✅ | wolfSSL TLS handshake (guarded) |

---

## 10. Settings App (`apps/settings/`)

| # | Function | File | Status | Description |
|---|----------|------|--------|-------------|
| 10.1 | Display event handlers | `settings_display.c` | ✅ | Brightness slider → LCD + config save. Sleep dropdown → config |
| 10.2 | Audio event handlers | `settings_audio.c` | ✅ | Volume slider → pc_audio_set_volume + config. Key click switch |
| 10.3 | Keyboard event handlers | `settings_keyboard.c` | ✅ | Repeat rate slider + delay dropdown → config save |
| 10.4 | WiFi scan/connect | `settings_wifi.c` | ✅ | rp23xx_wifi_scan + rp23xx_wifi_connect + UI list |
| 10.5 | Storage eject | `settings_storage.c` | ✅ | umount("/mnt/sd") + status feedback |
| 10.6 | System hostname/reboot | `settings_system.c` | ✅ | hostname_set + boardctl(BOARDIOC_RESET) |
| 10.7 | Packages install/remove | `settings_packages.c` | ✅ | pcpkg_install scan + pcpkg_uninstall + list refresh |

---

## Summary

| Category | Total | Implemented | Remaining |
|----------|-------|-------------|-----------|
| Core framework | 22 | 21 | 1 (terminal_resize — low priority) |
| Board support | 5 | 5 | 0 |
| Local terminal | 4 | 4 | 0 |
| Text editor | 13 | 13 | 0 |
| Spreadsheet | 4 | 4 | 0 |
| Audio player | 6 | 6 | 0 |
| Video player | 2 | 2 | 0 |
| SSH client | 14 | 14 | 0 |
| Web browser | 3 | 3 | 0 |
| Settings | 7 | 7 | 0 |
| Package manager | 5 | 5 | 0 |
| File explorer | 2 | 2 | 0 |
| **Total** | **~87** | **86** | **1** |

### New Implementations (Session 4)

| Component | Files | Functions |
|-----------|-------|-----------|
| General app loader | `app_framework.c` | `app_framework_refresh_thirdparty()`, enhanced `app_framework_launch()` (fallback to ELF), `pc_app_get_count()` (built-in + third-party), `pc_app_get_info()` (unified index) |
| App store system | `package_manager.c` | `pcpkg_fetch_catalog()`, `pcpkg_load_cached_catalog()`, `pcpkg_download_and_install()`, `pcpkg_scan_and_install_sd()`, `pcpkg_update_available()` |
| Settings packages | `settings_packages.c` | Full rewrite: two-tab UI (Installed + App Store), catalog browse, download buttons |
| File explorer | `pcfiles_main.c` | `pcfiles_main()`, `pcfiles_scan_dir()`, `pcfiles_navigate()`, `pcfiles_enter_selected()`, `pcfiles_show_preview()`, `pcfiles_create_ui()`, 8 button callbacks, keyboard handler |
| File operations | `pcfiles_ops.c` | `pcfiles_copy()`, `pcfiles_move()`, `pcfiles_delete()`, `pcfiles_mkdir()`, `pcfiles_rename()` |

### Remaining Items (non-critical)

1. `terminal_resize()` — Fixed 320×320 display, rarely needed

All vi command stubs and pccsv cell highlight have been fully implemented.

### Crypto Guard Pattern

All wolfSSH (SSH/SCP/SFTP) functions are guarded by `#ifdef CONFIG_CRYPTO_WOLFSSH` with raw TCP fallback.
HTTPS in pcweb_http.c is guarded by `#ifdef CONFIG_CRYPTO_WOLFSSL` with graceful error return.
