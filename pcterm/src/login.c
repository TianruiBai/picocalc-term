/****************************************************************************
 * pcterm/src/login.c
 *
 * Blocking login screen for PicoCalc-Term.
 *
 * Shows a password prompt when config->login_enabled is true and
 * config->login_hash is non-empty.  Spins the LVGL event loop internally
 * (no extra tasks) until the correct password is entered.
 *
 * Layout (fits within 320×300 app area):
 *
 *   ┌────────────────────────────────────┐
 *   │                                    │
 *   │   🔒  PicoCalc-Term               │  ← title, centred
 *   │                                    │
 *   │   Hello, <username>                │  ← greeting
 *   │   ┌──────────────────────────┐     │
 *   │   │ ••••••••                 │     │  ← password textarea
 *   │   └──────────────────────────┘     │
 *   │   [Press Enter to log in]         │  ← hint
 *   │   ← status / error message →      │
 *   │                                    │
 *   └────────────────────────────────────┘
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <stdbool.h>

#include <lvgl/lvgl.h>

#include "pcterm/login.h"
#include "pcterm/config.h"
#include "pcterm/user.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define LOGIN_BG_COLOR       0x1a1a2e  /* Dark navy */
#define LOGIN_TITLE_COLOR    0x00cccc  /* Cyan */
#define LOGIN_TEXT_COLOR     0xe0e0e0  /* Light grey */
#define LOGIN_HINT_COLOR     0x888888  /* Mid grey */
#define LOGIN_ERR_COLOR      0xff4444  /* Red */
#define LOGIN_OK_COLOR       0x44ff88  /* Green */

#define LOGIN_MAX_ATTEMPTS   5         /* After this many wrong tries, show
                                        * "try again" message but still allow */
#define LOGIN_INNER_LOOP_MS  10        /* Delay per iteration of inner loop */

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct login_ctx_s
{
  lv_obj_t *cont;          /* Full-cover container */
  lv_obj_t *lbl_greeting;
  lv_obj_t *ta_password;
  lv_obj_t *lbl_status;
  bool      done;          /* Set to true when login succeeds */
  int       attempts;
} login_ctx_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static login_ctx_t g_ctx;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: login_attempt
 *
 * Description:
 *   Verify the password currently in the textarea.  Updates the status
 *   label and sets g_ctx.done on success.
 *
 ****************************************************************************/

static void login_attempt(void)
{
  const char *entered = lv_textarea_get_text(g_ctx.ta_password);

  if (user_verify_password(entered, pc_config_get()->login_hash))
    {
      lv_label_set_text(g_ctx.lbl_status, LV_SYMBOL_OK " Welcome!");
      lv_obj_set_style_text_color(g_ctx.lbl_status,
                                  lv_color_hex(LOGIN_OK_COLOR), 0);
      g_ctx.done = true;
    }
  else
    {
      g_ctx.attempts++;
      char msg[64];
      snprintf(msg, sizeof(msg), LV_SYMBOL_CLOSE " Incorrect password (%d)",
               g_ctx.attempts);
      lv_label_set_text(g_ctx.lbl_status, msg);
      lv_obj_set_style_text_color(g_ctx.lbl_status,
                                  lv_color_hex(LOGIN_ERR_COLOR), 0);

      /* Clear the password field for re-entry */

      lv_textarea_set_text(g_ctx.ta_password, "");
    }
}

/****************************************************************************
 * Name: login_key_cb
 *
 * Description:
 *   Key event handler attached to the password textarea.
 *   Submits on LV_KEY_ENTER.
 *
 ****************************************************************************/

static void login_key_cb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_KEY)
    {
      uint32_t key = lv_event_get_key(e);
      if (key == LV_KEY_ENTER)
        {
          login_attempt();
        }
    }
  else if (code == LV_EVENT_READY)
    {
      /* Textarea fires READY when the user presses Enter in single-line
       * mode.  Treat as submit. */

      login_attempt();
    }
}

/****************************************************************************
 * Name: login_build_ui
 *
 * Description:
 *   Build the login screen widgets on top of 'parent'.
 *
 ****************************************************************************/

