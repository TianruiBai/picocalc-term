/****************************************************************************
 * pcterm/src/app_stubs.c
 *
 * Weak fallback built-in app registrations used when full app modules are
 * not linked yet. This keeps the launcher functional while individual apps
 * are integrated incrementally.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <syslog.h>

#include <lvgl/lvgl.h>

#include "pcterm/app.h"

static int stub_app_main(int argc, char *argv[])
{
  lv_obj_t *screen;
  lv_obj_t *label;

  (void)argc;
  (void)argv;

  screen = pc_app_get_screen();
  if (screen == NULL)
    {
      return PC_ERR_GENERIC;
    }

  lv_obj_clean(screen);

  label = lv_label_create(screen);
  lv_label_set_text(label, "App module not linked yet");
  lv_obj_center(label);

  syslog(LOG_WARNING, "pcterm: launched stub app\n");
  return PC_OK;
}

#define DECLARE_STUB_APP(symbol_name, app_name, app_display, app_category, app_flags) \
  const pc_app_t symbol_name __attribute__((weak)) = {                                 \
    .info = {                                                                            \
      .name         = app_name,                                                          \
      .display_name = app_display,                                                       \
      .version      = "0.1.0",                                                          \
      .category     = app_category,                                                      \
      .min_ram      = 0,                                                                 \
      .flags        = app_flags,                                                         \
    },                                                                                   \
    .main    = stub_app_main,                                                            \
    .save    = NULL,                                                                     \
    .restore = NULL,                                                                     \
  }

DECLARE_STUB_APP(g_settings_app,     "settings",      "Settings",          "system", PC_APP_FLAG_BUILTIN);
DECLARE_STUB_APP(g_pcedit_app,       "pcedit",        "Editor",            "office", PC_APP_FLAG_BUILTIN);
DECLARE_STUB_APP(g_pccsv_app,        "pccsv",         "Spreadsheet",       "office", PC_APP_FLAG_BUILTIN);
DECLARE_STUB_APP(g_pcaudio_app,      "pcaudio",       "Audio",             "media",  PC_APP_FLAG_BUILTIN);
DECLARE_STUB_APP(g_pcvideo_app,      "pcvideo",       "Video",             "media",  PC_APP_FLAG_BUILTIN);
DECLARE_STUB_APP(g_pcterm_local_app, "pcterm-local",  "Local Terminal",    "system", PC_APP_FLAG_BUILTIN);
DECLARE_STUB_APP(g_pcterm_serial_app,"pcterm-serial", "Serial Terminal",   "system", PC_APP_FLAG_BUILTIN);
DECLARE_STUB_APP(g_pcssh_app,        "pcssh",         "SSH",               "network", PC_APP_FLAG_BUILTIN | PC_APP_FLAG_NETWORK);
DECLARE_STUB_APP(g_pcwireless_app,   "pcwireless",    "Wireless Manager",  "network", PC_APP_FLAG_BUILTIN | PC_APP_FLAG_NETWORK);
DECLARE_STUB_APP(g_pcweb_app,        "pcweb",         "Web",               "network", PC_APP_FLAG_BUILTIN | PC_APP_FLAG_NETWORK);
DECLARE_STUB_APP(g_pcfiles_app,      "pcfiles",       "Files",             "system", PC_APP_FLAG_BUILTIN);
