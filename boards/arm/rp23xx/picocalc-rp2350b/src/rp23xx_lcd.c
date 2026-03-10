/****************************************************************************
 * boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_lcd.c
 *
 * ILI9488-compatible LCD driver for PicoCalc.
 * Direct SPI output — no framebuffer allocation (saves 200 KB SRAM).
 *
 * LVGL renders into its own 25 KB partial buffer and calls
 * rp23xx_lcd_flush_area() which converts RGB565→RGB666 row-by-row
 * through a tiny 960-byte static buffer and pushes to the panel.
 *
 * Display: 320×320 IPS, RGB666 over SPI (3 bytes/pixel), SPI1 @ 25 MHz
 * Backlight: controlled by STM32 south bridge register SB_REG_BKL
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/spi/spi.h>

#include "rp23xx_spi.h"
#include "rp23xx_gpio.h"
#include <arch/board/board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* ST7365P command set (ILI9488-compatible subset) */

#define ST7365P_NOP         0x00
#define ST7365P_SWRESET     0x01
#define ST7365P_SLPOUT      0x11
#define ST7365P_INVOFF      0x20
#define ST7365P_INVON       0x21
#define ST7365P_DISPOFF     0x28
#define ST7365P_DISPON      0x29
#define ST7365P_CASET       0x2A  /* Column address set */
#define ST7365P_RASET       0x2B  /* Row address set */
#define ST7365P_RAMWR       0x2C  /* Memory write */
#define ST7365P_MADCTL      0x36  /* Memory access control */
#define ST7365P_COLMOD      0x3A  /* Interface pixel format */
#define ST7365P_PGAMMA      0xE0
#define ST7365P_NGAMMA      0xE1

/* MADCTL bits */
#define MADCTL_MY           0x80  /* Row order */
#define MADCTL_MX           0x40  /* Column order */
#define MADCTL_MV           0x20  /* Row/col exchange */
#define MADCTL_ML           0x10  /* Scan order */
#define MADCTL_BGR          0x08  /* BGR pixel order */
#define MADCTL_MH           0x04  /* Horizontal order */

/* Static transfer buffer: 1 row of RGB666 (320 × 3 = 960 bytes).
 * Used by rp23xx_lcd_flush_area() to convert one row at a time.
 */

#define LCD_TRANSFER_LINES  1
#define LCD_TRANSFER_SIZE   (BOARD_LCD_WIDTH * LCD_TRANSFER_LINES * 3)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct st7365p_dev_s
{
  struct spi_dev_s *spi;            /* SPI bus handle */
  bool initialized;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct st7365p_dev_s g_lcddev;
static uint8_t g_lcd_transfer_buf[LCD_TRANSFER_SIZE];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lcd_send_cmd / lcd_send_data
 ****************************************************************************/

static void lcd_set_dc(bool data)
{
  rp23xx_gpio_put(BOARD_LCD_PIN_DC, data);
}

static void lcd_set_cs(bool selected)
{
  /* LCD CS is active-low */

  rp23xx_gpio_put(BOARD_LCD_PIN_CS, !selected);
}

static void lcd_send_cmd(struct st7365p_dev_s *dev, uint8_t cmd)
{
  lcd_set_dc(false);  /* Command mode */
  lcd_set_cs(true);
  SPI_SEND(dev->spi, cmd);
  lcd_set_cs(false);
}

static void lcd_send_data(struct st7365p_dev_s *dev,
                          const uint8_t *data, size_t len)
{
  lcd_set_dc(true);   /* Data mode */
  lcd_set_cs(true);
  SPI_SNDBLOCK(dev->spi, data, len);
  lcd_set_cs(false);
}

static void lcd_send_cmd_data(struct st7365p_dev_s *dev,
                              uint8_t cmd,
                              const uint8_t *params, size_t len)
{
  lcd_send_cmd(dev, cmd);
  if (len > 0)
    {
      lcd_send_data(dev, params, len);
    }
}

/****************************************************************************
 * Name: lcd_set_window
 *
 * Description:
 *   Set the column and row address window for the next RAMWR.
 *
 ****************************************************************************/

static void lcd_set_window(struct st7365p_dev_s *dev,
                           uint16_t x0, uint16_t y0,
                           uint16_t x1, uint16_t y1)
{
  uint8_t caset[4] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
  uint8_t raset[4] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };

  lcd_send_cmd_data(dev, ST7365P_CASET, caset, 4);
  lcd_send_cmd_data(dev, ST7365P_RASET, raset, 4);
}

