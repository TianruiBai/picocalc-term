/****************************************************************************
 * pcterm/src/statusbar.c
 *
 * Status bar widget implementation.
 * Persistent top bar (320×20) showing hostname, Wi-Fi, battery, clock,
 * and audio status.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <lvgl/lvgl.h>

#include "pcterm/statusbar.h"
#include "pcterm/hostname.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_obj_t *g_statusbar    = NULL;   /* Status bar container */
static lv_obj_t *g_app_area     = NULL;   /* App content area (320×300) */

/* Status bar label objects */

static lv_obj_t *g_lbl_hostname = NULL;
static lv_obj_t *g_lbl_wifi     = NULL;
static lv_obj_t *g_lbl_audio    = NULL;
static lv_obj_t *g_lbl_battery  = NULL;
static lv_obj_t *g_lbl_clock    = NULL;

/* Cached state for diff-based updates */

static statusbar_state_t g_last_state;
static bool g_initialized = false;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: statusbar_format_battery
 *
 * Description:
 *   Format battery level as a visual bar: ▰▰▰▱▱
 *
 ****************************************************************************/

static void statusbar_format_battery(char *buf, size_t len,
                                     uint8_t percent, bool charging)
{
  int bars = (percent + 19) / 20;  /* 0-5 bars */

  if (bars > 5) bars = 5;

  int pos = 0;

  if (charging)
    {
      pos += snprintf(buf + pos, len - pos, LV_SYMBOL_CHARGE);
    }

  for (int i = 0; i < 5 && pos < (int)len - 4; i++)
    {
      if (i < bars)
        {
          /* Filled bar (UTF-8: ▰ = E2 96 B0) */
          buf[pos++] = (char)0xE2;
          buf[pos++] = (char)0x96;
          buf[pos++] = (char)0xB0;
        }
      else
        {
          /* Empty bar (UTF-8: ▱ = E2 96 B1) */
          buf[pos++] = (char)0xE2;
          buf[pos++] = (char)0x96;
          buf[pos++] = (char)0xB1;
        }
    }

  buf[pos] = '\0';
}

/****************************************************************************
 * Name: statusbar_format_wifi
 *
 * Description:
 *   Format Wi-Fi indicator based on RSSI.
 *
 ****************************************************************************/

