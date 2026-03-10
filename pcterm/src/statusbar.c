/****************************************************************************
 * pcterm/src/statusbar.c
 *
 * Status bar widget implementation.
 * Persistent top bar (320×20) showing hostname, Wi-Fi, battery, clock,
 * and audio status.  Uses flex layout to prevent text overlap.
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
static bool     g_initialized = false;
static uint8_t  g_battery_style = BAT_STYLE_ICON;  /* Default battery display */
static uint8_t  g_bar_position = STATUSBAR_POS_TOP; /* Default position */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: statusbar_format_battery
 *
 * Description:
 *   Format battery as icon + optional text depending on style setting.
 *   Uses LVGL's built-in LV_SYMBOL_BATTERY_* symbols for icon.
 *
 ****************************************************************************/

static void statusbar_format_battery(char *buf, size_t len,
                                     uint8_t percent, bool charging,
                                     uint8_t style)
{
  const char *icon;

  if (percent > 90)
    {
      icon = LV_SYMBOL_BATTERY_FULL;
    }
  else if (percent > 60)
    {
      icon = LV_SYMBOL_BATTERY_3;
    }
  else if (percent > 35)
    {
      icon = LV_SYMBOL_BATTERY_2;
    }
  else if (percent > 10)
    {
      icon = LV_SYMBOL_BATTERY_1;
    }
  else
    {
      icon = LV_SYMBOL_BATTERY_EMPTY;
    }

  switch (style)
    {
      case BAT_STYLE_ICON_ONLY:
        if (charging)
          {
            snprintf(buf, len, "%s" LV_SYMBOL_CHARGE, icon);
          }
        else
          {
            snprintf(buf, len, "%s", icon);
          }
        break;

      case BAT_STYLE_TEXT:
        {
          /* Legacy text-bar style: ▰▰▰▱▱ */

          int bars = (percent + 19) / 20;
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
                  buf[pos++] = (char)0xE2;
                  buf[pos++] = (char)0x96;
                  buf[pos++] = (char)0xB0;
                }
              else
                {
                  buf[pos++] = (char)0xE2;
                  buf[pos++] = (char)0x96;
                  buf[pos++] = (char)0xB1;
                }
            }

          buf[pos] = '\0';
        }
        break;

      case BAT_STYLE_PERCENT:
        if (charging)
          {
            snprintf(buf, len, LV_SYMBOL_CHARGE "%u%%",
                     (unsigned)percent);
          }
        else
          {
            snprintf(buf, len, "%u%%", (unsigned)percent);
          }
        break;

      default:  /* BAT_STYLE_ICON — icon + percent */
        if (charging)
          {
            snprintf(buf, len, "%s" LV_SYMBOL_CHARGE "%u%%",
                     icon, (unsigned)percent);
          }
        else
          {
            snprintf(buf, len, "%s%u%%", icon, (unsigned)percent);
          }
        break;
    }
}

/****************************************************************************
 * Name: statusbar_format_wifi
 *
 * Description:
 *   Format Wi-Fi indicator based on RSSI.
 *
 ****************************************************************************/

