/****************************************************************************
 * boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_southbridge.c
 *
 * STM32F103R8T6 South Bridge I2C driver for PicoCalc.
 *
 * The south bridge manages:
 *   - 67-key keyboard matrix with key FIFO
 *   - AXP2101 battery PMIC (charge, percent, shutdown)
 *   - LCD backlight PWM (register 0x05, PA8 10 kHz)
 *   - Keyboard backlight PWM (register 0x0A, PC8 10 kHz)
 *   - PA (speaker amp) enable via PA14
 *   - Headphone detect via PC12 (auto-mutes PA)
 *   - Pico power enable via PA13
 *
 * I2C Protocol (slave address 0x1F):
 *   Write: [reg_id | 0x80, value]
 *   Read:  send [reg_id], read 2 bytes [echo, value]
 *   FIFO:  send [0x09], read 2 bytes [state, keycode]
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#include <nuttx/i2c/i2c_master.h>
#include <nuttx/mutex.h>

#include "rp23xx_i2c.h"
#include <arch/board/board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SB_I2C_RETRIES       3       /* Retry count for I2C errors */
#define SB_I2C_TIMEOUT_MS    50      /* I2C timeout per transfer */

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct sb_dev_s
{
  struct i2c_master_s *i2c;          /* I2C bus handle */
  mutex_t              lock;         /* Serializes access */
  bool                 initialized;  /* Init complete flag */
  uint8_t              bios_ver;     /* Cached BIOS version */
  uint8_t              lcd_bl;       /* Cached LCD backlight */
  uint8_t              kbd_bl;       /* Cached keyboard backlight */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct sb_dev_s g_sbdev;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sb_i2c_read
 *
 * Description:
 *   Read register from south bridge.
 *   Protocol: send [reg_id], then read 'len' bytes.
 *
 ****************************************************************************/

static int sb_i2c_read(struct sb_dev_s *dev, uint8_t reg,
                       uint8_t *buf, size_t len)
{
  struct i2c_msg_s msgs[2];
  int ret;

  /* Message 1: Write register address */

  msgs[0].frequency = BOARD_SB_I2C_FREQ;
  msgs[0].addr      = BOARD_SB_I2C_ADDR;
  msgs[0].flags     = 0;             /* Write */
  msgs[0].buffer    = &reg;
  msgs[0].length    = 1;

  /* Message 2: Read response */

  msgs[1].frequency = BOARD_SB_I2C_FREQ;
  msgs[1].addr      = BOARD_SB_I2C_ADDR;
  msgs[1].flags     = I2C_M_READ;
  msgs[1].buffer    = buf;
  msgs[1].length    = len;

  for (int retry = 0; retry < SB_I2C_RETRIES; retry++)
    {
      ret = I2C_TRANSFER(dev->i2c, msgs, 2);
      if (ret >= 0)
        {
          return 0;
        }

      /* Short delay before retry */

      up_udelay(100);
    }

  syslog(LOG_ERR, "SB: I2C read reg 0x%02X failed: %d\n", reg, ret);
  return ret;
}

/****************************************************************************
 * Name: sb_i2c_write
 *
 * Description:
 *   Write register to south bridge.
 *   Protocol: send [reg_id | 0x80, value]
 *
 ****************************************************************************/

static int sb_i2c_write(struct sb_dev_s *dev, uint8_t reg, uint8_t value)
{
  struct i2c_msg_s msg;
  uint8_t buf[2];
  int ret;

  buf[0] = reg | SB_WRITE_FLAG;
  buf[1] = value;

  msg.frequency = BOARD_SB_I2C_FREQ;
  msg.addr      = BOARD_SB_I2C_ADDR;
  msg.flags     = 0;             /* Write */
  msg.buffer    = buf;
  msg.length    = 2;

  for (int retry = 0; retry < SB_I2C_RETRIES; retry++)
    {
      ret = I2C_TRANSFER(dev->i2c, &msg, 1);
      if (ret >= 0)
        {
          return 0;
        }

      up_udelay(100);
    }

  syslog(LOG_ERR, "SB: I2C write reg 0x%02X=0x%02X failed: %d\n",
         reg, value, ret);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rp23xx_sb_init
 *
 * Description:
 *   Initialize the south bridge I2C connection.
 *   Verifies BIOS version and sets default backlight levels.
 *
 ****************************************************************************/

int rp23xx_sb_init(void)
{
  struct sb_dev_s *dev = &g_sbdev;
  int ret;

  if (dev->initialized)
    {
      return 0;
    }

  nxmutex_init(&dev->lock);

  /* Get I2C0 bus handle */

  dev->i2c = rp23xx_i2cbus_initialize(BOARD_SB_I2C_PORT);
  if (dev->i2c == NULL)
    {
      syslog(LOG_ERR, "SB: Failed to get I2C%d bus\n", BOARD_SB_I2C_PORT);
      return -ENODEV;
    }

  /* Read BIOS version to verify the south bridge is present */

  uint8_t verbuf[2];
  ret = sb_i2c_read(dev, SB_REG_VER, verbuf, 2);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SB: South bridge not responding on I2C%d:0x%02X\n",
             BOARD_SB_I2C_PORT, BOARD_SB_I2C_ADDR);
      return ret;
    }

  dev->bios_ver = verbuf[1];
  syslog(LOG_INFO, "SB: STM32 south bridge BIOS v0x%02X detected\n",
         dev->bios_ver);

  if (dev->bios_ver < SB_BIOS_VERSION)
    {
      syslog(LOG_WARNING, "SB: BIOS version 0x%02X is older than "
             "expected 0x%02X\n", dev->bios_ver, SB_BIOS_VERSION);
    }

  /* Enable keyboard interrupt reporting */

  ret = sb_i2c_write(dev, SB_REG_CFG,
                     SB_CFG_KEY_INT |
                     SB_CFG_USE_MODS |
                     SB_CFG_REPORT_MODS);
  if (ret < 0)
    {
      syslog(LOG_WARNING, "SB: Failed to set config: %d\n", ret);
    }

  /* Set LCD backlight to maximum initially */

  dev->lcd_bl = 255;
  sb_i2c_write(dev, SB_REG_BKL, 255);

  /* Set keyboard backlight off initially */

  dev->kbd_bl = 0;
  sb_i2c_write(dev, SB_REG_BK2, 0);

  dev->initialized = true;

  syslog(LOG_INFO, "SB: South bridge initialized (I2C%d:0x%02X)\n",
         BOARD_SB_I2C_PORT, BOARD_SB_I2C_ADDR);

  return 0;
}