/****************************************************************************
 * Name: lcd_init_sequence
 *
 * Description:
 *   Send the ST7365P initialization command sequence.
 *
 ****************************************************************************/

static void lcd_init_sequence(struct st7365p_dev_s *dev)
{
  /* Hardware reset */

  rp23xx_gpio_put(BOARD_LCD_PIN_RST, true);
  up_mdelay(10);
  rp23xx_gpio_put(BOARD_LCD_PIN_RST, false);
  up_mdelay(10);
  rp23xx_gpio_put(BOARD_LCD_PIN_RST, true);
  up_mdelay(120);

  /* Full ILI9488 init sequence from PicoCalc reference code.
   * All commands match the official picocalc_lvgl_graphics_demo.
   */

  /* Positive Gamma Correction */

  {
    uint8_t p[] = { 0x00, 0x03, 0x09, 0x08, 0x16, 0x0A,
                    0x3F, 0x78, 0x4C, 0x09, 0x0A, 0x08,
                    0x16, 0x1A, 0x0F };
    lcd_send_cmd_data(dev, 0xE0, p, 15);
  }

  /* Negative Gamma Correction */

  {
    uint8_t p[] = { 0x00, 0x16, 0x19, 0x03, 0x0F, 0x05,
                    0x32, 0x45, 0x46, 0x04, 0x0E, 0x0D,
                    0x35, 0x37, 0x0F };
    lcd_send_cmd_data(dev, 0xE1, p, 15);
  }

  /* Power Control 1 */

  {
    uint8_t p[] = { 0x17, 0x15 };
    lcd_send_cmd_data(dev, 0xC0, p, 2);
  }

  /* Power Control 2 */

  {
    uint8_t p[] = { 0x41 };
    lcd_send_cmd_data(dev, 0xC1, p, 1);
  }

  /* VCOM Control */

  {
    uint8_t p[] = { 0x00, 0x12, 0x80 };
    lcd_send_cmd_data(dev, 0xC5, p, 3);
  }

  /* Memory Access Control: MX | BGR for portrait orientation */

  {
    uint8_t p[] = { BOARD_LCD_MADCTL };  /* 0x48 = MX | BGR */
    lcd_send_cmd_data(dev, ST7365P_MADCTL, p, 1);
  }

  /* Interface Pixel Format: RGB666 (18-bit) for SPI interface
   * The ILI9488 only supports 18-bit over SPI (3 bytes per pixel).
   */

  {
    uint8_t p[] = { 0x66 };
    lcd_send_cmd_data(dev, ST7365P_COLMOD, p, 1);
  }

  /* Interface Mode Control */

  {
    uint8_t p[] = { 0x00 };
    lcd_send_cmd_data(dev, 0xB0, p, 1);
  }

  /* Frame Rate Control (normal mode): 60 Hz */

  {
    uint8_t p[] = { 0xA0 };
    lcd_send_cmd_data(dev, 0xB1, p, 1);
  }

  /* Display Inversion ON (required for IPS panel) */

#if BOARD_LCD_INVERSION
  lcd_send_cmd(dev, ST7365P_INVON);
#else
  lcd_send_cmd(dev, ST7365P_INVOFF);
#endif

  /* Display Inversion Control: 2-dot inversion */

  {
    uint8_t p[] = { 0x02 };
    lcd_send_cmd_data(dev, 0xB4, p, 1);
  }

  /* Display Function Control */

  {
    uint8_t p[] = { 0x02, 0x02, 0x3B };
    lcd_send_cmd_data(dev, 0xB6, p, 3);
  }

  /* Entry Mode Set */

  {
    uint8_t p[] = { 0xC6 };
    lcd_send_cmd_data(dev, 0xB7, p, 1);
  }

  /* Undocumented register 0xE9 */

  {
    uint8_t p[] = { 0x00 };
    lcd_send_cmd_data(dev, 0xE9, p, 1);
  }

  /* Adjust Control 3 */

  {
    uint8_t p[] = { 0xA9, 0x51, 0x2C, 0x82 };
    lcd_send_cmd_data(dev, 0xF7, p, 4);
  }

  /* Exit Sleep Mode */

  lcd_send_cmd(dev, ST7365P_SLPOUT);
  up_mdelay(120);

  /* Display ON */

  lcd_send_cmd(dev, ST7365P_DISPON);
  up_mdelay(120);

  /* Set MADCTL again (portrait) — matches reference */

  {
    uint8_t p[] = { BOARD_LCD_MADCTL };
    lcd_send_cmd_data(dev, ST7365P_MADCTL, p, 1);
  }

  /* Turn on backlight via south bridge */

  {
    int bl_ret = rp23xx_sb_set_lcd_backlight(255);
    if (bl_ret < 0)
      {
        syslog(LOG_WARNING, "fb0: backlight set failed (%d) — "
               "display may appear blank\n", bl_ret);
      }
  }
}

