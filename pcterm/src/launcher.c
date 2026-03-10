/****************************************************************************
 * pcterm/src/launcher.c
 *
 * Home screen launcher — grid of app icons.
 * Shows all registered (built-in + installed) apps in a scrollable grid.
 * Arrow keys navigate, Enter launches.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>

#include <lvgl/lvgl.h>

#include "pcterm/launcher.h"
#include "pcterm/app.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Single entry in the launcher grid */

typedef struct launcher_item_s
{
  const char *name;           /* Internal app name */
  const char *display_name;   /* Label under icon */
  lv_obj_t   *btn;           /* LVGL button object */
  lv_obj_t   *icon;          /* LVGL image/label for icon */
  lv_obj_t   *label;         /* LVGL label under icon */
} launcher_item_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_obj_t       *g_launcher_cont = NULL;  /* Main grid container */
static launcher_item_t g_items[48];  /* Max built-in + third-party apps */
static int             g_item_count = 0;
static int             g_selected   = 0;         /* Currently highlighted */
static bool            g_visible    = true;
static char            g_pending_launch[64];       /* Deferred app launch */
static bool            g_has_pending = false;
static int             g_arrange_mode = 0;         /* 0=builtin, 1=alpha */
static char            g_selected_name[64];        /* Persist selection */

/* Default icon symbols (LVGL built-in) for built-in apps */

static const char *g_builtin_icons[] =
{
  LV_SYMBOL_SETTINGS,    /* settings */
  LV_SYMBOL_EDIT,        /* pcedit */
  LV_SYMBOL_LIST,        /* pccsv */
  LV_SYMBOL_AUDIO,       /* pcaudio */
  LV_SYMBOL_VIDEO,       /* pcvideo */
  LV_SYMBOL_KEYBOARD,    /* pcterm */
  LV_SYMBOL_USB,         /* pcterm-serial */
  LV_SYMBOL_WIFI,        /* pcssh */
  LV_SYMBOL_BLUETOOTH,   /* pcwireless */
  LV_SYMBOL_GPS,         /* pcweb */
  LV_SYMBOL_DIRECTORY,   /* pcfiles */
};

static void launcher_update_selection(int old_sel, int new_sel);

typedef struct launcher_appref_s
{
  const pc_app_info_t *info;
} launcher_appref_t;

static int launcher_appref_cmp(const void *a, const void *b)
{
  const launcher_appref_t *aa = (const launcher_appref_t *)a;
  const launcher_appref_t *bb = (const launcher_appref_t *)b;
  return strcmp(aa->info->display_name, bb->info->display_name);
}

/****************************************************************************
 * Name: launcher_queue_selected
 *
 * Description:
 *   Queue launch of the currently selected app.
 *
 ****************************************************************************/

static void launcher_queue_selected(void)
{
  if (g_selected >= 0 && g_selected < g_item_count)
    {
      const char *name = g_items[g_selected].name;
      syslog(LOG_INFO, "launcher: User selected \"%s\"\n", name);
      strncpy(g_pending_launch, name, sizeof(g_pending_launch) - 1);
      g_pending_launch[sizeof(g_pending_launch) - 1] = '\0';
      g_has_pending = true;

      strncpy(g_selected_name, name, sizeof(g_selected_name) - 1);
      g_selected_name[sizeof(g_selected_name) - 1] = '\0';
    }
}

/****************************************************************************
 * Name: launcher_item_event_cb
 *
 * Description:
 *   Handle direct user interaction on an app tile (touch/click or
 *   keypad-activated click). Keeps visual selection and queues launch.
 *
 ****************************************************************************/

static void launcher_item_event_cb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  int index = (int)(intptr_t)lv_event_get_user_data(e);

  if (index < 0 || index >= g_item_count)
    {
      return;
    }

  if (code == LV_EVENT_CLICKED || code == LV_EVENT_SHORT_CLICKED)
    {
      if (g_selected != index)
        {
          launcher_update_selection(g_selected, index);
        }

      launcher_queue_selected();
    }
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: launcher_create_item
 *
 * Description:
 *   Create a single app tile in the grid.
 *
 ****************************************************************************/

