/****************************************************************************
 * pcterm/include/pcterm/config.h
 *
 * eUX OS — System settings persistence and configuration overlay API.
 *
 * Settings are stored as JSON on the internal flash LittleFS filesystem.
 * The flash is always mounted at /flash regardless of SD card presence.
 *
 * Configuration overlay resolution order (highest → lowest priority):
 *   1. /flash/etc/<path>   — Writable user overrides (LittleFS)
 *   2. /mnt/sd/etc/<path>  — SD card overrides (portable config)
 *   3. Compiled defaults   — Hardcoded in pc_config_defaults()
 *
 * Saving always writes to /flash/etc/ (writable flash).
 *
 ****************************************************************************/

#ifndef __PCTERM_CONFIG_H
#define __PCTERM_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Root of the internal SPI flash LittleFS filesystem */

#define FLASH_ROOT          "/flash"
#define FLASH_ETC           FLASH_ROOT "/etc"
#define FLASH_HOME          FLASH_ROOT "/home"
#define FLASH_HOME_USER     FLASH_HOME "/user"

/* eUX-specific config paths */

#define EUX_ETC             FLASH_ETC "/eux"
#define EUX_WIFI_ETC        FLASH_ETC "/wifi"
#define EUX_SSH_ETC         FLASH_ETC "/ssh"
#define EUX_CACHE           FLASH_ROOT "/var/cache"
#define EUX_LOG             FLASH_ROOT "/var/log"

/* SD card overlay paths */

#define SD_ROOT             "/mnt/sd"
#define SD_ETC              SD_ROOT "/etc"
#define SD_HOME             SD_ROOT "/home/user"

/* Default user home — SD card when available, flash fallback */

#define EUX_HOME_DEFAULT    SD_HOME
#define EUX_MUSIC_DIR       SD_HOME "/music"
#define EUX_VIDEO_DIR       SD_HOME "/video"
#define EUX_DOCUMENTS_DIR   SD_HOME "/documents"
#define EUX_DOWNLOADS_DIR   SD_HOME "/downloads"

#define CONFIG_PATH         EUX_ETC "/settings.json"
#define CONFIG_MAX_SIZE     (4 * 1024)  /* 4 KB max settings file */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* System settings structure */

typedef struct pc_config_s
{
  /* Display */
  uint8_t  brightness;       /* 0-100 (backlight PWM duty) */
  uint8_t  statusbar_position; /* 0=top, 1=bottom */

  /* Power */
  uint8_t  battery_style;    /* BAT_STYLE_* (0=icon, 1=icon-only, 2=text, 3=pct) */
  uint8_t  power_profile;    /* 0=standard, 1=high-perf, 2=power-save */
  uint16_t backlight_timeout; /* Seconds before backlight off (0=never) */

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

  /* User / Login */
  bool     login_enabled;    /* Show login screen on boot */
  char     login_user[32];   /* Active username */
  char     login_hash[65];   /* Hex-encoded password hash (empty = no password) */

  /* Virtual Consoles */
  uint8_t  startup_mode;     /* 0=GUI (tty0), 1=Console (tty1) */
  uint8_t  vconsole_count;   /* Number of text consoles (1-3, default 3) */

  /* Clock / Power (extended) */
  uint8_t  clock_profile;    /* Index into extended clock profile table (0-8) */
} pc_config_t;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Load settings from flash. Falls back to defaults if file missing.
 */
int pc_config_load(pc_config_t *config);

/**
 * Save settings to flash as JSON.
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

/**
 * Resolve a config file path using the overlay chain.
 * Checks (in order): /flash/etc/<relpath>, /mnt/sd/etc/<relpath>.
 * Returns pointer to static buffer with the first path that exists,
 * or the flash path if neither exists (for use as default write target).
 *
 * @param relpath  Relative path within /etc (e.g. "eux/settings.json")
 * @return         Resolved absolute path (static buffer — not thread-safe)
 */
const char *config_resolve(const char *relpath);

/**
 * Get the writable save path for a config file.
 * Always returns /flash/etc/<relpath> — the LittleFS writable location.
 *
 * @param relpath  Relative path within /etc (e.g. "eux/settings.json")
 * @return         Writable absolute path (static buffer — not thread-safe)
 */
const char *config_save_path(const char *relpath);

/**
 * Get the user's home directory.
 * Returns SD card home if available, flash home otherwise.
 *
 * @return  Path to home directory (static buffer)
 */
const char *config_home_dir(void);

#ifdef __cplusplus
}
#endif

#endif /* __PCTERM_CONFIG_H */