/****************************************************************************
 * Name: rp23xx_sb_read_reg
 *
 * Description:
 *   Read a single register from the south bridge.
 *   Returns the second byte of the 2-byte response.
 *
 ****************************************************************************/

uint8_t rp23xx_sb_read_reg(uint8_t reg)
{
  struct sb_dev_s *dev = &g_sbdev;
  uint8_t buf[2] = { 0, 0 };

  if (!dev->initialized)
    {
      return 0;
    }

  nxmutex_lock(&dev->lock);
  sb_i2c_read(dev, reg, buf, 2);
  nxmutex_unlock(&dev->lock);

  return buf[1];
}

/****************************************************************************
 * Name: rp23xx_sb_write_reg
 *
 * Description:
 *   Write a value to a south bridge register.
 *
 ****************************************************************************/

int rp23xx_sb_write_reg(uint8_t reg, uint8_t value)
{
  struct sb_dev_s *dev = &g_sbdev;
  int ret;

  if (!dev->initialized)
    {
      return -ENODEV;
    }

  nxmutex_lock(&dev->lock);
  ret = sb_i2c_write(dev, reg, value);
  nxmutex_unlock(&dev->lock);

  return ret;
}

/****************************************************************************
 * Name: rp23xx_sb_read_fifo
 *
 * Description:
 *   Read one key event from the south bridge FIFO.
 *   Returns state (1=pressed, 2=held, 3=released) and keycode.
 *   Returns -EAGAIN if FIFO is empty (keycode == 0).
 *
 ****************************************************************************/

