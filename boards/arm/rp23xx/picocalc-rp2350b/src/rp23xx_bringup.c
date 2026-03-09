/****************************************************************************
 * boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_bringup.c
 *
 * Board-specific peripheral initialization for PicoCalc RP2350B-Plus-W.
 * Called from NuttX board_app_initialize().
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <syslog.h>
#include <errno.h>

#include <nuttx/board.h>
#include <nuttx/spi/spi.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/mmcsd.h>

#include <sys/mount.h>
#include <sys/stat.h>

#include "rp23xx_spi.h"
#include "rp23xx_i2c.h"

#include <arch/board/board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rp23xx_bringup_spi
 *
 * Description:
 *   Initialize SPI buses and register SPI-attached devices.
 *
 *   SPI0 -> SD card
 *   SPI1 -> ST7365P display
 *
 ****************************************************************************/

static int rp23xx_bringup_spi(void)
{
  int ret = 0;

  /* Initialize SPI0 for SD card (only when PIO SDIO is not used,
   * since they share GP16/GP18/GP19)
   */

#if defined(CONFIG_RP23XX_SPI0) && !defined(CONFIG_PICOCALC_PIO_SDIO)
  struct spi_dev_s *spi0 = rp23xx_spibus_initialize(0);
  if (spi0 == NULL)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize SPI0\n");
      return -ENODEV;
    }

#ifdef CONFIG_MMCSD_SPI
  ret = mmcsd_spislotinitialize(0, 0, spi0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to bind SPI0 to MMCSD: %d\n", ret);
    }
  else
    {
      syslog(LOG_INFO, "SD card initialized on SPI0\n");
    }
#endif
#endif

  /* Initialize SPI1 for display — handled by LCD driver registration */

#ifdef CONFIG_RP23XX_SPI1
  syslog(LOG_INFO, "SPI1 available for display driver\n");
#endif

  return ret;
}

/****************************************************************************
 * Name: rp23xx_bringup_i2c
 *
 * Description:
 *   Initialize I2C buses.
 *
 *   I2C1 -> Keyboard (STM32 south-bridge at 0x1F)
 *          GPIO 6/7 are I2C1 in the RP2350 hardware mux
 *
 ****************************************************************************/

static int rp23xx_bringup_i2c(void)
{
#ifdef CONFIG_RP23XX_I2C1
  struct i2c_master_s *i2c1 = rp23xx_i2cbus_initialize(1);
  if (i2c1 == NULL)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize I2C1\n");
      return -ENODEV;
    }

  syslog(LOG_INFO, "I2C1 initialized for keyboard (0x%02X)\n",
         BOARD_KBD_I2C_ADDR);

  /* Register I2C1 as /dev/i2c1 for debug access */

#ifdef CONFIG_I2C_DRIVER
  i2c_register(i2c1, 1);
#endif
#endif

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rp23xx_bringup
 *
 * Description:
 *   Perform architecture-specific initialization.
 *
 *   CONFIG_BOARD_LATE_INITIALIZE=y:
 *     Called from board_late_initialize().
 *
 *   CONFIG_BOARD_LATE_INITIALIZE is not set:
 *     Called from board_app_initialize().
 *
 ****************************************************************************/

int rp23xx_bringup(void)
{
  int ret;

  syslog(LOG_INFO, "PicoCalc RP2350B bring-up starting...\n");

  /* --- PSRAM heap setup --- */

#ifdef CONFIG_PICOCALC_PSRAM
  extern int rp23xx_psram_init(void);
  ret = rp23xx_psram_init();
  if (ret < 0)
    {
      syslog(LOG_ERR, "PSRAM: Initialization failed: %d\n", ret);
    }
  else
    {
      syslog(LOG_INFO, "PSRAM: %d MB initialized\n",
             BOARD_PSRAM_SIZE / (1024 * 1024));
    }
#endif

  /* --- SPI buses (SD card, display) --- */

  ret = rp23xx_bringup_spi();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: SPI bringup failed: %d\n", ret);
    }

  /* --- I2C buses (keyboard) --- */

  ret = rp23xx_bringup_i2c();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: I2C bringup failed: %d\n", ret);
    }

  /* --- STM32 South Bridge (must be before LCD and keyboard) --- */

#ifdef CONFIG_RP23XX_I2C1
  ret = rp23xx_sb_init();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: South bridge init failed: %d\n", ret);
    }
  else
    {
      syslog(LOG_INFO, "South bridge: STM32 @ I2C1:0x%02X v0x%02X\n",
             BOARD_SB_I2C_ADDR, rp23xx_sb_get_version());
    }
#endif

  /* --- Framebuffer (ST7365P LCD) --- */

#ifdef CONFIG_PICOCALC_LCD_ST7365P
  extern int rp23xx_lcd_initialize(void);
  ret = rp23xx_lcd_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: LCD init failed: %d\n", ret);
    }
  else
    {
      syslog(LOG_INFO, "LCD: %dx%d framebuffer registered at /dev/fb0\n",
             BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT);
    }
#endif

  /* --- Keyboard input driver --- */

#ifdef CONFIG_PICOCALC_KEYBOARD
  extern int rp23xx_keyboard_initialize(void);
  ret = rp23xx_keyboard_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Keyboard init failed: %d\n", ret);
    }
  else
    {
      syslog(LOG_INFO, "Keyboard: I2C input driver registered\n");
    }
#endif

  /* --- PWM Audio --- */

#ifdef CONFIG_PICOCALC_AUDIO_PWM
  extern int rp23xx_audio_initialize(void);
  ret = rp23xx_audio_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Audio init failed: %d\n", ret);
    }
  else
    {
      syslog(LOG_INFO, "Audio: PWM driver registered\n");
    }
#endif

  /* --- Mount SD card filesystem --- */

#ifdef CONFIG_FS_FAT
  /* SD card auto-mount handled by NSH or explicit mount */
  syslog(LOG_INFO, "FAT filesystem support enabled\n");
#endif

  /* --- PIO SDIO (optional, replaces SPI-mode SD card) --- */

#ifdef CONFIG_PICOCALC_PIO_SDIO
  extern int rp23xx_pio_sdio_initialize(void);
  ret = rp23xx_pio_sdio_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: PIO SDIO init failed: %d\n", ret);
    }
  else
    {
      syslog(LOG_INFO, "SDIO: PIO 1-bit mode ready "
             "(CLK=GP%d, CMD=GP%d, DAT0=GP%d)\n",
             BOARD_SDIO_PIN_CLK, BOARD_SDIO_PIN_CMD,
             BOARD_SDIO_PIN_DAT0);
    }
#endif

  /* --- Mount procfs filesystem --- */

#ifdef CONFIG_FS_PROCFS
  mkdir("/proc", 0755);
  ret = mount(NULL, "/proc", "procfs", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to mount procfs at /proc: %d\n", ret);
    }
  else
    {
      syslog(LOG_INFO, "procfs mounted at /proc\n");
    }
#endif

  syslog(LOG_INFO, "PicoCalc RP2350B bring-up complete\n");

  return 0;
}
