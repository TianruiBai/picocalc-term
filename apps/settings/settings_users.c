/****************************************************************************
 * apps/settings/settings_users.c
 *
 * Users & Security settings tab.
 * Allows changing the username and setting / clearing the login password.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <lvgl/lvgl.h>

#include "pcterm/config.h"
#include "pcterm/user.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_obj_t *g_ta_username   = NULL;
static lv_obj_t *g_ta_pw_new     = NULL;
static lv_obj_t *g_ta_pw_confirm = NULL;
static lv_obj_t *g_sw_login      = NULL;
static lv_obj_t *g_lbl_status    = NULL;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void set_status(const char *msg, bool error)
{
  if (!g_lbl_status)
    {
      return;
    }

  lv_label_set_text(g_lbl_status, msg);
  lv_obj_set_style_text_color(
    g_lbl_status,
    error ? lv_color_hex(0xff4444) : lv_color_hex(0x44ff88),
    0);
}

/****************************************************************************
 * Name: apply_btn_cb
 *
 * Description:
 *   "Apply" button handler — validate inputs and save credentials.
 *
 ****************************************************************************/

static void apply_btn_cb(lv_event_t *e)
{
  (void)e;

  const char *username   = lv_textarea_get_text(g_ta_username);
  const char *pw_new     = lv_textarea_get_text(g_ta_pw_new);
  const char *pw_confirm = lv_textarea_get_text(g_ta_pw_confirm);

  /* Validate username */

  if (username == NULL || username[0] == '\0')
    {
      set_status("Username cannot be empty", true);
      return;
    }

  if (strlen(username) >= USER_MAX_NAME)
    {
      set_status("Username too long (max 31 chars)", true);
      return;
    }

  /* Validate password fields */

  if (pw_new[0] != '\0')
    {
      if (strcmp(pw_new, pw_confirm) != 0)
        {
          set_status("Passwords do not match", true);
          lv_textarea_set_text(g_ta_pw_new, "");
          lv_textarea_set_text(g_ta_pw_confirm, "");
          return;
        }
    }

  /* Apply: set credentials (pass NULL/empty password to clear) */

  int ret = user_set_credentials(username,
                                 (pw_new[0] != '\0') ? pw_new : NULL);

  /* Sync login_enabled toggle with what was actually saved */

  pc_config_t *cfg = pc_config_get();
  lv_obj_t *sw = g_sw_login;
  if (sw)
    {
      if (cfg->login_enabled)
        {
          lv_obj_add_state(sw, LV_STATE_CHECKED);
        }
      else
        {
          lv_obj_remove_state(sw, LV_STATE_CHECKED);
        }
    }

  /* Clear password fields */

  lv_textarea_set_text(g_ta_pw_new, "");
  lv_textarea_set_text(g_ta_pw_confirm, "");

  if (ret == 0)
    {
      set_status(LV_SYMBOL_OK " Saved", false);
      syslog(LOG_INFO, "SETTINGS: user credentials updated for \"%s\"\n",
             username);
    }
  else
    {
      set_status("Save failed (SD error?)", true);
    }
}

/****************************************************************************
 * Name: login_sw_cb
 *
 * Description:
 *   Toggle login enable/disable.  Disabling clears the stored hash.
 *
 ****************************************************************************/

static void login_sw_cb(lv_event_t *e)
{
  lv_obj_t *sw = lv_event_get_target_obj(e);
  bool checked  = lv_obj_has_state(sw, LV_STATE_CHECKED);

  pc_config_t *cfg = pc_config_get();

  if (!checked)
    {
      /* User disabled login — clear hash and save */

      cfg->login_enabled = false;
      cfg->login_hash[0] = '\0';
      int ret = pc_config_save(cfg);
      if (ret == 0)
        {
          set_status("Login disabled", false);
        }
      else
        {
          set_status("Save failed", true);
        }
    }
  else
    {
      /* Re-enabling login only has effect if a password is already set */

      if (cfg->login_hash[0] != '\0')
        {
          cfg->login_enabled = true;
          pc_config_save(cfg);
          set_status("Login enabled", false);
        }
      else
        {
          /* No password stored — can't enable without setting one first */

          lv_obj_remove_state(sw, LV_STATE_CHECKED);
          set_status("Set a password first", true);
        }
    }
}

/****************************************************************************
 * Name: add_label_row
 *
 * Description:
 *   Helper: add a label + full-width widget pair as a row inside a flex
 *   column container.
 *
 ****************************************************************************/

