/****************************************************************************
 * pcterm/src/lv_port_disp.c
 *
 * LVGL display port for NuttX — connects LVGL to /dev/fb0 framebuffer.
 *
 * The board code (rp23xx_lcd.c) registers a 320x320 RGB565 framebuffer
 * device at /dev/fb0.  This port opens it, obtains the framebuffer
 * pointer via FBIOGET_PLANEINFO, and creates an LVGL display that
 * renders into the buffer.  After each flush the FB_UPDATE ioctl
 * triggers the SPI transfer to the physical display.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <sys/ioctl.h>

#include <nuttx/video/fb.h>
#include <lvgl/lvgl.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define FB_DEV_PATH          "/dev/fb0"
#define DISP_BUF_ROWS        40   /* Rows per partial render buffer */

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int                    g_fb_fd   = -1;
static uint16_t              *g_fb_mem  = NULL;
static struct fb_videoinfo_s  g_vinfo;
static struct fb_planeinfo_s  g_pinfo;

/* Render buffer — sized for partial rendering (40 rows × 320 × 2B) */

static uint8_t g_disp_buf[320 * DISP_BUF_ROWS * 2]
  __attribute__((aligned(4)));

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: disp_flush_cb
 *
 * Description:
 *   LVGL flush callback.  Copies the rendered area from the LVGL draw
 *   buffer into the framebuffer, then asks the driver to refresh.
 *
 ****************************************************************************/

static void disp_flush_cb(lv_display_t *disp,
                           const lv_area_t *area,
                           uint8_t *px_map)
{
  int32_t src_w;
  int32_t x1;
  int32_t y1;
  int32_t x2;
  int32_t y2;
  int32_t y;
  uint32_t bpp;

  if (g_fb_mem == NULL)
    {
      lv_display_flush_ready(disp);
      return;
    }

  bpp = g_pinfo.bpp / 8;
  if (bpp == 0)
    {
      bpp = 2;
    }

  src_w = area->x2 - area->x1 + 1;

  /* Clip flush rectangle to framebuffer bounds */

  x1 = area->x1;
  y1 = area->y1;
  x2 = area->x2;
  y2 = area->y2;

  if (x1 < 0) x1 = 0;
  if (y1 < 0) y1 = 0;
  if (x2 >= (int32_t)g_vinfo.xres) x2 = g_vinfo.xres - 1;
  if (y2 >= (int32_t)g_vinfo.yres) y2 = g_vinfo.yres - 1;

  if (x1 > x2 || y1 > y2)
    {
      lv_display_flush_ready(disp);
      return;
    }

  /* Copy each row from LVGL buffer to framebuffer */

  for (y = y1; y <= y2; y++)
    {
      uint8_t *dst = ((uint8_t *)g_fb_mem) +
                     (size_t)y * g_pinfo.stride +
                     (size_t)x1 * bpp;

      uint8_t *src = px_map +
                     ((size_t)(y - area->y1) * src_w +
                      (size_t)(x1 - area->x1)) * bpp;

      memcpy(dst, src, (size_t)(x2 - x1 + 1) * bpp);
    }

#ifdef CONFIG_FB_UPDATE
  /* Tell the framebuffer driver to push the dirty area to the LCD */

  struct fb_area_s fb_area;
  fb_area.x = x1;
  fb_area.y = y1;
  fb_area.w = x2 - x1 + 1;
  fb_area.h = y2 - y1 + 1;

  int ret = ioctl(g_fb_fd, FBIO_UPDATE,
                  (unsigned long)((uintptr_t)&fb_area));
  if (ret < 0)
    {
      syslog(LOG_ERR, "disp: FBIO_UPDATE failed: %d\n", errno);
    }
#endif

  lv_display_flush_ready(disp);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lv_port_disp_init
 *
 * Description:
 *   Open /dev/fb0, obtain the framebuffer memory pointer, and create
 *   an LVGL display driver that renders into it.
 *
 ****************************************************************************/

int lv_port_disp_init(void)
{
  int ret;

  /* Open framebuffer device */

  g_fb_fd = open(FB_DEV_PATH, O_RDWR);
  if (g_fb_fd < 0)
    {
      syslog(LOG_ERR, "disp: Cannot open %s: %d\n", FB_DEV_PATH, errno);
      return -errno;
    }

  /* Get video info (resolution, format) */

  ret = ioctl(g_fb_fd, FBIOGET_VIDEOINFO,
              (unsigned long)((uintptr_t)&g_vinfo));
  if (ret < 0)
    {
      syslog(LOG_ERR, "disp: FBIOGET_VIDEOINFO failed: %d\n", errno);
      close(g_fb_fd);
      g_fb_fd = -1;
      return -errno;
    }

  /* Get plane info (fbmem pointer, stride, bpp) */

  ret = ioctl(g_fb_fd, FBIOGET_PLANEINFO,
              (unsigned long)((uintptr_t)&g_pinfo));
  if (ret < 0)
    {
      syslog(LOG_ERR, "disp: FBIOGET_PLANEINFO failed: %d\n", errno);
      close(g_fb_fd);
      g_fb_fd = -1;
      return -errno;
    }

  g_fb_mem = (uint16_t *)g_pinfo.fbmem;

  syslog(LOG_INFO, "disp: Framebuffer %ux%u %ubpp @ %p (stride %u)\n",
         g_vinfo.xres, g_vinfo.yres, g_pinfo.bpp,
         g_fb_mem, g_pinfo.stride);

  /* Create LVGL display */

  lv_display_t *disp = lv_display_create(g_vinfo.xres, g_vinfo.yres);
  if (disp == NULL)
    {
      syslog(LOG_ERR, "disp: lv_display_create failed\n");
      return -ENOMEM;
    }

  lv_display_set_flush_cb(disp, disp_flush_cb);

  /* Match framebuffer format (RGB565) explicitly */

  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

  /* Use a single partial-render buffer (40 rows) */

  lv_display_set_buffers(disp,
                         g_disp_buf, NULL,
                         sizeof(g_disp_buf),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  syslog(LOG_INFO, "disp: LVGL display created (%ux%u)\n",
         g_vinfo.xres, g_vinfo.yres);

  return 0;
}
