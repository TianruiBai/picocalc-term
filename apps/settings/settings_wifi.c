/****************************************************************************
 * apps/settings/settings_wifi.c
 *
 * Wi-Fi settings tab: scan networks, connect, manage saved networks.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <lvgl/lvgl.h>

/****************************************************************************
 * External References — WiFi driver (rp23xx_wifi.c)
 ****************************************************************************/

typedef struct wifi_scan_result_s
{
  char ssid[33];
  int  rssi;
  int  auth;   /* 0=open, 1=WPA, 2=WPA2, 3=WPA3 */
} wifi_scan_result_t;

extern int  rp23xx_wifi_scan(wifi_scan_result_t *results, int max_results);
extern int  rp23xx_wifi_connect(const char *ssid, const char *passphrase);
extern int  rp23xx_wifi_status(void);
extern const char *rp23xx_wifi_ip(void);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_obj_t *g_wifi_list   = NULL;
static lv_obj_t *g_wifi_status = NULL;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void wifi_net_click_cb(lv_event_t *e)
{
  lv_obj_t *btn = lv_event_get_target_obj(e);
  lv_obj_t *lbl = lv_obj_get_child(btn, 0);
  if (lbl)
    {
      const char *ssid = lv_label_get_text(lbl);
      syslog(LOG_INFO, "SETTINGS: Connecting to %s\n", ssid);
      lv_label_set_text(g_wifi_status, "Connecting...");
      lv_refr_now(NULL);

      int ret = rp23xx_wifi_connect(ssid, "");
      if (ret == 0)
        {
          char buf[64];
          snprintf(buf, sizeof(buf), "Connected: %s (%s)",
                   ssid, rp23xx_wifi_ip());
          lv_label_set_text(g_wifi_status, buf);
        }
      else
        {
          lv_label_set_text(g_wifi_status, "Connection failed");
        }
    }
}

static void scan_cb(lv_event_t *e)
{
  (void)e;
  wifi_scan_result_t results[16];
  int count;

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
      char label[48];
      snprintf(label, sizeof(label), "%s (%d dBm)",
               results[i].ssid, results[i].rssi);
      lv_obj_t *btn = lv_list_add_btn(g_wifi_list, LV_SYMBOL_WIFI, label);
      lv_obj_add_event_cb(btn, wifi_net_click_cb,
                          LV_EVENT_CLICKED, NULL);
    }

  char status[32];
  snprintf(status, sizeof(status), "Found %d networks", count);
  lv_label_set_text(g_wifi_status, status);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * Build the Wi-Fi settings tab content.
 *
 * @param parent  LVGL parent (tab content area)
 */
void settings_wifi_create(lv_obj_t *parent)
{
  /* Scan button */

  lv_obj_t *btn_scan = lv_button_create(parent);
  lv_obj_set_size(btn_scan, 120, 32);
  lv_obj_align(btn_scan, LV_ALIGN_TOP_RIGHT, -8, 8);
  lv_obj_add_event_cb(btn_scan, scan_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lbl_scan = lv_label_create(btn_scan);
  lv_label_set_text(lbl_scan, "Scan");
  lv_obj_center(lbl_scan);

  /* Network list */

  g_wifi_list = lv_list_create(parent);
  lv_obj_set_size(g_wifi_list, 280, 180);
  lv_obj_align(g_wifi_list, LV_ALIGN_TOP_MID, 0, 48);

  lv_list_add_text(g_wifi_list, "Saved Networks");
  lv_list_add_btn(g_wifi_list, LV_SYMBOL_WIFI, "(no saved networks)");

  /* Status label */

  g_wifi_status = lv_label_create(parent);
  lv_label_set_text(g_wifi_status, "Status: Disconnected");
  lv_obj_align(g_wifi_status, LV_ALIGN_BOTTOM_LEFT, 8, -8);

  /* Check current connection status */

  if (rp23xx_wifi_status() > 0)
    {
      char buf[64];
      snprintf(buf, sizeof(buf), "Connected: %s", rp23xx_wifi_ip());
      lv_label_set_text(g_wifi_status, buf);
    }

  syslog(LOG_DEBUG, "SETTINGS: Wi-Fi tab created\n");
}
