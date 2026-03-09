/****************************************************************************
 * apps/settings/settings_keyboard.c
 *
 * Keyboard settings tab: repeat rate, key mapping.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <lvgl/lvgl.h>
#include <syslog.h>
#include "pcterm/config.h"

static void repeat_cb(lv_event_t *e)
{
  lv_obj_t *slider = lv_event_get_target_obj(e);
  int val = (int)lv_slider_get_value(slider);
  lv_obj_t *lbl = (lv_obj_t *)lv_event_get_user_data(e);
  char buf[16];
  snprintf(buf, sizeof(buf), "%d ms", val);
  lv_label_set_text(lbl, buf);
  pc_config_get()->key_repeat_rate = val;
  pc_config_save(pc_config_get());
}

static const uint16_t delay_values[] = { 200, 300, 500, 750 };

static void delay_cb(lv_event_t *e)
{
  lv_obj_t *dd = lv_event_get_target_obj(e);
  uint16_t sel = lv_dropdown_get_selected(dd);
  if (sel < sizeof(delay_values) / sizeof(delay_values[0]))
    {
      pc_config_get()->key_repeat_delay = delay_values[sel];
      pc_config_save(pc_config_get());
    }
}

void settings_keyboard_create(lv_obj_t *parent)
{
  lv_obj_t *lbl_repeat = lv_label_create(parent);
  lv_label_set_text(lbl_repeat, "Key Repeat Rate");
  lv_obj_align(lbl_repeat, LV_ALIGN_TOP_LEFT, 8, 16);

  lv_obj_t *slider = lv_slider_create(parent);
  lv_obj_set_width(slider, 200);
  lv_slider_set_range(slider, 100, 500);
  lv_slider_set_value(slider, pc_config_get()->key_repeat_rate > 0
                              ? pc_config_get()->key_repeat_rate : 250,
                      LV_ANIM_OFF);
  lv_obj_align(slider, LV_ALIGN_TOP_LEFT, 8, 40);

  lv_obj_t *lbl_ms = lv_label_create(parent);
  lv_label_set_text(lbl_ms, "250 ms");
  lv_obj_align_to(lbl_ms, slider, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
  lv_obj_add_event_cb(slider, repeat_cb, LV_EVENT_VALUE_CHANGED, lbl_ms);

  /* Key repeat delay */

  lv_obj_t *lbl_delay = lv_label_create(parent);
  lv_label_set_text(lbl_delay, "Initial Delay");
  lv_obj_align(lbl_delay, LV_ALIGN_TOP_LEFT, 8, 80);

  lv_obj_t *dd = lv_dropdown_create(parent);
  lv_dropdown_set_options(dd, "200 ms\n300 ms\n500 ms\n750 ms");
  lv_obj_set_width(dd, 150);
  lv_obj_align(dd, LV_ALIGN_TOP_LEFT, 8, 104);
  lv_obj_add_event_cb(dd, delay_cb, LV_EVENT_VALUE_CHANGED, NULL);

  syslog(LOG_DEBUG, "SETTINGS: Keyboard tab created\n");
}
