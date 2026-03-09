/****************************************************************************
 * pcterm/include/pcterm/config.h
 *
 * System settings persistence API.
 * Settings are stored as JSON on /mnt/sd/etc/settings.json.
 *
 ****************************************************************************/

#ifndef __PCTERM_CONFIG_H
#define __PCTERM_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CONFIG_PATH         "/mnt/sd/etc/settings.json"
#define CONFIG_MAX_SIZE     (4 * 1024)  /* 4 KB max settings file */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* System settings structure */

typedef struct pc_config_s
{
  /* Display */
  uint8_t  brightness;       /* 0-100 (backlight PWM duty) */

  /* Audio */
  uint8_t  volume;           /* 0-100 */
  bool     muted;
  uint8_t  key_click;       /* 0=off, 1=on */

  /* Keyboard */
  uint8_t  key_repeat_delay; /* ms before repeat starts (default 500) */
  uint8_t  key_repeat_rate;  /* ms between repeats (default 50) */

  /* Wi-Fi */
  char     wifi_ssid[33];
  char     wifi_pass[65];
  bool     wifi_autoconnect;

  /* System */
  char     hostname[32];
  char     timezone[32];     /* e.g. "UTC", "EST-5" */
  bool     auto_sleep;       /* Auto-sleep on inactivity */
  uint16_t sleep_timeout;    /* Seconds before sleep */

  /* Terminal */
  uint8_t  term_font_size;   /* 0=small(6px), 1=medium(8px) */
  uint8_t  term_color_scheme;/* 0=dark, 1=light, 2=solarized */
} pc_config_t;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Load settings from SD card. Falls back to defaults if file missing.
 */
int pc_config_load(pc_config_t *config);

/**
 * Save settings to SD card as JSON.
 */
int pc_config_save(const pc_config_t *config);

/**
 * Reset all settings to default values.
 */
void pc_config_defaults(pc_config_t *config);

/**
 * Get the global config instance (loaded at boot).
 */
pc_config_t *pc_config_get(void);

#ifdef __cplusplus
}
#endif

#endif /* __PCTERM_CONFIG_H */
