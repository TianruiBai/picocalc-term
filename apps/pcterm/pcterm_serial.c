/****************************************************************************
 * apps/pcterm/pcterm_serial.c
 *
 * Serial terminal app — connects terminal widget to a UART device.
 *
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <syslog.h>

#include <lvgl/lvgl.h>

#include "pcterm/app.h"
#include "pcterm/terminal.h"
#include "pcterm/hostname.h"

/* Exit request detection (Fn+ESC) from LVGL indev */

extern bool lv_port_indev_exit_requested(void);

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_PCTERM_SERIAL_DEV
#  define CONFIG_PCTERM_SERIAL_DEV "/dev/ttyS0"
#endif

#define SERIAL_READ_BUF  128

/****************************************************************************
 * Private Data
 ****************************************************************************/

static terminal_t *g_term = NULL;
static int g_serial_fd = -1;
static pthread_t g_serial_read_thread;
static volatile bool g_serial_running = false;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void term_write_cb(const char *data, size_t len, void *user)
{
  if (g_serial_fd >= 0)
    {
      write(g_serial_fd, data, len);
    }
}

static void *serial_read_thread(void *arg)
{
  char buf[SERIAL_READ_BUF];

  while (g_serial_running)
    {
      if (g_serial_fd < 0)
        {
          usleep(100000);
          continue;
        }

      ssize_t n = read(g_serial_fd, buf, sizeof(buf));
      if (n > 0)
        {
          if (g_term != NULL)
            {
              terminal_feed(g_term, buf, (size_t)n);
              terminal_render(g_term);
            }
        }
      else
        {
          usleep(10000);
        }
    }

  return NULL;
}

static int pcterm_serial_main(int argc, char *argv[])
{
  lv_obj_t *screen = pc_app_get_screen();

  g_term = terminal_create(screen, term_write_cb, NULL);
  if (g_term == NULL)
    {
      syslog(LOG_ERR, "PCTERM-SERIAL: Failed to create terminal widget\n");
      return PC_ERR_NOMEM;
    }

  g_serial_fd = open(CONFIG_PCTERM_SERIAL_DEV, O_RDWR | O_NONBLOCK);
  if (g_serial_fd < 0)
    {
      char msg[192];
      snprintf(msg, sizeof(msg),
               "\r\neUX Serial Terminal\r\n"
               "Cannot open %s (errno=%d)\r\n"
               "\r\nPress Fn+ESC to return to launcher\r\n",
               CONFIG_PCTERM_SERIAL_DEV, errno);
      terminal_feed(g_term, msg, strlen(msg));
      terminal_render(g_term);
      syslog(LOG_WARNING, "PCTERM-SERIAL: open %s failed: %d\n",
             CONFIG_PCTERM_SERIAL_DEV, errno);

      /* Still enter event loop so user can read error and exit */

      while (true)
        {
          lv_timer_handler();
          if (lv_port_indev_exit_requested()) break;
          usleep(10000);
        }

      g_term = NULL;
      return 0;
    }

  {
    const char *hostname = hostname_get();
    char banner[192];
    snprintf(banner, sizeof(banner),
             "\r\neUX Serial Terminal\r\n"
             "Connected to %s\r\n"
             "%s serial> ",
             CONFIG_PCTERM_SERIAL_DEV, hostname);
    terminal_feed(g_term, banner, strlen(banner));
    terminal_render(g_term);
  }

  g_serial_running = true;

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 2048);
  pthread_create(&g_serial_read_thread, &attr, serial_read_thread, NULL);
  pthread_attr_destroy(&attr);

  syslog(LOG_INFO, "PCTERM-SERIAL: Connected to %s (Fn+ESC to exit)\n",
         CONFIG_PCTERM_SERIAL_DEV);

  /* Event loop — keep pumping LVGL until user requests exit */

  while (true)
    {
      lv_timer_handler();

      if (lv_port_indev_exit_requested())
        {
          syslog(LOG_INFO, "PCTERM-SERIAL: Exit requested\n");
          break;
        }

      usleep(5000);
    }

  /* Cleanup */

  g_serial_running = false;
  pthread_join(g_serial_read_thread, NULL);

  if (g_serial_fd >= 0)
    {
      close(g_serial_fd);
      g_serial_fd = -1;
    }

  g_term = NULL;

  pc_app_exit(0);  /* longjmp back to framework — skips framework event loop */
  return 0;  /* Not reached */
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

const pc_app_t g_pcterm_serial_app = {
  .info = {
    .name         = "pcterm-serial",
    .display_name = "Serial Terminal",
    .version      = "1.0.0",
    .category     = "system",
    .icon         = LV_SYMBOL_USB,
    .min_ram      = 32768,
    .flags        = PC_APP_FLAG_BUILTIN | PC_APP_FLAG_STATEFUL,
  },
  .main    = pcterm_serial_main,
  .save    = NULL,
  .restore = NULL,
};
