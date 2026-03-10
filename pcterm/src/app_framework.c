/****************************************************************************
 * pcterm/src/app_framework.c
 *
 * App lifecycle management, built-in app registry, and ELF loader
 * for third-party apps.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <setjmp.h>
#include <time.h>
#include <malloc.h>
#include <syslog.h>

#include <lvgl/lvgl.h>

#include "pcterm/app.h"
#include "pcterm/appstate.h"
#include "pcterm/launcher.h"
#include "pcterm/statusbar.h"
#include "pcterm/hostname.h"
#include "pcterm/package.h"

/****************************************************************************
 * External References
 ****************************************************************************/

/* Exit request detection (Fn+ESC) from LVGL indev */

extern bool lv_port_indev_exit_requested(void);

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX_BUILTIN_APPS    16
#define MAX_THIRD_PARTY     32

/****************************************************************************
 * External Built-in App Declarations
 ****************************************************************************/

/* These are defined in each app's source file */

extern const pc_app_t g_settings_app;
extern const pc_app_t g_pcedit_app;
extern const pc_app_t g_pccsv_app;
extern const pc_app_t g_pcaudio_app;
extern const pc_app_t g_pcvideo_app;
extern const pc_app_t g_pcterm_local_app;
extern const pc_app_t g_pcterm_serial_app;
extern const pc_app_t g_pcssh_app;
extern const pc_app_t g_pcwireless_app;
extern const pc_app_t g_pcweb_app;
extern const pc_app_t g_pcfiles_app;

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Built-in app registry (compiled into firmware) */

static const pc_app_t *g_builtin_apps[] =
{
  &g_settings_app,
  &g_pcedit_app,
  &g_pccsv_app,
  &g_pcaudio_app,
  &g_pcvideo_app,
  &g_pcterm_local_app,
  &g_pcterm_serial_app,
  &g_pcssh_app,
  &g_pcwireless_app,
  &g_pcweb_app,
  &g_pcfiles_app,
  NULL,
};

/* Third-party app info cache (populated from package registry) */

static pc_app_info_t g_thirdparty_info[MAX_THIRD_PARTY];
static int           g_thirdparty_count = 0;

/* Persistent strings for third-party app names (needed because
 * pc_app_info_t uses const char* pointers, not buffers) */

static char g_tp_names[MAX_THIRD_PARTY][PCPKG_NAME_MAX];
static char g_tp_display[MAX_THIRD_PARTY][PCPKG_NAME_MAX];
static char g_tp_versions[MAX_THIRD_PARTY][PCPKG_VERSION_MAX];
static char g_tp_categories[MAX_THIRD_PARTY][16];

/* Currently running app (NULL = launcher is showing) */

static const pc_app_t *g_current_app = NULL;

/* App screen container (child of the app area below status bar) */

static lv_obj_t *g_app_screen = NULL;
static lv_obj_t *g_app_root   = NULL;

/* setjmp buffer for pc_app_exit() / pc_app_yield() to longjmp back */

static jmp_buf g_app_jmpbuf;
static int  g_app_exit_code = 0;  /* Return code from app exit */
static bool g_app_yielded   = false;  /* True if yield (vs exit) */

/* Event callback */

static pc_event_cb_t g_event_callback = NULL;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: app_framework_init
 *
 * Description:
 *   Initialize the app framework. Registers all built-in apps.
 *
 ****************************************************************************/

/****************************************************************************
 * Name: app_framework_refresh_thirdparty
 *
 * Description:
 *   Scan the package registry and cache third-party app info so the
 *   launcher can enumerate them alongside built-in apps.
 *
 ****************************************************************************/

static void app_framework_refresh_thirdparty(void)
{
  pcpkg_entry_t entries[MAX_THIRD_PARTY];
  int count;

  count = pcpkg_list(entries, MAX_THIRD_PARTY);
  g_thirdparty_count = 0;

  for (int i = 0; i < count && i < MAX_THIRD_PARTY; i++)
    {
      pcpkg_manifest_t manifest;

      if (pcpkg_get_manifest(entries[i].name, &manifest) != PC_OK)
        {
          /* Fallback — use registry entry data */

          strncpy(g_tp_names[i], entries[i].name, PCPKG_NAME_MAX - 1);
          strncpy(g_tp_display[i], entries[i].name, PCPKG_NAME_MAX - 1);
          strncpy(g_tp_versions[i], entries[i].version,
                  PCPKG_VERSION_MAX - 1);
          strncpy(g_tp_categories[i], "other", 15);
          g_thirdparty_info[i].min_ram = 0;
        }
      else
        {
          strncpy(g_tp_names[i], manifest.name, PCPKG_NAME_MAX - 1);
          strncpy(g_tp_display[i], manifest.name, PCPKG_NAME_MAX - 1);

          /* Capitalize first letter for display */

          if (g_tp_display[i][0] >= 'a' && g_tp_display[i][0] <= 'z')
            {
              g_tp_display[i][0] -= 32;
            }

          strncpy(g_tp_versions[i], manifest.version,
                  PCPKG_VERSION_MAX - 1);
          strncpy(g_tp_categories[i], manifest.category, 15);
          g_thirdparty_info[i].min_ram = manifest.min_ram;
        }

      g_thirdparty_info[i].name         = g_tp_names[i];
      g_thirdparty_info[i].display_name = g_tp_display[i];
      g_thirdparty_info[i].version      = g_tp_versions[i];
      g_thirdparty_info[i].category     = g_tp_categories[i];
      g_thirdparty_info[i].flags        = 0;  /* Not BUILTIN */

      g_thirdparty_count++;
    }

  syslog(LOG_INFO, "app: Cached %d third-party apps from registry\n",
         g_thirdparty_count);
}

