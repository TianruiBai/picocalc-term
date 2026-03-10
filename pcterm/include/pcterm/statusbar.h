/****************************************************************************
 * pcterm/include/pcterm/statusbar.h
 *
 * Status bar widget — configurable position (top or bottom).
 * Shows: hostname, Wi-Fi indicator, battery level, clock, audio status.
 *
 * Layout (320×20 pixels):
 * ┌──────────────────────────────────────────────────────┐
 * │ picocalc   📶 ♪  🔋85%  12:34 │
 * └──────────────────────────────────────────────────────┘
 *
 ****************************************************************************/

#ifndef __PCTERM_STATUSBAR_H
#define __PCTERM_STATUSBAR_H

#include <stdint.h>
#include <stdbool.h>
#include <lvgl/lvgl.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define STATUSBAR_HEIGHT     20       /* Pixels */
#define STATUSBAR_BG_COLOR   0x000000 /* Black (24-bit for lv_color_hex) */
#define STATUSBAR_FG_COLOR   0xFFFFFF /* White (24-bit for lv_color_hex) */

/* Status bar position */

#define STATUSBAR_POS_TOP    0
#define STATUSBAR_POS_BOTTOM 1

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Battery display styles */

#define BAT_STYLE_ICON       0   /* Icon + percent text */
#define BAT_STYLE_ICON_ONLY  1   /* Icon only (compact) */
#define BAT_STYLE_TEXT       2   /* Text bars ▰▰▰▱▱ */
#define BAT_STYLE_PERCENT    3   /* "85%" text only */

typedef struct statusbar_state_s
{
  char     hostname[32];
  char     wifi_text[24];
  bool     wifi_connected;
  int8_t   wifi_rssi;          /* dBm, -30 to -90 */
  bool     audio_playing;
  uint8_t  battery_percent;
  bool     battery_charging;
  uint8_t  battery_style;      /* BAT_STYLE_* */
  uint16_t year;
  uint8_t  month;
  uint8_t  day;
  uint8_t  hour;
  uint8_t  minute;
} statusbar_state_t;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create and initialize the status bar.
 *
 * @param parent  LVGL screen object (full 320×320 screen)
 * @return 0 on success
 */
int statusbar_init(lv_obj_t *parent);

/**
 * Update the status bar with current system state.
 * Called periodically (e.g. every 1 second) by the main loop.
 *
 * @param state  Pointer to current status bar state
 */
void statusbar_update(const statusbar_state_t *state);

/**
 * Get the LVGL object for the app area (next to the status bar).
 * This is the 320×300 container where apps draw their UI.
 *
 * @return LVGL object for app content area
 */
lv_obj_t *statusbar_get_app_area(void);

/**
 * Show/hide audio indicator in status bar.
 */
void statusbar_set_audio_indicator(bool playing);

/**
 * Set the battery display style (BAT_STYLE_*).
 */
void statusbar_set_battery_style(uint8_t style);

/**
 * Set the status bar position (STATUSBAR_POS_TOP or STATUSBAR_POS_BOTTOM).
 * Takes effect on next statusbar_init() or can re-layout immediately.
 */
void statusbar_set_position(uint8_t position);

/**
 * Get the current status bar position.
 */
uint8_t statusbar_get_position(void);

#ifdef __cplusplus
}
#endif

#endif /* __PCTERM_STATUSBAR_H */
