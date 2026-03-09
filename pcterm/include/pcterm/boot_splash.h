/****************************************************************************
 * pcterm/include/pcterm/boot_splash.h
 *
 * Boot splash screen with startup animation.
 * Shows the PicoCalc-Term logo, version, and animated progress bar.
 *
 ****************************************************************************/

#ifndef __PCTERM_BOOT_SPLASH_H
#define __PCTERM_BOOT_SPLASH_H

#include <lvgl/lvgl.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SPLASH_PROGRESS_MAX  100

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create and show the boot splash screen.
 * Displays logo, version text, and an empty progress bar.
 *
 * @param parent  LVGL screen object (lv_scr_act())
 */
void boot_splash_show(lv_obj_t *parent);

/**
 * Update the progress bar value (0..100).
 * Call lv_timer_handler() after this to render the update.
 *
 * @param percent  Progress percentage (0-100)
 */
void boot_splash_set_progress(int percent);

/**
 * Set the status text below the progress bar.
 *
 * @param text  Short status message (e.g. "Loading settings...")
 */
void boot_splash_set_status(const char *text);

/**
 * Remove all splash screen objects.
 * Call before showing the launcher.
 */
void boot_splash_hide(void);

#ifdef __cplusplus
}
#endif

#endif /* __PCTERM_BOOT_SPLASH_H */