static void launcher_create_item(lv_obj_t *grid, int index,
                                 const char *name,
                                 const char *display_name,
                                 const char *icon_symbol)
{
  int col = index % LAUNCHER_COLS;
  int row = index / LAUNCHER_COLS;

  int tile_w = LAUNCHER_ICON_SIZE + LAUNCHER_ICON_PAD;
  int tile_h = LAUNCHER_ICON_SIZE + LAUNCHER_LABEL_H + LAUNCHER_ICON_PAD;

  int x_offset = (320 - (LAUNCHER_COLS * tile_w)) / 2;  /* Center grid */

  /* Button / tile container */

  lv_obj_t *btn = lv_obj_create(grid);
  lv_obj_set_size(btn, LAUNCHER_ICON_SIZE, LAUNCHER_ICON_SIZE);
  lv_obj_set_pos(btn, x_offset + col * tile_w,
                       LAUNCHER_ICON_PAD + row * tile_h);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A1A2E), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);

  /* Focus style (selected item) */

  lv_obj_set_style_border_width(btn, 2, LV_STATE_FOCUSED);
  lv_obj_set_style_border_color(btn, lv_color_hex(0x00B4D8),
                                LV_STATE_FOCUSED);

  /* Icon — using LVGL symbol fonts for now.
   * TODO: Load 32×32 RGB565 icons from SD card for third-party apps.
   */

  lv_obj_t *icon = lv_label_create(btn);
  lv_label_set_text(icon, icon_symbol);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(icon, lv_color_white(), 0);
  lv_obj_center(icon);

  /* Label below icon */

  lv_obj_t *label = lv_label_create(grid);
  lv_label_set_text(label, display_name);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(label, LAUNCHER_ICON_SIZE);
  lv_obj_align_to(label, btn, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

  /* Store references */

  g_items[index].name         = name;
  g_items[index].display_name = display_name;
  g_items[index].btn          = btn;
  g_items[index].icon         = icon;
  g_items[index].label        = label;

  /* Direct tile interaction (touch/click and keypad click generation) */

  lv_obj_add_event_cb(btn, launcher_item_event_cb,
                      LV_EVENT_CLICKED, (void *)(intptr_t)index);
  lv_obj_add_event_cb(btn, launcher_item_event_cb,
                      LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)index);
}

/****************************************************************************
 * Name: launcher_update_selection
 *
 * Description:
 *   Update visual focus indicator.
 *
 ****************************************************************************/

static void launcher_update_selection(int old_sel, int new_sel)
{
  if (old_sel >= 0 && old_sel < g_item_count)
    {
      lv_obj_clear_state(g_items[old_sel].btn, LV_STATE_FOCUSED);
    }

  if (new_sel >= 0 && new_sel < g_item_count)
    {
      lv_obj_add_state(g_items[new_sel].btn, LV_STATE_FOCUSED);
    }

  g_selected = new_sel;
}

/****************************************************************************
 * Name: launcher_key_event_cb
 *
 * Description:
 *   LVGL event callback—receives key events from the input group and
 *   forwards them to the manual launcher navigation handler.
 *
 ****************************************************************************/

