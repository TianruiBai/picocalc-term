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
#include "rp23xx_gpio.h"

#include <arch/board/board.h>

#ifdef CONFIG_IEEE80211_INFINEON_CYW43439
#include "rp23xx_cyw43439.h"
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_IEEE80211_INFINEON_CYW43439
static gspi_dev_t *g_cyw43439;
#endif

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
  /* Defer audio hardware/ring-buffer initialization until first real audio
   * use (startup chime/media playback). This keeps boot resilient when SRAM
   * is tight and PSRAM is unavailable.
   */
  syslog(LOG_INFO, "audio: deferred initialization (lazy-start)\n");
#endif

  /* --- Mount SD card filesystem --- */

#ifdef CONFIG_MMCSD
  mkdir("/mnt", 0777);
  mkdir("/mnt/sd", 0777);

  /* Check GPIO22 card-detect (active-low: 0 = card present) */

  rp23xx_gpio_init(BOARD_SD_PIN_DET);
  rp23xx_gpio_setdir(BOARD_SD_PIN_DET, false);   /* input */
  rp23xx_gpio_set_pulls(BOARD_SD_PIN_DET, 1, 0);  /* pull-up */
  up_udelay(10);  /* settle */

  bool sd_present = (rp23xx_gpio_get(BOARD_SD_PIN_DET) == 0);
  syslog(LOG_INFO, "vfat: SD detect GP%d = %d (%s)\n",
         BOARD_SD_PIN_DET,
         rp23xx_gpio_get(BOARD_SD_PIN_DET),
         sd_present ? "card inserted" : "no card");

  if (sd_present)
    {
      ret = mount("/dev/mmcsd0", "/mnt/sd", "vfat", 0, NULL);
      if (ret < 0)
        {
          syslog(LOG_WARNING, "vfat: SD card mount at /mnt/sd failed: %d\n",
                 ret);
        }
      else
        {
          syslog(LOG_INFO, "vfat: SD card mounted at /mnt/sd\n");
        }
    }
  else
    {
      syslog(LOG_INFO, "vfat: no SD card detected (GP%d high)\n",
             BOARD_SD_PIN_DET);
    }
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

  /* --- CYW43439 Wi-Fi (Broadcom FullMAC over GSPI) --- */

#if defined(CONFIG_PICOCALC_WIFI) && defined(CONFIG_IEEE80211_INFINEON_CYW43439)
  g_cyw43439 = rp23xx_cyw_setup(BOARD_CYW43_PIN_WL_ON,
                                BOARD_CYW43_PIN_WL_CS,
                                BOARD_CYW43_PIN_WL_D,
                                BOARD_CYW43_PIN_WL_CLK,
                                BOARD_CYW43_PIN_WL_D);
  if (g_cyw43439 == NULL)
    {
      ret = errno;
      syslog(LOG_ERR, "wifi: CYW43439 setup failed: %d\n", ret);
    }
  else
    {
      syslog(LOG_INFO, "wifi: CYW43439 ready "
             "(WL_ON=GP%d WL_D=GP%d WL_CS=GP%d WL_CLK=GP%d)\n",
             BOARD_CYW43_PIN_WL_ON,
             BOARD_CYW43_PIN_WL_D,
             BOARD_CYW43_PIN_WL_CS,
             BOARD_CYW43_PIN_WL_CLK);
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
                           "flash: mount failed, formatting LittleFS "
                           "(this may take a few minutes)...\n");

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

  /* --- Mount BINFS at /bin (built-in apps as executable files) ---
   *
   * BINFS exposes every NuttX built-in application (ls, cat, nsh, sh,
   * vi, lua, etc.) as an executable file under /bin.  Combined with
   * CONFIG_NSH_FILE_APPS=y and CONFIG_PATH_INITIAL="/bin:/usr/bin",
   * this lets users and scripts reference programs by absolute path
   * just like a real Unix system:  /bin/sh, /bin/ls, /bin/lua, etc.
   */

