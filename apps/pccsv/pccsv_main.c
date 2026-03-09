/****************************************************************************
 * apps/pccsv/pccsv_main.c
 *
 * CSV spreadsheet viewer/editor.
 * Features: RFC 4180 parsing, LVGL table widget, cell editing.
 *
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>
#include <lvgl/lvgl.h>

#include "pcterm/app.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct csv_data_s
{
  char   **cells;       /* 2D array of cell strings (PSRAM) */
  int      rows;
  int      cols;
  int      max_rows;
  int      max_cols;
  char     filepath[256];
  bool     modified;
} csv_data_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static csv_data_t g_csv;
static lv_obj_t *g_table     = NULL;
static lv_obj_t *g_status    = NULL;
static int       g_sel_row   = 1;
static int       g_sel_col   = 0;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Allocate a cell string in PSRAM */

static char *csv_cell_alloc(const char *value)
{
  size_t len = strlen(value) + 1;
  char *cell = (char *)pc_app_psram_alloc(len);
  if (cell) memcpy(cell, value, len);
  return cell;
}

/* Ensure we have room for (row, col) */

static void csv_ensure_capacity(csv_data_t *csv, int row, int col)
{
  if (row >= csv->max_rows || col >= csv->max_cols)
    {
      int new_max_rows = (row >= csv->max_rows) ? row + 64 : csv->max_rows;
      int new_max_cols = (col >= csv->max_cols) ? col + 8  : csv->max_cols;
      size_t new_size = new_max_rows * new_max_cols * sizeof(char *);

      char **new_cells = (char **)pc_app_psram_alloc(new_size);
      if (new_cells == NULL) return;
      memset(new_cells, 0, new_size);

      /* Copy old data */

      for (int r = 0; r < csv->rows; r++)
        {
          for (int c = 0; c < csv->cols; c++)
            {
              new_cells[r * new_max_cols + c] =
                csv->cells[r * csv->max_cols + c];
            }
        }

      if (csv->cells)
        {
          pc_app_psram_free(csv->cells);
        }

      csv->cells     = new_cells;
      csv->max_rows  = new_max_rows;
      csv->max_cols  = new_max_cols;
    }
}

/* Parse a CSV file (RFC 4180) */

static int csv_load_file(csv_data_t *csv, const char *path)
{
  FILE *f = fopen(path, "r");
  if (f == NULL) return PC_ERR_NOENT;

  strncpy(csv->filepath, path, sizeof(csv->filepath) - 1);
  csv->rows = 0;
  csv->cols = 0;

  /* Allocate initial cell storage */

  csv_ensure_capacity(csv, 64, 8);

  char line[1024];
  while (fgets(line, sizeof(line), f))
    {
      int col = 0;
      char *p = line;

      while (*p && *p != '\n' && *p != '\r')
        {
          char value[256] = {0};
          int vi = 0;
          bool quoted = false;

          if (*p == '"')
            {
              quoted = true;
              p++;
              while (*p)
                {
                  if (*p == '"' && *(p + 1) == '"')
                    {
                      if (vi < 255) value[vi++] = '"';
                      p += 2;
                    }
                  else if (*p == '"')
                    {
                      p++;
                      break;
                    }
                  else
                    {
                      if (vi < 255) value[vi++] = *p;
                      p++;
                    }
                }
              if (*p == ',') p++;
            }
          else
            {
              while (*p && *p != ',' && *p != '\n' && *p != '\r')
                {
                  if (vi < 255) value[vi++] = *p;
                  p++;
                }
              if (*p == ',') p++;
            }

          value[vi] = '\0';
          csv_ensure_capacity(csv, csv->rows, col);
          csv->cells[csv->rows * csv->max_cols + col] = csv_cell_alloc(value);
          col++;
        }

      if (col > csv->cols) csv->cols = col;
      csv->rows++;
    }

  fclose(f);

  syslog(LOG_INFO, "PCCSV: Loaded %s (%d rows, %d cols)\n",
         path, csv->rows, csv->cols);
  return PC_OK;
}

/* Save CSV to file */

static int csv_save_file(csv_data_t *csv)
{
  if (csv->filepath[0] == '\0') return PC_ERR_NOENT;

  FILE *f = fopen(csv->filepath, "w");
  if (f == NULL) return PC_ERR_IO;

  for (int r = 0; r < csv->rows; r++)
    {
      for (int c = 0; c < csv->cols; c++)
        {
          if (c > 0) fputc(',', f);

          const char *val = csv->cells[r * csv->max_cols + c];
          if (val == NULL) val = "";

          /* Quote if contains comma, quote, or newline */

          if (strchr(val, ',') || strchr(val, '"') || strchr(val, '\n'))
            {
              fputc('"', f);
              for (const char *p = val; *p; p++)
                {
                  if (*p == '"') fputc('"', f);
                  fputc(*p, f);
                }
              fputc('"', f);
            }
          else
            {
              fputs(val, f);
            }
        }
      fputc('\n', f);
    }

  fclose(f);
  csv->modified = false;

  syslog(LOG_INFO, "PCCSV: Saved %s\n", csv->filepath);
  return PC_OK;
}

/* Populate LVGL table from CSV data */

