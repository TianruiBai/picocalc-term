/****************************************************************************
 * pcterm/src/lv_port_disp.c
 *
 * LVGL display port — direct SPI flush to ST7365P LCD.
 *
 * LVGL renders into a 25 KB partial render buffer (320×40 RGB565).
 * The flush callback calls rp23xx_lcd_flush_area() which converts
 * RGB565→RGB666 one row at a time and pushes to the panel via SPI.
 *
 * No NuttX /dev/fb0 device or 200 KB RAM framebuffer is used,
 * saving critical heap space on the 520 KB SRAM RP2350B.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#include <lvgl/lvgl.h>
#include <arch/board/board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DISP_BUF_ROWS        40   /* Rows per partial render buffer */

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Render buffer — sized for partial rendering (40 rows × 320 × 2B) */

static uint8_t g_disp_buf[BOARD_LCD_WIDTH * DISP_BUF_ROWS * 2]
  __attribute__((aligned(4)));

/****************************************************************************
 * External Functions (board LCD driver)
 ****************************************************************************/

extern int rp23xx_lcd_flush_area(uint16_t x1, uint16_t y1,
                                 uint16_t x2, uint16_t y2,
                                 const uint16_t *data,
                                 uint16_t src_stride);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: disp_flush_cb
 *
 * Description:
 *   LVGL flush callback.  Sends the rendered area directly to the LCD
 *   panel via SPI (RGB565→RGB666 conversion in the board driver).
 *
 ****************************************************************************/

static void disp_flush_cb(lv_display_t *disp,
                           const lv_area_t *area,
                           uint8_t *px_map)
{
  uint16_t x1 = (uint16_t)area->x1;
  uint16_t y1 = (uint16_t)area->y1;
  uint16_t x2 = (uint16_t)area->x2;
  uint16_t y2 = (uint16_t)area->y2;
  uint16_t src_w = x2 - x1 + 1;

  rp23xx_lcd_flush_area(x1, y1, x2, y2,
                        (const uint16_t *)px_map,
                        src_w);

  lv_display_flush_ready(disp);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lv_port_disp_init
 *
 * Description:
 *   Create an LVGL display that renders directly to the LCD panel.
 *
 ****************************************************************************/

int lv_port_disp_init(void)
{
  syslog(LOG_INFO, "disp: direct SPI flush mode (%dx%d, %d-row buffer)\n",
         BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT, DISP_BUF_ROWS);

  /* Create LVGL display */

  lv_display_t *disp = lv_display_create(BOARD_LCD_WIDTH,
                                          BOARD_LCD_HEIGHT);
  if (disp == NULL)
    {
      syslog(LOG_ERR, "disp: lv_display_create failed\n");
      return -ENOMEM;
    }

  lv_display_set_flush_cb(disp, disp_flush_cb);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

  lv_display_set_buffers(disp,
                         g_disp_buf, NULL,
                         sizeof(g_disp_buf),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  syslog(LOG_INFO, "disp: LVGL display created (%dx%d, "
         "render buf %u bytes)\n",
         BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT,
         (unsigned)sizeof(g_disp_buf));

  return 0;
}
