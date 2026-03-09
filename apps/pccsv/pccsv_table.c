/****************************************************************************
 * apps/pccsv/pccsv_table.c
 *
 * LVGL table widget driver for CSV display.
 * Manages the scrollable table view with column headers and cell selection.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <lvgl/lvgl.h>
#include <syslog.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TABLE_MAX_VISIBLE_ROWS   20
#define TABLE_MAX_VISIBLE_COLS   8
#define TABLE_CELL_WIDTH         60
#define TABLE_HEADER_HEIGHT      24

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_obj_t *g_table      = NULL;
static int       g_sel_row    = 0;
static int       g_sel_col    = 0;
static int       g_scroll_row = 0;
static int       g_scroll_col = 0;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * Create the LVGL table widget for CSV display.
 */
lv_obj_t *pccsv_table_create(lv_obj_t *parent, int nrows, int ncols)
{
  g_table = lv_table_create(parent);
  lv_obj_set_size(g_table, 304, 260);
  lv_obj_align(g_table, LV_ALIGN_TOP_MID, 0, 4);

  /* Set column count */

  int visible_cols = (ncols < TABLE_MAX_VISIBLE_COLS)
                     ? ncols : TABLE_MAX_VISIBLE_COLS;
  lv_table_set_col_cnt(g_table, visible_cols);

  /* Set column widths */

  for (int c = 0; c < visible_cols; c++)
    {
      lv_table_set_col_width(g_table, c, TABLE_CELL_WIDTH);
    }

  /* Set row count (header + data) */

  int visible_rows = (nrows + 1 < TABLE_MAX_VISIBLE_ROWS)
                     ? nrows + 1 : TABLE_MAX_VISIBLE_ROWS;
  lv_table_set_row_cnt(g_table, visible_rows);

  /* Style header row */

  for (int c = 0; c < visible_cols; c++)
    {
      char hdr[4];
      hdr[0] = 'A' + c;
      hdr[1] = '\0';
      lv_table_set_cell_value(g_table, 0, c, hdr);
    }

  /* Table style */

  lv_obj_set_style_text_font(g_table, &lv_font_unscii_8, 0);
  lv_obj_set_style_border_width(g_table, 1,
                                LV_PART_ITEMS);
  lv_obj_set_style_border_color(g_table, lv_color_hex(0x444444),
                                LV_PART_ITEMS);

  g_sel_row = 1;
  g_sel_col = 0;

  syslog(LOG_DEBUG, "PCCSV: Table created %d×%d\n", nrows, ncols);
  return g_table;
}

/**
 * Populate the table from CSV data arrays.
 */
void pccsv_table_populate(char ***cells, int nrows, int ncols)
{
  if (g_table == NULL) return;

  int vis_rows = lv_table_get_row_cnt(g_table);
  int vis_cols = lv_table_get_col_cnt(g_table);

  for (int r = 0; r < vis_rows - 1 && r < nrows; r++)
    {
      for (int c = 0; c < vis_cols && c < ncols; c++)
        {
          int data_r = r + g_scroll_row;
          int data_c = c + g_scroll_col;

          const char *val = "";
          if (data_r < nrows && data_c < ncols && cells[data_r])
            {
              val = cells[data_r][data_c] ? cells[data_r][data_c] : "";
            }

          lv_table_set_cell_value(g_table, r + 1, c, val);
        }
    }
}

/**
 * Move cell selection.
 */
void pccsv_table_move_selection(int dr, int dc, int max_rows, int max_cols)
{
  g_sel_row += dr;
  g_sel_col += dc;

  if (g_sel_row < 1) g_sel_row = 1;
  if (g_sel_row > max_rows) g_sel_row = max_rows;
  if (g_sel_col < 0) g_sel_col = 0;
  if (g_sel_col >= max_cols) g_sel_col = max_cols - 1;

  /* Highlight selected cell — use custom draw or style */
  /* TODO: Implement cell highlight via table cell user data */
}

/**
 * Get current selection.
 */
void pccsv_table_get_selection(int *row, int *col)
{
  *row = g_sel_row - 1 + g_scroll_row;  /* Convert to data index */
  *col = g_sel_col + g_scroll_col;
}
