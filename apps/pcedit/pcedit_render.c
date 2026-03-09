/****************************************************************************
 * apps/pcedit/pcedit_render.c
 *
 * Canvas-based text rendering for the vi/vim editor (LVGL v9).
 *
 * Features:
 *   - Syntax-highlighted text via pcedit_syntax.c
 *   - Line numbers (absolute and relative)
 *   - Cursor block / line indicator
 *   - Cursorline highlighting
 *   - Search match highlighting (hlsearch)
 *   - Visual selection highlighting
 *   - Status line with mode, filename, position
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <lvgl/lvgl.h>
#include "pcterm/app.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define EDITOR_COLS       53      /* 320 / 6 */
#define EDITOR_ROWS       23      /* 288 / 12 = 24 rows, 23 text + 1 status */
#define CELL_W            6
#define CELL_H            12
#define LINE_NUM_WIDTH    4       /* 4 chars for line numbers */
#define TEXT_AREA_X       (LINE_NUM_WIDTH * CELL_W)
#define TEXT_COLS         (EDITOR_COLS - LINE_NUM_WIDTH)
#define MAX_SPANS         64

/****************************************************************************
 * External declarations
 ****************************************************************************/

/* pcedit_syntax.c */

typedef enum
{
  SYN_NORMAL, SYN_KEYWORD, SYN_TYPE, SYN_STRING, SYN_CHAR,
  SYN_NUMBER, SYN_COMMENT, SYN_PREPROC, SYN_FUNCTION,
  SYN_OPERATOR, SYN_BRACKET, SYN_CONSTANT, SYN_SPECIAL,
} syntax_class_t;

typedef struct syntax_span_s
{
  uint16_t       start;
  uint16_t       end;
  syntax_class_t cls;
} syntax_span_t;

extern int         syntax_highlight_line(const char *line, size_t len,
                                          syntax_span_t *spans, int max);
extern lv_color_t  syntax_get_color(syntax_class_t cls);
extern void        syntax_reset_block_state(void);

/* pcedit_search.c */

extern bool        search_is_active(void);
extern int         search_find_in_line(const char *line, int len,
                                        int start, bool ic, int *mlen);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/**
 * Draw a filled rectangle on a canvas layer.
 */
static void layer_fill_rect(lv_layer_t *layer, int x, int y,
                              int w, int h, lv_color_t color)
{
  lv_draw_rect_dsc_t rect_dsc;
  lv_draw_rect_dsc_init(&rect_dsc);
  rect_dsc.bg_color = color;
  rect_dsc.bg_opa   = LV_OPA_COVER;
  rect_dsc.radius   = 0;

  lv_area_t area = { x, y, x + w - 1, y + h - 1 };
  lv_draw_rect(layer, &rect_dsc, &area);
}

/**
 * Get color for a column within syntax spans.
 */
