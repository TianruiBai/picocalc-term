/****************************************************************************
 * pcterm/src/config.c
 *
 * System settings persistence — JSON on SD card.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/mutex.h>
#include <cJSON.h>

#include "pcterm/config.h"
#include "pcterm/app.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static pc_config_t g_global_config;
static mutex_t     g_config_lock = NXMUTEX_INITIALIZER;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void pc_config_defaults(pc_config_t *config)
{
  memset(config, 0, sizeof(*config));

  config->brightness       = 80;
  config->statusbar_position = 0;  /* Top */
  config->battery_style    = 0;          /* Icon + percent */
  config->power_profile    = 0;          /* Standard */
  config->backlight_timeout = 60;        /* 1 minute */
  config->volume           = 80;
  config->muted            = false;
  config->key_repeat_delay = 500 / 10;  /* stored as / 10 for uint8_t */
  config->key_repeat_rate  = 50 / 10;
  config->wifi_autoconnect = false;
  strncpy(config->hostname, "picocalc", sizeof(config->hostname));
  strncpy(config->timezone, "UTC", sizeof(config->timezone));
  config->auto_sleep       = true;
  config->sleep_timeout    = 300;        /* 5 minutes */
  config->term_font_size   = 0;         /* small */
  config->term_color_scheme = 0;        /* dark */

  config->startup_mode     = 0;         /* GUI (tty0) */
  config->vconsole_count   = 3;         /* 3 text consoles (tty1-3) */
  config->clock_profile    = 2;         /* Standard 150 MHz (index 2 in ext table) */

  /* Default login: username "picocalc", password "password" */

  config->login_enabled    = true;
  strncpy(config->login_user, "picocalc", sizeof(config->login_user));
  strncpy(config->login_hash,
          "a171dd7e5d3961e4c8ce6f0cb0a1e4c4"
          "d8d3034b2cc1ca2c7d42aa085fc9a1e8",
          sizeof(config->login_hash));
}

int pc_config_load(pc_config_t *config)
{
  pc_config_defaults(config);

  nxmutex_lock(&g_config_lock);

  FILE *f = fopen(CONFIG_PATH, "r");
  if (f == NULL)
    {
      syslog(LOG_INFO, "config: No settings file, using defaults\n");
      nxmutex_unlock(&g_config_lock);
      return PC_ERR_NOENT;
    }

  /* Read entire file */

  char buf[CONFIG_MAX_SIZE];
  size_t len = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[len] = '\0';

  /* Parse JSON */

  cJSON *root = cJSON_Parse(buf);
  if (root == NULL)
    {
      syslog(LOG_ERR, "config: JSON parse error\n");
      nxmutex_unlock(&g_config_lock);
      return PC_ERR_INVAL;
    }

  /* Read fields (keeping defaults for missing fields) */

  cJSON *item;

  item = cJSON_GetObjectItem(root, "brightness");
  if (cJSON_IsNumber(item)) config->brightness = item->valueint;

  item = cJSON_GetObjectItem(root, "statusbar_position");
  if (cJSON_IsNumber(item)) config->statusbar_position = item->valueint;

  item = cJSON_GetObjectItem(root, "battery_style");
  if (cJSON_IsNumber(item)) config->battery_style = item->valueint;

  item = cJSON_GetObjectItem(root, "power_profile");
  if (cJSON_IsNumber(item)) config->power_profile = item->valueint;

  item = cJSON_GetObjectItem(root, "backlight_timeout");
  if (cJSON_IsNumber(item)) config->backlight_timeout = item->valueint;

  item = cJSON_GetObjectItem(root, "volume");
  if (cJSON_IsNumber(item)) config->volume = item->valueint;

  item = cJSON_GetObjectItem(root, "muted");
  if (cJSON_IsBool(item)) config->muted = cJSON_IsTrue(item);

  item = cJSON_GetObjectItem(root, "wifi_ssid");
  if (cJSON_IsString(item))
    strncpy(config->wifi_ssid, item->valuestring,
            sizeof(config->wifi_ssid) - 1);

  item = cJSON_GetObjectItem(root, "wifi_autoconnect");
  if (cJSON_IsBool(item)) config->wifi_autoconnect = cJSON_IsTrue(item);

  item = cJSON_GetObjectItem(root, "hostname");
  if (cJSON_IsString(item))
    strncpy(config->hostname, item->valuestring,
            sizeof(config->hostname) - 1);

  item = cJSON_GetObjectItem(root, "timezone");
  if (cJSON_IsString(item))
    strncpy(config->timezone, item->valuestring,
            sizeof(config->timezone) - 1);

  item = cJSON_GetObjectItem(root, "auto_sleep");
  if (cJSON_IsBool(item)) config->auto_sleep = cJSON_IsTrue(item);

  item = cJSON_GetObjectItem(root, "sleep_timeout");
  if (cJSON_IsNumber(item)) config->sleep_timeout = item->valueint;

  item = cJSON_GetObjectItem(root, "term_font_size");
  if (cJSON_IsNumber(item)) config->term_font_size = item->valueint;

  item = cJSON_GetObjectItem(root, "term_color_scheme");
  if (cJSON_IsNumber(item)) config->term_color_scheme = item->valueint;

  item = cJSON_GetObjectItem(root, "startup_mode");
  if (cJSON_IsNumber(item)) config->startup_mode = item->valueint;

  item = cJSON_GetObjectItem(root, "vconsole_count");
  if (cJSON_IsNumber(item)) config->vconsole_count = item->valueint;

  item = cJSON_GetObjectItem(root, "clock_profile");
  if (cJSON_IsNumber(item)) config->clock_profile = item->valueint;

  /* Login / user credentials */

  item = cJSON_GetObjectItem(root, "login_enabled");
  if (cJSON_IsBool(item)) config->login_enabled = cJSON_IsTrue(item);

  item = cJSON_GetObjectItem(root, "login_user");
  if (cJSON_IsString(item))
    strncpy(config->login_user, item->valuestring,
            sizeof(config->login_user) - 1);

  item = cJSON_GetObjectItem(root, "login_hash");
  if (cJSON_IsString(item))
    strncpy(config->login_hash, item->valuestring,
            sizeof(config->login_hash) - 1);

  cJSON_Delete(root);

  syslog(LOG_INFO, "config: Loaded settings from %s\n", CONFIG_PATH);

  /* Copy to global */

  memcpy(&g_global_config, config, sizeof(pc_config_t));

  nxmutex_unlock(&g_config_lock);
  return PC_OK;
}

