/****************************************************************************
 * apps/settings/settings_main.c
 *
 * Settings app — system configuration UI.
 * Categories: Wi-Fi, Display, Keyboard, Audio, Storage, System, Packages
 *
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <lvgl/lvgl.h>

#include "pcterm/app.h"
#include "pcterm/config.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef enum
{
  SETTINGS_TAB_WIFI,
  SETTINGS_TAB_DISPLAY,
  SETTINGS_TAB_POWER,
  SETTINGS_TAB_KEYBOARD,
  SETTINGS_TAB_AUDIO,
  SETTINGS_TAB_STORAGE,
  SETTINGS_TAB_SYSTEM,
  SETTINGS_TAB_PACKAGES,
  SETTINGS_TAB_USERS,
  SETTINGS_TAB_COUNT,
} settings_tab_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const char *g_tab_names[] = {
  "Wi-Fi", "Display", "Battery", "Keyboard", "Audio",
  "Storage", "System", "Packages", "Users"
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Forward declarations for tab page builders */
extern void settings_wifi_create(lv_obj_t *parent);
extern void settings_display_create(lv_obj_t *parent);
extern void settings_power_create(lv_obj_t *parent);
extern void settings_keyboard_create(lv_obj_t *parent);
extern void settings_audio_create(lv_obj_t *parent);
extern void settings_storage_create(lv_obj_t *parent);
extern void settings_system_create(lv_obj_t *parent);
extern void settings_packages_create(lv_obj_t *parent);
extern void settings_users_create(lv_obj_t *parent);

static int settings_main(int argc, char *argv[])
{
  lv_obj_t *screen = pc_app_get_screen();

  /* Create tabview for settings categories */

  lv_obj_t *tabview = lv_tabview_create(screen);
  lv_tabview_set_tab_bar_position(tabview, LV_DIR_LEFT);
  lv_tabview_set_tab_bar_size(tabview, 80);

  /* Create tabs */

  lv_obj_t *tabs[SETTINGS_TAB_COUNT];
  for (int i = 0; i < SETTINGS_TAB_COUNT; i++)
    {
      tabs[i] = lv_tabview_add_tab(tabview, g_tab_names[i]);
    }

  /* Build each tab page */

  settings_wifi_create(tabs[SETTINGS_TAB_WIFI]);
  settings_display_create(tabs[SETTINGS_TAB_DISPLAY]);
  settings_power_create(tabs[SETTINGS_TAB_POWER]);
  settings_keyboard_create(tabs[SETTINGS_TAB_KEYBOARD]);
  settings_audio_create(tabs[SETTINGS_TAB_AUDIO]);
  settings_storage_create(tabs[SETTINGS_TAB_STORAGE]);
  settings_system_create(tabs[SETTINGS_TAB_SYSTEM]);
  settings_packages_create(tabs[SETTINGS_TAB_PACKAGES]);
  settings_users_create(tabs[SETTINGS_TAB_USERS]);

  /* Event loop — LVGL handles it via the main loop */

  /* TODO: The app framework should provide a blocking wait
   * that processes LVGL events until the app exits. */

  return 0;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

const pc_app_t g_settings_app = {
  .info = {
    .name         = "settings",
    .display_name = "Settings",
    .version      = "1.0.0",
    .category     = "system",
    .icon         = LV_SYMBOL_SETTINGS,
    .min_ram      = 16384,
    .flags        = PC_APP_FLAG_BUILTIN,
  },
  .main    = settings_main,
  .save    = NULL,     /* Settings don't need state save — they persist to config */
  .restore = NULL,
};
