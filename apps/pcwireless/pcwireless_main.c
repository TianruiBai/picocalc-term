/****************************************************************************
 * apps/pcwireless/pcwireless_main.c
 *
 * Wireless Manager app (Wi-Fi + Bluetooth).
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#include <lvgl/lvgl.h>

#include "pcterm/app.h"

/****************************************************************************
 * External References — Wi-Fi driver
 ****************************************************************************/

typedef struct wifi_scan_result_s
{
  char ssid[33];
  int  rssi;
  int  auth;
} wifi_scan_result_t;

extern int  rp23xx_wifi_scan(wifi_scan_result_t *results, int max_results);
extern int  rp23xx_wifi_connect(const char *ssid, const char *passphrase);
extern int  rp23xx_wifi_status(void);
extern const char *rp23xx_wifi_ip(void);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_obj_t *g_wifi_list = NULL;
static lv_obj_t *g_wifi_status = NULL;
static lv_obj_t *g_bt_list = NULL;
static lv_obj_t *g_bt_status = NULL;
static char      g_wifi_ssids[16][33];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void wifi_item_connect_cb(lv_event_t *e)
{
  const char *ssid = (const char *)lv_event_get_user_data(e);
  int ret;

  if (ssid == NULL)
    {
      return;
    }

  lv_label_set_text(g_wifi_status, "Connecting...");
  lv_refr_now(NULL);

  ret = rp23xx_wifi_connect(ssid, "");
  if (ret == 0)
    {
      char buf[96];
      snprintf(buf, sizeof(buf), "Connected: %s (%s)",
               ssid, rp23xx_wifi_ip());
      lv_label_set_text(g_wifi_status, buf);
    }
  else
    {
      lv_label_set_text(g_wifi_status, "Connect failed");
    }
}

static void wifi_scan_cb(lv_event_t *e)
{
  wifi_scan_result_t results[16];
  int count;

  (void)e;

  lv_label_set_text(g_wifi_status, "Scanning...");
  lv_refr_now(NULL);

  count = rp23xx_wifi_scan(results, 16);
  lv_obj_clean(g_wifi_list);

  if (count <= 0)
    {
      lv_list_add_text(g_wifi_list, "No networks found");
      lv_label_set_text(g_wifi_status, "Scan complete: 0 networks");
      return;
    }

  lv_list_add_text(g_wifi_list, "Available Networks");

  for (int i = 0; i < count; i++)
    {
      char label[64];
      lv_obj_t *btn;

      snprintf(label, sizeof(label), "%.32s (%d dBm)",
               results[i].ssid, results[i].rssi);

      btn = lv_list_add_btn(g_wifi_list, LV_SYMBOL_WIFI, label);

      strncpy(g_wifi_ssids[i], results[i].ssid, sizeof(g_wifi_ssids[i]) - 1);
      g_wifi_ssids[i][sizeof(g_wifi_ssids[i]) - 1] = '\0';
      lv_obj_add_event_cb(btn, wifi_item_connect_cb,
                          LV_EVENT_CLICKED, g_wifi_ssids[i]);
    }

  {
    char status[48];
    snprintf(status, sizeof(status), "Found %d networks", count);
    lv_label_set_text(g_wifi_status, status);
  }
}

static void bt_scan_cb(lv_event_t *e)
{
  (void)e;

  lv_obj_clean(g_bt_list);
  lv_list_add_text(g_bt_list, "Bluetooth Devices");
  lv_list_add_btn(g_bt_list, LV_SYMBOL_BLUETOOTH,
                  "No devices (driver pending)");
  lv_label_set_text(g_bt_status, "BT scan complete");
}

static void bt_toggle_cb(lv_event_t *e)
{
  static bool bt_enabled = false;
  lv_obj_t *btn = lv_event_get_target_obj(e);
  lv_obj_t *lbl = lv_obj_get_child(btn, 0);

  bt_enabled = !bt_enabled;

  if (lbl != NULL)
    {
      lv_label_set_text(lbl, bt_enabled ? "Disable BT" : "Enable BT");
    }

  lv_label_set_text(g_bt_status,
                    bt_enabled ? "Bluetooth enabled" : "Bluetooth disabled");
}

