/****************************************************************************
 * apps/settings/settings_system.c
 *
 * System settings tab: hostname, timezone, about, reboot.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <lvgl/lvgl.h>
#include <syslog.h>
#include <nuttx/board.h>
#include <sys/boardctl.h>
#include "pcterm/config.h"
#include "pcterm/hostname.h"

static void hostname_cb(lv_event_t *e)
{
  lv_obj_t *ta = lv_event_get_target_obj(e);
  const char *text = lv_textarea_get_text(ta);
  if (text && text[0])
    {
      hostname_set(text);
      strncpy(pc_config_get()->hostname, text, 31);
      pc_config_save(pc_config_get());
      syslog(LOG_INFO, "SETTINGS: Hostname set to %s\n", text);
    }
}

static void reboot_cb(lv_event_t *e)
{
  (void)e;
  syslog(LOG_WARNING, "SETTINGS: Reboot requested\n");
  boardctl(BOARDIOC_RESET, 0);
}

void settings_system_create(lv_obj_t *parent)
{
  /* Hostname */

  lv_obj_t *lbl_host = lv_label_create(parent);
  lv_label_set_text(lbl_host, "Hostname");
  lv_obj_align(lbl_host, LV_ALIGN_TOP_LEFT, 8, 8);

  lv_obj_t *ta_host = lv_textarea_create(parent);
  lv_textarea_set_one_line(ta_host, true);
  lv_textarea_set_max_length(ta_host, 31);
  lv_textarea_set_text(ta_host, hostname_get());
  lv_obj_set_width(ta_host, 180);
  lv_obj_align(ta_host, LV_ALIGN_TOP_LEFT, 8, 32);
  lv_obj_add_event_cb(ta_host, hostname_cb, LV_EVENT_DEFOCUSED, NULL);

  /* Timezone */

  lv_obj_t *lbl_tz = lv_label_create(parent);
  lv_label_set_text(lbl_tz, "Timezone");
  lv_obj_align(lbl_tz, LV_ALIGN_TOP_LEFT, 8, 72);

  lv_obj_t *dd_tz = lv_dropdown_create(parent);
  lv_dropdown_set_options(dd_tz,
    "UTC\nEST (UTC-5)\nCST (UTC-6)\nMST (UTC-7)\nPST (UTC-8)\n"
    "CET (UTC+1)\nJST (UTC+9)\nAEST (UTC+10)");
  lv_obj_set_width(dd_tz, 180);
  lv_obj_align(dd_tz, LV_ALIGN_TOP_LEFT, 8, 96);

  /* About section */

  lv_obj_t *lbl_about = lv_label_create(parent);
  lv_label_set_text(lbl_about,
    "eUX OS v0.1.0\n"
    "NuttX RTOS + LVGL\n"
    "RP2350B + CYW43439");
  lv_obj_align(lbl_about, LV_ALIGN_TOP_LEFT, 8, 148);

  /* Reboot button */

  lv_obj_t *btn_reboot = lv_button_create(parent);
  lv_obj_set_size(btn_reboot, 100, 32);
  lv_obj_align(btn_reboot, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
  lv_obj_set_style_bg_color(btn_reboot, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_add_event_cb(btn_reboot, reboot_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lbl_reboot = lv_label_create(btn_reboot);
  lv_label_set_text(lbl_reboot, "Reboot");
  lv_obj_center(lbl_reboot);

  syslog(LOG_DEBUG, "SETTINGS: System tab created\n");
}
