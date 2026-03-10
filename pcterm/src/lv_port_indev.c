/****************************************************************************
 * pcterm/src/lv_port_indev.c
 *
 * LVGL keyboard input port for NuttX — reads from STM32 south-bridge
 * keyboard via rp23xx_keyboard_read().
 *
 * The board code (rp23xx_keyboard.c) initialises the I2C keyboard and
 * provides rp23xx_keyboard_read() which returns ASCII / special key
 * codes from the south-bridge FIFO.  This port maps those codes to
 * LVGL key constants and feeds them into a keypad input device.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <syslog.h>

#include <lvgl/lvgl.h>

#include <arch/board/board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Key codes from official PicoCalc south-bridge firmware */

#define KBD_KEY_NONE        0x00
#define KBD_KEY_ESC         0xB1
#define KBD_KEY_LEFT        0xB4
#define KBD_KEY_UP          0xB5
#define KBD_KEY_DOWN        0xB6
#define KBD_KEY_RIGHT       0xB7
#define KBD_KEY_HOME        0xD2
#define KBD_KEY_END         0xD5
#define KBD_KEY_PGUP        0xD6
#define KBD_KEY_PGDN        0xD7
#define KBD_KEY_INSERT      0xD1
#define KBD_KEY_DELETE      0xD4
#define KBD_KEY_TAB         0x09
#define KBD_KEY_ENTER       0x0A
#define KBD_KEY_BACKSPACE   0x08

/* Function key codes from STM32 south-bridge keyboard firmware.
 * Physical keyboard has F1-F5 keys; Shift+F1-F5 produce F6-F10.
 * F11/F12 do not exist on this hardware.
 */

#define KBD_KEY_F1          0x81
#define KBD_KEY_F2          0x82
#define KBD_KEY_F3          0x83
#define KBD_KEY_F4          0x84
#define KBD_KEY_F5          0x85
#define KBD_KEY_F6          0x86
#define KBD_KEY_F7          0x87
#define KBD_KEY_F8          0x88
#define KBD_KEY_F9          0x89
#define KBD_KEY_F10         0x90

/* Custom LVGL key codes for function keys (0xF001+).
 * LVGL has no built-in F-key constants, so we define custom
 * codes in a range that doesn't collide with ASCII or LV_KEY_*.
 * Apps can listen for these via LV_EVENT_KEY.
 */

#define LV_KEY_F1           0xF001
#define LV_KEY_F2           0xF002
#define LV_KEY_F3           0xF003
#define LV_KEY_F4           0xF004
#define LV_KEY_F5           0xF005
#define LV_KEY_F6           0xF006
#define LV_KEY_F7           0xF007
#define LV_KEY_F8           0xF008
#define LV_KEY_F9           0xF009
#define LV_KEY_F10          0xF00A

/****************************************************************************
 * External References
 ****************************************************************************/

/* Provided by rp23xx_keyboard.c in board code */

extern uint8_t rp23xx_keyboard_read(uint8_t *modifiers);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_indev_t  *g_indev_keypad = NULL;
static lv_group_t  *g_default_group = NULL;
static volatile bool g_app_exit_request = false;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: map_key_to_lvgl
 *
 * Description:
 *   Convert a south-bridge key code to an LVGL key constant.
 *
 ****************************************************************************/