static const char *statusbar_format_wifi(bool connected, int8_t rssi)
{
  if (!connected)
    {
      return LV_SYMBOL_CLOSE;   /* No connection */
    }

  if (rssi > -50)
    {
      return LV_SYMBOL_WIFI;    /* Strong */
    }
  else if (rssi > -70)
    {
      return LV_SYMBOL_WIFI;    /* Medium (same icon, could tint) */
    }
  else
    {
      return LV_SYMBOL_WIFI;    /* Weak */
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: statusbar_init
 *
 * Description:
 *   Create the status bar (top 20px) and app content area (remaining 300px).
 *
 ****************************************************************************/

int statusbar_init(lv_obj_t *parent)
{
  /* --- Status bar container: 320×20 at top --- */

  g_statusbar = lv_obj_create(parent);
  lv_obj_set_size(g_statusbar, 320, STATUSBAR_HEIGHT);
  lv_obj_align(g_statusbar, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(g_statusbar,
                            lv_color_hex(STATUSBAR_BG_COLOR), 0);
  lv_obj_set_style_bg_opa(g_statusbar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_statusbar, 0, 0);
  lv_obj_set_style_pad_all(g_statusbar, 2, 0);
  lv_obj_set_style_radius(g_statusbar, 0, 0);
  lv_obj_clear_flag(g_statusbar, LV_OBJ_FLAG_SCROLLABLE);

  /* Shared text style: small white text */

  static lv_style_t style_label;
  lv_style_init(&style_label);
  lv_style_set_text_color(&style_label,
                          lv_color_hex(STATUSBAR_FG_COLOR));
  lv_style_set_text_font(&style_label, &lv_font_montserrat_12);

  /* Hostname label — left-aligned */

  g_lbl_hostname = lv_label_create(g_statusbar);
  lv_obj_add_style(g_lbl_hostname, &style_label, 0);
  lv_obj_align(g_lbl_hostname, LV_ALIGN_LEFT_MID, 4, 0);
  lv_label_set_text(g_lbl_hostname, hostname_get());

  /* Wi-Fi indicator */

  g_lbl_wifi = lv_label_create(g_statusbar);
  lv_obj_add_style(g_lbl_wifi, &style_label, 0);
  lv_obj_align(g_lbl_wifi, LV_ALIGN_LEFT_MID, 120, 0);
  lv_label_set_text(g_lbl_wifi, LV_SYMBOL_CLOSE);

  /* Audio indicator */

  g_lbl_audio = lv_label_create(g_statusbar);
  lv_obj_add_style(g_lbl_audio, &style_label, 0);
  lv_obj_align(g_lbl_audio, LV_ALIGN_LEFT_MID, 150, 0);
  lv_label_set_text(g_lbl_audio, "");  /* Hidden by default */

  /* Battery indicator */

  g_lbl_battery = lv_label_create(g_statusbar);
  lv_obj_add_style(g_lbl_battery, &style_label, 0);
  lv_obj_align(g_lbl_battery, LV_ALIGN_LEFT_MID, 190, 0);
  lv_label_set_text(g_lbl_battery, "");

  /* Clock — right-aligned */

  g_lbl_clock = lv_label_create(g_statusbar);
  lv_obj_add_style(g_lbl_clock, &style_label, 0);
  lv_obj_align(g_lbl_clock, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_label_set_text(g_lbl_clock, "--:--");

  /* --- App content area: 320×300 below status bar --- */

  g_app_area = lv_obj_create(parent);
  lv_obj_set_size(g_app_area, 320, 320 - STATUSBAR_HEIGHT);
  lv_obj_align(g_app_area, LV_ALIGN_TOP_LEFT, 0, STATUSBAR_HEIGHT);
  lv_obj_set_style_bg_color(g_app_area, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_app_area, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_app_area, 0, 0);
  lv_obj_set_style_pad_all(g_app_area, 0, 0);
  lv_obj_set_style_radius(g_app_area, 0, 0);
  lv_obj_clear_flag(g_app_area, LV_OBJ_FLAG_SCROLLABLE);

  memset(&g_last_state, 0, sizeof(g_last_state));
  g_initialized = true;

  syslog(LOG_INFO, "STATUSBAR: Initialized (320×%d)\n", STATUSBAR_HEIGHT);
  return 0;
}

/****************************************************************************
 * Name: statusbar_update
 *
 * Description:
 *   Update status bar labels. Only redraws changed elements.
 *
 ****************************************************************************/

void statusbar_update(const statusbar_state_t *state)
{
  if (!g_initialized || state == NULL)
    {
      return;
    }

  /* Update hostname if changed */

  if (strcmp(state->hostname, g_last_state.hostname) != 0)
    {
      lv_label_set_text(g_lbl_hostname, state->hostname);
    }

  /* Update Wi-Fi if changed */

  if (state->wifi_connected != g_last_state.wifi_connected ||
      state->wifi_rssi != g_last_state.wifi_rssi)
    {
      lv_label_set_text(g_lbl_wifi,
                        statusbar_format_wifi(state->wifi_connected,
                                              state->wifi_rssi));
    }

  /* Update audio indicator */

  if (state->audio_playing != g_last_state.audio_playing)
    {
      lv_label_set_text(g_lbl_audio,
                        state->audio_playing ? LV_SYMBOL_AUDIO : "");
    }

  /* Update battery */

  if (state->battery_percent != g_last_state.battery_percent ||
      state->battery_charging != g_last_state.battery_charging)
    {
      char bat_buf[32];
      statusbar_format_battery(bat_buf, sizeof(bat_buf),
                               state->battery_percent,
                               state->battery_charging);
      lv_label_set_text(g_lbl_battery, bat_buf);
    }

  /* Update clock */

  if (state->hour != g_last_state.hour ||
      state->minute != g_last_state.minute)
    {
      char time_buf[8];
      snprintf(time_buf, sizeof(time_buf), "%02d:%02d",
               state->hour, state->minute);
      lv_label_set_text(g_lbl_clock, time_buf);
    }

  /* Cache state for next diff */

  memcpy(&g_last_state, state, sizeof(statusbar_state_t));
}

/****************************************************************************
 * Name: statusbar_get_app_area
 *
 * Description:
 *   Return the LVGL container for the app content area (320×300).
 *
 ****************************************************************************/

lv_obj_t *statusbar_get_app_area(void)
{
  return g_app_area;
}

/****************************************************************************
 * Name: statusbar_set_audio_indicator
 *
 * Description:
 *   Show or hide the audio playing indicator.
 *
 ****************************************************************************/

void statusbar_set_audio_indicator(bool playing)
{
  if (g_initialized && g_lbl_audio != NULL)
    {
      lv_label_set_text(g_lbl_audio,
                        playing ? LV_SYMBOL_AUDIO : "");
    }
}
