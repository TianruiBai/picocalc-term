/****************************************************************************
 * boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_lcd.c
 *
 * ILI9488-compatible LCD driver for PicoCalc.
 * Registers as /dev/fb0 framebuffer device.
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
#include <nuttx/spi/spi.h>
#include <nuttx/video/fb.h>

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

/* Framebuffer size */
#define LCD_STRIDE          (BOARD_LCD_WIDTH * sizeof(uint16_t))
#define LCD_FB_SIZE         (BOARD_LCD_WIDTH * BOARD_LCD_HEIGHT * sizeof(uint16_t))

/* Partial buffer for DMA transfers (in SRAM)
 * Uses 3 bytes per pixel (RGB666) for SPI output format.
 */
#define LCD_PARTIAL_LINES   10
#define LCD_PARTIAL_SIZE    (BOARD_LCD_WIDTH * LCD_PARTIAL_LINES * 3)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct st7365p_dev_s
{
  struct fb_vtable_s vtable;        /* Framebuffer interface */
  struct fb_videoinfo_s vinfo;      /* Video info */
  struct fb_planeinfo_s pinfo;      /* Plane info */
  struct spi_dev_s *spi;            /* SPI bus handle */
  uint16_t *framebuffer;            /* Full framebuffer in PSRAM (RGB565) */
  uint8_t  *partial_buf;            /* Partial buffer in SRAM (RGB666, 3B/px) */
  bool initialized;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct st7365p_dev_s g_lcddev;

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

static void lcd_send_cmd(struct st7365p_dev_s *dev, uint8_t cmd)
{
  lcd_set_dc(false);  /* Command mode */
  SPI_SELECT(dev->spi, SPIDEV_DISPLAY(0), true);
  SPI_SEND(dev->spi, cmd);
  SPI_SELECT(dev->spi, SPIDEV_DISPLAY(0), false);
}

static void lcd_send_data(struct st7365p_dev_s *dev,
                          const uint8_t *data, size_t len)
{
  lcd_set_dc(true);   /* Data mode */
  SPI_SELECT(dev->spi, SPIDEV_DISPLAY(0), true);
  SPI_SNDBLOCK(dev->spi, data, len);
  SPI_SELECT(dev->spi, SPIDEV_DISPLAY(0), false);
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

  /* Turn on backlight via south bridge (will silently fail if
   * south bridge is not yet initialized) */

  rp23xx_sb_set_lcd_backlight(255);
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
 * Name: lcd_flush
 *
 * Description:
 *   Flush the full framebuffer (PSRAM, RGB565) to the display via SPI.
 *   Converts RGB565→RGB666 in chunks through the SRAM partial buffer.
 *
 ****************************************************************************/

static void lcd_flush(struct st7365p_dev_s *dev)
{
  uint16_t y;

  lcd_set_window(dev, 0, 0,
                 BOARD_LCD_WIDTH - 1, BOARD_LCD_HEIGHT - 1);
  lcd_send_cmd(dev, ST7365P_RAMWR);

  lcd_set_dc(true);
  SPI_SELECT(dev->spi, SPIDEV_DISPLAY(0), true);

  for (y = 0; y < BOARD_LCD_HEIGHT; y += LCD_PARTIAL_LINES)
    {
      uint16_t lines = LCD_PARTIAL_LINES;
      if (y + lines > BOARD_LCD_HEIGHT)
        {
          lines = BOARD_LCD_HEIGHT - y;
        }

      size_t pixels = BOARD_LCD_WIDTH * lines;

      /* Convert RGB565 → RGB666 in SRAM partial buffer */

      lcd_rgb565_to_rgb666(dev->partial_buf,
                           &dev->framebuffer[y * BOARD_LCD_WIDTH],
                           pixels);

      /* DMA-capable SPI send (3 bytes per pixel) */

      SPI_SNDBLOCK(dev->spi, dev->partial_buf, pixels * 3);
    }

  SPI_SELECT(dev->spi, SPIDEV_DISPLAY(0), false);
}

/****************************************************************************
 * Framebuffer Interface Methods
 ****************************************************************************/

static int lcd_getvideoinfo(struct fb_vtable_s *vtable,
                            struct fb_videoinfo_s *vinfo)
{
  struct st7365p_dev_s *dev = (struct st7365p_dev_s *)vtable;
  memcpy(vinfo, &dev->vinfo, sizeof(struct fb_videoinfo_s));
  return 0;
}

static int lcd_getplaneinfo(struct fb_vtable_s *vtable,
                            int planeno,
                            struct fb_planeinfo_s *pinfo)
{
  struct st7365p_dev_s *dev = (struct st7365p_dev_s *)vtable;
  if (planeno != 0)
    {
      return -EINVAL;
    }
  memcpy(pinfo, &dev->pinfo, sizeof(struct fb_planeinfo_s));
  return 0;
}

#ifdef CONFIG_FB_UPDATE
static int lcd_updatearea(struct fb_vtable_s *vtable,
                          const struct fb_area_s *area)
{
  struct st7365p_dev_s *dev = (struct st7365p_dev_s *)vtable;

  /* Partial update: flush only the dirty rectangle rows.
   * We flush full rows for the affected row range to keep
   * the SPI transfer simple (row-aligned).
   */

  uint16_t y0 = area->y;
  uint16_t y1 = area->y + area->h - 1;

  if (y0 >= BOARD_LCD_HEIGHT) return 0;
  if (y1 >= BOARD_LCD_HEIGHT) y1 = BOARD_LCD_HEIGHT - 1;

  lcd_set_window(dev, 0, y0, BOARD_LCD_WIDTH - 1, y1);
  lcd_send_cmd(dev, ST7365P_RAMWR);

  lcd_set_dc(true);
  SPI_SELECT(dev->spi, SPIDEV_DISPLAY(0), true);

  for (uint16_t y = y0; y <= y1; y += LCD_PARTIAL_LINES)
    {
      uint16_t lines = LCD_PARTIAL_LINES;
      if (y + lines > y1 + 1)
        {
          lines = y1 - y + 1;
        }

      size_t pixels = BOARD_LCD_WIDTH * lines;

      /* Convert RGB565 → RGB666 in SRAM partial buffer */

      lcd_rgb565_to_rgb666(dev->partial_buf,
                           &dev->framebuffer[y * BOARD_LCD_WIDTH],
                           pixels);

      SPI_SNDBLOCK(dev->spi, dev->partial_buf, pixels * 3);
    }

  SPI_SELECT(dev->spi, SPIDEV_DISPLAY(0), false);
  return 0;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rp23xx_lcd_initialize
 *
 * Description:
 *   Initialize the ST7365P display and register as /dev/fb0.
 *   Allocates framebuffer in PSRAM and partial buffer in SRAM.
 *
 ****************************************************************************/

int rp23xx_lcd_initialize(void)
{
  struct st7365p_dev_s *dev = &g_lcddev;
  int ret;

  if (dev->initialized)
    {
      return 0;
    }

  /* Get SPI1 bus handle */

  dev->spi = rp23xx_spibus_initialize(BOARD_LCD_SPI_PORT);
  if (dev->spi == NULL)
    {
      syslog(LOG_ERR, "LCD: Failed to get SPI%d bus\n",
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

  /* Note: LCD backlight is controlled by the STM32 south bridge
   * (register SB_REG_BKL, PA8 PWM). No direct GPIO control.
   * Brightness is set during lcd_init_sequence() via
   * rp23xx_sb_set_lcd_backlight(255).
   */

  /* Allocate framebuffer in SRAM (directly addressable).
   *
   * The framebuffer MUST be in directly-accessible memory because:
   *   - LVGL/NuttX fb interface exposes fbmem for direct CPU writes
   *   - lcd_flush() reads pixels from framebuffer every display refresh
   *   - PSRAM handles (via PIO-SPI) are NOT dereferenceable pointers
   *
   * 320×320×2 = 200 KB — fits in RP2350B's 520 KB SRAM.
   * PSRAM should be reserved for large, bulk-accessed data
   * (editor buffers, file caches) accessed via psram_memcpy_to/from.
   */

  dev->framebuffer = kmm_zalloc(LCD_FB_SIZE);
  if (dev->framebuffer == NULL)
    {
      syslog(LOG_ERR, "LCD: Failed to allocate framebuffer (%d bytes)\n",
             LCD_FB_SIZE);
      return -ENOMEM;
    }

  syslog(LOG_INFO, "LCD: Framebuffer allocated in SRAM (%d bytes)\n",
         LCD_FB_SIZE);

  /* Allocate partial transfer buffer in SRAM (RGB666, 3 bytes/pixel) */

  dev->partial_buf = (uint8_t *)kmm_malloc(LCD_PARTIAL_SIZE);
  if (dev->partial_buf == NULL)
    {
      syslog(LOG_ERR, "LCD: Failed to allocate partial buffer\n");
      return -ENOMEM;
    }

  /* Run LCD init sequence */

  lcd_init_sequence(dev);

  /* Fill video info */

  dev->vinfo.fmt     = FB_FMT_RGB16_565;
  dev->vinfo.xres    = BOARD_LCD_WIDTH;
  dev->vinfo.yres    = BOARD_LCD_HEIGHT;
  dev->vinfo.nplanes = 1;

  dev->pinfo.fbmem   = (void *)dev->framebuffer;
  dev->pinfo.fblen   = LCD_FB_SIZE;
  dev->pinfo.stride  = LCD_STRIDE;
  dev->pinfo.display = 0;
  dev->pinfo.bpp     = BOARD_LCD_BPP;

  /* Fill vtable */

  dev->vtable.getvideoinfo = lcd_getvideoinfo;
  dev->vtable.getplaneinfo = lcd_getplaneinfo;
#ifdef CONFIG_FB_UPDATE
  dev->vtable.updatearea   = lcd_updatearea;
#endif

  /* Register as /dev/fb0 */

  ret = fb_register_device(0, 0, (struct fb_vtable_s *)dev);
  if (ret < 0)
    {
      syslog(LOG_ERR, "LCD: Failed to register /dev/fb0: %d\n", ret);
      return ret;
    }

  dev->initialized = true;

  /* Clear screen to black */

  memset(dev->framebuffer, 0, LCD_FB_SIZE);
  lcd_flush(dev);

  syslog(LOG_INFO, "LCD: ST7365P %dx%d RGB565 ready\n",
         BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT);

  return 0;
}
