/****************************************************************************
 * boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_flash_mtd.h
 *
 * Flash MTD driver for RP2350 — provides read/write access to the
 * internal QSPI flash region that lies beyond the firmware binary.
 * Used by LittleFS mounted at /flash.
 *
 ****************************************************************************/

#ifndef __BOARDS_ARM_RP23XX_PICOCALC_RP2350B_SRC_RP23XX_FLASH_MTD_H
#define __BOARDS_ARM_RP23XX_PICOCALC_RP2350B_SRC_RP23XX_FLASH_MTD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/mtd/mtd.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: rp23xx_flash_mtd_initialize
 *
 * Description:
 *   Create an MTD device backed by the unused portion of the on-board
 *   QSPI flash (everything after the firmware binary, aligned to 4 KB).
 *
 * Returned Value:
 *   A pointer to an MTD device, or NULL on failure (errno is set).
 *
 ****************************************************************************/

struct mtd_dev_s *rp23xx_flash_mtd_initialize(void);

#endif /* __BOARDS_ARM_RP23XX_PICOCALC_RP2350B_SRC_RP23XX_FLASH_MTD_H */