static lv_obj_t *add_labeled_ta(lv_obj_t *parent, const char *label_text,
                                 bool pw_mode)
{
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, label_text);
  lv_obj_set_width(lbl, 90);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xe0e0e0), 0);

  lv_obj_t *ta = lv_textarea_create(row);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_max_length(ta, 64);
  lv_obj_set_flex_grow(ta, 1);

  if (pw_mode)
    {
      lv_textarea_set_password_mode(ta, true);
    }

  return ta;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: settings_users_create
 *
 * Description:
 *   Build the Users & Security settings tab page.
 *
 ****************************************************************************/

void settings_users_create(lv_obj_t *parent)
{
  const pc_config_t *cfg = pc_config_get();

  /* Scrollable flex column */

  lv_obj_set_style_bg_color(parent, lv_color_hex(0x1a1a2e), 0);
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(parent, 8, 0);
  lv_obj_set_style_pad_row(parent, 6, 0);

  /* Section header — Account */

  lv_obj_t *hdr_account = lv_label_create(parent);
  lv_label_set_text(hdr_account, LV_SYMBOL_SETTINGS "  Account");
  lv_obj_set_style_text_color(hdr_account, lv_color_hex(0x00cccc), 0);

  /* Username textarea */

  g_ta_username = add_labeled_ta(parent, "Username:", false);
  lv_textarea_set_text(g_ta_username, user_get_name());

  /* Section header — Password */

  lv_obj_t *hdr_pw = lv_label_create(parent);
  lv_label_set_text(hdr_pw, LV_SYMBOL_CHARGE "  Password");
  lv_obj_set_style_text_color(hdr_pw, lv_color_hex(0x00cccc), 0);
  lv_obj_set_style_pad_top(hdr_pw, 6, 0);

  lv_obj_t *lbl_pw_hint = lv_label_create(parent);
  lv_label_set_text(lbl_pw_hint,
                    "Leave blank to keep current password.\n"
                    "Clear both fields to remove password.");
  lv_obj_set_style_text_color(lbl_pw_hint, lv_color_hex(0x888888), 0);
  lv_obj_set_width(lbl_pw_hint, LV_PCT(100));

  /* New password */

  g_ta_pw_new = add_labeled_ta(parent, "New:", true);
  lv_textarea_set_placeholder_text(g_ta_pw_new, "(new password)");

  /* Confirm password */

  g_ta_pw_confirm = add_labeled_ta(parent, "Confirm:", true);
  lv_textarea_set_placeholder_text(g_ta_pw_confirm, "(confirm)");

  /* Section header — Login screen */

  lv_obj_t *hdr_login = lv_label_create(parent);
  lv_label_set_text(hdr_login, LV_SYMBOL_EYE_OPEN "  Login Screen");
  lv_obj_set_style_text_color(hdr_login, lv_color_hex(0x00cccc), 0);
  lv_obj_set_style_pad_top(hdr_login, 6, 0);

  /* Row with "Require login on boot" label and toggle switch */

  lv_obj_t *sw_row = lv_obj_create(parent);
  lv_obj_set_size(sw_row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(sw_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(sw_row, 0, 0);
  lv_obj_set_style_pad_all(sw_row, 0, 0);
  lv_obj_set_flex_flow(sw_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(sw_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *sw_lbl = lv_label_create(sw_row);
  lv_label_set_text(sw_lbl, "Require login on boot");
  lv_obj_set_style_text_color(sw_lbl, lv_color_hex(0xe0e0e0), 0);
  lv_obj_set_flex_grow(sw_lbl, 1);

  g_sw_login = lv_switch_create(sw_row);
  if (cfg->login_enabled && cfg->login_hash[0] != '\0')
    {
      lv_obj_add_state(g_sw_login, LV_STATE_CHECKED);
    }

  lv_obj_add_event_cb(g_sw_login, login_sw_cb, LV_EVENT_VALUE_CHANGED, NULL);

  /* Apply button */

  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_set_width(btn, LV_PCT(100));
  lv_obj_set_style_pad_top(btn, 4, 0);

  lv_obj_t *btn_lbl = lv_label_create(btn);
  lv_label_set_text(btn_lbl, LV_SYMBOL_SAVE "  Apply Changes");
  lv_obj_center(btn_lbl);

  lv_obj_add_event_cb(btn, apply_btn_cb, LV_EVENT_CLICKED, NULL);

  /* Status message label */

  g_lbl_status = lv_label_create(parent);
  lv_label_set_text(g_lbl_status, "");
  lv_obj_set_width(g_lbl_status, LV_PCT(100));
}
