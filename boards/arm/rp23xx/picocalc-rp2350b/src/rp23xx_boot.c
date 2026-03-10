/****************************************************************************
 * boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_boot.c
 *
 * Early board initialization for PicoCalc RP2350B-Plus-W.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/board.h>
#include <debug.h>

#include <arch/board/board.h>
#include "rp23xx_gpio.h"

/****************************************************************************
 * Name: rp23xx_boardearlyinitialize
 *
 * Description:
 *   Very early board initialization — called before OS is fully started.
 *   Configure only essential pins and clocks here.
 *
 ****************************************************************************/

void rp23xx_boardearlyinitialize(void)
{
  /* Configure GPIO pins for SPI1 (display)
   *
   * SCK, MOSI, MISO use hardware SPI function.
   * CS is software-controlled GPIO (not hardware SPI CSn) because:
   *  - The common rp23xx_spi1select() toggles CS via rp23xx_gpio_put()
   *  - We need explicit control to hold CS across multi-byte sequences
   *  - CONFIG_RP23XX_SPI1_CS_GPIO must match BOARD_LCD_PIN_CS (GP9)
   */

  rp23xx_gpio_set_function(BOARD_LCD_PIN_SCK,  RP23XX_GPIO_FUNC_SPI);
  rp23xx_gpio_set_function(BOARD_LCD_PIN_MOSI, RP23XX_GPIO_FUNC_SPI);
  rp23xx_gpio_set_function(BOARD_LCD_PIN_MISO, RP23XX_GPIO_FUNC_SPI);

  /* LCD CS as GPIO output, default HIGH (deasserted) */

  rp23xx_gpio_init(BOARD_LCD_PIN_CS);
  rp23xx_gpio_setdir(BOARD_LCD_PIN_CS, true);
  rp23xx_gpio_put(BOARD_LCD_PIN_CS, true);

  /* LCD control pins (DC, RST) as GPIO outputs.
   * Backlight is controlled via south bridge I2C (SB_REG_BKL), not GPIO. */

  rp23xx_gpio_init(BOARD_LCD_PIN_DC);
  rp23xx_gpio_setdir(BOARD_LCD_PIN_DC, true);

  rp23xx_gpio_init(BOARD_LCD_PIN_RST);
  rp23xx_gpio_setdir(BOARD_LCD_PIN_RST, true);

  /* Configure GPIO pins for SPI0 (SD card) */

  rp23xx_gpio_set_function(BOARD_SD_PIN_SCK,  RP23XX_GPIO_FUNC_SPI);
  rp23xx_gpio_set_function(BOARD_SD_PIN_MOSI, RP23XX_GPIO_FUNC_SPI);
  rp23xx_gpio_set_function(BOARD_SD_PIN_MISO, RP23XX_GPIO_FUNC_SPI);
  rp23xx_gpio_set_function(BOARD_SD_PIN_CS,   RP23XX_GPIO_FUNC_SPI);

  /* Note: PicoCalc has no SD card detect pin; card is always assumed present */

  /* Configure GPIO pins for I2C1 (keyboard south bridge)
   * GPIO 6/7 are I2C1 in the RP2350 hardware mux */

  rp23xx_gpio_set_function(BOARD_KBD_PIN_SDA, RP23XX_GPIO_FUNC_I2C);
  rp23xx_gpio_set_function(BOARD_KBD_PIN_SCL, RP23XX_GPIO_FUNC_I2C);

  /* Keyboard interrupt pin as input */

  rp23xx_gpio_init(BOARD_KBD_PIN_INT);
  rp23xx_gpio_setdir(BOARD_KBD_PIN_INT, false);

  /* Configure UART0 for debug console */

  rp23xx_gpio_set_function(BOARD_UART0_PIN_TX, RP23XX_GPIO_FUNC_UART);
  rp23xx_gpio_set_function(BOARD_UART0_PIN_RX, RP23XX_GPIO_FUNC_UART);

  /* Configure audio PWM pins */

  rp23xx_gpio_set_function(BOARD_AUDIO_PIN_LEFT,  RP23XX_GPIO_FUNC_PWM);
  rp23xx_gpio_set_function(BOARD_AUDIO_PIN_RIGHT, RP23XX_GPIO_FUNC_PWM);
}

/****************************************************************************
 * Name: rp23xx_boardinitialize
 *
 * Description:
 *   Called after early init. Initialize PSRAM and other hardware that needs
 *   to be ready before the OS scheduler starts.
 *
 ****************************************************************************/

void rp23xx_boardinitialize(void)
{
  /* PSRAM initialization is deferred to rp23xx_bringup() (board_late_initialize)
   * because it needs OS primitives (mutex, syslog) that are not available here.
   */
}

/****************************************************************************
 * Name: board_late_initialize
 *
 * Description:
 *   Called after OS primitives are available. Bring up all peripherals.
 *
 ****************************************************************************/

#ifdef CONFIG_BOARD_LATE_INITIALIZE
void board_late_initialize(void)
{
  extern int rp23xx_bringup(void);
  rp23xx_bringup();
}
#endif