static const char *statusbar_format_wifi(bool connected, int8_t rssi,
                                         const char *wifi_text)
{
  static char buf[32];

  if (wifi_text != NULL && wifi_text[0] != '\0')
    {
      snprintf(buf, sizeof(buf), "%s", wifi_text);
      return buf;
    }

  if (!connected)
    {
      return LV_SYMBOL_CLOSE " OFF";
    }

  snprintf(buf, sizeof(buf), "%s %ddBm", LV_SYMBOL_WIFI, (int)rssi);
  return buf;
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
  /* --- Status bar container: 320×20, flex row layout --- */

  g_statusbar = lv_obj_create(parent);
  lv_obj_set_size(g_statusbar, 320, STATUSBAR_HEIGHT);

  /* Position: top or bottom based on config */

  if (g_bar_position == STATUSBAR_POS_BOTTOM)
    {
      lv_obj_align(g_statusbar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
  else
    {
      lv_obj_align(g_statusbar, LV_ALIGN_TOP_LEFT, 0, 0);
    }

  lv_obj_set_style_bg_color(g_statusbar,
                            lv_color_hex(STATUSBAR_BG_COLOR), 0);
  lv_obj_set_style_bg_opa(g_statusbar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_statusbar, 0, 0);
  lv_obj_set_style_radius(g_statusbar, 0, 0);
  lv_obj_clear_flag(g_statusbar, LV_OBJ_FLAG_SCROLLABLE);

  /* Use flex row layout with space-between for auto-spacing */

  lv_obj_set_layout(g_statusbar, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(g_statusbar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g_statusbar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_left(g_statusbar, 4, 0);
  lv_obj_set_style_pad_right(g_statusbar, 4, 0);
  lv_obj_set_style_pad_top(g_statusbar, 0, 0);
  lv_obj_set_style_pad_bottom(g_statusbar, 0, 0);
  lv_obj_set_style_pad_column(g_statusbar, 4, 0);

  /* Shared text style: small white text */

  static lv_style_t style_label;
  lv_style_init(&style_label);
  lv_style_set_text_color(&style_label,
                          lv_color_hex(STATUSBAR_FG_COLOR));
  lv_style_set_text_font(&style_label, &lv_font_montserrat_12);

  /* Hostname label — first item (left) */

  g_lbl_hostname = lv_label_create(g_statusbar);
  lv_obj_add_style(g_lbl_hostname, &style_label, 0);
  lv_label_set_text(g_lbl_hostname, hostname_get());

  /* Wi-Fi indicator */

  g_lbl_wifi = lv_label_create(g_statusbar);
  lv_obj_add_style(g_lbl_wifi, &style_label, 0);
  lv_label_set_text(g_lbl_wifi, LV_SYMBOL_CLOSE);

  /* Audio indicator */

  g_lbl_audio = lv_label_create(g_statusbar);
  lv_obj_add_style(g_lbl_audio, &style_label, 0);
  lv_label_set_text(g_lbl_audio, "");  /* Hidden by default */
  lv_obj_set_width(g_lbl_audio, LV_SIZE_CONTENT);

  /* Battery indicator — with icon */

  g_lbl_battery = lv_label_create(g_statusbar);
  lv_obj_add_style(g_lbl_battery, &style_label, 0);
  lv_label_set_text(g_lbl_battery, LV_SYMBOL_BATTERY_FULL);

  /* Clock — right-most item */

  g_lbl_clock = lv_label_create(g_statusbar);
  lv_obj_add_style(g_lbl_clock, &style_label, 0);
  lv_label_set_text(g_lbl_clock, "--:--");

  /* --- App content area: 320×300 adjacent to status bar --- */

  g_app_area = lv_obj_create(parent);
  lv_obj_set_size(g_app_area, 320, 320 - STATUSBAR_HEIGHT);

  if (g_bar_position == STATUSBAR_POS_BOTTOM)
    {
      lv_obj_align(g_app_area, LV_ALIGN_TOP_LEFT, 0, 0);
    }
  else
    {
      lv_obj_align(g_app_area, LV_ALIGN_TOP_LEFT, 0, STATUSBAR_HEIGHT);
    }

  lv_obj_set_style_bg_color(g_app_area, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_app_area, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_app_area, 0, 0);
  lv_obj_set_style_pad_all(g_app_area, 0, 0);
  lv_obj_set_style_radius(g_app_area, 0, 0);
  lv_obj_clear_flag(g_app_area, LV_OBJ_FLAG_SCROLLABLE);

  memset(&g_last_state, 0, sizeof(g_last_state));
  g_initialized = true;

  syslog(LOG_INFO, "statusbar: Initialized (320×%d)\n", STATUSBAR_HEIGHT);
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
                      state->wifi_rssi,
                      state->wifi_text));
    }

  /* Update audio indicator */

  if (state->audio_playing != g_last_state.audio_playing)
    {
      lv_label_set_text(g_lbl_audio,
                        state->audio_playing ? LV_SYMBOL_AUDIO : "");
    }

  /* Update battery */

  if (state->battery_percent != g_last_state.battery_percent ||
      state->battery_charging != g_last_state.battery_charging ||
      state->battery_style != g_last_state.battery_style)
    {
      char bat_buf[48];
      statusbar_format_battery(bat_buf, sizeof(bat_buf),
                               state->battery_percent,
                               state->battery_charging,
                               state->battery_style);
      lv_label_set_text(g_lbl_battery, bat_buf);
    }

  /* Update clock — compact format HH:MM for status bar */

  if (state->hour != g_last_state.hour ||
      state->minute != g_last_state.minute)
    {
      char time_buf[8];
      snprintf(time_buf, sizeof(time_buf), "%02u:%02u",
               (unsigned)state->hour,
               (unsigned)state->minute);
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

/****************************************************************************
 * Name: statusbar_set_battery_style
 *
 * Description:
 *   Change the battery display style at runtime.
 *
 ****************************************************************************/

void statusbar_set_battery_style(uint8_t style)
{
  if (style <= BAT_STYLE_PERCENT)
    {
      g_battery_style = style;
    }
}

/****************************************************************************
 * Name: statusbar_set_position
 *
 * Description:
 *   Change the status bar position. If already initialized, re-layouts
 *   the bar and app area immediately.
 *
 ****************************************************************************/

void statusbar_set_position(uint8_t position)
{
  if (position > STATUSBAR_POS_BOTTOM)
    {
      return;
    }

  g_bar_position = position;

  if (!g_initialized || g_statusbar == NULL || g_app_area == NULL)
    {
      return;
    }

  /* Re-layout status bar */

  if (position == STATUSBAR_POS_BOTTOM)
    {
      lv_obj_align(g_statusbar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
      lv_obj_align(g_app_area, LV_ALIGN_TOP_LEFT, 0, 0);
    }
  else
    {
      lv_obj_align(g_statusbar, LV_ALIGN_TOP_LEFT, 0, 0);
      lv_obj_align(g_app_area, LV_ALIGN_TOP_LEFT, 0, STATUSBAR_HEIGHT);
    }

  lv_obj_invalidate(lv_scr_act());
}

/****************************************************************************
 * Name: statusbar_get_position
 *
 * Description:
 *   Return the current status bar position.
 *
 ****************************************************************************/

uint8_t statusbar_get_position(void)
{
  return g_bar_position;
}