int app_framework_init(void)
{
  int count = 0;

  for (int i = 0; g_builtin_apps[i] != NULL; i++)
    {
      syslog(LOG_INFO, "app: Registered \"%s\" (%s)\n",
             g_builtin_apps[i]->info.display_name,
             g_builtin_apps[i]->info.name);
      count++;
    }

  syslog(LOG_INFO, "app: %d built-in apps registered\n", count);

  /* Cache installed third-party packages */

  app_framework_refresh_thirdparty();

  return 0;
}

/****************************************************************************
 * Name: app_framework_launch
 *
 * Description:
 *   Launch an app by name. Hides the launcher, creates the app screen,
 *   optionally restores saved state, then calls the app's main function.
 *
 ****************************************************************************/

int app_framework_launch(const char *name)
{
  const pc_app_t *app = NULL;

  if (g_current_app != NULL)
    {
      syslog(LOG_ERR, "app: Cannot launch \"%s\" — \"%s\" is running\n",
             name, g_current_app->info.name);
      return PC_ERR_BUSY;
    }

  /* Find the app by name */

  for (int i = 0; g_builtin_apps[i] != NULL; i++)
    {
      if (strcmp(g_builtin_apps[i]->info.name, name) == 0)
        {
          app = g_builtin_apps[i];
          break;
        }
    }

  if (app == NULL)
    {
      /* Not a built-in app — check if it's an installed third-party package.
       * Third-party apps are loaded as ELF binaries by the package manager.
       */

      syslog(LOG_INFO, "app: \"%s\" not built-in, trying package loader\n",
             name);

      /* Set up the screen container for the third-party app */

      launcher_hide();
      launcher_set_selected_name(name);

      lv_obj_t *app_area = statusbar_get_app_area();
      g_app_screen = lv_obj_create(app_area);
      lv_obj_set_size(g_app_screen, 320, 300);
      lv_obj_set_style_pad_all(g_app_screen, 0, 0);
      lv_obj_set_style_border_width(g_app_screen, 0, 0);
      lv_obj_set_style_bg_color(g_app_screen, lv_color_black(), 0);

      int pkg_ret = pcpkg_launch(name);

      /* Clean up */

      if (g_app_screen != NULL)
        {
          lv_obj_delete(g_app_screen);
          g_app_screen = NULL;
        }

      launcher_show();

      /* Refresh third-party cache in case something changed */

      app_framework_refresh_thirdparty();
      launcher_refresh();

      return pkg_ret;
    }

  /* Check PSRAM availability */

  size_t avail = pc_app_psram_available();
  if (avail < app->info.min_ram)
    {
      syslog(LOG_ERR, "app: Insufficient PSRAM for \"%s\" "
             "(need %lu, have %lu)\n",
             name, (unsigned long)app->info.min_ram,
             (unsigned long)avail);
      return PC_ERR_NOMEM;
    }

  /* Hide launcher (also removes it from the LVGL input group) */

  launcher_hide();
  launcher_set_selected_name(name);

  /* Create app screen container */

  lv_obj_t *app_area = statusbar_get_app_area();
  g_app_screen = lv_obj_create(app_area);
  lv_obj_set_size(g_app_screen, 320, 300);
  lv_obj_set_style_pad_all(g_app_screen, 0, 0);
  lv_obj_set_style_border_width(g_app_screen, 0, 0);
  lv_obj_set_style_bg_color(g_app_screen, lv_color_black(), 0);

  /* Create a dedicated root container for this app instance.
   * Apps should render only inside this object so each launch has
   * fully isolated GUI state.
   */

  g_app_root = lv_obj_create(g_app_screen);
  lv_obj_set_size(g_app_root, 320, 300);
  lv_obj_align(g_app_root, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_pad_all(g_app_root, 0, 0);
  lv_obj_set_style_border_width(g_app_root, 0, 0);
  lv_obj_set_style_radius(g_app_root, 0, 0);
  lv_obj_set_style_bg_color(g_app_root, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_app_root, LV_OPA_COVER, 0);
  lv_obj_clear_flag(g_app_root, LV_OBJ_FLAG_SCROLLABLE);

  /* Add the app root container to the LVGL input group so that
   * any app widgets created as children can receive keyboard events.
   * Apps may override this by focusing their own sub-widget.
   */

  lv_group_t *launch_group = lv_group_get_default();
  if (launch_group != NULL)
    {
      lv_group_add_obj(launch_group, g_app_root);
      lv_group_focus_obj(g_app_root);
      lv_group_set_editing(launch_group, true);
    }

  g_current_app = app;

  syslog(LOG_INFO, "app: Launching \"%s\" v%s\n",
         app->info.display_name, app->info.version);

  /* Check for saved state and restore if available */

  if ((app->info.flags & PC_APP_FLAG_STATEFUL) &&
      app->restore != NULL &&
      pc_appstate_exists(app->info.name))
    {
      syslog(LOG_INFO, "app: Restoring saved state for \"%s\"\n",
             app->info.name);
      pc_appstate_restore(app->info.name, app->restore);
    }

  /* Call app main — this runs within the current task context.
   * The app takes over the LVGL screen until it calls
   * pc_app_exit() or pc_app_yield(), or returns normally.
   *
   * setjmp/longjmp is used so pc_app_exit()/pc_app_yield() can
   * unwind the app's call stack immediately.
   */

  g_app_exit_code = 0;
  g_app_yielded   = false;

  int jmp_ret = setjmp(g_app_jmpbuf);

  if (jmp_ret == 0)
    {
      /* Normal path — call app main */

      int ret = app->main(0, NULL);
      g_app_exit_code = ret;

      /* If the app returned successfully (code 0), it set up an LVGL-based
       * UI and expects the event loop to keep running.  Pump LVGL here
       * until the user presses Fn+ESC to return to the launcher.
       *
       * Apps that manage their own event loops (e.g. pcterm_local) should
       * call pc_app_exit(0) instead of returning, which longjmps past
       * this loop directly to cleanup.
       */

      if (ret == 0)
        {
          syslog(LOG_INFO, "app: \"%s\" returned 0 — entering framework "
                 "event loop (Fn+ESC to exit)\n", app->info.name);

          while (true)
            {
              lv_timer_handler();

              if (lv_port_indev_exit_requested())
                {
                  syslog(LOG_INFO,
                         "app: Fn+ESC exit from framework event loop\n");
                  break;
                }

              usleep(5000);  /* 5 ms = 200 Hz */
            }
        }
    }
  else
    {
      /* Returned via longjmp from pc_app_exit() or pc_app_yield() */

      syslog(LOG_INFO, "app: \"%s\" %s (code %d)\n",
             app->info.name,
             g_app_yielded ? "yielded" : "exited",
             g_app_exit_code);
    }

  /* App has returned — clean up */

  syslog(LOG_INFO, "app: \"%s\" finished with code %d\n",
         app->info.name, g_app_exit_code);

  /* Remove all objects from the input group that the app may have added.
   * This prevents stale references after we delete the app screen.
   */

  lv_group_t *cleanup_group = lv_group_get_default();
  if (cleanup_group != NULL)
    {
      if (g_app_root != NULL)
        {
          lv_group_remove_obj(g_app_root);
        }

      if (g_app_screen != NULL)
        {
          lv_group_remove_obj(g_app_screen);
        }
    }

  /* Destroy app screen */

  if (g_app_screen != NULL)
    {
      lv_obj_delete(g_app_screen);
      g_app_screen = NULL;
      g_app_root   = NULL;
    }

  g_current_app = NULL;

  /* Show launcher */

  launcher_show();

  /* Refresh third-party cache (e.g. if the app installed/removed packages) */

  app_framework_refresh_thirdparty();
  launcher_refresh();

  return g_app_exit_code;
}

/****************************************************************************
 * Name: pc_app_get_screen
 *
 * Description:
 *   Get the current app's LVGL screen container.
 *   Called by apps to get their root drawing area.
 *
 ****************************************************************************/

lv_obj_t *pc_app_get_screen(void)
{
  return g_app_root != NULL ? g_app_root : g_app_screen;
}

/****************************************************************************
 * Name: pc_app_exit
 *
 * Description:
 *   Terminate the current app. Discard any saved state.
 *
 ****************************************************************************/

void pc_app_exit(int code)
{
  if (g_current_app != NULL)
    {
      /* Discard saved state on explicit exit */

      pc_appstate_discard(g_current_app->info.name);
    }

  /* longjmp back to app_framework_launch */

  g_app_exit_code = code;
  g_app_yielded   = false;
  longjmp(g_app_jmpbuf, 1);
}

/****************************************************************************
 * Name: pc_app_yield
 *
 * Description:
 *   Save state and return to launcher (Fn+Home).
 *
 ****************************************************************************/

void pc_app_yield(void)
{
  if (g_current_app != NULL &&
      (g_current_app->info.flags & PC_APP_FLAG_STATEFUL) &&
      g_current_app->save != NULL)
    {
      syslog(LOG_INFO, "app: Saving state for \"%s\"\n",
             g_current_app->info.name);
      pc_appstate_save(g_current_app->info.name, g_current_app->save);
    }

  /* longjmp back to app_framework_launch */

  g_app_exit_code = 0;
  g_app_yielded   = true;
  longjmp(g_app_jmpbuf, 2);
}

/****************************************************************************
 * Name: pc_app_psram_alloc / realloc / free / available
 *
 * Description:
 *   Wrappers around the PSRAM heap allocator.
 *
 ****************************************************************************/

extern void *psram_malloc(size_t size);
extern void *psram_realloc(void *ptr, size_t size);
extern void  psram_free(void *ptr);
extern size_t psram_available(void);

void *pc_app_psram_alloc(size_t size)
{
  return psram_malloc(size);
}

void *pc_app_psram_realloc(void *ptr, size_t size)
{
  return psram_realloc(ptr, size);
}

void pc_app_psram_free(void *ptr)
{
  psram_free(ptr);
}

size_t pc_app_psram_available(void)
{
  return psram_available();
}

/****************************************************************************
 * Name: pc_app_get_builtin_count
 *
 * Description:
 *   Return the number of built-in apps (internal helper).
 *
 ****************************************************************************/

static int pc_app_get_builtin_count(void)
{
  int count = 0;
  while (g_builtin_apps[count] != NULL)
    {
      count++;
    }
  return count;
}

/****************************************************************************
 * Name: pc_app_get_count
 *
 * Description:
 *   Return the total number of registered apps (built-in + third-party).
 *
 ****************************************************************************/

int pc_app_get_count(void)
{
  return pc_app_get_builtin_count() + g_thirdparty_count;
}

/****************************************************************************
 * Name: pc_app_get_info
 *
 * Description:
 *   Get app info by index. Built-in apps come first (0..N-1),
 *   then third-party apps (N..N+M-1).
 *
 ****************************************************************************/

const pc_app_info_t *pc_app_get_info(int index)
{
  int builtin_count = pc_app_get_builtin_count();

  if (index < 0)
    {
      return NULL;
    }

  /* Built-in apps range */

  if (index < builtin_count)
    {
      return &g_builtin_apps[index]->info;
    }

  /* Third-party apps range */

  int tp_index = index - builtin_count;
  if (tp_index < g_thirdparty_count)
    {
      return &g_thirdparty_info[tp_index];
    }

  return NULL;
}

/****************************************************************************
 * Name: pc_app_get_hostname
 *
 * Description:
 *   Return the system hostname string.
 *
 ****************************************************************************/

const char *pc_app_get_hostname(void)
{
  return hostname_get();
}

/****************************************************************************
 * Name: pc_app_get_system_info
 *
 * Description:
 *   Populate a system info struct with current system status.
 *
 ****************************************************************************/

int pc_app_get_system_info(pc_system_info_t *info)
{
  if (info == NULL)
    {
      return PC_ERR_INVAL;
    }

  memset(info, 0, sizeof(pc_system_info_t));

  strncpy(info->hostname, hostname_get(), sizeof(info->hostname) - 1);

  /* Wi-Fi status — not yet integrated with CYW43 driver */

  info->wifi_connected = false;
  info->wifi_rssi      = -80;

  /* Battery — not yet reading ADC */

  info->battery_percent  = 100;
  info->battery_charging = false;

  /* Uptime from clock */

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  info->uptime_sec = (uint32_t)ts.tv_sec;

  /* Memory */

  info->psram_free = (uint32_t)pc_app_psram_available();

  struct mallinfo mi = mallinfo();
  info->sram_free = (uint32_t)mi.fordblks;

  return PC_OK;
}

/****************************************************************************
 * Name: pc_app_register_event_cb
 *
 * Description:
 *   Register a callback for system events (Wi-Fi, battery, SD card).
 *
 ****************************************************************************/

int pc_app_register_event_cb(pc_event_cb_t callback)
{
  g_event_callback = callback;
  return PC_OK;
}
