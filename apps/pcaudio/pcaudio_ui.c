/****************************************************************************
 * apps/pcaudio/pcaudio_ui.c
 *
 * LVGL player interface: progress bar, album art placeholder, controls.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <lvgl/lvgl.h>
#include <syslog.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_obj_t *g_lbl_title   = NULL;
static lv_obj_t *g_lbl_artist  = NULL;
static lv_obj_t *g_bar_prog    = NULL;
static lv_obj_t *g_lbl_time    = NULL;
static lv_obj_t *g_lbl_dur     = NULL;
static lv_obj_t *g_lbl_status  = NULL;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void pcaudio_ui_create(lv_obj_t *parent)
{
  /* Album art placeholder */

  lv_obj_t *art = lv_obj_create(parent);
  lv_obj_set_size(art, 120, 120);
  lv_obj_align(art, LV_ALIGN_TOP_MID, 0, 16);
  lv_obj_set_style_bg_color(art, lv_color_hex(0x1A1A2E), 0);
  lv_obj_set_style_radius(art, 8, 0);

  lv_obj_t *art_icon = lv_label_create(art);
  lv_label_set_text(art_icon, LV_SYMBOL_AUDIO);
  lv_obj_set_style_text_font(art_icon, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(art_icon, lv_color_hex(0x666666), 0);
  lv_obj_center(art_icon);

  /* Title */

  g_lbl_title = lv_label_create(parent);
  lv_label_set_text(g_lbl_title, "No file loaded");
  lv_obj_set_style_text_font(g_lbl_title, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(g_lbl_title, lv_color_white(), 0);
  lv_obj_set_width(g_lbl_title, 280);
  lv_label_set_long_mode(g_lbl_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align(g_lbl_title, LV_ALIGN_TOP_MID, 0, 148);

  /* Artist */

  g_lbl_artist = lv_label_create(parent);
  lv_label_set_text(g_lbl_artist, "");
  lv_obj_set_style_text_color(g_lbl_artist, lv_color_hex(0xAAAAAA), 0);
  lv_obj_align(g_lbl_artist, LV_ALIGN_TOP_MID, 0, 168);

  /* Progress bar */

  g_bar_prog = lv_bar_create(parent);
  lv_obj_set_size(g_bar_prog, 260, 8);
  lv_obj_align(g_bar_prog, LV_ALIGN_TOP_MID, 0, 200);
  lv_bar_set_range(g_bar_prog, 0, 1000);

  /* Time labels */

  g_lbl_time = lv_label_create(parent);
  lv_label_set_text(g_lbl_time, "0:00");
  lv_obj_set_style_text_color(g_lbl_time, lv_color_hex(0xAAAAAA), 0);
  lv_obj_align(g_lbl_time, LV_ALIGN_TOP_LEFT, 22, 214);

  g_lbl_dur = lv_label_create(parent);
  lv_label_set_text(g_lbl_dur, "0:00");
  lv_obj_set_style_text_color(g_lbl_dur, lv_color_hex(0xAAAAAA), 0);
  lv_obj_align(g_lbl_dur, LV_ALIGN_TOP_RIGHT, -22, 214);

  /* Control hints */

  g_lbl_status = lv_label_create(parent);
  lv_label_set_text(g_lbl_status,
    "Space: Play/Pause  "
    LV_SYMBOL_LEFT "/" LV_SYMBOL_RIGHT ": Seek  "
    "+/-: Volume");
  lv_obj_set_style_text_color(g_lbl_status, lv_color_hex(0x666666), 0);
  lv_obj_set_style_text_font(g_lbl_status, &lv_font_montserrat_12, 0);
  lv_obj_align(g_lbl_status, LV_ALIGN_BOTTOM_MID, 0, -8);
}

void pcaudio_ui_set_title(const char *title)
{
  if (g_lbl_title) lv_label_set_text(g_lbl_title, title);
}

void pcaudio_ui_set_artist(const char *artist)
{
  if (g_lbl_artist) lv_label_set_text(g_lbl_artist, artist);
}

void pcaudio_ui_set_progress(int current_ms, int total_ms)
{
  if (g_bar_prog && total_ms > 0)
    {
      lv_bar_set_value(g_bar_prog,
                       (int)((int64_t)current_ms * 1000 / total_ms),
                       LV_ANIM_OFF);
    }

  if (g_lbl_time)
    {
      char buf[8];
      snprintf(buf, sizeof(buf), "%d:%02d",
               current_ms / 60000, (current_ms / 1000) % 60);
      lv_label_set_text(g_lbl_time, buf);
    }

  if (g_lbl_dur)
    {
      char buf[8];
      snprintf(buf, sizeof(buf), "%d:%02d",
               total_ms / 60000, (total_ms / 1000) % 60);
      lv_label_set_text(g_lbl_dur, buf);
    }
}
