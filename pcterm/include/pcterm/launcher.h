/****************************************************************************
 * pcterm/include/pcterm/launcher.h
 *
 * Home screen launcher interface.
 * Displays the app grid, handles app selection and launch.
 *
 ****************************************************************************/

#ifndef __PCTERM_LAUNCHER_H
#define __PCTERM_LAUNCHER_H

#include <lvgl/lvgl.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Launcher grid configuration */

#define LAUNCHER_COLS        4      /* Apps per row */
#define LAUNCHER_ICON_SIZE   48     /* Icon pixel size (48×48) */
#define LAUNCHER_ICON_PAD    12     /* Padding between icons */
#define LAUNCHER_LABEL_H     16     /* Height for app name label */

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize and display the launcher screen.
 * Creates the app grid with icons for all registered apps.
 *
 * @param parent  LVGL parent container (the app area below status bar)
 * @return 0 on success
 */
int launcher_init(lv_obj_t *parent);

/**
 * Refresh the launcher grid.
 * Called after installing/uninstalling packages, or on return from an app.
 */
void launcher_refresh(void);

/**
 * Show the launcher (return from an app).
 */
void launcher_show(void);

/**
 * Hide the launcher (entering an app).
 */
void launcher_hide(void);

/**
 * Handle keyboard navigation on the launcher grid.
 * Arrow keys move selection, Enter launches the selected app.
 *
 * @param key  LVGL key code
 * @return true if the key was consumed
 */
bool launcher_handle_key(uint32_t key);

/**
 * Check for a deferred app launch request.
 * Returns the app name, or NULL if none pending.
 */
const char *launcher_get_pending_launch(void);

/**
 * Clear the pending launch request after processing.
 */
void launcher_clear_pending_launch(void);

/**
 * Set launcher arrangement mode.
 * 0 = built-in registration order (default)
 * 1 = alphabetical by display name
 */
void launcher_set_arrange_mode(int mode);

/**
 * Restore launcher selection to an app by internal name.
 */
void launcher_set_selected_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* __PCTERM_LAUNCHER_H */
