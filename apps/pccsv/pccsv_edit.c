/****************************************************************************
 * apps/pccsv/pccsv_edit.c
 *
 * Cell editing logic for the CSV viewer/editor.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <lvgl/lvgl.h>
#include <syslog.h>

#include "pcterm/app.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_obj_t *g_edit_ta     = NULL;  /* Text area for editing */
static bool       g_editing    = false;
static int        g_edit_row   = -1;
static int        g_edit_col   = -1;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * Begin editing a cell. Shows a text area overlay.
 */
void pccsv_edit_begin(lv_obj_t *parent, int row, int col,
                      const char *current_value)
{
  if (g_editing)
    {
      return;
    }

  g_edit_ta = lv_textarea_create(parent);
  lv_textarea_set_one_line(g_edit_ta, true);
  lv_obj_set_size(g_edit_ta, 200, 28);
  lv_obj_align(g_edit_ta, LV_ALIGN_BOTTOM_MID, 0, -4);

  if (current_value)
    {
      lv_textarea_set_text(g_edit_ta, current_value);
    }
  else
    {
      lv_textarea_set_text(g_edit_ta, "");
    }

  g_editing  = true;
  g_edit_row = row;
  g_edit_col = col;

  syslog(LOG_DEBUG, "PCCSV: Editing cell [%d,%d]\n", row, col);
}

/**
 * Confirm the edit and update the CSV data.
 * Returns the new cell value (caller must copy/store it).
 */
const char *pccsv_edit_confirm(void)
{
  if (!g_editing || g_edit_ta == NULL)
    {
      return NULL;
    }

  const char *value = lv_textarea_get_text(g_edit_ta);

  syslog(LOG_DEBUG, "PCCSV: Confirmed [%d,%d] = \"%s\"\n",
         g_edit_row, g_edit_col, value);

  return value;
}

/**
 * Cancel the edit and remove the text area.
 */
void pccsv_edit_cancel(void)
{
  if (g_edit_ta != NULL)
    {
      lv_obj_delete(g_edit_ta);
      g_edit_ta = NULL;
    }

  g_editing  = false;
  g_edit_row = -1;
  g_edit_col = -1;
}

/**
 * Finish editing (cleanup after confirm).
 */
void pccsv_edit_end(void)
{
  if (g_edit_ta != NULL)
    {
      lv_obj_delete(g_edit_ta);
      g_edit_ta = NULL;
    }

  g_editing  = false;
  g_edit_row = -1;
  g_edit_col = -1;
}

/**
 * Check if currently editing a cell.
 */
bool pccsv_edit_active(void)
{
  return g_editing;
}

/**
 * Get the currently editing cell coordinates.
 */
void pccsv_edit_get_cell(int *row, int *col)
{
  *row = g_edit_row;
  *col = g_edit_col;
}
