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

#ifdef CONFIG_RP23XX_FLASH_FILE_SYSTEM
#include <nuttx/mtd/mtd.h>
#include "rp23xx_flash_mtd.h"
#endif

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
      syslog(LOG_ERR, "spi spi0: failed to initialize master\n");
      return -ENODEV;
    }

  syslog(LOG_INFO, "spi spi0: master registered "
         "(SCK=GP%d MOSI=GP%d MISO=GP%d CS=GP%d)\n",
         BOARD_SD_PIN_SCK, BOARD_SD_PIN_MOSI,
         BOARD_SD_PIN_MISO, BOARD_SD_PIN_CS);

#ifdef CONFIG_MMCSD_SPI
  ret = mmcsd_spislotinitialize(0, 0, spi0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "mmc mmc0: failed to bind SPI0 to MMCSD slot: %d\n",
             ret);
    }
  else
    {
      syslog(LOG_INFO, "mmc mmc0: SD card on SPI0 @ %d MHz as /dev/mmcsd0\n",
             BOARD_SD_SPI_FREQ / 1000000);
    }
#endif
#endif

  /* Initialize SPI1 for display — handled by LCD driver registration */

#ifdef CONFIG_RP23XX_SPI1
  syslog(LOG_INFO, "spi spi1: master registered "
         "(SCK=GP%d MOSI=GP%d MISO=GP%d CS=GP%d)\n",
         BOARD_LCD_PIN_SCK, BOARD_LCD_PIN_MOSI,
         BOARD_LCD_PIN_MISO, BOARD_LCD_PIN_CS);
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
      syslog(LOG_ERR, "i2c i2c1: failed to initialize bus\n");
      return -ENODEV;
    }

  syslog(LOG_INFO, "i2c i2c1: bus initialized at %d kHz "
         "(SDA=GP%d SCL=GP%d)\n",
         BOARD_SB_I2C_FREQ / 1000,
         BOARD_KBD_PIN_SDA, BOARD_KBD_PIN_SCL);

  /* Register I2C1 as /dev/i2c1 for debug access */

#ifdef CONFIG_I2C_DRIVER
  i2c_register(i2c1, 1);
  syslog(LOG_INFO, "i2c i2c1: registered as /dev/i2c1\n");
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

  syslog(LOG_INFO, "picocalc: PicoCalc-Term on Waveshare RP2350B-Plus-W\n");
  syslog(LOG_INFO, "picocalc: CPU: RP2350B dual Cortex-M33 @ %d MHz\n",
         BOARD_SYS_FREQ / 1000000);
  syslog(LOG_INFO, "picocalc: Memory: 520 KB SRAM, "
         "XOSC %d MHz, USB %d MHz\n",
         BOARD_XOSC_FREQ / 1000000, BOARD_USB_FREQ / 1000000);
  syslog(LOG_INFO, "picocalc: UART0 console at %d baud "
         "(TX=GP%d RX=GP%d)\n",
         BOARD_UART0_BAUD, BOARD_UART0_PIN_TX, BOARD_UART0_PIN_RX);

  /* --- PSRAM heap setup --- */

#ifdef CONFIG_PICOCALC_PSRAM
  extern int rp23xx_psram_init(void);
  ret = rp23xx_psram_init();
  if (ret < 0)
    {
      syslog(LOG_ERR, "psram: initialization failed (err %d)\n", ret);
    }
#endif

  /* --- SPI buses (SD card, display) --- */

  ret = rp23xx_bringup_spi();
  if (ret < 0)
    {
      syslog(LOG_ERR, "picocalc: SPI bus bringup failed: %d\n", ret);
    }

  /* --- I2C buses (keyboard) --- */

  ret = rp23xx_bringup_i2c();
  if (ret < 0)
    {
      syslog(LOG_ERR, "picocalc: I2C bus bringup failed: %d\n", ret);
    }

  /* --- STM32 South Bridge (must be before LCD and keyboard) --- */

#ifdef CONFIG_RP23XX_I2C1
  syslog(LOG_INFO, "southbridge: probing STM32 on I2C%d:0x%02X...\n",
         BOARD_SB_I2C_PORT, BOARD_SB_I2C_ADDR);
  ret = rp23xx_sb_init();
  if (ret < 0)
    {
      syslog(LOG_ERR, "southbridge: init failed: %d\n", ret);
    }
#endif

  /* --- Framebuffer (ST7365P LCD) --- */