static void csv_populate_table(void)
{
  if (g_table == NULL) return;

  int vis_cols = (g_csv.cols > 0) ? g_csv.cols : 5;
  int vis_rows = g_csv.rows + 1;  /* +1 for header row */

  if (vis_cols > 26) vis_cols = 26;
  if (vis_rows > 100) vis_rows = 100;

  lv_table_set_col_cnt(g_table, vis_cols);
  lv_table_set_row_cnt(g_table, vis_rows);

  for (int c = 0; c < vis_cols; c++)
    {
      lv_table_set_col_width(g_table, c, 60);
      char hdr[2] = { 'A' + c, '\0' };
      lv_table_set_cell_value(g_table, 0, c, hdr);
    }

  for (int r = 0; r < g_csv.rows && r < vis_rows - 1; r++)
    {
      for (int c = 0; c < vis_cols; c++)
        {
          const char *val = "";
          if (c < g_csv.max_cols &&
              g_csv.cells[r * g_csv.max_cols + c] != NULL)
            {
              val = g_csv.cells[r * g_csv.max_cols + c];
            }
          lv_table_set_cell_value(g_table, r + 1, c, val);
        }
    }
}

static void csv_update_status(void)
{
  if (g_status == NULL) return;

  char status[80];
  char col_letter = 'A' + g_sel_col;
  snprintf(status, sizeof(status),
           "Row:%d Col:%c %s | [Enter] Edit [Tab] Next [Ctrl+S] Save",
           g_sel_row, col_letter,
           g_csv.modified ? "[+]" : "");
  lv_label_set_text(g_status, status);
}

/* Key handler for CSV navigation and editing */

static void pccsv_key_handler(lv_event_t *e)
{
  uint32_t key = lv_event_get_key(e);

  switch (key)
    {
      case LV_KEY_DOWN:
        if (g_sel_row < g_csv.rows) g_sel_row++;
        break;

      case LV_KEY_UP:
        if (g_sel_row > 1) g_sel_row--;
        break;

      case LV_KEY_RIGHT:
      case '\t':
        if (g_sel_col < g_csv.cols - 1) g_sel_col++;
        break;

      case LV_KEY_LEFT:
        if (g_sel_col > 0) g_sel_col--;
        break;

      default:
        return;
    }

  csv_update_status();
}

static int pccsv_main(int argc, char *argv[])
{
  lv_obj_t *screen = pc_app_get_screen();

  /* Initialize CSV data */

  memset(&g_csv, 0, sizeof(g_csv));

  /* Create table widget */

  g_table = lv_table_create(screen);
  lv_obj_set_size(g_table, 320, 280);
  lv_obj_align(g_table, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_text_font(g_table, &lv_font_unscii_8, 0);

  /* Try to load CSV file from arguments or default location */

  if (argc > 1 && argv[1] != NULL)
    {
      csv_load_file(&g_csv, argv[1]);
      csv_populate_table();
    }
  else
    {
      /* Show empty 10×5 grid */

      lv_table_set_col_cnt(g_table, 5);
      lv_table_set_row_cnt(g_table, 11);

      for (int c = 0; c < 5; c++)
        {
          lv_table_set_col_width(g_table, c, 60);
          char hdr[2] = { 'A' + c, '\0' };
          lv_table_set_cell_value(g_table, 0, c, hdr);
        }

      g_csv.cols = 5;
      g_csv.rows = 10;
    }

  /* Register keyboard handler */

  lv_obj_add_event_cb(g_table, pccsv_key_handler, LV_EVENT_KEY, NULL);

  lv_group_t *grp = lv_group_get_default();
  if (grp != NULL)
    {
      lv_group_add_obj(grp, g_table);
      lv_group_focus_obj(g_table);
    }

  /* Status bar at bottom */

  g_status = lv_label_create(screen);
  lv_obj_align(g_status, LV_ALIGN_BOTTOM_LEFT, 4, -4);
  lv_obj_set_style_text_font(g_status, &lv_font_unscii_8, 0);
  lv_obj_set_style_text_color(g_status, lv_color_make(180, 180, 180), 0);

  g_sel_row = 1;
  g_sel_col = 0;
  csv_update_status();

  return 0;
}

static int pccsv_save(void *buf, size_t *len)
{
  /* Save filepath and cursor position */

  size_t need = sizeof(g_csv.filepath) + sizeof(int) * 2;
  if (need > *len) return PC_ERR_TOOBIG;

  char *p = (char *)buf;
  memcpy(p, g_csv.filepath, sizeof(g_csv.filepath));
  p += sizeof(g_csv.filepath);
  memcpy(p, &g_sel_row, sizeof(int));
  p += sizeof(int);
  memcpy(p, &g_sel_col, sizeof(int));

  /* Save to file if modified */

  if (g_csv.modified)
    {
      csv_save_file(&g_csv);
    }

  *len = need;
  return PC_OK;
}

static int pccsv_restore(const void *buf, size_t len)
{
  size_t need = sizeof(g_csv.filepath) + sizeof(int) * 2;
  if (len < need) return PC_ERR_INVAL;

  const char *p = (const char *)buf;
  char filepath[256];
  memcpy(filepath, p, sizeof(filepath));
  p += sizeof(filepath);
  memcpy(&g_sel_row, p, sizeof(int));
  p += sizeof(int);
  memcpy(&g_sel_col, p, sizeof(int));

  /* Reload file */

  if (filepath[0] != '\0')
    {
      csv_load_file(&g_csv, filepath);
    }

  return PC_OK;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

const pc_app_t g_pccsv_app = {
  .info = {
    .name         = "pccsv",
    .display_name = "CSV Editor",
    .version      = "1.0.0",
    .category     = "office",
    .min_ram      = 131072,
    .flags        = PC_APP_FLAG_BUILTIN | PC_APP_FLAG_STATEFUL,
  },
  .main    = pccsv_main,
  .save    = pccsv_save,
  .restore = pccsv_restore,
};
