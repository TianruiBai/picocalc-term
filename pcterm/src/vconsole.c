/****************************************************************************
 * pcterm/src/vconsole.c
 *
 * Virtual console manager for PicoCalc-Term.
 *
 * Manages 4 virtual consoles:
 *   tty0 = GUI (LVGL launcher + apps)
 *   tty1-3 = Text terminal consoles with independent NSH sessions
 *
 * Each text console uses:
 *   - A pseudo-terminal (PTY) pair for I/O
 *   - A reader thread that feeds PTY output → terminal grid
 *   - A login/shell task running on the PTY slave
 *
 * Console switching hides/shows LVGL containers.
 * LVGL rendering is always done from the main loop (thread-safe).
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>
#include <termios.h>
#include <pthread.h>

#include <nuttx/sched.h>
#include <nuttx/mutex.h>
#include <lvgl/lvgl.h>

#include "pcterm/vconsole.h"
#include "pcterm/terminal.h"
#include "pcterm/config.h"
#include "pcterm/user.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PTY_READ_BUF_SIZE   256

/* Modifier bitmask from south-bridge keyboard */

#define MOD_SHIFT   0x01
#define MOD_CTRL    0x02
#define MOD_ALT     0x04
#define MOD_FN      0x08

/****************************************************************************
 * Private Data
 ****************************************************************************/

static vconsole_t g_consoles[VCONSOLE_COUNT];
static int        g_active_console = VCONSOLE_GUI;
static lv_obj_t  *g_app_area = NULL;
static mutex_t    g_vc_lock = NXMUTEX_INITIALIZER;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: vc_pty_write_cb
 *
 * Description:
 *   Terminal write callback — sends data to PTY master when the user
 *   types a key (terminal_input_key translates to ANSI and calls this).
 *
 ****************************************************************************/

static void vc_pty_write_cb(const char *data, size_t len, void *user)
{
  vconsole_t *vc = (vconsole_t *)user;

  if (vc->pty_master >= 0)
    {
      ssize_t n = write(vc->pty_master, data, len);
      (void)n;
    }
}

/****************************************************************************
 * Name: vc_reader_thread
 *
 * Description:
 *   Background thread that reads output from the PTY master and feeds
 *   it into the terminal emulator grid.  The main loop renders.
 *
 ****************************************************************************/

static void *vc_reader_thread(void *arg)
{
  vconsole_t *vc = (vconsole_t *)arg;
  char buf[PTY_READ_BUF_SIZE];

  syslog(LOG_INFO, "vconsole: reader thread started for tty%d\n",
         vc->index);

  while (vc->pty_master >= 0)
    {
      ssize_t n = read(vc->pty_master, buf, sizeof(buf));

      if (n > 0 && vc->term != NULL)
        {
          terminal_feed(vc->term, buf, (size_t)n);
        }
      else if (n == 0 || (n < 0 && errno != EINTR))
        {
          /* PTY closed or error — shell exited */

          syslog(LOG_INFO, "vconsole: tty%d shell exited\n", vc->index);
          break;
        }
    }

  syslog(LOG_INFO, "vconsole: reader thread exiting for tty%d\n",
         vc->index);
  return NULL;
}

/****************************************************************************
 * Name: vc_console_task
 *
 * Description:
 *   Login + shell task that runs on the PTY slave.
 *   Handles text-mode authentication then starts NSH.
 *   On NSH exit, shows an "exited" message.
 *
 *   argv[1] = PTY slave device path (e.g. "/dev/pts/0")
 *   argv[2] = TTY name (e.g. "tty1")
 *
 ****************************************************************************/

