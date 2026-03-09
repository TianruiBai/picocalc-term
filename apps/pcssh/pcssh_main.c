/****************************************************************************
 * apps/pcssh/pcssh_main.c
 *
 * SSH client with SCP and SFTP support.
 * Uses wolfSSH library for SSH protocol.
 *
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <lvgl/lvgl.h>

#include "pcterm/app.h"
#include "pcterm/terminal.h"

/****************************************************************************
 * External References — pcssh modules
 ****************************************************************************/

/* pcssh_client.c */

typedef struct ssh_session_s ssh_session_t;

extern int            ssh_client_init(void);
extern ssh_session_t *ssh_connect(const char *host, uint16_t port,
                                  const char *username,
                                  const char *password);
extern int            ssh_send(ssh_session_t *sess,
                               const char *data, size_t len);
extern int            ssh_recv(ssh_session_t *sess,
                               char *buf, size_t len);
extern void           ssh_disconnect(ssh_session_t *sess);
extern void           ssh_client_cleanup(void);

/* pcssh_connections.c */

#include "pcssh.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static terminal_t       *g_term     = NULL;
static ssh_session_t    *g_session  = NULL;
static lv_timer_t       *g_poll_tmr = NULL;
static lv_obj_t         *g_conn_ui  = NULL;  /* Connection selection panel */
static char              g_recv_buf[512];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ssh_write_cb
 *
 * Description:
 *   Terminal widget callback — user typed a key, send to SSH channel.
 *
 ****************************************************************************/

static void ssh_write_cb(const char *data, size_t len, void *user)
{
  ssh_session_t *sess = (ssh_session_t *)user;

  if (sess != NULL)
    {
      ssh_send(sess, data, len);
    }
}

/****************************************************************************
 * Name: ssh_poll_timer
 *
 * Description:
 *   LVGL timer: poll for incoming SSH data and feed to terminal.
 *
 ****************************************************************************/

static void ssh_poll_timer(lv_timer_t *timer)
{
  (void)timer;

  if (g_session == NULL || g_term == NULL)
    {
      return;
    }

  int n = ssh_recv(g_session, g_recv_buf, sizeof(g_recv_buf));

  if (n > 0)
    {
      terminal_feed(g_term, g_recv_buf, n);
      terminal_render(g_term);
    }
  else if (n < 0 && n != -EAGAIN && n != -EWOULDBLOCK)
    {
      /* Connection dropped */

      terminal_feed(g_term, "\r\n[Connection closed]\r\n", 22);
      terminal_render(g_term);

      ssh_disconnect(g_session);
      g_session = NULL;
    }
}

/****************************************************************************
 * Name: ssh_do_connect
 *
 * Description:
 *   Establish SSH connection and switch to terminal UI.
 *
 ****************************************************************************/

static void ssh_do_connect(const char *host, uint16_t port,
                           const char *username, const char *password)
{
  lv_obj_t *screen = pc_app_get_screen();

  /* Hide connection selection UI */

  if (g_conn_ui)
    {
      lv_obj_add_flag(g_conn_ui, LV_OBJ_FLAG_HIDDEN);
    }

  /* Create terminal widget for the SSH session */

  g_term = terminal_create(screen, ssh_write_cb, NULL);

  if (g_term == NULL)
    {
      /* Show error and return to connection list */

      if (g_conn_ui)
        {
          lv_obj_clear_flag(g_conn_ui, LV_OBJ_FLAG_HIDDEN);
        }

      return;
    }

  /* Show connecting message */

  char msg[128];
  snprintf(msg, sizeof(msg), "Connecting to %s@%s:%d...\r\n",
           username, host, port);
  terminal_feed(g_term, msg, strlen(msg));
  terminal_render(g_term);

  /* Connect */

  g_session = ssh_connect(host, port, username, password);

  if (g_session == NULL)
    {
      terminal_feed(g_term, "Connection failed.\r\n", 19);
      terminal_render(g_term);
      return;
    }

  /* Wire terminal write callback to this session */

  g_term->write_user = g_session;

  terminal_feed(g_term, "Connected.\r\n", 12);
  terminal_render(g_term);

  /* Start polling for SSH data */

  g_poll_tmr = lv_timer_create(ssh_poll_timer, 50, NULL);
}

/****************************************************************************
 * Name: pcssh_key_handler
 ****************************************************************************/

