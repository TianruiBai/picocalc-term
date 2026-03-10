/****************************************************************************
 * pcterm/include/pcterm/keys.h
 *
 * Custom key code definitions for PicoCalc-Term.
 *
 * LVGL doesn't define function key constants.  We define custom codes
 * in the 0xF000+ range so they don't collide with ASCII, Unicode,
 * or LVGL's built-in LV_KEY_* constants.
 *
 * Hardware key codes come from the STM32 south-bridge keyboard firmware.
 * Physical keyboard has 5 F-keys; Shift produces F6-F10.
 * F11/F12 do not exist on this hardware.
 *
 ****************************************************************************/

#ifndef __PCTERM_KEYS_H
#define __PCTERM_KEYS_H

/****************************************************************************
 * Custom LVGL Key Codes — Function Keys
 ****************************************************************************/

#define LV_KEY_F1           0xF001
#define LV_KEY_F2           0xF002
#define LV_KEY_F3           0xF003
#define LV_KEY_F4           0xF004
#define LV_KEY_F5           0xF005
#define LV_KEY_F6           0xF006
#define LV_KEY_F7           0xF007
#define LV_KEY_F8           0xF008
#define LV_KEY_F9           0xF009
#define LV_KEY_F10          0xF00A

/****************************************************************************
 * South-Bridge Keyboard Raw Codes
 ****************************************************************************/

#define KBD_RAW_F1          0x81
#define KBD_RAW_F2          0x82
#define KBD_RAW_F3          0x83
#define KBD_RAW_F4          0x84
#define KBD_RAW_F5          0x85
#define KBD_RAW_F6          0x86
#define KBD_RAW_F7          0x87
#define KBD_RAW_F8          0x88
#define KBD_RAW_F9          0x89
#define KBD_RAW_F10         0x90

/****************************************************************************
 * VT100/ANSI Escape Sequences for Function Keys
 *
 * For terminal emulator (pcterm) — maps F-keys to standard escape codes.
 ****************************************************************************/

#define VT100_F1            "\033OP"
#define VT100_F2            "\033OQ"
#define VT100_F3            "\033OR"
#define VT100_F4            "\033OS"
#define VT100_F5            "\033[15~"
#define VT100_F6            "\033[17~"
#define VT100_F7            "\033[18~"
#define VT100_F8            "\033[19~"
#define VT100_F9            "\033[20~"
#define VT100_F10           "\033[21~"

#endif /* __PCTERM_KEYS_H */