/****************************************************************************
 * Name: lcd_rgb565_to_rgb666
 *
 * Description:
 *   Convert a row of RGB565 pixels to RGB666 (3 bytes per pixel).
 *   ILI9488 SPI interface only supports 18-bit color.
 *
 ****************************************************************************/

static void lcd_rgb565_to_rgb666(uint8_t *dst, const uint16_t *src,
                                 size_t pixel_count)
{
  for (size_t i = 0; i < pixel_count; i++)
    {
      uint16_t px = src[i];
      uint8_t r = (px >> 11) & 0x1F;  /* 5 bits */
      uint8_t g = (px >> 5)  & 0x3F;  /* 6 bits */
      uint8_t b = px & 0x1F;          /* 5 bits */

      /* Scale to 8-bit then mask to 6-bit (top 6 bits) */

      dst[i * 3 + 0] = (r << 3) | (r >> 2);  /* R: 5→8 bit */
      dst[i * 3 + 1] = (g << 2) | (g >> 4);  /* G: 6→8 bit */
      dst[i * 3 + 2] = (b << 3) | (b >> 2);  /* B: 5→8 bit */
    }
}

/****************************************************************************
 * Name: lcd_clear_screen
 *
 * Description:
 *   Fill the display with black pixels via SPI.
 *
 ****************************************************************************/