static lv_color_t get_span_color(int col, const syntax_span_t *spans,
                                  int span_count)
{
  for (int s = 0; s < span_count; s++)
    {
      if (col >= spans[s].start && col < spans[s].end)
        {
          return syntax_get_color(spans[s].cls);
        }
    }

  return lv_color_white();
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * Render the visible portion of the text buffer to the LVGL canvas.
 *
 * @param canvas     LVGL canvas object
 * @param text       Pointer to linearized text
 * @param text_len   Total text length
 * @param cursor_x   Cursor column
 * @param cursor_y   Cursor row (absolute line number)
 * @param scroll_y   First visible line number
 * @param mode_str   Current vi mode string (for status line)
 * @param filename   Current filename (for status line)
 * @param modified   File modified flag
 */
void pcedit_render(lv_obj_t *canvas,
                   const char *text, size_t text_len,
                   int cursor_x, int cursor_y, int scroll_y,
                   const char *mode_str, const char *filename,
                   bool modified)
{
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);

  /* Open a drawing layer on the canvas */

  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);

  /* Prepare default text descriptor */

  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.font  = &lv_font_unscii_8;
  dsc.color = lv_color_white();

  /* Reset syntax block comment state for the visible region.
   * For accuracy we should track from file start, but we
   * approximate by resetting at each render pass. */

  syntax_reset_block_state();

  /* Pre-scan lines before scroll_y so block comment state is correct */

  {
    int line = 0;
    size_t pos = 0;

    while (line < scroll_y && pos < text_len)
      {
        /* Extract line for syntax state tracking */

        const char *line_start = text + pos;
        size_t line_len = 0;

        while (pos + line_len < text_len &&
               text[pos + line_len] != '\n')
          {
            line_len++;
          }

        syntax_span_t dummy_spans[4];
        syntax_highlight_line(line_start, line_len, dummy_spans, 4);

        pos += line_len;
        if (pos < text_len && text[pos] == '\n') pos++;
        line++;
      }
  }

  /* Find the start of scroll_y-th line in the text */

  int line = 0;
  size_t pos = 0;

  while (line < scroll_y && pos < text_len)
    {
      if (text[pos] == '\n') line++;
      pos++;
    }

  /* Render visible lines */

  for (int row = 0; row < EDITOR_ROWS && pos <= text_len; row++)
    {
      int display_line = scroll_y + row;
      int py = row * CELL_H;
      bool is_cursor_line = (display_line == cursor_y);

      /* Cursorline background highlight */

      if (is_cursor_line)
        {
          layer_fill_rect(&layer, 0, py, 320, CELL_H,
                           lv_color_hex(0x1A1A2E));
        }

      /* Line number */

      char num_buf[8];
      snprintf(num_buf, sizeof(num_buf), "%3d ", display_line + 1);

      lv_draw_label_dsc_t num_dsc = dsc;
      num_dsc.color = is_cursor_line ?
                      lv_color_hex(0xFFFF00) :  /* Bright for cursorline */
                      lv_color_hex(0x666666);
      num_dsc.text = num_buf;
      {
        lv_area_t area = { 0, py + 2,
                           LINE_NUM_WIDTH * CELL_W - 1, py + CELL_H - 1 };
        lv_draw_label(&layer, &num_dsc, &area);
      }

      /* Extract line text */

      const char *line_start = text + pos;
      size_t line_len = 0;

      while (pos + line_len < text_len &&
             text[pos + line_len] != '\n')
        {
          line_len++;
        }

      /* Syntax highlighting for this line */

      syntax_span_t spans[MAX_SPANS];
      int span_count = syntax_highlight_line(line_start, line_len,
                                              spans, MAX_SPANS);

      /* Render characters with syntax colors */

      int col = 0;
      size_t ci = 0;

      while (ci < line_len && col < TEXT_COLS)
        {
          char ch = line_start[ci];
          int px = TEXT_AREA_X + col * CELL_W;

          /* Check for search match highlighting */

          bool in_search_match = false;

          if (search_is_active())
            {
              int mlen;
              int mcol = search_find_in_line(line_start, line_len,
                                              0, true, &mlen);
              /* Simple check: scan all matches on the line */

              int scan_col = 0;
              while (scan_col < (int)line_len)
                {
                  int sc = search_find_in_line(line_start, line_len,
                                                scan_col, true, &mlen);
                  if (sc < 0) break;

                  if ((int)ci >= sc && (int)ci < sc + mlen)
                    {
                      in_search_match = true;
                      break;
                    }

                  scan_col = sc + (mlen > 0 ? mlen : 1);
                }
            }

          /* Draw search match background */

          if (in_search_match)
            {
              layer_fill_rect(&layer, px, py, CELL_W, CELL_H,
                               lv_color_hex(0x4A3000));
            }

          /* Character color from syntax */

          char ch_buf[2] = { ch, '\0' };
          lv_draw_label_dsc_t ch_dsc = dsc;
          ch_dsc.color = get_span_color((int)ci, spans, span_count);
          ch_dsc.text = ch_buf;
          ch_dsc.text_local = 1;

          {
            lv_area_t area = { px, py + 2, px + CELL_W, py + CELL_H - 1 };
            lv_draw_label(&layer, &ch_dsc, &area);
          }
          col++;
          ci++;
        }

      /* Draw cursor block */

      if (is_cursor_line && cursor_x >= 0)
        {
          int cx = TEXT_AREA_X + cursor_x * CELL_W;
          int cy_px = py;

          /* Draw white cursor block */

          layer_fill_rect(&layer, cx, cy_px, CELL_W, CELL_H,
                          lv_color_white());

          /* Re-draw the character under cursor in black */

          if ((size_t)cursor_x < line_len)
            {
              char cur_ch[2] = { line_start[cursor_x], '\0' };
              lv_draw_label_dsc_t cur_dsc = dsc;
              cur_dsc.color = lv_color_black();
              cur_dsc.text = cur_ch;
              cur_dsc.text_local = 1;

              lv_area_t area = { cx, cy_px + 2,
                                 cx + CELL_W, cy_px + CELL_H - 1 };
              lv_draw_label(&layer, &cur_dsc, &area);
            }
        }

      /* Advance past newline */

      pos += line_len;
      if (pos < text_len && text[pos] == '\n')
        {
          pos++;
        }
    }

  /* Status line at bottom */

  int status_y = EDITOR_ROWS * CELL_H;

  /* Background bar */

  layer_fill_rect(&layer, 0, status_y, 320, CELL_H,
                   lv_color_hex(0x333333));

  /* Mode indicator */

  lv_draw_label_dsc_t status_dsc = dsc;
  status_dsc.color = lv_color_hex(0x00FF00);
  status_dsc.text = mode_str;
  {
    lv_area_t area = { 2, status_y + 2, 101, status_y + CELL_H - 1 };
    lv_draw_label(&layer, &status_dsc, &area);
  }

  /* Filename + modified flag */

  char file_info[64];
  snprintf(file_info, sizeof(file_info), "%s%s",
           filename ? filename : "[No Name]",
           modified ? " [+]" : "");

  status_dsc.color = lv_color_white();
  status_dsc.text = file_info;
  status_dsc.text_local = 1;
  {
    lv_area_t area = { 110, status_y + 2, 249, status_y + CELL_H - 1 };
    lv_draw_label(&layer, &status_dsc, &area);
  }

  /* Cursor position */

  char pos_buf[16];
  snprintf(pos_buf, sizeof(pos_buf), "%d:%d",
           cursor_y + 1, cursor_x + 1);

  status_dsc.color = lv_color_hex(0xAAAAAA);
  status_dsc.text = pos_buf;
  status_dsc.text_local = 1;
  {
    lv_area_t area = { 280, status_y + 2, 319, status_y + CELL_H - 1 };
    lv_draw_label(&layer, &status_dsc, &area);
  }

  /* Finalize the drawing layer and invalidate */

  lv_canvas_finish_layer(canvas, &layer);
  lv_obj_invalidate(canvas);
}