static void pcssh_key_handler(lv_event_t *e)
{
  uint32_t key = lv_event_get_key(e);

  if (g_term != NULL)
    {
      /* In terminal mode, pass keys to terminal widget */

      if (key == 'q' && (lv_event_get_param(e) == NULL))
        {
          /* Ctrl+Q to exit */
        }

      return;
    }

  /* Connection list mode */

  switch (key)
    {
      case LV_KEY_ENTER:
        {
          /* Connect to first saved connection for now */

          ssh_connection_t conn;
          if (ssh_connections_count() > 0 &&
              ssh_connections_get(0, &conn) == 0)
            {
              ssh_do_connect(conn.host, conn.port,
                             conn.username, NULL);
            }
        }
        break;

      case 'q':
      case 'Q':
        pc_app_exit(0);
        break;

      default:
        break;
    }
}

/****************************************************************************
 * Name: pcssh_main
 ****************************************************************************/

static int pcssh_main(int argc, char *argv[])
{
  lv_obj_t *screen = pc_app_get_screen();

  /* Initialize SSH subsystem */

  ssh_client_init();

  /* Load saved connections */

  ssh_connections_load();

  /* Connection selection UI */

  g_conn_ui = lv_obj_create(screen);
  lv_obj_set_size(g_conn_ui, 320, 300);
  lv_obj_set_style_bg_color(g_conn_ui, lv_color_black(), 0);
  lv_obj_set_style_pad_all(g_conn_ui, 0, 0);
  lv_obj_set_style_border_width(g_conn_ui, 0, 0);

  lv_obj_t *title = lv_label_create(g_conn_ui);
  lv_label_set_text(title, "SSH Client");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

  /* Connection list */

  lv_obj_t *list = lv_list_create(g_conn_ui);
  lv_obj_set_size(list, 300, 200);
  lv_obj_align(list, LV_ALIGN_CENTER, 0, 0);

  lv_list_add_text(list, "Saved Connections");

  /* Populate saved connections */

  int count = ssh_connections_count();
  for (int i = 0; i < count; i++)
    {
      ssh_connection_t conn;
      if (ssh_connections_get(i, &conn) == 0)
        {
          char label[192];
          snprintf(label, sizeof(label), "%s@%s:%d",
                   conn.username, conn.host, conn.port);
          lv_list_add_btn(list, LV_SYMBOL_WIFI, label);
        }
    }

  if (count == 0)
    {
      lv_list_add_btn(list, NULL, "(No saved connections)");
    }

  lv_list_add_btn(list, LV_SYMBOL_PLUS, "New Connection...");

  /* Bottom info */

  lv_obj_t *info = lv_label_create(g_conn_ui);
  lv_label_set_text(info,
    "[Enter] Connect  [N] New  [D] Delete  [Q] Quit");
  lv_obj_align(info, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_text_font(info, &lv_font_unscii_8, 0);
  lv_obj_set_style_text_color(info, lv_color_make(120, 120, 120), 0);
  lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);

  /* Register keyboard handler */

  lv_obj_add_event_cb(screen, pcssh_key_handler, LV_EVENT_KEY, NULL);
  lv_group_t *grp = lv_group_get_default();
  if (grp)
    {
      lv_group_add_obj(grp, screen);
    }

  /* If command-line args given, connect directly */

  if (argc >= 3)
    {
      /* Usage: pcssh user@host [port] */

      char *at = strchr(argv[1], '@');
      if (at)
        {
          *at = '\0';
          const char *user = argv[1];
          const char *host = at + 1;
          uint16_t port = (argc >= 3) ? atoi(argv[2]) : 22;
          ssh_do_connect(host, port, user, NULL);
        }
    }

  return 0;
}

/****************************************************************************
 * Name: pcssh_save / pcssh_restore
 ****************************************************************************/

static int pcssh_save(void *buf, size_t *len)
{
  /* Save current connection info for session restore */

  if (g_session != NULL)
    {
      /* For now just note that a session was active */

      const char *marker = "active";
      size_t mlen = strlen(marker) + 1;
      if (*len >= mlen)
        {
          memcpy(buf, marker, mlen);
          *len = mlen;
          return 0;
        }
    }

  *len = 0;
  return 0;
}

static int pcssh_restore(const void *buf, size_t len)
{
  /* Session cannot be restored — SSH connections are stateful */

  (void)buf;
  (void)len;
  return 0;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

const pc_app_t g_pcssh_app = {
  .info = {
    .name         = "pcssh",
    .display_name = "SSH Client",
    .version      = "1.0.0",
    .category     = "network",
    .min_ram      = 131072,   /* SSH buffers + terminal */
    .flags        = PC_APP_FLAG_BUILTIN | PC_APP_FLAG_NETWORK
                    | PC_APP_FLAG_STATEFUL,
  },
  .main    = pcssh_main,
  .save    = pcssh_save,
  .restore = pcssh_restore,
};
