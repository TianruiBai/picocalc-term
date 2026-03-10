/****************************************************************************
 * apps/settings/settings_display.c
 *
 * Display settings tab: brightness, sleep timeout, status bar position.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <syslog.h>
#include <lvgl/lvgl.h>
#include "pcterm/config.h"
#include "pcterm/statusbar.h"

extern void rp23xx_lcd_set_brightness(int pct);

static void brightness_cb(lv_event_t *e)
{
  lv_obj_t *slider = lv_event_get_target_obj(e);
  int val = (int)lv_slider_get_value(slider);
  rp23xx_lcd_set_brightness(val);
  pc_config_get()->brightness = val;
  pc_config_save(pc_config_get());
}

static const uint16_t sleep_values[] = { 0, 60, 120, 300, 600 };

static void sleep_cb(lv_event_t *e)
{
  lv_obj_t *dd = lv_event_get_target_obj(e);
  uint16_t sel = lv_dropdown_get_selected(dd);
  if (sel < sizeof(sleep_values) / sizeof(sleep_values[0]))
    {
      pc_config_get()->sleep_timeout = sleep_values[sel];
      pc_config_save(pc_config_get());
    }
}

static void statusbar_pos_cb(lv_event_t *e)
{
  lv_obj_t *dd = lv_event_get_target_obj(e);
  uint16_t sel = lv_dropdown_get_selected(dd);

  pc_config_get()->statusbar_position = (uint8_t)sel;
  statusbar_set_position((uint8_t)sel);
  pc_config_save(pc_config_get());
}

void settings_display_create(lv_obj_t *parent)
{
  /* Brightness slider */

  lv_obj_t *lbl_bright = lv_label_create(parent);
  lv_label_set_text(lbl_bright, "Brightness");
  lv_obj_align(lbl_bright, LV_ALIGN_TOP_LEFT, 8, 16);

  lv_obj_t *slider_bright = lv_slider_create(parent);
  lv_obj_set_width(slider_bright, 200);
  lv_slider_set_range(slider_bright, 10, 100);
  lv_slider_set_value(slider_bright, pc_config_get()->brightness, LV_ANIM_OFF);
  lv_obj_align(slider_bright, LV_ALIGN_TOP_LEFT, 8, 40);
  lv_obj_add_event_cb(slider_bright, brightness_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);

  /* Sleep timeout dropdown */

  lv_obj_t *lbl_sleep = lv_label_create(parent);
  lv_label_set_text(lbl_sleep, "Screen Sleep");
  lv_obj_align(lbl_sleep, LV_ALIGN_TOP_LEFT, 8, 80);

  lv_obj_t *dd_sleep = lv_dropdown_create(parent);
  lv_dropdown_set_options(dd_sleep, "Never\n1 min\n2 min\n5 min\n10 min");
  lv_obj_align(dd_sleep, LV_ALIGN_TOP_LEFT, 8, 104);
  lv_obj_set_width(dd_sleep, 150);
  lv_obj_add_event_cb(dd_sleep, sleep_cb, LV_EVENT_VALUE_CHANGED, NULL);

  /* Status bar position dropdown */

  lv_obj_t *lbl_sbpos = lv_label_create(parent);
  lv_label_set_text(lbl_sbpos, "Status Bar Position");
  lv_obj_align(lbl_sbpos, LV_ALIGN_TOP_LEFT, 8, 148);

  lv_obj_t *dd_sbpos = lv_dropdown_create(parent);
  lv_dropdown_set_options(dd_sbpos, "Top\nBottom");
  lv_dropdown_set_selected(dd_sbpos, pc_config_get()->statusbar_position);
  lv_obj_align(dd_sbpos, LV_ALIGN_TOP_LEFT, 8, 172);
  lv_obj_set_width(dd_sbpos, 150);
  lv_obj_add_event_cb(dd_sbpos, statusbar_pos_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);

  syslog(LOG_DEBUG, "SETTINGS: Display tab created\n");
}