#ifdef CONFIG_FS_BINFS
  mkdir("/bin", 0755);
  ret = mount(NULL, "/bin", "binfs", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "binfs: mount at /bin failed: %d\n", ret);
    }
  else
    {
      syslog(LOG_INFO, "binfs: mounted at /bin "
             "(built-in apps as executables)\n");
    }
#endif

  /* --- Mount TMPFS at /tmp (volatile RAM filesystem) ---
   *
   * Provides a RAM-backed scratch area that does not wear flash.
   * Cleared on every reboot, just like /tmp on Linux.
   */

#ifdef CONFIG_FS_TMPFS
  mkdir("/tmp", 0755);
  ret = mount(NULL, "/tmp", "tmpfs", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "tmpfs: mount at /tmp failed: %d\n", ret);
    }
  else
    {
      syslog(LOG_INFO, "tmpfs: mounted at /tmp\n");
    }
#endif

  /* --- Buildroot-like directory tree on internal flash ---
   *
   * Create a Unix-like hierarchy under /flash/ so the system
   * feels like a small embedded Linux.  Top-level symlinks
   * (when CONFIG_PSEUDOFS_SOFTLINKS=y) let users use familiar
   * paths like /etc, /home, /sbin directly.
   *
   * /bin  = BINFS mount (built-in apps — see above)
   * /tmp  = TMPFS mount (RAM-backed volatile — see above)
   * /proc = procfs mount (kernel info — see above)
   * Other standard paths are symlinked to /flash/ equivalents.
   */

#ifdef CONFIG_RP23XX_FLASH_FILE_SYSTEM
  {
    static const char * const dirs[] =
      {
        "/flash/sbin",
        "/flash/lib",
        "/flash/etc",
        "/flash/etc/eux",
        "/flash/etc/ssh",
        "/flash/etc/wifi",
        "/flash/etc/appstate",
        "/flash/home",
        "/flash/home/user",
        "/flash/home/user/.config",
        "/flash/usr",
        "/flash/usr/lib",
        "/flash/usr/share",
        "/flash/var",
        "/flash/var/log",
        "/flash/var/cache",
        "/flash/var/tmp",
      };

    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
      {
        mkdir(dirs[i], 0755);
      }

    syslog(LOG_INFO, "rootfs: buildroot directory tree created on /flash\n");

    /* Create top-level symlinks for Unix-like root paths.
     *
     * /bin and /tmp are NOT symlinked — they are real mounts above
     * (BINFS and TMPFS respectively).  /usr/bin → /bin so that
     * PATH="/bin:/usr/bin" finds executables in both places.
     */

#ifdef CONFIG_PSEUDOFS_SOFTLINKS
    static const struct
    {
      const char *link;
      const char *target;
    } links[] =
      {
        { "/sbin", "/flash/sbin" },
        { "/lib",  "/flash/lib"  },
        { "/etc",  "/flash/etc"  },
        { "/home", "/flash/home" },
        { "/usr",  "/flash/usr"  },
        { "/var",  "/flash/var"  },
      };

    for (size_t i = 0; i < sizeof(links) / sizeof(links[0]); i++)
      {
        /* symlink() is safe to call repeatedly — fails harmlessly
         * if the link already exists in the pseudo-filesystem.
         */

        symlink(links[i].target, links[i].link);
      }

    /* /usr/bin → /bin so PATH lookup finds BINFS executables
     * via either /bin/foo or /usr/bin/foo.
     */

    symlink("/bin", "/flash/usr/bin");

    syslog(LOG_INFO,
           "rootfs: top-level symlinks created "
           "(/sbin /lib /etc /home /usr /var; "
           "/usr/bin -> /bin)\n");
#endif /* CONFIG_PSEUDOFS_SOFTLINKS */
  }
#endif /* CONFIG_RP23XX_FLASH_FILE_SYSTEM */

  syslog(LOG_INFO, "eux: board bring-up complete\n");

  return 0;
}