static int vc_console_task(int argc, FAR char *argv[])
{
  const char *pty_path = argv[1];
  const char *tty_name = argv[2];
  int con_idx = tty_name[3] - '0';  /* "tty1" → 1 */

  /* Redirect stdio to PTY slave */

  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);

  int fd = open(pty_path, O_RDWR);
  if (fd < 0)
    {
      return -errno;
    }

  /* fd becomes 0 (stdin) */

  dup2(fd, STDOUT_FILENO);
  dup2(fd, STDERR_FILENO);

  if (fd > STDERR_FILENO)
    {
      close(fd);
    }

  /* Configure terminal: canonical mode, echo on */

  struct termios tio;
  if (tcgetattr(STDIN_FILENO, &tio) == 0)
    {
      tio.c_lflag |= (ECHO | ICANON | ISIG);
      tio.c_oflag |= OPOST;
      tcsetattr(STDIN_FILENO, TCSANOW, &tio);
    }

  /* Banner */

  printf("\neUX OS %s\n", tty_name);
  printf("NuttX / RP2350B\n\n");

  /* --- Authentication --- */

  const pc_config_t *cfg = pc_config_get();
  if (cfg->login_enabled && cfg->login_hash[0] != '\0')
    {
      int attempts = 0;

      while (1)
        {
          char user_buf[32];
          char pass_buf[65];

          printf("login: ");
          fflush(stdout);

          if (fgets(user_buf, sizeof(user_buf), stdin) == NULL)
            {
              return 0;
            }

          /* Strip newline */

          size_t ulen = strlen(user_buf);
          if (ulen > 0 && user_buf[ulen - 1] == '\n')
            {
              user_buf[ulen - 1] = '\0';
            }

          /* Disable echo for password */

          if (tcgetattr(STDIN_FILENO, &tio) == 0)
            {
              tio.c_lflag &= ~ECHO;
              tcsetattr(STDIN_FILENO, TCSANOW, &tio);
            }

          printf("Password: ");
          fflush(stdout);

          if (fgets(pass_buf, sizeof(pass_buf), stdin) == NULL)
            {
              return 0;
            }

          /* Restore echo */

          if (tcgetattr(STDIN_FILENO, &tio) == 0)
            {
              tio.c_lflag |= ECHO;
              tcsetattr(STDIN_FILENO, TCSANOW, &tio);
            }

          printf("\n");

          /* Strip newline */

          size_t plen = strlen(pass_buf);
          if (plen > 0 && pass_buf[plen - 1] == '\n')
            {
              pass_buf[plen - 1] = '\0';
            }

          if (user_verify_password(pass_buf, cfg->login_hash))
            {
              printf("Welcome, %s!\n\n", user_buf);
              if (con_idx >= 0 && con_idx < VCONSOLE_COUNT)
                {
                  g_consoles[con_idx].logged_in = true;
                }

              break;
            }

          attempts++;
          printf("Login incorrect.\n\n");

          if (attempts >= 5)
            {
              printf("Too many failed attempts. Wait 5 seconds...\n");
              sleep(5);
              attempts = 0;
            }
        }
    }
  else
    {
      /* No login required */

      if (con_idx >= 0 && con_idx < VCONSOLE_COUNT)
        {
          g_consoles[con_idx].logged_in = true;
        }
    }

  /* --- Start NuttShell --- */

  extern int nsh_main(int argc, char *argv[]);

  syslog(LOG_INFO, "vconsole: starting NSH on %s\n", tty_name);
  nsh_main(0, NULL);

  /* Shell exited */

  printf("\n[Shell exited. Console closed.]\n");
  return 0;
}

/****************************************************************************
 * Name: vc_start_console
 *
 * Description:
 *   Set up PTY, reader thread, and login/shell task for a text console.
 *
 ****************************************************************************/

