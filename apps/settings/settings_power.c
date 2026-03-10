/****************************************************************************
 * apps/settings/settings_power.c
 *
 * Battery & Power settings tab:
 *   - Battery display style (icon, icon-only, text bars, percent)
 *   - Power profile (standard 150 MHz, high-perf 200 MHz,
 *     power-save 100 MHz) — core clock only, peripherals unchanged
 *   - Backlight timeout
 *   - Sleep timer
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <syslog.h>
#include <lvgl/lvgl.h>
#include "pcterm/config.h"
#include "pcterm/statusbar.h"

/****************************************************************************
 * External References
 ****************************************************************************/

extern void rp23xx_set_power_profile(int profile);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bat_style_cb(lv_event_t *e)
{
  lv_obj_t *dd = lv_event_get_target_obj(e);
  uint16_t sel = lv_dropdown_get_selected(dd);

  if (sel <= BAT_STYLE_PERCENT)
    {
      pc_config_get()->battery_style = sel;
      statusbar_set_battery_style(sel);
      pc_config_save(pc_config_get());
    }
}

static void power_profile_cb(lv_event_t *e)
{
  lv_obj_t *dd = lv_event_get_target_obj(e);
  uint16_t sel = lv_dropdown_get_selected(dd);

  if (sel <= 2)
    {
      pc_config_get()->power_profile = sel;
      rp23xx_set_power_profile(sel);
      pc_config_save(pc_config_get());
    }
}

static const uint16_t bkl_timeout_values[] = { 0, 30, 60, 120, 300, 600 };

static void bkl_timeout_cb(lv_event_t *e)
{
  lv_obj_t *dd = lv_event_get_target_obj(e);
  uint16_t sel = lv_dropdown_get_selected(dd);

  if (sel < sizeof(bkl_timeout_values) / sizeof(bkl_timeout_values[0]))
    {
      pc_config_get()->backlight_timeout = bkl_timeout_values[sel];
      pc_config_save(pc_config_get());
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void settings_power_create(lv_obj_t *parent)
{
  int y = 8;

  /* --- Battery Display Style --- */

  lv_obj_t *lbl_bat = lv_label_create(parent);
  lv_label_set_text(lbl_bat, LV_SYMBOL_BATTERY_FULL " Battery Display");
  lv_obj_align(lbl_bat, LV_ALIGN_TOP_LEFT, 8, y);
  y += 24;

  lv_obj_t *dd_bat = lv_dropdown_create(parent);
  lv_dropdown_set_options(dd_bat,
    "Icon + Percent\nIcon Only\nText Bars\nPercent Only");
  lv_obj_set_width(dd_bat, 180);
  lv_obj_align(dd_bat, LV_ALIGN_TOP_LEFT, 8, y);
  lv_dropdown_set_selected(dd_bat, pc_config_get()->battery_style);
  lv_obj_add_event_cb(dd_bat, bat_style_cb, LV_EVENT_VALUE_CHANGED, NULL);
  y += 44;

  /* --- Power Profile (core clock speed) --- */

  lv_obj_t *lbl_pwr = lv_label_create(parent);
  lv_label_set_text(lbl_pwr, LV_SYMBOL_CHARGE " Power Profile");
  lv_obj_align(lbl_pwr, LV_ALIGN_TOP_LEFT, 8, y);
  y += 24;

  lv_obj_t *dd_pwr = lv_dropdown_create(parent);
  lv_dropdown_set_options(dd_pwr,
    "Standard (150 MHz)\nHigh Performance (200 MHz)\nPower Save (100 MHz)");
  lv_obj_set_width(dd_pwr, 210);
  lv_obj_align(dd_pwr, LV_ALIGN_TOP_LEFT, 8, y);
  lv_dropdown_set_selected(dd_pwr, pc_config_get()->power_profile);
  lv_obj_add_event_cb(dd_pwr, power_profile_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);
  y += 44;

  /* --- Backlight Timeout --- */

  lv_obj_t *lbl_bkl = lv_label_create(parent);
  lv_label_set_text(lbl_bkl, "Backlight Timeout");
  lv_obj_align(lbl_bkl, LV_ALIGN_TOP_LEFT, 8, y);
  y += 24;

  lv_obj_t *dd_bkl = lv_dropdown_create(parent);
  lv_dropdown_set_options(dd_bkl,
    "Never\n30 sec\n1 min\n2 min\n5 min\n10 min");
  lv_obj_set_width(dd_bkl, 150);
  lv_obj_align(dd_bkl, LV_ALIGN_TOP_LEFT, 8, y);

  /* Find which dropdown index matches current setting */

  uint16_t saved = pc_config_get()->backlight_timeout;
  for (int i = 0;
       i < (int)(sizeof(bkl_timeout_values) /
                 sizeof(bkl_timeout_values[0])); i++)
    {
      if (bkl_timeout_values[i] == saved)
        {
          lv_dropdown_set_selected(dd_bkl, i);
          break;
        }
    }

  lv_obj_add_event_cb(dd_bkl, bkl_timeout_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);

  syslog(LOG_DEBUG, "SETTINGS: Battery & Power tab created\n");
}
