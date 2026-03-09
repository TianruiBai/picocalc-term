/****************************************************************************
 * apps/settings/settings_audio.c
 *
 * Audio settings tab: volume, notification sounds.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <lvgl/lvgl.h>
#include <syslog.h>
#include "pcterm/config.h"

extern void pc_audio_set_volume(int vol);

static void volume_cb(lv_event_t *e)
{
  lv_obj_t *slider = lv_event_get_target_obj(e);
  int val = (int)lv_slider_get_value(slider);
  pc_audio_set_volume(val);
  pc_config_get()->volume = val;
  pc_config_save(pc_config_get());
}

static void keyclick_cb(lv_event_t *e)
{
  lv_obj_t *sw = lv_event_get_target_obj(e);
  bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
  pc_config_get()->key_click = on ? 1 : 0;
  pc_config_save(pc_config_get());
}

void settings_audio_create(lv_obj_t *parent)
{
  /* Volume slider */

  lv_obj_t *lbl_vol = lv_label_create(parent);
  lv_label_set_text(lbl_vol, LV_SYMBOL_AUDIO " Volume");
  lv_obj_align(lbl_vol, LV_ALIGN_TOP_LEFT, 8, 16);

  lv_obj_t *slider = lv_slider_create(parent);
  lv_obj_set_width(slider, 200);
  lv_slider_set_range(slider, 0, 100);
  lv_slider_set_value(slider, pc_config_get()->volume, LV_ANIM_OFF);
  lv_obj_align(slider, LV_ALIGN_TOP_LEFT, 8, 40);
  lv_obj_add_event_cb(slider, volume_cb, LV_EVENT_VALUE_CHANGED, NULL);

  /* Key click toggle */

  lv_obj_t *lbl_click = lv_label_create(parent);
  lv_label_set_text(lbl_click, "Key Click Sound");
  lv_obj_align(lbl_click, LV_ALIGN_TOP_LEFT, 8, 80);

  lv_obj_t *sw = lv_switch_create(parent);
  lv_obj_align(sw, LV_ALIGN_TOP_LEFT, 200, 78);
  if (pc_config_get()->key_click)
    {
      lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
  lv_obj_add_event_cb(sw, keyclick_cb, LV_EVENT_VALUE_CHANGED, NULL);

  /* Audio output */

  lv_obj_t *lbl_out = lv_label_create(parent);
  lv_label_set_text(lbl_out, "Output");
  lv_obj_align(lbl_out, LV_ALIGN_TOP_LEFT, 8, 120);

  lv_obj_t *dd = lv_dropdown_create(parent);
  lv_dropdown_set_options(dd, "Speaker\nHeadphone\nAuto");
  lv_obj_set_width(dd, 150);
  lv_obj_align(dd, LV_ALIGN_TOP_LEFT, 8, 144);

  syslog(LOG_DEBUG, "SETTINGS: Audio tab created\n");
}
