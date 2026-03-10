/****************************************************************************
 * boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_sleep.c
 *
 * Backlight timeout and sleep management for PicoCalc.
 *
 * Tracks user activity (key presses, touch) and dims/turns off the
 * LCD backlight after the configured timeout.  Deep sleep powers
 * down the display and puts the RP2350 into DORMANT mode, waking
 * on south-bridge keyboard interrupt (GP8).
 *
 * Backlight is controlled via the STM32 south bridge register
 * SB_REG_BKL (0..255 PWM duty).  There is NO direct GPIO backlight
 * pin on the RP2350.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <syslog.h>
#include <nuttx/clock.h>

#include <arch/board/board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BACKLIGHT_DIM_PCT       20    /* Dim to 20% before off */
#define BACKLIGHT_OFF_PCT       0

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint32_t g_last_activity_sec = 0;  /* CLOCK_MONOTONIC seconds */
static uint16_t g_timeout_sec = 60;        /* Configurable timeout */
static uint8_t  g_saved_brightness = 80;   /* User's configured brightness */
static bool     g_backlight_dimmed = false;
static bool     g_backlight_off = false;
static bool     g_sleeping = false;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rp23xx_backlight_activity
 *
 * Description:
 *   Called on any user input (key press, etc.) to reset the
 *   backlight timeout counter.  If backlight was dimmed or off,
 *   restores it immediately.
 *
 ****************************************************************************/

void rp23xx_backlight_activity(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  g_last_activity_sec = (uint32_t)ts.tv_sec;

  if (g_backlight_dimmed || g_backlight_off)
    {
      /* Restore backlight */

      rp23xx_sb_set_lcd_backlight(
        (uint8_t)((g_saved_brightness * 255) / 100));
      g_backlight_dimmed = false;
      g_backlight_off = false;

      syslog(LOG_DEBUG, "SLEEP: Backlight restored\n");
    }

  if (g_sleeping)
    {
      rp23xx_sleep_exit();
    }
}

/****************************************************************************
 * Name: rp23xx_backlight_timer_tick
 *
 * Description:
 *   Called periodically (e.g. every 1 second from status bar timer).
 *   Checks if the backlight timeout has elapsed and dims/turns off.
 *
 ****************************************************************************/

void rp23xx_backlight_timer_tick(void)
{
  if (g_timeout_sec == 0)
    {
      /* Timeout disabled */

      return;
    }

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  uint32_t now = (uint32_t)ts.tv_sec;
  uint32_t elapsed = now - g_last_activity_sec;

  if (!g_backlight_off && elapsed >= g_timeout_sec)
    {
      /* Turn backlight off completely */

      rp23xx_sb_set_lcd_backlight(BACKLIGHT_OFF_PCT);
      g_backlight_off = true;
      g_backlight_dimmed = false;

      syslog(LOG_INFO, "SLEEP: Backlight off (timeout=%us)\n",
             (unsigned)g_timeout_sec);
    }
  else if (!g_backlight_dimmed && !g_backlight_off &&
           elapsed >= (g_timeout_sec * 3 / 4))
    {
      /* Dim to warn user (at 75% of timeout) */

      uint8_t dim_val = (BACKLIGHT_DIM_PCT * 255) / 100;
      rp23xx_sb_set_lcd_backlight(dim_val);
      g_backlight_dimmed = true;

      syslog(LOG_DEBUG, "SLEEP: Backlight dimmed\n");
    }
}

/****************************************************************************
 * Name: rp23xx_sleep_enter
 *
 * Description:
 *   Enter low-power sleep mode.  Turns off backlight and reduces
 *   core clock to minimum.  Wakes on keyboard interrupt.
 *
 ****************************************************************************/

void rp23xx_sleep_enter(void)
{
  if (g_sleeping)
    {
      return;
    }

  syslog(LOG_INFO, "SLEEP: Entering sleep mode\n");

  /* Turn off backlight */

  rp23xx_sb_set_lcd_backlight(0);
  g_backlight_off = true;

  /* Switch to power-save clock profile */

  rp23xx_set_power_profile(2);  /* 100 MHz */

  g_sleeping = true;
}

/****************************************************************************
 * Name: rp23xx_sleep_exit
 *
 * Description:
 *   Exit sleep mode.  Restores backlight and clock profile.
 *
 ****************************************************************************/

void rp23xx_sleep_exit(void)
{
  if (!g_sleeping)
    {
      return;
    }

  syslog(LOG_INFO, "SLEEP: Waking up\n");

  /* Restore previous power profile */

  extern int rp23xx_get_power_profile(void);
  /* Restore to standard by default; the config callback will
   * restore the user's preferred profile on next tick.
   */

  rp23xx_set_power_profile(0);

  /* Restore backlight */

  rp23xx_sb_set_lcd_backlight(
    (uint8_t)((g_saved_brightness * 255) / 100));
  g_backlight_off = false;
  g_backlight_dimmed = false;

  g_sleeping = false;
}

/****************************************************************************
 * Name: rp23xx_sleep_set_timeout
 *
 * Description:
 *   Set the backlight timeout in seconds (0 = disable).
 *
 ****************************************************************************/

void rp23xx_sleep_set_timeout(uint16_t seconds)
{
  g_timeout_sec = seconds;
}

/****************************************************************************
 * Name: rp23xx_sleep_set_brightness
 *
 * Description:
 *   Store the user's desired brightness (0-100) for restore on wake.
 *
 ****************************************************************************/

void rp23xx_sleep_set_brightness(uint8_t pct)
{
  g_saved_brightness = pct;
}