static uint32_t map_key_to_lvgl(uint8_t key)
{
  switch (key)
    {
      /* Navigation keys — official firmware codes */

      case KBD_KEY_UP:        return LV_KEY_UP;
      case KBD_KEY_DOWN:      return LV_KEY_DOWN;
      case KBD_KEY_LEFT:      return LV_KEY_LEFT;
      case KBD_KEY_RIGHT:     return LV_KEY_RIGHT;
      case KBD_KEY_ENTER:     return LV_KEY_ENTER;
      case KBD_KEY_ESC:       return LV_KEY_ESC;
      case KBD_KEY_BACKSPACE: return LV_KEY_BACKSPACE;
      case KBD_KEY_DELETE:    return LV_KEY_DEL;
      case KBD_KEY_HOME:      return LV_KEY_HOME;
      case KBD_KEY_END:       return LV_KEY_END;
      case KBD_KEY_INSERT:    return LV_KEY_HOME;
      case KBD_KEY_TAB:       return LV_KEY_NEXT;
      case KBD_KEY_PGUP:      return LV_KEY_PREV;
      case KBD_KEY_PGDN:      return LV_KEY_NEXT;

      /* Function keys F1-F10 from STM32 firmware */

      case KBD_KEY_F1:        return LV_KEY_F1;
      case KBD_KEY_F2:        return LV_KEY_F2;
      case KBD_KEY_F3:        return LV_KEY_F3;
      case KBD_KEY_F4:        return LV_KEY_F4;
      case KBD_KEY_F5:        return LV_KEY_F5;
      case KBD_KEY_F6:        return LV_KEY_F6;
      case KBD_KEY_F7:        return LV_KEY_F7;
      case KBD_KEY_F8:        return LV_KEY_F8;
      case KBD_KEY_F9:        return LV_KEY_F9;
      case KBD_KEY_F10:       return LV_KEY_F10;

      /* Alternate Enter/Esc codes */

      case 0x1B: return LV_KEY_ESC;
      case 0x0D: return LV_KEY_ENTER;

      default:
        /* Printable ASCII passes through as-is */
        if (key >= 0x20 && key < 0x7F)
          {
            return (uint32_t)key;
          }
        return 0;
    }
}

/****************************************************************************
 * Name: keypad_read_cb
 *
 * Description:
 *   LVGL keypad read callback.  Reads one key event from the board
 *   keyboard driver and translates it to LVGL format.
 *
 ****************************************************************************/

static void keypad_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
  static uint32_t last_key = 0;
  static bool     pending_release = false;

  /* If we sent a PRESSED last time, send RELEASED now */

  if (pending_release)
    {
      data->state = LV_INDEV_STATE_RELEASED;
      data->key   = last_key;
      pending_release = false;
      return;
    }

  /* Read a key from the south-bridge keyboard */

  uint8_t mods = 0;
  uint8_t raw  = rp23xx_keyboard_read(&mods);

  /* Fn + ESC = request app exit (back to launcher) */

  if (raw == KBD_KEY_ESC && (mods & 0x08))
    {
      g_app_exit_request = true;
      data->state = LV_INDEV_STATE_RELEASED;
      data->key   = 0;
      return;
    }

  if (raw == KBD_KEY_NONE)
    {
      data->state = LV_INDEV_STATE_RELEASED;
      data->key   = 0;
      return;
    }

  uint32_t lv_key = map_key_to_lvgl(raw);
  if (lv_key == 0)
    {
      data->state = LV_INDEV_STATE_RELEASED;
      data->key   = 0;
      return;
    }

  data->state = LV_INDEV_STATE_PRESSED;
  data->key   = lv_key;
  last_key    = lv_key;
  pending_release = true;

  /* Signal user activity to reset backlight timeout */

  rp23xx_backlight_activity();
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lv_port_indev_init
 *
 * Description:
 *   Create an LVGL keypad input device connected to the PicoCalc
 *   I2C keyboard.  Creates a default group for keyboard navigation.
 *
 ****************************************************************************/

void lv_port_indev_init(void)
{
  /* Create keypad input device */

  g_indev_keypad = lv_indev_create();
  lv_indev_set_type(g_indev_keypad, LV_INDEV_TYPE_KEYPAD);
  lv_indev_set_read_cb(g_indev_keypad, keypad_read_cb);

  /* Create and set default group for keyboard navigation */

  g_default_group = lv_group_create();
  lv_group_set_default(g_default_group);
  lv_group_set_wrap(g_default_group, true);
  lv_indev_set_group(g_indev_keypad, g_default_group);

  syslog(LOG_INFO, "indev: LVGL keypad input registered\n");
}

/****************************************************************************
 * Name: lv_port_indev_exit_requested
 *
 * Description:
 *   Returns true (and clears) the app-exit flag set by Fn+ESC.
 *
 ****************************************************************************/

bool lv_port_indev_exit_requested(void)
{
  if (g_app_exit_request)
    {
      g_app_exit_request = false;
      return true;
    }

  return false;
}
