/****************************************************************************
 * apps/pccsv/pccsv_parser.c
 *
 * RFC 4180 compliant CSV parser.
 * Handles quoted fields, embedded commas, embedded newlines,
 * and escaped quotes (double-quote "").
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "pcterm/app.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CSV_MAX_FIELD_LEN  1024
#define CSV_MAX_COLS       26     /* A-Z columns */
#define CSV_MAX_ROWS       10000

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef struct csv_data_s
{
  char   ***cells;          /* [row][col] string pointers */
  int      nrows;
  int      ncols;
  size_t   total_alloc;     /* Track PSRAM usage */
} csv_data_t;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static char *csv_strdup(const char *s)
{
  size_t len = strlen(s);
  char *dup = (char *)pc_app_psram_alloc(len + 1);
  if (dup) memcpy(dup, s, len + 1);
  return dup;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * Parse a CSV file and return structured data.
 */
csv_data_t *csv_parse(const char *data, size_t len)
{
  csv_data_t *csv = (csv_data_t *)pc_app_psram_alloc(sizeof(csv_data_t));
  if (csv == NULL) return NULL;

  memset(csv, 0, sizeof(csv_data_t));

  /* First pass: count rows to pre-allocate */

  int est_rows = 1;
  for (size_t i = 0; i < len; i++)
    {
      if (data[i] == '\n') est_rows++;
    }

  if (est_rows > CSV_MAX_ROWS) est_rows = CSV_MAX_ROWS;

  csv->cells = (char ***)pc_app_psram_alloc(sizeof(char **) * est_rows);
  if (csv->cells == NULL)
    {
      pc_app_psram_free(csv);
      return NULL;
    }

  /* Parse state machine */

  char field[CSV_MAX_FIELD_LEN];
  int  field_len = 0;
  bool in_quotes = false;
  int  col = 0;

  /* Allocate first row */

  csv->cells[0] = (char **)pc_app_psram_alloc(sizeof(char *) * CSV_MAX_COLS);
  if (csv->cells[0] == NULL)
    {
      pc_app_psram_free(csv->cells);
      pc_app_psram_free(csv);
      return NULL;
    }
  memset(csv->cells[0], 0, sizeof(char *) * CSV_MAX_COLS);

  for (size_t i = 0; i <= len; i++)
    {
      char ch = (i < len) ? data[i] : '\n';  /* Force final newline */

      if (in_quotes)
        {
          if (ch == '"')
            {
              /* Check for escaped quote "" */

              if (i + 1 < len && data[i + 1] == '"')
                {
                  if (field_len < CSV_MAX_FIELD_LEN - 1)
                    {
                      field[field_len++] = '"';
                    }
                  i++;  /* Skip second quote */
                }
              else
                {
                  in_quotes = false;
                }
            }
          else
            {
              if (field_len < CSV_MAX_FIELD_LEN - 1)
                {
                  field[field_len++] = ch;
                }
            }
        }
      else
        {
          if (ch == '"' && field_len == 0)
            {
              in_quotes = true;
            }
          else if (ch == ',')
            {
              /* End of field */

              field[field_len] = '\0';
              if (col < CSV_MAX_COLS)
                {
                  csv->cells[csv->nrows][col] = csv_strdup(field);
                }
              col++;
              field_len = 0;
            }
          else if (ch == '\n' || ch == '\r')
            {
              /* End of row */

              if (ch == '\r' && i + 1 < len && data[i + 1] == '\n')
                {
                  i++;  /* Skip LF after CR */
                }

              field[field_len] = '\0';
              if (col < CSV_MAX_COLS)
                {
                  csv->cells[csv->nrows][col] = csv_strdup(field);
                }

              col++;
              if (col > csv->ncols) csv->ncols = col;

              csv->nrows++;
              col = 0;
              field_len = 0;

              /* Allocate next row */

              if (csv->nrows < est_rows && i < len)
                {
                  csv->cells[csv->nrows] = (char **)pc_app_psram_alloc(
                    sizeof(char *) * CSV_MAX_COLS);
                  if (csv->cells[csv->nrows])
                    {
                      memset(csv->cells[csv->nrows], 0,
                             sizeof(char *) * CSV_MAX_COLS);
                    }
                }
            }
          else
            {
              if (field_len < CSV_MAX_FIELD_LEN - 1)
                {
                  field[field_len++] = ch;
                }
            }
        }
    }

  syslog(LOG_INFO, "CSV: Parsed %d rows × %d cols\n",
         csv->nrows, csv->ncols);
  return csv;
}

/**
 * Free all CSV data.
 */
void csv_free(csv_data_t *csv)
{
  if (csv == NULL) return;

  for (int r = 0; r < csv->nrows; r++)
    {
      if (csv->cells[r])
        {
          for (int c = 0; c < csv->ncols; c++)
            {
              if (csv->cells[r][c])
                {
                  pc_app_psram_free(csv->cells[r][c]);
                }
            }
          pc_app_psram_free(csv->cells[r]);
        }
    }

  pc_app_psram_free(csv->cells);
  pc_app_psram_free(csv);
}

/**
 * Serialize CSV data back to text (for saving).
 * Returns a PSRAM-allocated string.
 */
char *csv_serialize(const csv_data_t *csv, size_t *out_len)
{
  /* Estimate output size */

  size_t est = csv->nrows * csv->ncols * 32;
  char *out = (char *)pc_app_psram_alloc(est);
  if (out == NULL) return NULL;

  size_t pos = 0;

  for (int r = 0; r < csv->nrows; r++)
    {
      for (int c = 0; c < csv->ncols; c++)
        {
          const char *cell = csv->cells[r] ? csv->cells[r][c] : NULL;
          if (cell == NULL) cell = "";

          /* Check if field needs quoting */

          bool needs_quote = (strchr(cell, ',') != NULL ||
                              strchr(cell, '"') != NULL ||
                              strchr(cell, '\n') != NULL);

          if (needs_quote)
            {
              out[pos++] = '"';
              for (const char *p = cell; *p && pos < est - 4; p++)
                {
                  if (*p == '"') out[pos++] = '"';  /* Escape */
                  out[pos++] = *p;
                }
              out[pos++] = '"';
            }
          else
            {
              size_t clen = strlen(cell);
              if (pos + clen < est)
                {
                  memcpy(out + pos, cell, clen);
                  pos += clen;
                }
            }

          if (c < csv->ncols - 1) out[pos++] = ',';
        }

      out[pos++] = '\n';
    }

  out[pos] = '\0';
  if (out_len) *out_len = pos;
  return out;
}
