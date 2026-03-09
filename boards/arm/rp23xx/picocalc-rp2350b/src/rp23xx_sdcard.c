/****************************************************************************
 * boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_sdcard.c
 *
 * SD card SPI driver and FAT32 mount for PicoCalc.
 * SD card is on SPI0.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <errno.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/mount.h>

#include <nuttx/mmcsd.h>
#include <nuttx/spi/spi.h>

#include <arch/board/board.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rp23xx_sdcard_mount
 *
 * Description:
 *   Mount the SD card at /mnt/sd after FAT driver registration.
 *   Called after SPI0 and MMCSD are initialized in bringup.
 *
 *   Returns OK on success, negative errno on failure.
 *
 ****************************************************************************/

int rp23xx_sdcard_mount(void)
{
  int ret;

  /* Create mount point directories (may already exist) */

  mkdir("/mnt", 0755);
  mkdir("/mnt/sd", 0755);

  /* The MMCSD SPI driver registers /dev/mmcsd0 during bringup.
   * Here we mount it as FAT32 at /mnt/sd.
   */

  ret = mount("/dev/mmcsd0", "/mnt/sd", "vfat", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SD: Failed to mount /dev/mmcsd0 at /mnt/sd: %d\n",
             ret);

      /* Try formatting as last resort? No — user data could be lost.
       * Just report the error. The OS will run in limited mode
       * without SD card access.
       */

      return ret;
    }

  syslog(LOG_INFO, "SD: Mounted FAT32 at /mnt/sd\n");

  /* Verify expected directory structure exists */

  struct stat st;
  const char *required_dirs[] = {
    "/mnt/sd/etc",
    "/mnt/sd/apps",
    "/mnt/sd/documents",
    "/mnt/sd/music",
    "/mnt/sd/video",
    "/mnt/sd/ssh",
    "/mnt/sd/etc/appstate",
    NULL
  };

  for (int i = 0; required_dirs[i] != NULL; i++)
    {
      if (stat(required_dirs[i], &st) < 0)
        {
          /* Create missing directory */
          mkdir(required_dirs[i], 0755);
          syslog(LOG_INFO, "SD: Created %s\n", required_dirs[i]);
        }
    }

  return 0;
}
