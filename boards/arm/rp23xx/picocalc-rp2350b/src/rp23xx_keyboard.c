/****************************************************************************
 * boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_keyboard.c
 *
 * I2C keyboard driver for PicoCalc STM32 south-bridge.
 * Uses the south bridge FIFO protocol (register 0x09) to read key events.
 *
 * Protocol (via south bridge at I2C0:0x1F):
 *   Read FIFO: send [0x09], read 2 bytes [state, keycode]
 *     state: 1=pressed, 2=held, 3=released
 *     keycode: ASCII or special code (0x80+ for Fn/arrows/etc.)
 *   Check FIFO count: read register 0x04, bits 0-4 = count
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <poll.h>

#include <nuttx/i2c/i2c_master.h>
#include <nuttx/input/keyboard.h>

#include "rp23xx_i2c.h"
#include "rp23xx_gpio.h"
#include <arch/board/board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Special key codes from the STM32 south-bridge
 * Official PicoCalc keyboard firmware codes.
 */

#define KBD_KEY_NONE        0x00
#define KBD_KEY_FN          0xA5
#define KBD_KEY_UP          0xB5
#define KBD_KEY_DOWN        0xB6
#define KBD_KEY_LEFT        0xB4
#define KBD_KEY_RIGHT       0xB7
#define KBD_KEY_HOME        0xD2
#define KBD_KEY_END         0xD5
#define KBD_KEY_PGUP        0xD6
#define KBD_KEY_PGDN        0xD7
#define KBD_KEY_INSERT      0xD1
#define KBD_KEY_DELETE      0xD4
#define KBD_KEY_ESC         0xB1
#define KBD_KEY_TAB         0x09
#define KBD_KEY_ENTER       0x0A
#define KBD_KEY_BACKSPACE   0x08
#define KBD_KEY_POWER       SB_KEY_POWER  /* 0x91 - power button */

/* Modifier key codes reported by south bridge FIFO
 * Official firmware uses 0xA1-0xA5 for modifier keys.
 */

#define KBD_KEY_LSHIFT      0xA1
#define KBD_KEY_RSHIFT      0xA2
#define KBD_KEY_CTRL        0xA3
#define KBD_KEY_ALT         0xA4

/* Modifier flags (second byte from STM32, if supported) */

#define KBD_MOD_SHIFT       0x01
#define KBD_MOD_CTRL        0x02
#define KBD_MOD_ALT         0x04
#define KBD_MOD_FN          0x08

/* Polling interval in milliseconds */

#define KBD_POLL_INTERVAL   20  /* 50 Hz scan rate */

/* Key status register bit fields */