static int vc_start_console(vconsole_t *vc)
{
  int ret;

  if (vc->initialized)
    {
      return 0;
    }

  /* Open PTY master */

  vc->pty_master = open("/dev/ptmx", O_RDWR);
  if (vc->pty_master < 0)
    {
      syslog(LOG_ERR, "vconsole: tty%d ptmx open failed: %d\n",
             vc->index, errno);
      return -errno;
    }

  /* Unlock and get slave name */

  grantpt(vc->pty_master);
  unlockpt(vc->pty_master);

  char *sname = ptsname(vc->pty_master);
  if (sname == NULL)
    {
      syslog(LOG_ERR, "vconsole: tty%d ptsname failed\n", vc->index);
      close(vc->pty_master);
      vc->pty_master = -1;
      return -ENODEV;
    }

  strncpy(vc->pty_path, sname, sizeof(vc->pty_path) - 1);
  vc->pty_path[sizeof(vc->pty_path) - 1] = '\0';

  syslog(LOG_INFO, "vconsole: tty%d PTY master=%d slave=%s\n",
         vc->index, vc->pty_master, vc->pty_path);

  /* Create terminal widget if not yet created */

  if (vc->term == NULL && vc->container != NULL)
    {
      vc->term = terminal_create(vc->container, vc_pty_write_cb, vc);
      if (vc->term == NULL)
        {
          syslog(LOG_ERR, "vconsole: tty%d terminal_create failed\n",
                 vc->index);
          close(vc->pty_master);
          vc->pty_master = -1;
          return -ENOMEM;
        }
    }

  /* Start reader thread */

  pthread_t reader_thread;
  pthread_attr_t attr;

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, VCONSOLE_READER_STACK);

  ret = pthread_create(&reader_thread, &attr, vc_reader_thread, vc);
  pthread_attr_destroy(&attr);

  if (ret != 0)
    {
      syslog(LOG_ERR, "vconsole: tty%d reader thread failed: %d\n",
             vc->index, ret);
      close(vc->pty_master);
      vc->pty_master = -1;
      return -ret;
    }

  pthread_detach(reader_thread);

  /* Start login/shell task — use struct fields for argv so they remain
   * valid after this function returns (avoiding stack use-after-free).
   */

  snprintf(vc->tty_name, sizeof(vc->tty_name), "tty%d", vc->index);

  const char *task_argv[3];
  task_argv[0] = vc->pty_path;
  task_argv[1] = vc->tty_name;
  task_argv[2] = NULL;

  char task_name[16];
  snprintf(task_name, sizeof(task_name), "console-%d", vc->index);

  vc->shell_pid = task_create(task_name, 100, VCONSOLE_SHELL_STACK,
                              (main_t)vc_console_task,
                              (FAR char * const *)task_argv);

  if (vc->shell_pid < 0)
    {
      syslog(LOG_ERR, "vconsole: tty%d shell task failed: %d\n",
             vc->index, errno);
      close(vc->pty_master);
      vc->pty_master = -1;
      return -errno;
    }

  syslog(LOG_INFO, "vconsole: tty%d started (shell pid=%d)\n",
         vc->index, vc->shell_pid);

  vc->initialized = true;
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: vconsole_init
 ****************************************************************************/

void vconsole_init(lv_obj_t *app_area)
{
  g_app_area = app_area;

  memset(g_consoles, 0, sizeof(g_consoles));

  for (int i = 0; i < VCONSOLE_COUNT; i++)
    {
      vconsole_t *vc = &g_consoles[i];

      vc->index      = i;
      vc->is_gui     = (i == VCONSOLE_GUI);
      vc->active     = false;
      vc->initialized = false;
      vc->logged_in  = false;
      vc->term       = NULL;
      vc->pty_master = -1;
      vc->pty_slave  = -1;
      vc->shell_pid  = -1;
      vc->reader_pid = -1;
      vc->container  = NULL;
    }

  /* tty0 is GUI — it uses the existing launcher/app widgets.
   * No separate container needed; app_area IS its container.
   */

  g_consoles[VCONSOLE_GUI].active = true;

  /* Create LVGL containers for text consoles (tty1-3).
   * They are hidden initially — shown when switched to.
   */

  for (int i = VCONSOLE_TTY1; i < VCONSOLE_COUNT; i++)
    {
      lv_obj_t *cont = lv_obj_create(app_area);
      lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
      lv_obj_set_pos(cont, 0, 0);
      lv_obj_set_style_bg_color(cont, lv_color_hex(0x000000), 0);
      lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(cont, 0, 0);
      lv_obj_set_style_pad_all(cont, 0, 0);
      lv_obj_set_style_radius(cont, 0, 0);
      lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);

      g_consoles[i].container = cont;
    }

  syslog(LOG_INFO, "vconsole: initialized %d virtual consoles\n",
         VCONSOLE_COUNT);
}

/****************************************************************************
 * Name: vconsole_switch
 ****************************************************************************/