static void launcher_key_event_cb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_KEY)
    {
      uint32_t key = lv_event_get_key(e);
      launcher_handle_key(key);
    }
  else if (code == LV_EVENT_CLICKED || code == LV_EVENT_SHORT_CLICKED)
    {
      /* Some keypad paths convert Enter to click events */
      launcher_queue_selected();
    }
  else if (code == LV_EVENT_READY)
    {
      /* LVGL v9: LV_KEY_ENTER in editing mode exits editing and fires
       * LV_EVENT_READY instead of CLICKED.  Treat this as app launch
       * and immediately re-enable editing so arrow keys keep working.
       */

      launcher_queue_selected();

      lv_group_t *group = lv_group_get_default();
      if (group != NULL)
        {
          lv_group_set_editing(group, true);
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: launcher_init
 *
 * Description:
 *   Build the launcher grid. Queries the app framework for registered apps.
 *
 ****************************************************************************/

int launcher_init(lv_obj_t *parent)
{
  /* Create scrollable grid container */

  g_launcher_cont = lv_obj_create(parent);
  lv_obj_set_size(g_launcher_cont, 320, 300);
  lv_obj_align(g_launcher_cont, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(g_launcher_cont, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_launcher_cont, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_launcher_cont, 0, 0);
  lv_obj_set_style_pad_all(g_launcher_cont, 0, 0);
  lv_obj_set_style_radius(g_launcher_cont, 0, 0);

  launcher_refresh();

  if (g_item_count == 0)
    {
      syslog(LOG_INFO, "launcher: Grid created with 0 apps\n");
      return 0;
    }

  /* Register with the default LVGL input group so keyboard events
   * are routed to this container.  The event callback dispatches
   * to launcher_handle_key() for grid navigation and app launch.
   */

  lv_group_t *group = lv_group_get_default();
  if (group != NULL)
    {
      lv_group_add_obj(group, g_launcher_cont);

      /* Set editing mode so ALL keys (including arrows) go directly
       * to this object as LV_EVENT_KEY instead of being consumed
       * by LVGL's group focus navigation.
       */

      lv_group_set_editing(group, true);
    }

  lv_obj_add_event_cb(g_launcher_cont, launcher_key_event_cb,
                      LV_EVENT_KEY, NULL);
  lv_obj_add_event_cb(g_launcher_cont, launcher_key_event_cb,
                      LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(g_launcher_cont, launcher_key_event_cb,
                      LV_EVENT_SHORT_CLICKED, NULL);
  lv_obj_add_event_cb(g_launcher_cont, launcher_key_event_cb,
                      LV_EVENT_READY, NULL);

  g_visible = true;
  syslog(LOG_INFO, "launcher: Grid created with %d apps\n", g_item_count);

  return 0;
}

/****************************************************************************
 * Name: launcher_refresh
 *
 * Description:
 *   Rebuild the launcher grid (e.g. after package install/uninstall).
 *
 ****************************************************************************/

void launcher_refresh(void)
{
  if (g_launcher_cont == NULL)
    {
      return;
    }

  /* Clear existing items */

  lv_obj_clean(g_launcher_cont);
  g_item_count = 0;
  g_selected = 0;

  /* Rebuild */

  int total = pc_app_get_count();
  launcher_appref_t refs[48];
  int ref_count = 0;

  for (int i = 0; i < total && ref_count < 48; i++)
    {
      const pc_app_info_t *info = pc_app_get_info(i);
      if (info == NULL)
        {
          continue;
        }

      refs[ref_count++].info = info;
    }

  if (g_arrange_mode == 1 && ref_count > 1)
    {
      qsort(refs, ref_count, sizeof(refs[0]), launcher_appref_cmp);
    }

  for (int i = 0; i < ref_count && g_item_count < 48; i++)
    {
      const pc_app_info_t *info = refs[i].info;

      const char *icon_sym = LV_SYMBOL_FILE;
      if (i < (int)(sizeof(g_builtin_icons) / sizeof(g_builtin_icons[0])))
        {
          icon_sym = g_builtin_icons[i];
        }

      launcher_create_item(g_launcher_cont, g_item_count,
                           info->name, info->display_name, icon_sym);
      g_item_count++;
    }

  if (g_item_count > 0)
    {
      int sel = 0;

      if (g_selected_name[0] != '\0')
        {
          for (int i = 0; i < g_item_count; i++)
            {
              if (strcmp(g_items[i].name, g_selected_name) == 0)
                {
                  sel = i;
                  break;
                }
            }
        }

      launcher_update_selection(-1, sel);
    }

  syslog(LOG_INFO, "launcher: Refreshed with %d apps\n", g_item_count);
}

/****************************************************************************
 * Name: launcher_show / launcher_hide
 *
 * Description:
 *   Show or hide the launcher grid (when entering/exiting an app).
 *
 ****************************************************************************/

void launcher_show(void)
{
  if (g_launcher_cont != NULL)
    {
      lv_obj_clear_flag(g_launcher_cont, LV_OBJ_FLAG_HIDDEN);

      /* Re-add to the input group so keyboard navigation works */

      lv_group_t *group = lv_group_get_default();
      if (group != NULL)
        {
          lv_group_add_obj(group, g_launcher_cont);
          lv_group_focus_obj(g_launcher_cont);
          lv_group_set_editing(group, true);
        }

      g_visible = true;
    }
}

void launcher_hide(void)
{
  if (g_launcher_cont != NULL)
    {
      lv_obj_add_flag(g_launcher_cont, LV_OBJ_FLAG_HIDDEN);

      /* Remove from input group so the launched app gets keyboard
       * events instead of the hidden launcher.
       */

      lv_group_t *group = lv_group_get_default();
      if (group != NULL)
        {
          lv_group_remove_obj(g_launcher_cont);
        }

      g_visible = false;
    }
}

/****************************************************************************
 * Name: launcher_handle_key
 *
 * Description:
 *   Handle keyboard navigation on the launcher grid.
 *   Arrow keys move the selection, Enter launches the app.
 *
 ****************************************************************************/

bool launcher_handle_key(uint32_t key)
{
  if (!g_visible || g_item_count == 0)
    {
      return false;
    }

  int old_sel = g_selected;
  int new_sel = g_selected;

  switch (key)
    {
      case LV_KEY_LEFT:
        new_sel = (g_selected > 0) ? g_selected - 1 : g_selected;
        break;

      case LV_KEY_RIGHT:
        new_sel = (g_selected < g_item_count - 1)
                  ? g_selected + 1 : g_selected;
        break;

      case LV_KEY_UP:
        new_sel = (g_selected >= LAUNCHER_COLS)
                  ? g_selected - LAUNCHER_COLS : g_selected;
        break;

      case LV_KEY_DOWN:
        new_sel = (g_selected + LAUNCHER_COLS < g_item_count)
                  ? g_selected + LAUNCHER_COLS : g_selected;
        break;

      case LV_KEY_ENTER:
      case '\r':
        /* Queue a deferred app launch (runs outside lv_timer_handler) */
        launcher_queue_selected();
        return true;

      default:
        return false;
    }

  if (new_sel != old_sel)
    {
      launcher_update_selection(old_sel, new_sel);
    }

  return true;
}

/****************************************************************************
 * Name: launcher_get_pending_launch / launcher_clear_pending_launch
 *
 * Description:
 *   Deferred app launch — launcher_handle_key() queues the launch request
 *   instead of calling app_framework_launch() directly (which would be
 *   called from within lv_timer_handler, blocking re-entry).
 *   The main loop polls this after lv_timer_handler() returns.
 *
 ****************************************************************************/

const char *launcher_get_pending_launch(void)
{
  if (g_has_pending)
    {
      return g_pending_launch;
    }

  return NULL;
}

void launcher_clear_pending_launch(void)
{
  g_has_pending = false;
  g_pending_launch[0] = '\0';
}

void launcher_set_arrange_mode(int mode)
{
  if (mode != 0 && mode != 1)
    {
      return;
    }

  if (g_arrange_mode != mode)
    {
      g_arrange_mode = mode;
      launcher_refresh();
    }
}

void launcher_set_selected_name(const char *name)
{
  if (name == NULL || name[0] == '\0')
    {
      return;
    }

  strncpy(g_selected_name, name, sizeof(g_selected_name) - 1);
  g_selected_name[sizeof(g_selected_name) - 1] = '\0';
}