#define KBD_KEY_FIFO_MASK   0x1F  /* Bits 0-4: FIFO count */
#define KBD_KEY_CAPSLOCK    0x20  /* Bit 5: capslock active */
#define KBD_KEY_NUMLOCK     0x40  /* Bit 6: numlock active */

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct kbd_dev_s
{
  bool initialized;
  uint8_t last_key;              /* Last reported key */
  uint8_t last_state;            /* Last key state */
  uint8_t modifiers;             /* Current modifier state */
  bool    capslock;              /* Capslock status */
  bool    numlock;               /* Numlock status */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct kbd_dev_s g_kbddev;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rp23xx_keyboard_initialize
 *
 * Description:
 *   Initialize the keyboard driver. Requires south bridge to be
 *   initialized first (rp23xx_sb_init).
 *
 ****************************************************************************/

int rp23xx_keyboard_initialize(void)
{
  struct kbd_dev_s *dev = &g_kbddev;

  if (dev->initialized)
    {
      return 0;
    }

  /* South bridge must be initialized first */

  int ret = rp23xx_sb_init();
  if (ret < 0)
    {
      syslog(LOG_ERR, "KBD: South bridge init failed: %d\n", ret);
      return ret;
    }

  /* Read key status to get initial capslock/numlock state */

  uint8_t keystatus = rp23xx_sb_read_reg(SB_REG_KEY);
  dev->capslock = (keystatus & KBD_KEY_CAPSLOCK) != 0;
  dev->numlock  = (keystatus & KBD_KEY_NUMLOCK) != 0;

  /* Drain any stale keys from FIFO */

  uint8_t state, keycode;
  int count = 0;

  while (rp23xx_sb_read_fifo(&state, &keycode) == 0 && count < 16)
    {
      count++;
    }

  if (count > 0)
    {
      syslog(LOG_DEBUG, "KBD: Drained %d stale keys from FIFO\n", count);
    }

  dev->initialized = true;

  syslog(LOG_INFO, "KBD: STM32 keyboard ready (caps=%d, num=%d)\n",
         dev->capslock, dev->numlock);

  return 0;
}

/****************************************************************************
 * Name: rp23xx_keyboard_read
 *
 * Description:
 *   Read the next key event from the south bridge FIFO.
 *   Called by LVGL input driver at the scan rate.
 *
 *   Returns:
 *     keycode: ASCII or special code, 0 if no event
 *     *state:  SB_KEY_STATE_PRESSED/HELD/RELEASED
 *     *modifiers: modifier flags (if available)
 *
 ****************************************************************************/

uint8_t rp23xx_keyboard_read(uint8_t *modifiers)
{
  struct kbd_dev_s *dev = &g_kbddev;
  uint8_t state, keycode;

  if (!dev->initialized)
    {
      return KBD_KEY_NONE;
    }

  /* Read one event from FIFO */

  int ret = rp23xx_sb_read_fifo(&state, &keycode);
  if (ret < 0)
    {
      /* FIFO empty or error */

      return KBD_KEY_NONE;
    }

  /* Track modifier key state from press/release events.
   * The south bridge FIFO reports modifier keys like normal keys
   * with state = PRESSED/RELEASED. We accumulate the flags.
   */

  switch (keycode)
    {
      case KBD_KEY_LSHIFT:
      case KBD_KEY_RSHIFT:
        if (state == SB_KEY_STATE_PRESSED ||
            state == SB_KEY_STATE_HELD)
          {
            dev->modifiers |= KBD_MOD_SHIFT;
          }
        else
          {
            dev->modifiers &= ~KBD_MOD_SHIFT;
          }
        break;

      case KBD_KEY_CTRL:
        if (state == SB_KEY_STATE_PRESSED ||
            state == SB_KEY_STATE_HELD)
          {
            dev->modifiers |= KBD_MOD_CTRL;
          }
        else
          {
            dev->modifiers &= ~KBD_MOD_CTRL;
          }
        break;

      case KBD_KEY_ALT:
        if (state == SB_KEY_STATE_PRESSED ||
            state == SB_KEY_STATE_HELD)
          {
            dev->modifiers |= KBD_MOD_ALT;
          }
        else
          {
            dev->modifiers &= ~KBD_MOD_ALT;
          }
        break;

      case KBD_KEY_FN:
        if (state == SB_KEY_STATE_PRESSED ||
            state == SB_KEY_STATE_HELD)
          {
            dev->modifiers |= KBD_MOD_FN;
          }
        else
          {
            dev->modifiers &= ~KBD_MOD_FN;
          }
        break;

      default:
        break;
    }

  /* Only report pressed events for non-modifier keys to LVGL.
   * Modifier key events update the flags above but aren't
   * reported as separate keycodes.
   */

  if (state != SB_KEY_STATE_PRESSED)
    {
      return KBD_KEY_NONE;
    }

  if (keycode == KBD_KEY_LSHIFT || keycode == KBD_KEY_RSHIFT ||
      keycode == KBD_KEY_CTRL   || keycode == KBD_KEY_ALT    ||
      keycode == KBD_KEY_FN)
    {
      /* Modifier-only event — don't report as a printable key */

      return KBD_KEY_NONE;
    }

  /* Update capslock/numlock from key status register */

  uint8_t keystatus = rp23xx_sb_read_reg(SB_REG_KEY);
  dev->capslock = (keystatus & KBD_KEY_CAPSLOCK) != 0;
  dev->numlock  = (keystatus & KBD_KEY_NUMLOCK) != 0;

  if (modifiers != NULL)
    {
      *modifiers = dev->modifiers;
    }

  dev->last_key   = keycode;
  dev->last_state = state;

  return keycode;
}

/****************************************************************************
 * Name: rp23xx_keyboard_fifo_count
 *
 * Description:
 *   Return the number of pending key events in the FIFO.
 *
 ****************************************************************************/

int rp23xx_keyboard_fifo_count(void)
{
  if (!g_kbddev.initialized)
    {
      return 0;
    }

  uint8_t keystatus = rp23xx_sb_read_reg(SB_REG_KEY);
  return keystatus & KBD_KEY_FIFO_MASK;
}
