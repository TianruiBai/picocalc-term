/****************************************************************************
 * apps/settings/settings_storage.c
 *
 * Storage settings tab: SD card info, free space, format option.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <sys/statfs.h>
#include <sys/mount.h>
#include <lvgl/lvgl.h>
#include <syslog.h>

static void eject_cb(lv_event_t *e)
{
  int ret = umount("/mnt/sd");
  lv_obj_t *lbl = (lv_obj_t *)lv_event_get_user_data(e);
  if (ret == 0)
    {
      lv_label_set_text(lbl, "SD card safely ejected");
      syslog(LOG_INFO, "SETTINGS: SD card ejected\n");
    }
  else
    {
      lv_label_set_text(lbl, "Eject failed (files in use?)");
      syslog(LOG_ERR, "SETTINGS: SD eject failed\n");
    }
}

void settings_storage_create(lv_obj_t *parent)
{
  /* SD card info */

  lv_obj_t *lbl_title = lv_label_create(parent);
  lv_label_set_text(lbl_title, LV_SYMBOL_SD_CARD " SD Card");
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 8, 8);

  /* Get SD card stats */

  struct statfs sfs;
  char info_buf[128];

  if (statfs("/mnt/sd", &sfs) == 0)
    {
      unsigned long total_mb = (sfs.f_blocks * sfs.f_bsize) / (1024 * 1024);
      unsigned long free_mb  = (sfs.f_bfree * sfs.f_bsize) / (1024 * 1024);
      unsigned long used_mb  = total_mb - free_mb;

      snprintf(info_buf, sizeof(info_buf),
               "Total: %lu MB\nUsed:  %lu MB\nFree:  %lu MB",
               total_mb, used_mb, free_mb);
    }
  else
    {
      snprintf(info_buf, sizeof(info_buf), "SD card not mounted");
    }

  lv_obj_t *lbl_info = lv_label_create(parent);
  lv_label_set_text(lbl_info, info_buf);
  lv_obj_align(lbl_info, LV_ALIGN_TOP_LEFT, 8, 36);

  /* Usage bar */

  lv_obj_t *bar = lv_bar_create(parent);
  lv_obj_set_size(bar, 260, 16);
  lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 8, 100);
  lv_bar_set_range(bar, 0, 100);

  if (statfs("/mnt/sd", &sfs) == 0 && sfs.f_blocks > 0)
    {
      int pct = (int)(((sfs.f_blocks - sfs.f_bfree) * 100) / sfs.f_blocks);
      lv_bar_set_value(bar, pct, LV_ANIM_OFF);
    }

  /* Eject button */

  lv_obj_t *btn_eject = lv_button_create(parent);
  lv_obj_set_size(btn_eject, 120, 32);
  lv_obj_align(btn_eject, LV_ALIGN_BOTTOM_LEFT, 8, -8);
  lv_obj_add_event_cb(btn_eject, eject_cb, LV_EVENT_CLICKED, lbl_info);

  lv_obj_t *lbl_eject = lv_label_create(btn_eject);
  lv_label_set_text(lbl_eject, "Safe Eject");
  lv_obj_center(lbl_eject);

  syslog(LOG_DEBUG, "SETTINGS: Storage tab created\n");
}
