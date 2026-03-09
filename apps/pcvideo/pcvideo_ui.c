/****************************************************************************
 * apps/pcvideo/pcvideo_ui.c
 *
 * LVGL controls overlay for video playback.
 * Semi-transparent overlay with playback controls, shown on keypress.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <lvgl/lvgl.h>
#include <syslog.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_obj_t *g_overlay     = NULL;
static lv_obj_t *g_lbl_title   = NULL;
static lv_obj_t *g_bar_prog    = NULL;
static lv_obj_t *g_lbl_time    = NULL;
static lv_obj_t *g_lbl_status  = NULL;
static bool       g_visible    = false;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void pcvideo_ui_create(lv_obj_t *parent)
{
  /* Semi-transparent overlay at the bottom */

  g_overlay = lv_obj_create(parent);
  lv_obj_set_size(g_overlay, 320, 60);
  lv_obj_align(g_overlay, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(g_overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_overlay, LV_OPA_70, 0);
  lv_obj_set_style_border_width(g_overlay, 0, 0);
  lv_obj_set_style_radius(g_overlay, 0, 0);
  lv_obj_clear_flag(g_overlay, LV_OBJ_FLAG_SCROLLABLE);

  /* Title */

  g_lbl_title = lv_label_create(g_overlay);
  lv_label_set_text(g_lbl_title, "");
  lv_obj_set_style_text_color(g_lbl_title, lv_color_white(), 0);
  lv_obj_align(g_lbl_title, LV_ALIGN_TOP_LEFT, 8, 2);

  /* Progress bar */

  g_bar_prog = lv_bar_create(g_overlay);
  lv_obj_set_size(g_bar_prog, 260, 6);
  lv_obj_align(g_bar_prog, LV_ALIGN_TOP_MID, 0, 22);
  lv_bar_set_range(g_bar_prog, 0, 1000);

  /* Time label */

  g_lbl_time = lv_label_create(g_overlay);
  lv_label_set_text(g_lbl_time, "0:00 / 0:00");
  lv_obj_set_style_text_color(g_lbl_time, lv_color_hex(0xAAAAAA), 0);
  lv_obj_set_style_text_font(g_lbl_time, &lv_font_montserrat_12, 0);
  lv_obj_align(g_lbl_time, LV_ALIGN_TOP_MID, 0, 32);

  /* Controls hint */

  g_lbl_status = lv_label_create(g_overlay);
  lv_label_set_text(g_lbl_status,
    "Space: Pause  " LV_SYMBOL_LEFT "/" LV_SYMBOL_RIGHT ": Seek  Q: Exit");
  lv_obj_set_style_text_color(g_lbl_status, lv_color_hex(0x666666), 0);
  lv_obj_set_style_text_font(g_lbl_status, &lv_font_montserrat_12, 0);
  lv_obj_align(g_lbl_status, LV_ALIGN_BOTTOM_MID, 0, -2);

  /* Start hidden */

  lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
  g_visible = false;
}

void pcvideo_ui_show(void)
{
  if (g_overlay)
    {
      lv_obj_clear_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
      g_visible = true;
    }
}

void pcvideo_ui_hide(void)
{
  if (g_overlay)
    {
      lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
      g_visible = false;
    }
}

bool pcvideo_ui_visible(void)
{
  return g_visible;
}

void pcvideo_ui_set_title(const char *title)
{
  if (g_lbl_title) lv_label_set_text(g_lbl_title, title);
}

void pcvideo_ui_update(uint32_t current_frame, uint32_t total_frames,
                       uint8_t fps)
{
  if (total_frames > 0 && g_bar_prog)
    {
      lv_bar_set_value(g_bar_prog,
                       (int)((uint64_t)current_frame * 1000 / total_frames),
                       LV_ANIM_OFF);
    }

  if (g_lbl_time && fps > 0)
    {
      int cur_sec = current_frame / fps;
      int tot_sec = total_frames / fps;
      char buf[24];
      snprintf(buf, sizeof(buf), "%d:%02d / %d:%02d",
               cur_sec / 60, cur_sec % 60,
               tot_sec / 60, tot_sec % 60);
      lv_label_set_text(g_lbl_time, buf);
    }
}
