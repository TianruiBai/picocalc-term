/****************************************************************************
 * pcterm/include/pcterm/app.h
 *
 * PicoCalc-Term App Framework API
 *
 * Every application running on PicoCalc-Term interacts with the OS
 * through this API. Provides lifecycle management, LVGL screen access,
 * PSRAM allocation, state save/restore, and system queries.
 *
 ****************************************************************************/

#ifndef __PCTERM_APP_H
#define __PCTERM_APP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <lvgl/lvgl.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* App flags */

#define PC_APP_FLAG_BUILTIN     (1 << 0)  /* System app, cannot be removed */
#define PC_APP_FLAG_BACKGROUND  (1 << 1)  /* Can run background service (e.g. audio) */
#define PC_APP_FLAG_NETWORK     (1 << 2)  /* Requires Wi-Fi */
#define PC_APP_FLAG_STATEFUL    (1 << 3)  /* Supports save/restore */

/* Error codes */

#define PC_OK               0
#define PC_ERR_NOMEM        (-1)
#define PC_ERR_IO           (-2)
#define PC_ERR_NOENT        (-3)
#define PC_ERR_INVAL        (-4)
#define PC_ERR_BUSY         (-5)
#define PC_ERR_PERM         (-6)
#define PC_ERR_NET          (-7)
#define PC_ERR_TOOBIG       (-8)
#define PC_ERR_GENERIC      (-9)

/* Event types */

#define PC_EVENT_WIFI_CONNECTED     1
#define PC_EVENT_WIFI_DISCONNECTED  2
#define PC_EVENT_BATTERY_LOW        3
#define PC_EVENT_BATTERY_CHARGING   4
#define PC_EVENT_SD_REMOVED         5
#define PC_EVENT_SD_INSERTED        6

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Application metadata */

typedef struct pc_app_info_s
{
  const char *name;           /* Internal ID: "pcedit", "pcaudio", etc. */
  const char *display_name;   /* Human-readable: "Text Editor" */
  const char *version;        /* Semantic version: "1.0.0" */
  const char *category;       /* "system", "office", "entertainment", etc. */
  uint32_t    min_ram;        /* Minimum PSRAM bytes required */
  uint32_t    flags;          /* PC_APP_FLAG_* bitfield */
} pc_app_info_t;

/* App lifecycle callbacks */

typedef int  (*pc_app_main_fn)(int argc, char *argv[]);
typedef int  (*pc_app_save_fn)(void *buf, size_t *len);
typedef int  (*pc_app_restore_fn)(const void *buf, size_t len);

/* Complete app registration structure */

typedef struct pc_app_s
{
  pc_app_info_t      info;
  pc_app_main_fn     main;      /* App entry point */
  pc_app_save_fn     save;      /* State serializer (NULL if not stateful) */
  pc_app_restore_fn  restore;   /* State deserializer (NULL if not stateful) */
} pc_app_t;

/* Event callback */

typedef void (*pc_event_cb_t)(int event_type, void *data);

/* Audio status */

typedef struct pc_audio_status_s
{
  bool     playing;
  bool     paused;
  uint32_t position_ms;
  uint32_t duration_ms;
  uint8_t  volume;
  char     title[64];
  char     artist[64];
} pc_audio_status_t;

/* System information */

typedef struct pc_system_info_s
{
  char     hostname[32];
  bool     wifi_connected;
  char     wifi_ssid[33];
  int8_t   wifi_rssi;
  uint8_t  battery_percent;
  bool     battery_charging;
  uint32_t uptime_sec;
  uint32_t psram_free;
  uint32_t sram_free;
} pc_system_info_t;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/* --- Framework (internal, used by launcher/main) --- */

/**
 * Initialize the app framework. Registers all built-in apps.
 */
int app_framework_init(void);

/**
 * Launch an app by name. Hides launcher, runs app main, restores launcher.
 */
int app_framework_launch(const char *name);

/**
 * Get the number of registered apps (built-in + third-party).
 */
int pc_app_get_count(void);

/**
 * Get app info by index.
 * @param index  0-based index (0..pc_app_get_count()-1)
 * @return Pointer to app info, or NULL if out of range
 */
const pc_app_info_t *pc_app_get_info(int index);

/* --- Lifecycle --- */

/**
 * Terminate the app cleanly. State is DISCARDED.
 * Equivalent to Ctrl+Q.
 */
void pc_app_exit(int code);

/**
 * Yield back to the launcher. State is SAVED via the app's save callback.
 * Equivalent to Fn+Home.
 */
void pc_app_yield(void);

/* --- Screen Access --- */

/**
 * Get the app's LVGL screen container (320×300 usable area below status bar).
 * The returned object is the app's root — add all widgets to this container.
 */
lv_obj_t *pc_app_get_screen(void);

/* --- PSRAM Memory --- */

/**
 * Allocate memory from the PSRAM heap.
 * Use for large buffers: editor content, audio/video data, SSH buffers.
 */
void *pc_app_psram_alloc(size_t size);
void *pc_app_psram_realloc(void *ptr, size_t size);
void  pc_app_psram_free(void *ptr);
size_t pc_app_psram_available(void);

/* --- System Queries --- */

/**
 * Get the device hostname (from /mnt/sd/etc/hostname).
 */
const char *pc_app_get_hostname(void);

/**
 * Get comprehensive system information snapshot.
 */
int pc_app_get_system_info(pc_system_info_t *info);

/* --- Audio Service --- */

/**
 * Audio playback controls.
 * These communicate with the background audio service running on Core 1.
 */
int  pc_audio_play(const char *filepath);
int  pc_audio_pause(void);
int  pc_audio_resume(void);
int  pc_audio_stop(void);
int  pc_audio_next(void);
int  pc_audio_prev(void);
int  pc_audio_set_volume(uint8_t volume);
int  pc_audio_get_status(pc_audio_status_t *status);

/* --- Events --- */

/**
 * Register a callback for system events (Wi-Fi, battery, SD card).
 * Only one callback per app — calling again replaces the previous one.
 */
int pc_app_register_event_cb(pc_event_cb_t callback);

#ifdef __cplusplus
}
#endif

#endif /* __PCTERM_APP_H */