void vconsole_switch(int index)
{
  if (index < 0 || index >= VCONSOLE_COUNT)
    {
      return;
    }

  nxmutex_lock(&g_vc_lock);

  if (index == g_active_console)
    {
      nxmutex_unlock(&g_vc_lock);
      return;
    }

  syslog(LOG_INFO, "vconsole: switching tty%d → tty%d\n",
         g_active_console, index);

  /* Hide current console's container */

  vconsole_t *old_vc = &g_consoles[g_active_console];
  old_vc->active = false;

  if (!old_vc->is_gui && old_vc->container != NULL)
    {
      lv_obj_add_flag(old_vc->container, LV_OBJ_FLAG_HIDDEN);
    }

  /* For GUI console (tty0), hide the launcher + app UI elements.
   * We do this by hiding all direct children of app_area that are
   * NOT vconsole containers.
   */

  if (old_vc->is_gui && g_app_area != NULL)
    {
      uint32_t child_cnt = lv_obj_get_child_count(g_app_area);

      for (uint32_t c = 0; c < child_cnt; c++)
        {
          lv_obj_t *child = lv_obj_get_child(g_app_area, c);
          bool is_vc_container = false;

          for (int j = VCONSOLE_TTY1; j < VCONSOLE_COUNT; j++)
            {
              if (child == g_consoles[j].container)
                {
                  is_vc_container = true;
                  break;
                }
            }

          if (!is_vc_container)
            {
              lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

  /* Show new console */

  vconsole_t *new_vc = &g_consoles[index];
  new_vc->active = true;
  g_active_console = index;

  if (new_vc->is_gui && g_app_area != NULL)
    {
      /* Re-show GUI elements */

      uint32_t child_cnt = lv_obj_get_child_count(g_app_area);

      for (uint32_t c = 0; c < child_cnt; c++)
        {
          lv_obj_t *child = lv_obj_get_child(g_app_area, c);
          bool is_vc_container = false;

          for (int j = VCONSOLE_TTY1; j < VCONSOLE_COUNT; j++)
            {
              if (child == g_consoles[j].container)
                {
                  is_vc_container = true;
                  break;
                }
            }

          if (!is_vc_container)
            {
              lv_obj_clear_flag(child, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
  else if (!new_vc->is_gui && new_vc->container != NULL)
    {
      lv_obj_clear_flag(new_vc->container, LV_OBJ_FLAG_HIDDEN);

      /* Start console if not yet initialized (lazy init on first switch) */

      if (!new_vc->initialized)
        {
          vc_start_console(new_vc);
        }

      /* Force terminal render */

      if (new_vc->term != NULL)
        {
          new_vc->term->dirty = true;
        }
    }

  /* Force LVGL to redraw */

  lv_obj_invalidate(lv_scr_act());

  nxmutex_unlock(&g_vc_lock);
}

/****************************************************************************
 * Name: vconsole_get_active
 ****************************************************************************/

int vconsole_get_active(void)
{
  int active;

  nxmutex_lock(&g_vc_lock);
  active = g_active_console;
  nxmutex_unlock(&g_vc_lock);
  return active;
}

/****************************************************************************
 * Name: vconsole_is_text_active
 ****************************************************************************/

bool vconsole_is_text_active(void)
{
  int active;

  nxmutex_lock(&g_vc_lock);
  active = g_active_console;
  nxmutex_unlock(&g_vc_lock);
  return active != VCONSOLE_GUI;
}

/****************************************************************************
 * Name: vconsole_key_input
 ****************************************************************************/

void vconsole_key_input(uint8_t raw_key, uint8_t mods)
{
  nxmutex_lock(&g_vc_lock);

  vconsole_t *vc = &g_consoles[g_active_console];

  if (vc->is_gui || vc->term == NULL)
    {
      nxmutex_unlock(&g_vc_lock);
      return;
    }

  terminal_input_key(vc->term, raw_key, mods);
  nxmutex_unlock(&g_vc_lock);
}

/****************************************************************************
 * Name: vconsole_render_if_dirty
 ****************************************************************************/

void vconsole_render_if_dirty(void)
{
  nxmutex_lock(&g_vc_lock);

  vconsole_t *vc = &g_consoles[g_active_console];

  if (!vc->is_gui && vc->term != NULL && vc->term->dirty)
    {
      terminal_render(vc->term);
    }

  nxmutex_unlock(&g_vc_lock);
}

/****************************************************************************
 * Name: vconsole_shutdown
 ****************************************************************************/

void vconsole_shutdown(void)
{
  nxmutex_lock(&g_vc_lock);

  for (int i = VCONSOLE_TTY1; i < VCONSOLE_COUNT; i++)
    {
      vconsole_t *vc = &g_consoles[i];

      if (vc->pty_master >= 0)
        {
          close(vc->pty_master);
          vc->pty_master = -1;
        }

      if (vc->term != NULL)
        {
          terminal_destroy(vc->term);
          vc->term = NULL;
        }

      vc->initialized = false;
    }

  nxmutex_unlock(&g_vc_lock);

  syslog(LOG_INFO, "vconsole: all consoles shut down\n");
}
