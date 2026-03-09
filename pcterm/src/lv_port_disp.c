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
  int32_t w = area->x2 - area->x1 + 1;
  int32_t y;

  if (g_fb_mem == NULL)
    {
      lv_display_flush_ready(disp);
      return;
    }

  /* Copy each row from LVGL buffer to framebuffer */

  for (y = area->y1; y <= area->y2; y++)
    {
      memcpy(&g_fb_mem[y * g_vinfo.xres + area->x1],
             px_map,
             w * sizeof(uint16_t));
      px_map += w * sizeof(uint16_t);
    }

#ifdef CONFIG_FB_UPDATE
  /* Tell the framebuffer driver to push the dirty area to the LCD */

  struct fb_area_s fb_area;
  fb_area.x = area->x1;
  fb_area.y = area->y1;
  fb_area.w = w;
  fb_area.h = area->y2 - area->y1 + 1;
  ioctl(g_fb_fd, FBIO_UPDATE, (unsigned long)((uintptr_t)&fb_area));
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
      syslog(LOG_ERR, "DISP: Cannot open %s: %d\n", FB_DEV_PATH, errno);
      return -errno;
    }

  /* Get video info (resolution, format) */

  ret = ioctl(g_fb_fd, FBIOGET_VIDEOINFO,
              (unsigned long)((uintptr_t)&g_vinfo));
  if (ret < 0)
    {
      syslog(LOG_ERR, "DISP: FBIOGET_VIDEOINFO failed: %d\n", errno);
      close(g_fb_fd);
      g_fb_fd = -1;
      return -errno;
    }

  /* Get plane info (fbmem pointer, stride, bpp) */

  ret = ioctl(g_fb_fd, FBIOGET_PLANEINFO,
              (unsigned long)((uintptr_t)&g_pinfo));
  if (ret < 0)
    {
      syslog(LOG_ERR, "DISP: FBIOGET_PLANEINFO failed: %d\n", errno);
      close(g_fb_fd);
      g_fb_fd = -1;
      return -errno;
    }

  g_fb_mem = (uint16_t *)g_pinfo.fbmem;

  syslog(LOG_INFO, "DISP: Framebuffer %ux%u %ubpp @ %p (stride %u)\n",
         g_vinfo.xres, g_vinfo.yres, g_pinfo.bpp,
         g_fb_mem, g_pinfo.stride);

  /* Create LVGL display */

  lv_display_t *disp = lv_display_create(g_vinfo.xres, g_vinfo.yres);
  if (disp == NULL)
    {
      syslog(LOG_ERR, "DISP: lv_display_create failed\n");
      return -ENOMEM;
    }

  lv_display_set_flush_cb(disp, disp_flush_cb);

  /* Use a single partial-render buffer (40 rows) */

  lv_display_set_buffers(disp,
                         g_disp_buf, NULL,
                         sizeof(g_disp_buf),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  syslog(LOG_INFO, "DISP: LVGL display created (%ux%u)\n",
         g_vinfo.xres, g_vinfo.yres);

  return 0;
}