static void login_build_ui(lv_obj_t *parent)
{
  const pc_config_t *cfg = pc_config_get();
  const char *username   = user_get_name();

  /* Full-cover opaque container */

  g_ctx.cont = lv_obj_create(parent);
  lv_obj_set_size(g_ctx.cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(g_ctx.cont, 0, 0);
  lv_obj_set_style_bg_color(g_ctx.cont, lv_color_hex(LOGIN_BG_COLOR), 0);
  lv_obj_set_style_bg_opa(g_ctx.cont, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_ctx.cont, 0, 0);
  lv_obj_set_style_pad_all(g_ctx.cont, 16, 0);
  lv_obj_set_style_radius(g_ctx.cont, 0, 0);
  lv_obj_set_flex_flow(g_ctx.cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(g_ctx.cont, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  /* Title */

  lv_obj_t *lbl_title = lv_label_create(g_ctx.cont);
  lv_label_set_text(lbl_title, LV_SYMBOL_CHARGE "  PicoCalc-Term");
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(LOGIN_TITLE_COLOR), 0);
  lv_obj_set_style_pad_bottom(lbl_title, 12, 0);

  /* Greeting */

  g_ctx.lbl_greeting = lv_label_create(g_ctx.cont);
  {
    char greeting[64];
    snprintf(greeting, sizeof(greeting), "Hello, %s", username);
    lv_label_set_text(g_ctx.lbl_greeting, greeting);
  }
  lv_obj_set_style_text_color(g_ctx.lbl_greeting,
                               lv_color_hex(LOGIN_TEXT_COLOR), 0);
  lv_obj_set_style_pad_bottom(g_ctx.lbl_greeting, 8, 0);

  /* Password textarea */

  g_ctx.ta_password = lv_textarea_create(g_ctx.cont);
  lv_textarea_set_password_mode(g_ctx.ta_password, true);
  lv_textarea_set_one_line(g_ctx.ta_password, true);
  lv_textarea_set_placeholder_text(g_ctx.ta_password, "Password");
  lv_textarea_set_max_length(g_ctx.ta_password, 64);
  lv_obj_set_width(g_ctx.ta_password, 240);
  lv_obj_set_style_pad_bottom(g_ctx.ta_password, 4, 0);

  /* Add to default focus group so keypad indev delivers key events */

  lv_group_t *grp = lv_group_get_default();
  if (grp != NULL)
    {
      lv_group_add_obj(grp, g_ctx.ta_password);
      lv_group_focus_obj(g_ctx.ta_password);
    }

  lv_obj_add_event_cb(g_ctx.ta_password, login_key_cb, LV_EVENT_KEY, NULL);
  lv_obj_add_event_cb(g_ctx.ta_password, login_key_cb, LV_EVENT_READY, NULL);

  /* Hint */

  lv_obj_t *lbl_hint = lv_label_create(g_ctx.cont);
  lv_label_set_text(lbl_hint, "Press Enter to log in");
  lv_obj_set_style_text_color(lbl_hint, lv_color_hex(LOGIN_HINT_COLOR), 0);
  lv_obj_set_style_pad_top(lbl_hint, 4, 0);
  lv_obj_set_style_pad_bottom(lbl_hint, 8, 0);

  /* Status / error label */

  g_ctx.lbl_status = lv_label_create(g_ctx.cont);
  lv_label_set_text(g_ctx.lbl_status, "");
  lv_obj_set_style_text_color(g_ctx.lbl_status,
                               lv_color_hex(LOGIN_HINT_COLOR), 0);

  (void)cfg;  /* used via user_get_name() -> config */
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: login_show_if_needed
 ****************************************************************************/

bool login_show_if_needed(lv_obj_t *parent)
{
  const pc_config_t *cfg = pc_config_get();

  /* Skip login if disabled or no password set */

  if (!cfg->login_enabled || cfg->login_hash[0] == '\0')
    {
      syslog(LOG_INFO, "login: not required, skipping\n");
      return true;
    }

  syslog(LOG_INFO, "login: showing login screen\n");

  memset(&g_ctx, 0, sizeof(g_ctx));
  g_ctx.done = false;

  login_build_ui(parent);

  /* Force an initial render */

  lv_timer_handler();

  /* Spin our own mini event loop until login succeeds */

  while (!g_ctx.done)
    {
      lv_timer_handler();
      usleep(LOGIN_INNER_LOOP_MS * 1000);
    }

  /* Remove focus and destroy login widgets */

  lv_group_t *grp = lv_group_get_default();
  if (grp != NULL)
    {
      lv_group_remove_obj(g_ctx.ta_password);
    }

  lv_obj_delete(g_ctx.cont);
  g_ctx.cont = NULL;

  /* Repaint so the screen behind us is shown */

  lv_obj_invalidate(lv_scr_act());
  lv_timer_handler();

  syslog(LOG_INFO, "login: authenticated as \"%s\"\n", user_get_name());
  return true;
}
