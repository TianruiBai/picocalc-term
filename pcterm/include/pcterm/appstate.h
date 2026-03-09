/****************************************************************************
 * pcterm/include/pcterm/appstate.h
 *
 * App state save/restore API for suspend/resume functionality.
 *
 * State lifecycle:
 *   Fn+Home  → pc_appstate_save()  → PSRAM + /mnt/sd/etc/appstate/<name>
 *   Relaunch → pc_appstate_restore() → app's restore callback
 *   Ctrl+Q   → pc_appstate_discard() → state removed
 *
 ****************************************************************************/

#ifndef __PCTERM_APPSTATE_H
#define __PCTERM_APPSTATE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define APPSTATE_DIR          "/mnt/sd/etc/appstate"
#define APPSTATE_MAX_SIZE     (256 * 1024)  /* 256 KB max state per app */

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Save the current app's state.
 * Calls the app's save callback, stores result in PSRAM cache + SD card.
 *
 * @param app_name  App identifier (e.g. "pcedit")
 * @param save_fn   App's serialization callback
 * @return PC_OK on success, negative error on failure
 */
int pc_appstate_save(const char *app_name,
                     int (*save_fn)(void *buf, size_t *len));

/**
 * Restore a previously saved app state.
 * Reads from PSRAM cache (fast) or SD card (fallback).
 *
 * @param app_name   App identifier
 * @param restore_fn App's deserialization callback
 * @return PC_OK on success, PC_ERR_NOENT if no saved state
 */
int pc_appstate_restore(const char *app_name,
                        int (*restore_fn)(const void *buf, size_t len));

/**
 * Discard saved state for an app.
 * Called on clean exit (Ctrl+Q) or explicit clear.
 *
 * @param app_name  App identifier
 */
void pc_appstate_discard(const char *app_name);

/**
 * Check if saved state exists for an app.
 *
 * @param app_name  App identifier
 * @return true if state exists, false otherwise
 */
bool pc_appstate_exists(const char *app_name);

/**
 * Get the size of saved state (for pre-allocation).
 *
 * @param app_name  App identifier
 * @return Size in bytes, or 0 if no state exists
 */
size_t pc_appstate_size(const char *app_name);

#ifdef __cplusplus
}
#endif

#endif /* __PCTERM_APPSTATE_H */
