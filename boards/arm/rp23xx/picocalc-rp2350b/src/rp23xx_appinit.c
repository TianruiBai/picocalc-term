/****************************************************************************
 * boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_appinit.c
 *
 * Application initialization entry point.
 * Called by NuttX after board_app_initialize().
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/board.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_app_initialize
 *
 * Description:
 *   Perform application-specific initialization. This is called from the
 *   NSH or custom startup code.
 *
 *   Input Parameters:
 *     arg - The boardctl() argument (not used here)
 *
 *   Returned Value:
 *     Zero (OK) is returned on success.
 *
 ****************************************************************************/

#ifdef CONFIG_BOARDCTL
int board_app_initialize(uintptr_t arg)
{
#ifndef CONFIG_BOARD_LATE_INITIALIZE
  /* If late init is not enabled, do bringup here */
  extern int rp23xx_bringup(void);
  return rp23xx_bringup();
#else
  return 0;
#endif
}
#endif
