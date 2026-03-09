/****************************************************************************
 * apps/pcterm/pcterm_local.c
 *
 * Local terminal app — NuttShell via pseudo-terminal.
 * Uses the shared terminal widget (pcterm/terminal.h) for rendering.
 *
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <syslog.h>
#include <lvgl/lvgl.h>

#include "pcterm/app.h"
#include "pcterm/terminal.h"
#include "pcterm/hostname.h"

/* Forward declaration for NSH PTY helpers */

typedef void (*nsh_output_cb)(const char *data, size_t len, void *user);
typedef struct pcterm_nsh_s pcterm_nsh_t;

extern pcterm_nsh_t *pcterm_nsh_start(nsh_output_cb output_cb, void *user);
extern int  pcterm_nsh_write(pcterm_nsh_t *nsh, const char *data, size_t len);
extern void pcterm_nsh_stop(pcterm_nsh_t *nsh);

/* Exit request detection (Fn+ESC) from LVGL indev */

extern bool lv_port_indev_exit_requested(void);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static terminal_t *g_term = NULL;
static pcterm_nsh_t *g_nsh = NULL;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/**
 * Called by the terminal widget when the user types a key.
 * Forwards the data to the NuttShell PTY.
 */
static void term_write_cb(const char *data, size_t len, void *user)
{
  if (g_nsh != NULL)
    {
      pcterm_nsh_write(g_nsh, data, len);
    }
}

/**
 * Called by pcterm_nsh when NuttShell produces output.
 * Feeds data into the terminal emulator widget.
 */
static void nsh_output_handler(const char *data, size_t len, void *user)
{
  if (g_term != NULL)
    {
      terminal_feed(g_term, data, len);
      terminal_render(g_term);
    }
}

static int pcterm_local_main(int argc, char *argv[])
{
  lv_obj_t *screen = pc_app_get_screen();

  /* Always start a fresh local terminal session */

  if (g_nsh != NULL)
    {
      pcterm_nsh_stop(g_nsh);
      g_nsh = NULL;
    }

  g_term = NULL;

  /* Create terminal widget */

  g_term = terminal_create(screen, term_write_cb, NULL);
  if (g_term == NULL)
    {
      syslog(LOG_ERR, "PCTERM: Failed to create terminal widget\n");
      return PC_ERR_NOMEM;
    }

  /* Start NuttShell via pseudo-terminal */

  g_nsh = pcterm_nsh_start(nsh_output_handler, NULL);
  if (g_nsh == NULL)
    {
      syslog(LOG_WARNING, "PCTERM: NSH start failed, showing welcome only\n");

      /* Show welcome message as fallback */

      const char *hostname = hostname_get();
      char welcome[128];
      snprintf(welcome, sizeof(welcome),
               "\r\nPicoCalc-Term v0.1.0\r\n"
               "%s login: user\r\n"
               "user@%s:~$ ",
               hostname, hostname);
      terminal_feed(g_term, welcome, strlen(welcome));
      terminal_render(g_term);
    }
  else
    {
      syslog(LOG_INFO, "PCTERM: NuttShell session started\n");
    }

  /* Run local event loop — LVGL pumps display + keyboard.
   * This loop keeps running until the user presses Fn+ESC
   * to return to the launcher.
   */

  syslog(LOG_INFO, "PCTERM: Entering event loop (Fn+ESC to exit)\n");

  while (true)
    {
      lv_timer_handler();

      if (lv_port_indev_exit_requested())
        {
          syslog(LOG_INFO, "PCTERM: Exit requested via Fn+ESC\n");
          break;
        }

      usleep(5000);  /* 5 ms = 200 Hz */
    }

  /* Cleanup */

  if (g_nsh != NULL)
    {
      pcterm_nsh_stop(g_nsh);
      g_nsh = NULL;
    }

  g_term = NULL;  /* Destroyed by framework when app screen is deleted */

  pc_app_exit(0);  /* longjmp back to framework — skips framework event loop */
  return 0;  /* Not reached */
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

const pc_app_t g_pcterm_local_app = {
  .info = {
    .name         = "pcterm",
    .display_name = "Local Terminal",
    .version      = "1.0.0",
    .category     = "system",
    .min_ram      = 32768,   /* Terminal grid + scrollback */
    .flags        = PC_APP_FLAG_BUILTIN | PC_APP_FLAG_STATEFUL,
  },
  .main    = pcterm_local_main,
  .save    = NULL,  /* TODO: save scrollback + PTY state */
  .restore = NULL,
};
