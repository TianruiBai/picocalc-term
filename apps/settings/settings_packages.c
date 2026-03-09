/****************************************************************************
 * apps/settings/settings_packages.c
 *
 * Package manager settings tab: list installed, install from SD,
 * uninstall, and app store browsing/download.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <lvgl/lvgl.h>
#include <syslog.h>
#include "pcterm/package.h"
#include "pcterm/app.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_obj_t *g_installed_list = NULL;
static lv_obj_t *g_store_list     = NULL;
static lv_obj_t *g_status_label   = NULL;

static pcpkg_catalog_t g_catalog;
static bool g_catalog_loaded = false;

/****************************************************************************
 * Private Functions — Installed Packages Tab
 ****************************************************************************/

static void refresh_installed_list(void)
{
  if (g_installed_list == NULL) return;

  lv_obj_clean(g_installed_list);

  pcpkg_entry_t entries[32];
  int count = pcpkg_list(entries, 32);

  if (count == 0)
    {
      lv_list_add_text(g_installed_list, "No packages installed");
      lv_list_add_text(g_installed_list,
                       "Use Store tab or copy .pcpkg to SD");
    }
  else
    {
      for (int i = 0; i < count; i++)
        {
          char label_buf[80];
          snprintf(label_buf, sizeof(label_buf), "%s v%s  (%luKB)",
                   entries[i].name, entries[i].version,
                   (unsigned long)(entries[i].installed_size / 1024));
          lv_list_add_btn(g_installed_list, LV_SYMBOL_FILE, label_buf);
        }
    }
}

static void scan_sd_cb(lv_event_t *e)
{
  (void)e;

  if (g_status_label != NULL)
    {
      lv_label_set_text(g_status_label, "Scanning SD card...");
    }

  int installed = pcpkg_scan_and_install_sd();

  char msg[64];
  if (installed > 0)
    {
      snprintf(msg, sizeof(msg), "Installed %d package(s)", installed);
    }
  else if (installed == 0)
    {
      snprintf(msg, sizeof(msg), "No .pcpkg files found on SD");
    }
  else
    {
      snprintf(msg, sizeof(msg), "Error scanning SD: %d", installed);
    }

  if (g_status_label != NULL)
    {
      lv_label_set_text(g_status_label, msg);
    }

  refresh_installed_list();
  syslog(LOG_INFO, "SETTINGS: %s\n", msg);
}

static void remove_cb(lv_event_t *e)
{
  (void)e;

  pcpkg_entry_t entries[32];
  int count = pcpkg_list(entries, 32);
  if (count <= 0) return;

  /* Uninstall the last package (TODO: proper selection) */

  pcpkg_uninstall(entries[count - 1].name);

  if (g_status_label != NULL)
    {
      char msg[64];
      snprintf(msg, sizeof(msg), "Removed \"%s\"",
               entries[count - 1].name);
      lv_label_set_text(g_status_label, msg);
    }

  refresh_installed_list();
}

/****************************************************************************
 * Private Functions — App Store Tab
 ****************************************************************************/

static void store_download_cb(lv_event_t *e)
{
  int idx = (int)(intptr_t)lv_event_get_user_data(e);

  if (!g_catalog_loaded || idx < 0 || idx >= g_catalog.count)
    {
      return;
    }

  pcpkg_catalog_entry_t *app = &g_catalog.entries[idx];

  if (g_status_label != NULL)
    {
      char msg[80];
      snprintf(msg, sizeof(msg), "Downloading \"%s\"...", app->name);
      lv_label_set_text(g_status_label, msg);
    }

  int ret = pcpkg_download_and_install(app->download_url, app->name);

  if (g_status_label != NULL)
    {
      if (ret == PC_OK)
        {
          char msg[80];
          snprintf(msg, sizeof(msg), "\"%s\" installed!", app->name);
          lv_label_set_text(g_status_label, msg);
        }
      else
        {
          char msg[80];
          snprintf(msg, sizeof(msg), "Failed to install \"%s\" (%d)",
                   app->name, ret);
          lv_label_set_text(g_status_label, msg);
        }
    }

  refresh_installed_list();
}

static void refresh_store_list(void)
{
  if (g_store_list == NULL) return;

  lv_obj_clean(g_store_list);

  if (!g_catalog_loaded || g_catalog.count == 0)
    {
      lv_list_add_text(g_store_list, "No apps available");
      lv_list_add_text(g_store_list, "Press Refresh to fetch catalog");
      return;
    }

  for (int i = 0; i < g_catalog.count; i++)
    {
      pcpkg_catalog_entry_t *app = &g_catalog.entries[i];

      /* Check if already installed */

      bool installed = false;
      pcpkg_entry_t entries[32];
      int inst_count = pcpkg_list(entries, 32);
      for (int j = 0; j < inst_count; j++)
        {
          if (strcmp(entries[j].name, app->name) == 0)
            {
              installed = true;
              break;
            }
        }

      char label_buf[80];
      snprintf(label_buf, sizeof(label_buf), "%s%s v%s (%luKB)",
               installed ? LV_SYMBOL_OK " " : "",
               app->name, app->version,
               (unsigned long)(app->size_bytes / 1024));

      lv_obj_t *btn = lv_list_add_btn(g_store_list,
                                       installed ? LV_SYMBOL_OK
                                                 : LV_SYMBOL_DOWNLOAD,
                                       label_buf);

      if (!installed)
        {
          lv_obj_add_event_cb(btn, store_download_cb,
                              LV_EVENT_CLICKED,
                              (void *)(intptr_t)i);
        }
    }
}