int rp23xx_sb_read_fifo(uint8_t *state, uint8_t *keycode)
{
  struct sb_dev_s *dev = &g_sbdev;
  uint8_t buf[2] = { 0, 0 };
  int ret;

  if (!dev->initialized)
    {
      return -ENODEV;
    }

  nxmutex_lock(&dev->lock);
  ret = sb_i2c_read(dev, SB_REG_FIF, buf, 2);
  nxmutex_unlock(&dev->lock);

  if (ret < 0)
    {
      return ret;
    }

  *state   = buf[0];
  *keycode = buf[1];

  if (buf[1] == 0)
    {
      return -EAGAIN;  /* FIFO empty */
    }

  return 0;
}

/****************************************************************************
 * Name: rp23xx_sb_get_battery
 *
 * Description:
 *   Read battery percentage and charging status from AXP2101 PMIC
 *   (via south bridge register 0x0B).
 *
 *   Returns:
 *     percent:  0-100%
 *     charging: true if charging, false if discharging
 *
 ****************************************************************************/

int rp23xx_sb_get_battery(uint8_t *percent, bool *charging)
{
  struct sb_dev_s *dev = &g_sbdev;
  uint8_t buf[2] = { 0, 0 };
  int ret;

  if (!dev->initialized)
    {
      return -ENODEV;
    }

  nxmutex_lock(&dev->lock);
  ret = sb_i2c_read(dev, SB_REG_BAT, buf, 2);
  nxmutex_unlock(&dev->lock);

  if (ret < 0)
    {
      return ret;
    }

  if (percent != NULL)
    {
      *percent = buf[1] & SB_BAT_PERCENT_MASK;
    }

  if (charging != NULL)
    {
      *charging = (buf[1] & SB_BAT_CHARGING) != 0;
    }

  return 0;
}

/****************************************************************************
 * Name: rp23xx_sb_set_lcd_backlight
 *
 * Description:
 *   Set LCD backlight brightness (0-255).
 *   The south bridge drives PA8 PWM at 10 kHz.
 *
 ****************************************************************************/

int rp23xx_sb_set_lcd_backlight(uint8_t brightness)
{
  struct sb_dev_s *dev = &g_sbdev;
  int ret;

  if (!dev->initialized)
    {
      return -ENODEV;
    }

  nxmutex_lock(&dev->lock);
  ret = sb_i2c_write(dev, SB_REG_BKL, brightness);
  if (ret >= 0)
    {
      dev->lcd_bl = brightness;
    }

  nxmutex_unlock(&dev->lock);
  return ret;
}

/****************************************************************************
 * Name: rp23xx_sb_set_kbd_backlight
 *
 * Description:
 *   Set keyboard backlight brightness (0-255).
 *   The south bridge drives PC8 PWM at 10 kHz.
 *
 ****************************************************************************/

int rp23xx_sb_set_kbd_backlight(uint8_t brightness)
{
  struct sb_dev_s *dev = &g_sbdev;
  int ret;

  if (!dev->initialized)
    {
      return -ENODEV;
    }

  nxmutex_lock(&dev->lock);
  ret = sb_i2c_write(dev, SB_REG_BK2, brightness);
  if (ret >= 0)
    {
      dev->kbd_bl = brightness;
    }

  nxmutex_unlock(&dev->lock);
  return ret;
}

/****************************************************************************
 * Name: rp23xx_sb_power_off
 *
 * Description:
 *   Request system power off via south bridge.
 *   The south bridge calls AXP2101 PMU.shutdown() after 'delay_secs'.
 *   delay_secs = 0: immediate shutdown
 *
 ****************************************************************************/

int rp23xx_sb_power_off(uint8_t delay_secs)
{
  struct sb_dev_s *dev = &g_sbdev;

  if (!dev->initialized)
    {
      return -ENODEV;
    }

  syslog(LOG_NOTICE, "SB: Power off requested (delay=%d sec)\n",
         delay_secs);

  /* No lock needed — this is a one-way operation */

  return sb_i2c_write(dev, SB_REG_OFF, delay_secs);
}

/****************************************************************************
 * Name: rp23xx_sb_get_version
 *
 * Description:
 *   Return the cached south bridge BIOS version.
 *
 ****************************************************************************/

uint8_t rp23xx_sb_get_version(void)
{
  return g_sbdev.bios_ver;
}
