/****************************************************************************
 * pcterm/src/boot_splash.c
 *
 * Boot splash screen with animated progress bar.
 * Displays the PicoCalc-Term logo centred on screen with a progress
 * indicator that advances as boot steps complete.
 *
 * Layout (320x320 screen):
 *
 *    (vertically centred)
 *       PicoCalc-Term        <- title (Montserrat 20, white)
 *          v0.1.0            <- version (Montserrat 12, grey)
 *       ╔══════════╗         <- progress bar 200x12 (cyan fill)
 *       Loading...           <- status text (Montserrat 12, grey)
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <lvgl/lvgl.h>

#include "pcterm/boot_splash.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SPLASH_BAR_W         200
#define SPLASH_BAR_H         12
#define SPLASH_BG_COLOR      0x000000   /* Black */
#define SPLASH_TITLE_COLOR   0xFFFFFF   /* White */
#define SPLASH_VERSION_COLOR 0x888888   /* Grey */
#define SPLASH_BAR_BG_COLOR  0x333333   /* Dark grey track */
#define SPLASH_BAR_FG_COLOR  0x00CCCC   /* Cyan indicator */
#define SPLASH_STATUS_COLOR  0x888888   /* Grey */

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_obj_t *g_splash_cont  = NULL;   /* Full-screen container */
static lv_obj_t *g_lbl_title    = NULL;
static lv_obj_t *g_lbl_version  = NULL;
static lv_obj_t *g_bar_progress = NULL;
static lv_obj_t *g_lbl_status   = NULL;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: boot_splash_show
 ****************************************************************************/

void boot_splash_show(lv_obj_t *parent)
{
  /* Full-screen black container */

  g_splash_cont = lv_obj_create(parent);
  lv_obj_set_size(g_splash_cont, 320, 320);
  lv_obj_set_pos(g_splash_cont, 0, 0);
  lv_obj_set_style_bg_color(g_splash_cont,
                            lv_color_hex(SPLASH_BG_COLOR), 0);
  lv_obj_set_style_bg_opa(g_splash_cont, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_splash_cont, 0, 0);
  lv_obj_set_style_pad_all(g_splash_cont, 0, 0);
  lv_obj_set_style_radius(g_splash_cont, 0, 0);

  /* Use flex layout for vertical centering */

  lv_obj_set_flex_flow(g_splash_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(g_splash_cont,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  /* Title: eUX OS */

  g_lbl_title = lv_label_create(g_splash_cont);
  lv_label_set_text(g_lbl_title, "eUX OS");
  lv_obj_set_style_text_color(g_lbl_title,
                              lv_color_hex(SPLASH_TITLE_COLOR), 0);
  lv_obj_set_style_text_font(g_lbl_title,
                             &lv_font_montserrat_16, 0);

  /* Version */

  g_lbl_version = lv_label_create(g_splash_cont);
  lv_label_set_text(g_lbl_version, "v0.1.0");
  lv_obj_set_style_text_color(g_lbl_version,
                              lv_color_hex(SPLASH_VERSION_COLOR), 0);
  lv_obj_set_style_text_font(g_lbl_version,
                             &lv_font_montserrat_12, 0);
  lv_obj_set_style_pad_top(g_lbl_version, 4, 0);

  /* Progress bar */

  g_bar_progress = lv_bar_create(g_splash_cont);
  lv_obj_set_size(g_bar_progress, SPLASH_BAR_W, SPLASH_BAR_H);
  lv_bar_set_range(g_bar_progress, 0, SPLASH_PROGRESS_MAX);
  lv_bar_set_value(g_bar_progress, 0, LV_ANIM_OFF);
  lv_obj_set_style_pad_top(g_bar_progress, 20, 0);

  /* Bar track (background) */

  lv_obj_set_style_bg_color(g_bar_progress,
                            lv_color_hex(SPLASH_BAR_BG_COLOR), 0);
  lv_obj_set_style_bg_opa(g_bar_progress, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(g_bar_progress, 4, 0);

  /* Bar indicator (fill) */

  lv_obj_set_style_bg_color(g_bar_progress,
                            lv_color_hex(SPLASH_BAR_FG_COLOR),
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(g_bar_progress, LV_OPA_COVER,
                          LV_PART_INDICATOR);
  lv_obj_set_style_radius(g_bar_progress, 4, LV_PART_INDICATOR);

  /* Status text */

  g_lbl_status = lv_label_create(g_splash_cont);
  lv_label_set_text(g_lbl_status, "Booting...");
  lv_obj_set_style_text_color(g_lbl_status,
                              lv_color_hex(SPLASH_STATUS_COLOR), 0);
  lv_obj_set_style_text_font(g_lbl_status,
                             &lv_font_montserrat_12, 0);
  lv_obj_set_style_pad_top(g_lbl_status, 8, 0);

  syslog(LOG_INFO, "splash: Boot splash displayed\n");
}

/****************************************************************************
 * Name: boot_splash_set_progress
 ****************************************************************************/

void boot_splash_set_progress(int percent)
{
  if (g_bar_progress != NULL)
    {
      if (percent < 0)
        {
          percent = 0;
        }
      else if (percent > SPLASH_PROGRESS_MAX)
        {
          percent = SPLASH_PROGRESS_MAX;
        }

      lv_bar_set_value(g_bar_progress, percent, LV_ANIM_ON);
    }
}

/****************************************************************************
 * Name: boot_splash_set_status
 ****************************************************************************/

void boot_splash_set_status(const char *text)
{
  if (g_lbl_status != NULL && text != NULL)
    {
      lv_label_set_text(g_lbl_status, text);
    }
}

/****************************************************************************
 * Name: boot_splash_hide
 ****************************************************************************/

void boot_splash_hide(void)
{
  if (g_splash_cont != NULL)
    {
      lv_obj_delete(g_splash_cont);
      g_splash_cont  = NULL;
      g_lbl_title    = NULL;
      g_lbl_version  = NULL;
      g_bar_progress = NULL;
      g_lbl_status   = NULL;

      syslog(LOG_INFO, "splash: Boot splash removed\n");
    }
}