int pc_config_save(const pc_config_t *config)
{
  nxmutex_lock(&g_config_lock);

  cJSON *root = cJSON_CreateObject();

  if (root == NULL)
    {
      nxmutex_unlock(&g_config_lock);
      return PC_ERR_NOMEM;
    }

  cJSON_AddNumberToObject(root, "brightness", config->brightness);
  cJSON_AddNumberToObject(root, "statusbar_position",
                          config->statusbar_position);
  cJSON_AddNumberToObject(root, "battery_style", config->battery_style);
  cJSON_AddNumberToObject(root, "power_profile", config->power_profile);
  cJSON_AddNumberToObject(root, "backlight_timeout",
                          config->backlight_timeout);
  cJSON_AddNumberToObject(root, "volume", config->volume);
  cJSON_AddBoolToObject(root, "muted", config->muted);
  cJSON_AddStringToObject(root, "wifi_ssid", config->wifi_ssid);
  cJSON_AddBoolToObject(root, "wifi_autoconnect", config->wifi_autoconnect);
  cJSON_AddStringToObject(root, "hostname", config->hostname);
  cJSON_AddStringToObject(root, "timezone", config->timezone);
  cJSON_AddBoolToObject(root, "auto_sleep", config->auto_sleep);
  cJSON_AddNumberToObject(root, "sleep_timeout", config->sleep_timeout);
  cJSON_AddNumberToObject(root, "term_font_size", config->term_font_size);
  cJSON_AddNumberToObject(root, "term_color_scheme",
                          config->term_color_scheme);
  cJSON_AddNumberToObject(root, "startup_mode", config->startup_mode);
  cJSON_AddNumberToObject(root, "vconsole_count", config->vconsole_count);
  cJSON_AddNumberToObject(root, "clock_profile", config->clock_profile);

  /* Login / user credentials */

  cJSON_AddBoolToObject(root, "login_enabled", config->login_enabled);
  cJSON_AddStringToObject(root, "login_user", config->login_user);
  cJSON_AddStringToObject(root, "login_hash", config->login_hash);

  char *json = cJSON_Print(root);
  cJSON_Delete(root);

  if (json == NULL)
    {
      nxmutex_unlock(&g_config_lock);
      return PC_ERR_NOMEM;
    }

  FILE *f = fopen(CONFIG_PATH, "w");
  if (f == NULL)
    {
      free(json);
      nxmutex_unlock(&g_config_lock);
      return PC_ERR_IO;
    }

  fputs(json, f);
  fclose(f);
  free(json);

  /* Update global copy */

  memcpy(&g_global_config, config, sizeof(pc_config_t));

  syslog(LOG_INFO, "config: Saved settings to %s\n", CONFIG_PATH);
  nxmutex_unlock(&g_config_lock);
  return PC_OK;
}

pc_config_t *pc_config_get(void)
{
  return &g_global_config;
}
