/****************************************************************************
 * boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_aonrtc.c
 *
 * RP2350 always-on timer time source helper.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <errno.h>
#include <time.h>

#include <nuttx/arch.h>

#include "arm_internal.h"
#include "hardware/rp23xx_memorymap.h"

#define RP23XX_POWMAN_READ_TIME_UPPER \
  (RP23XX_POWMAN_BASE + 0x70)
#define RP23XX_POWMAN_READ_TIME_LOWER \
  (RP23XX_POWMAN_BASE + 0x74)

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rp23xx_aon_timer_gettime
 *
 * Description:
 *   Read current RP2350 POWMAN always-on timer value and return it as a
 *   timespec. The hardware counter is exposed as a 64-bit value via
 *   READ_TIME_UPPER/READ_TIME_LOWER and is treated as microseconds.
 *
 ****************************************************************************/

int rp23xx_aon_timer_gettime(struct timespec *ts)
{
  uint32_t upper1;
  uint32_t upper2;
  uint32_t lower;
  uint64_t ticks_us;

  if (ts == NULL)
    {
      return -EINVAL;
    }

  do
    {
      upper1 = getreg32(RP23XX_POWMAN_READ_TIME_UPPER);
      lower  = getreg32(RP23XX_POWMAN_READ_TIME_LOWER);
      upper2 = getreg32(RP23XX_POWMAN_READ_TIME_UPPER);
    }
  while (upper1 != upper2);

  ticks_us = ((uint64_t)upper2 << 32) | (uint64_t)lower;

  ts->tv_sec  = (time_t)(ticks_us / 1000000ULL);
  ts->tv_nsec = (long)((ticks_us % 1000000ULL) * 1000ULL);
  return 0;
}