static int pcwireless_main(int argc, char *argv[])
{
  lv_obj_t *screen = pc_app_get_screen();
  lv_obj_t *tabview;
  lv_obj_t *tab_wifi;
  lv_obj_t *tab_bt;
  lv_obj_t *btn;
  lv_obj_t *lbl;

  (void)argc;
  (void)argv;

  tabview = lv_tabview_create(screen);
  lv_obj_set_size(tabview, 320, 300);
  lv_obj_align(tabview, LV_ALIGN_TOP_LEFT, 0, 0);

  tab_wifi = lv_tabview_add_tab(tabview, "Wi-Fi");
  tab_bt   = lv_tabview_add_tab(tabview, "Bluetooth");

  btn = lv_button_create(tab_wifi);
  lv_obj_set_size(btn, 110, 32);
  lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -8, 8);
  lv_obj_add_event_cb(btn, wifi_scan_cb, LV_EVENT_CLICKED, NULL);
  lbl = lv_label_create(btn);
  lv_label_set_text(lbl, "Scan Wi-Fi");
  lv_obj_center(lbl);

  g_wifi_list = lv_list_create(tab_wifi);
  lv_obj_set_size(g_wifi_list, 300, 200);
  lv_obj_align(g_wifi_list, LV_ALIGN_TOP_MID, 0, 48);

  g_wifi_status = lv_label_create(tab_wifi);
  lv_obj_align(g_wifi_status, LV_ALIGN_BOTTOM_LEFT, 8, -8);
  if (rp23xx_wifi_status() > 0)
    {
      char connected[96];
      snprintf(connected, sizeof(connected), "Connected: %s", rp23xx_wifi_ip());
      lv_label_set_text(g_wifi_status, connected);
    }
  else
    {
      lv_label_set_text(g_wifi_status, "Wi-Fi disconnected");
    }

  btn = lv_button_create(tab_bt);
  lv_obj_set_size(btn, 120, 32);
  lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 8, 8);
  lv_obj_add_event_cb(btn, bt_toggle_cb, LV_EVENT_CLICKED, NULL);
  lbl = lv_label_create(btn);
  lv_label_set_text(lbl, "Enable BT");
  lv_obj_center(lbl);

  btn = lv_button_create(tab_bt);
  lv_obj_set_size(btn, 110, 32);
  lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -8, 8);
  lv_obj_add_event_cb(btn, bt_scan_cb, LV_EVENT_CLICKED, NULL);
  lbl = lv_label_create(btn);
  lv_label_set_text(lbl, "Scan BT");
  lv_obj_center(lbl);

  g_bt_list = lv_list_create(tab_bt);
  lv_obj_set_size(g_bt_list, 300, 200);
  lv_obj_align(g_bt_list, LV_ALIGN_TOP_MID, 0, 48);
  lv_list_add_text(g_bt_list, "Bluetooth Devices");
  lv_list_add_btn(g_bt_list, LV_SYMBOL_BLUETOOTH,
                  "No devices (driver pending)");

  g_bt_status = lv_label_create(tab_bt);
  lv_label_set_text(g_bt_status, "Bluetooth disabled");
  lv_obj_align(g_bt_status, LV_ALIGN_BOTTOM_LEFT, 8, -8);

  syslog(LOG_INFO, "PCWIRELESS: Wireless Manager opened\n");
  return PC_OK;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

const pc_app_t g_pcwireless_app = {
  .info = {
    .name         = "pcwireless",
    .display_name = "Wireless Manager",
    .version      = "1.0.0",
    .category     = "system",
    .min_ram      = 16384,
    .flags        = PC_APP_FLAG_BUILTIN | PC_APP_FLAG_NETWORK,
  },
  .main    = pcwireless_main,
  .save    = NULL,
  .restore = NULL,
};
