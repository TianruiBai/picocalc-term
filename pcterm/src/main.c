/****************************************************************************
 * pcterm/src/main.c
 *
 * PicoCalc-Term OS entry point.
 * Boot sequence:
 *   1. Mount SD card
 *   2. Load hostname
 *   3. Load settings
 *   4. Initialize LVGL
 *   5. Create status bar
 *   6. Initialize app framework (register built-in apps)
 *   7. Initialize package manager (scan SD for third-party apps)
 *   8. Launch the home screen (launcher)
 *   9. Enter main event loop
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <syslog.h>

#include <nuttx/clock.h>
#include <lvgl/lvgl.h>
#include <arch/board/board.h>

#include "pcterm/app.h"
#include "pcterm/config.h"
#include "pcterm/hostname.h"
#include "pcterm/statusbar.h"
#include "pcterm/launcher.h"
#include "pcterm/package.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define LVGL_TICK_MS         5      /* LVGL tick period */
#define STATUSBAR_UPDATE_MS  1000   /* Status bar refresh interval */
#define MAIN_LOOP_SLEEP_MS   5      /* Main loop sleep period */

/****************************************************************************
 * Private Data
 ****************************************************************************/

static pc_config_t g_config;
static bool g_running = true;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: boot_mount_sd
 *
 * Description:
 *   Mount the SD card and verify directory structure.
 *
 ****************************************************************************/

static int boot_mount_sd(void)
{
  extern int rp23xx_sdcard_mount(void);
  int ret = rp23xx_sdcard_mount();

  if (ret < 0)
    {
      syslog(LOG_WARNING,
             "boot: SD card not available — running in limited mode\n");
    }

  return ret;
}

/****************************************************************************
 * Name: boot_init_lvgl
 *
 * Description:
 *   Initialize LVGL, register display and input drivers.
 *
 ****************************************************************************/

/* Forward declarations for LVGL port layer */

extern void lv_port_disp_init(void);
extern void lv_port_indev_init(void);

/****************************************************************************
 * Name: lv_nuttx_tick_cb
 *
 * Description:
 *   LVGL tick callback — returns elapsed milliseconds since boot.
 *   LVGL v9 requires a tick source via lv_tick_set_cb() so that
 *   its internal timer system can advance.  Without this callback,
 *   lv_timer_handler() never processes any timers and the display
 *   flush callback is never invoked.
 *
 ****************************************************************************/

static uint32_t lv_nuttx_tick_cb(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static int boot_init_lvgl(void)
{
  lv_init();

  /* Register tick source — CRITICAL for LVGL v9.
   * Without this, LVGL timers never fire and the display
   * never receives flush commands.
   */

  lv_tick_set_cb(lv_nuttx_tick_cb);

  /* Register framebuffer display driver.
   * This uses NuttX's /dev/fb0 via the LVGL fbdev port layer.
   * The port reads the framebuffer info and creates a display
   * driver with the appropriate resolution (320×320, RGB565).
   */

  lv_port_disp_init();

  /* Register keyboard input driver.
   * This reads from /dev/kbd0 (I2C keyboard via STM32 south-bridge)
   * and maps keys into an LVGL input group.
   */

  lv_port_indev_init();

  syslog(LOG_INFO, "boot: LVGL v%d.%d.%d initialized "
         "(display %dx%d + keyboard input)\n",
         LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
         BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT);
  return 0;
}

/****************************************************************************
 * Name: status_bar_timer_cb
 *
 * Description:
 *   Periodic callback to update the status bar with current system state.
 *
 ****************************************************************************/

static void status_bar_timer_cb(lv_timer_t *timer)
{
  statusbar_state_t state;

  /* Populate state from system queries */

  strncpy(state.hostname, hostname_get(), sizeof(state.hostname));
  state.wifi_connected = false;  /* TODO: query CYW43 driver */
  state.wifi_rssi = -80;
  state.audio_playing = false;   /* TODO: query audio service */
  state.battery_percent = 100;   /* TODO: read ADC */
  state.battery_charging = false;

  /* Get time — NuttX clock */

  struct timespec ts;
  if (rp23xx_aon_timer_gettime(&ts) < 0)
    {
      clock_gettime(CLOCK_REALTIME, &ts);
    }

  struct tm tm;
  gmtime_r(&ts.tv_sec, &tm);
  state.hour = tm.tm_hour;
  state.minute = tm.tm_min;

  statusbar_update(&state);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pcterm_main
 *
 * Description:
 *   PicoCalc-Term OS entry point (CONFIG_INIT_ENTRYPOINT in full config).
 *
 ****************************************************************************/

int pcterm_main(int argc, char *argv[])
{
  int ret;

  printf("\n");
  printf("  PicoCalc-Term  v0.1.0\n");
  printf("  NuttX / LVGL / RP2350B\n");
  printf("\n");

  /* --- Step 1: Mount SD card --- */

  syslog(LOG_INFO, "boot: mount /dev/mmcsd0 -> /mnt/sd\n");
  boot_mount_sd();

  /* --- Step 2: Load hostname --- */

  syslog(LOG_INFO, "boot: loading hostname\n");
  hostname_init();
  syslog(LOG_INFO, "boot: hostname \"%s\"\n", hostname_get());

  /* --- Step 3: Load settings --- */

  syslog(LOG_INFO, "boot: loading settings\n");
  ret = pc_config_load(&g_config);
  if (ret < 0)
    {
      syslog(LOG_WARNING, "boot: using default settings\n");
      pc_config_defaults(&g_config);
    }

  /* --- Step 4: Initialize LVGL --- */

  syslog(LOG_INFO, "boot: initializing LVGL display subsystem\n");
  boot_init_lvgl();

  /* --- Step 5: Create status bar --- */

  syslog(LOG_INFO, "boot: creating status bar\n");
  lv_obj_t *screen = lv_scr_act();
  statusbar_init(screen);

  /* Create periodic status bar update timer */

  lv_timer_create(status_bar_timer_cb, STATUSBAR_UPDATE_MS, NULL);

  /* --- Step 6: Initialize app framework --- */

  syslog(LOG_INFO, "boot: initializing app framework\n");
  app_framework_init();

  /* --- Step 7: Initialize package manager --- */

  syslog(LOG_INFO, "boot: scanning /mnt/sd/apps for packages\n");
  pcpkg_init();

  /* --- Step 8: Launch home screen --- */

  syslog(LOG_INFO, "boot: launching home screen\n");
  lv_obj_t *app_area = statusbar_get_app_area();
  launcher_init(app_area);

  syslog(LOG_INFO, "boot: PicoCalc-Term ready!\n");

  /* --- Step 9: Main event loop --- */

  while (g_running)
    {
      uint32_t time_till_next = lv_timer_handler();

      /* Check for deferred app launch from the launcher */

      const char *pending = launcher_get_pending_launch();
      if (pending != NULL)
        {
          app_framework_launch(pending);
          launcher_clear_pending_launch();
        }

      /* Sleep for the minimum of time_till_next or MAIN_LOOP_SLEEP_MS */

      uint32_t sleep_ms = time_till_next < MAIN_LOOP_SLEEP_MS
                          ? time_till_next : MAIN_LOOP_SLEEP_MS;
      usleep(sleep_ms * 1000);
    }

  syslog(LOG_INFO, "PicoCalc-Term shutting down\n");
  return 0;
}