#ifdef CONFIG_PICOCALC_LCD_ST7365P
  syslog(LOG_INFO, "fb0: initializing ST7365P on SPI%d...\n",
         BOARD_LCD_SPI_PORT);
  extern int rp23xx_lcd_initialize(void);
  ret = rp23xx_lcd_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "fb0: LCD initialization failed: %d\n", ret);
    }
#endif

  /* --- Keyboard input driver --- */

#ifdef CONFIG_PICOCALC_KEYBOARD
  syslog(LOG_INFO, "input: initializing keyboard driver...\n");
  extern int rp23xx_keyboard_initialize(void);
  ret = rp23xx_keyboard_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "input: keyboard initialization failed: %d\n", ret);
    }
#endif

  /* --- PWM Audio --- */

#ifdef CONFIG_PICOCALC_AUDIO_PWM
  extern int rp23xx_audio_initialize(void);
  ret = rp23xx_audio_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "audio: PWM initialization failed: %d\n", ret);
    }
#endif

  /* --- Mount SD card filesystem --- */

#ifdef CONFIG_FS_FAT
  syslog(LOG_INFO, "vfat: FAT filesystem support enabled\n");
#endif

  /* --- PIO SDIO (optional, replaces SPI-mode SD card) --- */

#ifdef CONFIG_PICOCALC_PIO_SDIO
  extern int rp23xx_pio_sdio_initialize(void);
  ret = rp23xx_pio_sdio_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "sdio: PIO SDIO initialization failed: %d\n", ret);
    }
  else
    {
      syslog(LOG_INFO, "sdio: PIO1 1-bit SDIO ready "
             "(CLK=GP%d CMD=GP%d DAT0=GP%d)\n",
             BOARD_SDIO_PIN_CLK, BOARD_SDIO_PIN_CMD,
             BOARD_SDIO_PIN_DAT0);
    }
#endif

  /* --- Mount procfs filesystem --- */

#ifdef CONFIG_RP23XX_FLASH_FILE_SYSTEM
  /* --- Internal flash filesystem --- */
  syslog(LOG_INFO, "flash: initializing internal MTD...\n");
  {
    struct mtd_dev_s *flash_mtd = rp23xx_flash_mtd_initialize();
    if (flash_mtd == NULL)
      {
        syslog(LOG_ERR, "flash: MTD initialization failed\n");
      }
    else
      {
        syslog(LOG_INFO, "flash: MTD device initialized\n");

#ifdef CONFIG_FS_LITTLEFS
        /* Register as MTD device and mount LittleFS */

        ret = register_mtddriver("/dev/flash0", flash_mtd, 0755, NULL);
        if (ret < 0)
          {
            syslog(LOG_ERR, "flash: register_mtddriver failed: %d\n", ret);
          }
        else
          {
            const char *mp = CONFIG_RP23XX_FLASH_MOUNT_POINT;
            if (mp != NULL && mp[0] != '\0')
              {
                mkdir(mp, 0777);
                ret = mount("/dev/flash0", mp, "littlefs", 0, NULL);
                if (ret < 0)
                  {
                    /* First mount fails → format and retry */

                    syslog(LOG_WARNING,
                           "flash: mount failed, formatting LittleFS...\n");

                    ret = mount("/dev/flash0", mp, "littlefs", 0,
                                "autoformat");
                    if (ret < 0)
                      {
                        syslog(LOG_ERR,
                               "flash: LittleFS format+mount failed: %d\n",
                               ret);
                      }
                    else
                      {
                        syslog(LOG_INFO,
                               "flash: LittleFS formatted and mounted at %s\n",
                               mp);
                      }
                  }
                else
                  {
                    syslog(LOG_INFO, "flash: LittleFS mounted at %s\n", mp);
                  }
              }
          }
#elif defined(CONFIG_FS_SMARTFS)
        ret = smart_initialize(0, flash_mtd, NULL);
        if (ret < 0)
          {
            syslog(LOG_ERR, "flash: SmartFS initialization failed: %d\n",
                   ret);
          }
#endif /* CONFIG_FS_LITTLEFS / CONFIG_FS_SMARTFS */
      }
  }
#endif /* CONFIG_RP23XX_FLASH_FILE_SYSTEM */

#ifdef CONFIG_FS_PROCFS
  mkdir("/proc", 0755);
  ret = mount(NULL, "/proc", "procfs", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "procfs: mount at /proc failed: %d\n", ret);
    }
  else
    {
      syslog(LOG_INFO, "procfs: mounted at /proc\n");
    }
#endif

  syslog(LOG_INFO, "picocalc: board bring-up complete\n");

  return 0;
}