static void lcd_clear_screen(struct st7365p_dev_s *dev)
{
  lcd_set_window(dev, 0, 0,
                 BOARD_LCD_WIDTH - 1, BOARD_LCD_HEIGHT - 1);
  lcd_send_cmd(dev, ST7365P_RAMWR);

  lcd_set_dc(true);
  lcd_set_cs(true);

  /* Zero the static transfer buffer and send one row at a time */

  memset(g_lcd_transfer_buf, 0, LCD_TRANSFER_SIZE);

  for (uint16_t y = 0; y < BOARD_LCD_HEIGHT; y++)
    {
      SPI_SNDBLOCK(dev->spi, g_lcd_transfer_buf, LCD_TRANSFER_SIZE);
    }

  lcd_set_cs(false);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rp23xx_lcd_initialize
 *
 * Description:
 *   Initialize the ST7365P display (SPI setup + panel init commands).
 *   No framebuffer is allocated — LVGL drives the panel directly via
 *   rp23xx_lcd_flush_area().
 *
 ****************************************************************************/

int rp23xx_lcd_initialize(void)
{
  struct st7365p_dev_s *dev = &g_lcddev;

  if (dev->initialized)
    {
      return 0;
    }

  syslog(LOG_INFO, "lcd: initializing ST7365P on SPI%d...\n",
         BOARD_LCD_SPI_PORT);

  /* Get SPI1 bus handle */

  dev->spi = rp23xx_spibus_initialize(BOARD_LCD_SPI_PORT);
  if (dev->spi == NULL)
    {
      syslog(LOG_ERR, "lcd: failed to get SPI%d bus\n",
             BOARD_LCD_SPI_PORT);
      return -ENODEV;
    }

  /* Configure SPI for the display */

  SPI_SETFREQUENCY(dev->spi, BOARD_LCD_SPI_FREQ);
  SPI_SETBITS(dev->spi, 8);
  SPI_SETMODE(dev->spi, SPIDEV_MODE0);

  /* Configure GPIO for DC, RST */

  rp23xx_gpio_init(BOARD_LCD_PIN_DC);
  rp23xx_gpio_setdir(BOARD_LCD_PIN_DC, true);

  rp23xx_gpio_init(BOARD_LCD_PIN_RST);
  rp23xx_gpio_setdir(BOARD_LCD_PIN_RST, true);

  /* LCD CS as GPIO output, default deasserted */

  rp23xx_gpio_init(BOARD_LCD_PIN_CS);
  rp23xx_gpio_setdir(BOARD_LCD_PIN_CS, true);
  lcd_set_cs(false);

  /* Run LCD init sequence (reset, configure registers, sleep out, display on) */

  lcd_init_sequence(dev);

  dev->initialized = true;

  /* Clear screen to black */

  lcd_clear_screen(dev);

  syslog(LOG_INFO, "lcd: ST7365P %dx%d IPS on SPI%d @ %d MHz "
         "(DC=GP%d RST=GP%d CS=GP%d)\n",
         BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT,
         BOARD_LCD_SPI_PORT, BOARD_LCD_SPI_FREQ / 1000000,
         BOARD_LCD_PIN_DC, BOARD_LCD_PIN_RST, BOARD_LCD_PIN_CS);
  syslog(LOG_INFO, "lcd: no framebuffer — direct SPI flush "
         "(saves 200 KB SRAM)\n");

  return 0;
}

/****************************************************************************
 * Name: rp23xx_lcd_flush_area
 *
 * Description:
 *   Write a rectangular area of RGB565 pixels to the LCD panel.
 *   Converts RGB565→RGB666 one row at a time through the 960-byte
 *   static transfer buffer.
 *
 *   Called from the LVGL display flush callback — this is the fast path.
 *
 * Input Parameters:
 *   x1, y1, x2, y2 - Bounding rectangle (inclusive)
 *   data            - RGB565 pixel array, row-major
 *   src_stride      - Pixels per row in the source data
 *
 ****************************************************************************/

int rp23xx_lcd_flush_area(uint16_t x1, uint16_t y1,
                          uint16_t x2, uint16_t y2,
                          const uint16_t *data,
                          uint16_t src_stride)
{
  struct st7365p_dev_s *dev = &g_lcddev;

  if (!dev->initialized || dev->spi == NULL)
    {
      return -ENODEV;
    }

  /* Clip to display bounds */

  if (x2 >= BOARD_LCD_WIDTH)  x2 = BOARD_LCD_WIDTH - 1;
  if (y2 >= BOARD_LCD_HEIGHT) y2 = BOARD_LCD_HEIGHT - 1;
  if (x1 > x2 || y1 > y2)    return 0;

  uint16_t w = x2 - x1 + 1;
  uint16_t h = y2 - y1 + 1;

  /* Set the LCD address window */

  lcd_set_window(dev, x1, y1, x2, y2);
  lcd_send_cmd(dev, ST7365P_RAMWR);

  lcd_set_dc(true);
  lcd_set_cs(true);

  /* Convert and send one row at a time through the static buffer.
   * The buffer is 960 bytes = 320 pixels × 3 bytes (one full row).
   * For sub-width areas, each row uses only w × 3 bytes.
   */

  for (uint16_t row = 0; row < h; row++)
    {
      const uint16_t *src = &data[row * src_stride];
      lcd_rgb565_to_rgb666(g_lcd_transfer_buf, src, w);
      SPI_SNDBLOCK(dev->spi, g_lcd_transfer_buf, (size_t)w * 3);
    }

  lcd_set_cs(false);
  return 0;
}