static void store_refresh_cb(lv_event_t *e)
{
  (void)e;

  if (g_status_label != NULL)
    {
      lv_label_set_text(g_status_label, "Fetching catalog...");
    }

  int ret = pcpkg_fetch_catalog(NULL, &g_catalog);

  if (ret == PC_OK)
    {
      g_catalog_loaded = true;

      char msg[64];
      snprintf(msg, sizeof(msg), "Found %d apps in store",
               g_catalog.count);
      if (g_status_label != NULL)
        {
          lv_label_set_text(g_status_label, msg);
        }
    }
  else
    {
      if (g_status_label != NULL)
        {
          lv_label_set_text(g_status_label,
                            "Failed to fetch catalog");
        }
    }

  refresh_store_list();
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void settings_packages_create(lv_obj_t *parent)
{
  /* Status bar at top */

  g_status_label = lv_label_create(parent);
  lv_label_set_text(g_status_label, "Package Manager");
  lv_obj_set_style_text_font(g_status_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(g_status_label,
                              lv_palette_main(LV_PALETTE_CYAN), 0);
  lv_obj_align(g_status_label, LV_ALIGN_TOP_MID, 0, 4);

  /* Tab view: "Installed" and "Store" */

  lv_obj_t *tabview = lv_tabview_create(parent);
  lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
  lv_tabview_set_tab_bar_size(tabview, 28);
  lv_obj_set_size(tabview, 300, 220);
  lv_obj_align(tabview, LV_ALIGN_TOP_MID, 0, 22);

  lv_obj_t *tab_installed = lv_tabview_add_tab(tabview, "Installed");
  lv_obj_t *tab_store     = lv_tabview_add_tab(tabview, "App Store");

  /* ---- Installed tab ---- */

  g_installed_list = lv_list_create(tab_installed);
  lv_obj_set_size(g_installed_list, 280, 130);
  lv_obj_align(g_installed_list, LV_ALIGN_TOP_MID, 0, 0);

  refresh_installed_list();

  /* Scan SD button */

  lv_obj_t *btn_scan = lv_button_create(tab_installed);
  lv_obj_set_size(btn_scan, 130, 28);
  lv_obj_align(btn_scan, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_add_event_cb(btn_scan, scan_sd_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lbl_scan = lv_label_create(btn_scan);
  lv_label_set_text(lbl_scan, LV_SYMBOL_SD_CARD " Scan SD");
  lv_obj_center(lbl_scan);

  /* Remove button */

  lv_obj_t *btn_remove = lv_button_create(tab_installed);
  lv_obj_set_size(btn_remove, 130, 28);
  lv_obj_align(btn_remove, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_bg_color(btn_remove,
                            lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_add_event_cb(btn_remove, remove_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lbl_remove = lv_label_create(btn_remove);
  lv_label_set_text(lbl_remove, LV_SYMBOL_TRASH " Remove");
  lv_obj_center(lbl_remove);

  /* ---- App Store tab ---- */

  g_store_list = lv_list_create(tab_store);
  lv_obj_set_size(g_store_list, 280, 130);
  lv_obj_align(g_store_list, LV_ALIGN_TOP_MID, 0, 0);

  /* Try loading cached catalog */

  if (pcpkg_load_cached_catalog(&g_catalog) == PC_OK)
    {
      g_catalog_loaded = true;
    }

  refresh_store_list();

  /* Refresh catalog button */

  lv_obj_t *btn_refresh = lv_button_create(tab_store);
  lv_obj_set_size(btn_refresh, 280, 28);
  lv_obj_align(btn_refresh, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(btn_refresh,
                            lv_palette_main(LV_PALETTE_BLUE), 0);
  lv_obj_add_event_cb(btn_refresh, store_refresh_cb,
                      LV_EVENT_CLICKED, NULL);

  lv_obj_t *lbl_refresh = lv_label_create(btn_refresh);
  lv_label_set_text(lbl_refresh, LV_SYMBOL_REFRESH " Refresh Catalog");
  lv_obj_center(lbl_refresh);

  syslog(LOG_DEBUG, "SETTINGS: Packages tab created with App Store\n");
}
