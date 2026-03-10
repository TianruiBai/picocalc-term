/****************************************************************************
 * pcterm/src/terminal_widget.c
 *
 * VT100/ANSI terminal emulator widget for LVGL.
 * Shared between pcterm (local NuttShell) and pcssh (SSH client).
 *
 * Supports:
 *   - Basic text output with cursor movement
 *   - ANSI SGR colors (8 foreground + 8 background)
 *   - Bold, underline, inverse attributes
 *   - Cursor movement (CUU/CUD/CUF/CUB), absolute (CUP)
 *   - Erase (ED, EL)
 *   - Scroll region (DECSTBM)
 *   - Scrollback buffer
 *   - Keyboard input → ANSI escape sequences
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <lvgl/lvgl.h>

#include "pcterm/terminal.h"
#include "pcterm/app.h"

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

/* ANSI color palette */

static const lv_color_t g_ansi_colors[8] =
{
  /* Black   */ LV_COLOR_MAKE(0x00, 0x00, 0x00),
  /* Red     */ LV_COLOR_MAKE(0xFF, 0x00, 0x00),
  /* Green   */ LV_COLOR_MAKE(0x00, 0xFF, 0x00),
  /* Yellow  */ LV_COLOR_MAKE(0xFF, 0xFF, 0x00),
  /* Blue    */ LV_COLOR_MAKE(0x00, 0x00, 0xFF),
  /* Magenta */ LV_COLOR_MAKE(0xFF, 0x00, 0xFF),
  /* Cyan    */ LV_COLOR_MAKE(0x00, 0xFF, 0xFF),
  /* White   */ LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
};

/* PicoCalc official south-bridge keycodes */

#define KBD_KEY_ESC         0xB1
#define KBD_KEY_LEFT        0xB4
#define KBD_KEY_UP          0xB5
#define KBD_KEY_DOWN        0xB6
#define KBD_KEY_RIGHT       0xB7
#define KBD_KEY_INSERT      0xD1
#define KBD_KEY_HOME        0xD2
#define KBD_KEY_DELETE      0xD4
#define KBD_KEY_END         0xD5
#define KBD_KEY_PGUP        0xD6
#define KBD_KEY_PGDN        0xD7

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: term_cell_clear
 ****************************************************************************/

static void term_cell_clear(term_cell_t *cell)
{
  cell->ch   = ' ';
  cell->fg   = TERM_COLOR_WHITE;
  cell->bg   = TERM_COLOR_BLACK;
  cell->attr = 0;
}

/****************************************************************************
 * Name: term_clear_region
 *
 * Description:
 *   Clear a rectangular region of the grid.
 *
 ****************************************************************************/

static void term_clear_region(terminal_t *term,
                              int r1, int c1, int r2, int c2)
{
  for (int r = r1; r <= r2 && r < TERM_ROWS; r++)
    {
      for (int c = c1; c <= c2 && c < TERM_COLS; c++)
        {
          term_cell_clear(&term->grid[r * TERM_COLS + c]);
        }
    }

  term->dirty = true;
}

/****************************************************************************
 * Name: term_scroll_up
 *
 * Description:
 *   Scroll the scroll region up by one line.
 *   Copies the top line into the scrollback buffer.
 *
 ****************************************************************************/

static void term_scroll_up(terminal_t *term)
{
  int top = term->scroll_top;
  int bot = term->scroll_bottom;

  /* Copy top line of scroll region to scrollback */

  if (term->scrollback != NULL && top == 0)
    {
      memmove(&term->scrollback[TERM_COLS],
              &term->scrollback[0],
              sizeof(term_cell_t) * TERM_COLS * (TERM_SCROLLBACK - 1));
      memcpy(&term->scrollback[0],
             &term->grid[top * TERM_COLS],
             sizeof(term_cell_t) * TERM_COLS);
    }

  /* Shift rows up within the scroll region */

  for (int r = top; r < bot; r++)
    {
      memcpy(&term->grid[r * TERM_COLS],
             &term->grid[(r + 1) * TERM_COLS],
             sizeof(term_cell_t) * TERM_COLS);
    }

  /* Clear the bottom row */

  term_clear_region(term, bot, 0, bot, TERM_COLS - 1);
}

/****************************************************************************
 * Name: term_scroll_down
 *
 * Description:
 *   Scroll the scroll region down by one line.
 *
 ****************************************************************************/

static void term_scroll_down(terminal_t *term)
{
  int top = term->scroll_top;
  int bot = term->scroll_bottom;

  for (int r = bot; r > top; r--)
    {
      memcpy(&term->grid[r * TERM_COLS],
             &term->grid[(r - 1) * TERM_COLS],
             sizeof(term_cell_t) * TERM_COLS);
    }

  term_clear_region(term, top, 0, top, TERM_COLS - 1);
}

/****************************************************************************
 * Name: term_newline
 *
 * Description:
 *   Move cursor to next line, scrolling if necessary.
 *
 ****************************************************************************/

static void term_newline(terminal_t *term)
{
  term->cursor_x = 0;

  if (term->cursor_y >= term->scroll_bottom)
    {
      term_scroll_up(term);
    }
  else
    {
      term->cursor_y++;
    }
}

/****************************************************************************
 * Name: term_put_char
 *
 * Description:
 *   Place a character at the current cursor position.
 *
 ****************************************************************************/

static void term_put_char(terminal_t *term, char ch)
{
  if (term->cursor_x >= TERM_COLS)
    {
      /* Autowrap */

      term_newline(term);
    }

  int idx = term->cursor_y * TERM_COLS + term->cursor_x;
  term->grid[idx].ch   = ch;
  term->grid[idx].fg   = term->cur_fg;
  term->grid[idx].bg   = term->cur_bg;
  term->grid[idx].attr = term->cur_attr;

  term->cursor_x++;
  term->dirty = true;
}

/****************************************************************************
 * Name: term_process_csi
 *
 * Description:
 *   Process a CSI (Control Sequence Introducer) command.
 *   ESC [ <params> <command>
 *
 ****************************************************************************/

static void term_process_csi(terminal_t *term, char cmd)
{
  int p0 = (term->csi_nparam > 0) ? term->csi_params[0] : 0;
  int p1 = (term->csi_nparam > 1) ? term->csi_params[1] : 0;

  switch (cmd)
    {
      /* Cursor movement */

      case 'A':  /* CUU — Cursor Up */
        term->cursor_y -= (p0 > 0) ? p0 : 1;
        if (term->cursor_y < 0) term->cursor_y = 0;
        break;

      case 'B':  /* CUD — Cursor Down */
        term->cursor_y += (p0 > 0) ? p0 : 1;
        if (term->cursor_y >= TERM_ROWS) term->cursor_y = TERM_ROWS - 1;
        break;

      case 'C':  /* CUF — Cursor Forward */
        term->cursor_x += (p0 > 0) ? p0 : 1;
        if (term->cursor_x >= TERM_COLS) term->cursor_x = TERM_COLS - 1;
        break;

      case 'D':  /* CUB — Cursor Back */
        term->cursor_x -= (p0 > 0) ? p0 : 1;
        if (term->cursor_x < 0) term->cursor_x = 0;
        break;

      case 'H':  /* CUP — Cursor Position */
      case 'f':
        term->cursor_y = ((p0 > 0) ? p0 : 1) - 1;
        term->cursor_x = ((p1 > 0) ? p1 : 1) - 1;
        if (term->cursor_y >= TERM_ROWS) term->cursor_y = TERM_ROWS - 1;
        if (term->cursor_x >= TERM_COLS) term->cursor_x = TERM_COLS - 1;
        break;

      /* Erase */

      case 'J':  /* ED — Erase Display */
        if (p0 == 0)
          {
            /* Erase below (including cursor) */
            term_clear_region(term, term->cursor_y, term->cursor_x,
                              TERM_ROWS - 1, TERM_COLS - 1);
          }
        else if (p0 == 1)
          {
            /* Erase above (including cursor) */
            term_clear_region(term, 0, 0,
                              term->cursor_y, term->cursor_x);
          }
        else if (p0 == 2)
          {
            /* Erase entire screen */
            term_clear_region(term, 0, 0,
                              TERM_ROWS - 1, TERM_COLS - 1);
          }
        break;

      case 'K':  /* EL — Erase Line */
        if (p0 == 0)
          {
            term_clear_region(term, term->cursor_y, term->cursor_x,
                              term->cursor_y, TERM_COLS - 1);
          }
        else if (p0 == 1)
          {
            term_clear_region(term, term->cursor_y, 0,
                              term->cursor_y, term->cursor_x);
          }
        else if (p0 == 2)
          {
            term_clear_region(term, term->cursor_y, 0,
                              term->cursor_y, TERM_COLS - 1);
          }
        break;

      /* SGR — Set Graphic Rendition */

      case 'm':
        for (int i = 0; i < term->csi_nparam; i++)
          {
            int sgr = term->csi_params[i];

            if (sgr == 0)
              {
                /* Reset */
                term->cur_fg   = TERM_COLOR_WHITE;
                term->cur_bg   = TERM_COLOR_BLACK;
                term->cur_attr = 0;
              }
            else if (sgr == 1)
              {
                term->cur_attr |= TERM_ATTR_BOLD;
              }
            else if (sgr == 4)
              {
                term->cur_attr |= TERM_ATTR_UNDERLINE;
              }
            else if (sgr == 7)
              {
                term->cur_attr |= TERM_ATTR_INVERSE;
              }
            else if (sgr >= 30 && sgr <= 37)
              {
                term->cur_fg = sgr - 30;
              }
            else if (sgr >= 40 && sgr <= 47)
              {
                term->cur_bg = sgr - 40;
              }
            else if (sgr == 39)
              {
                term->cur_fg = TERM_COLOR_WHITE;  /* Default FG */
              }
            else if (sgr == 49)
              {
                term->cur_bg = TERM_COLOR_BLACK;  /* Default BG */
              }
          }

        /* Handle missing parameter (ESC[m = ESC[0m) */

        if (term->csi_nparam == 0)
          {
            term->cur_fg   = TERM_COLOR_WHITE;
            term->cur_bg   = TERM_COLOR_BLACK;
            term->cur_attr = 0;
          }
        break;

      /* Scroll region */

      case 'r':  /* DECSTBM — Set Top and Bottom Margins */
        term->scroll_top    = ((p0 > 0) ? p0 : 1) - 1;
        term->scroll_bottom = ((p1 > 0) ? p1 : TERM_ROWS) - 1;
        if (term->scroll_top < 0) term->scroll_top = 0;
        if (term->scroll_bottom >= TERM_ROWS)
          {
            term->scroll_bottom = TERM_ROWS - 1;
          }
        term->cursor_x = 0;
        term->cursor_y = 0;
        break;

      /* Cursor visibility */

      case 'l':  /* DECTCEM — hide cursor (ESC[?25l) */
        if (p0 == 25)
          {
            term->cursor_visible = false;
          }
        break;

      case 'h':  /* DECTCEM — show cursor (ESC[?25h) */
        if (p0 == 25)
          {
            term->cursor_visible = true;
          }
        break;

      default:
        /* Unhandled CSI command */
        break;
    }

  term->dirty = true;
}

/****************************************************************************
 * Name: terminal_key_event_cb
 *
 * Description:
 *   LVGL key event callback for the terminal canvas.
 *   Forwards LVGL key events into terminal_input_key().
 *
 ****************************************************************************/

static void terminal_key_event_cb(lv_event_t *e)
{
  terminal_t *term = (terminal_t *)lv_event_get_user_data(e);
  uint32_t key = lv_event_get_key(e);

  if (term != NULL)
    {
      terminal_input_key(term, (uint8_t)key, 0);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: terminal_create
 *
 * Description:
 *   Allocate a terminal widget with grid, scrollback, and LVGL canvas.
 *   All large buffers are allocated in PSRAM.
 *
 ****************************************************************************/

terminal_t *terminal_create(lv_obj_t *parent,
                            void (*write_cb)(const char *, size_t, void *),
                            void *user)
{
  terminal_t *term;
  size_t grid_size;
  size_t scrollback_size;
  size_t canvas_size;

  /* Allocate terminal struct in PSRAM */

  term = (terminal_t *)pc_app_psram_alloc(sizeof(terminal_t));
  if (term == NULL)
    {
      syslog(LOG_ERR, "term: Failed to allocate terminal struct\n");
      return NULL;
    }

  memset(term, 0, sizeof(terminal_t));

  /* Allocate grid: COLS × ROWS cells */

  grid_size = sizeof(term_cell_t) * TERM_COLS * TERM_ROWS;
  term->grid = (term_cell_t *)pc_app_psram_alloc(grid_size);
  if (term->grid == NULL)
    {
      syslog(LOG_ERR, "term: Failed to allocate grid (%zu bytes)\n",
             grid_size);
      pc_app_psram_free(term);
      return NULL;
    }

  /* Allocate scrollback buffer */

  scrollback_size = sizeof(term_cell_t) * TERM_COLS * TERM_SCROLLBACK;
  term->scrollback = (term_cell_t *)pc_app_psram_alloc(scrollback_size);
  if (term->scrollback == NULL)
    {
      syslog(LOG_WARNING, "term: No scrollback (PSRAM low)\n");
      /* Non-fatal — proceed without scrollback */
    }

  /* Initialize grid to blanks */

  for (int i = 0; i < TERM_COLS * TERM_ROWS; i++)
    {
      term_cell_clear(&term->grid[i]);
    }

  if (term->scrollback != NULL)
    {
      for (int i = 0; i < TERM_COLS * TERM_SCROLLBACK; i++)
        {
          term_cell_clear(&term->scrollback[i]);
        }
    }

  /* Default attributes */

  term->cur_fg         = TERM_COLOR_WHITE;
  term->cur_bg         = TERM_COLOR_BLACK;
  term->cur_attr       = 0;
  term->cursor_visible = true;
  term->scroll_top     = 0;
  term->scroll_bottom  = TERM_ROWS - 1;
  term->parse_state    = TERM_STATE_GROUND;

  /* Write callback */

  term->write_cb   = write_cb;
  term->write_user = user;

  /* Create LVGL canvas for rendering the terminal grid.
   * Canvas size: TERM_COLS * TERM_CELL_W × TERM_ROWS * TERM_CELL_H
   *            = 53 * 6 × 25 * 12 = 318 × 300 pixels
   * Buffer: 318 × 300 × 2 bytes (RGB565) = ~186KB → PSRAM
   */

  canvas_size = sizeof(lv_color_t) *
                (TERM_COLS * TERM_CELL_W) *
                (TERM_ROWS * TERM_CELL_H);

  term->canvas_buf = (lv_color_t *)pc_app_psram_alloc(canvas_size);
  if (term->canvas_buf == NULL)
    {
      syslog(LOG_ERR, "term: Failed to allocate canvas (%zu bytes)\n",
             canvas_size);
      if (term->scrollback) pc_app_psram_free(term->scrollback);
      pc_app_psram_free(term->grid);
      pc_app_psram_free(term);
      return NULL;
    }

  term->canvas = lv_canvas_create(parent);
  lv_canvas_set_buffer(term->canvas, term->canvas_buf,
                       TERM_COLS * TERM_CELL_W,
                       TERM_ROWS * TERM_CELL_H,
                       LV_COLOR_FORMAT_RGB565);
  lv_obj_align(term->canvas, LV_ALIGN_CENTER, 0, 0);
  lv_canvas_fill_bg(term->canvas, lv_color_black(), LV_OPA_COVER);

  /* Route keyboard input to terminal via LVGL default group */

  lv_group_t *group = lv_group_get_default();
  if (group != NULL)
    {
      lv_group_add_obj(group, term->canvas);
      lv_group_focus_obj(term->canvas);
    }

  lv_obj_add_event_cb(term->canvas, terminal_key_event_cb,
                      LV_EVENT_KEY, term);

  term->dirty = true;

  syslog(LOG_INFO, "term: Created %dx%d terminal (%zu + %zu + %zu bytes)\n",
         TERM_COLS, TERM_ROWS, grid_size, scrollback_size, canvas_size);

  return term;
}

/****************************************************************************
 * Name: terminal_destroy
 *
 * Description:
 *   Free all terminal resources.
 *
 ****************************************************************************/

void terminal_destroy(terminal_t *term)
{
  if (term == NULL)
    {
      return;
    }

  if (term->canvas != NULL)
    {
      lv_obj_delete(term->canvas);
    }

  if (term->canvas_buf)  pc_app_psram_free(term->canvas_buf);
  if (term->scrollback)  pc_app_psram_free(term->scrollback);
  if (term->grid)        pc_app_psram_free(term->grid);

  pc_app_psram_free(term);
}

/****************************************************************************
 * Name: terminal_feed
 *
 * Description:
 *   Process incoming data (from shell or SSH) and update the terminal grid.
 *   Implements the VT100/ANSI state machine.
 *
 ****************************************************************************/

void terminal_feed(terminal_t *term, const char *data, size_t len)
{
  for (size_t i = 0; i < len; i++)
    {
      char ch = data[i];

      switch (term->parse_state)
        {
          case TERM_STATE_GROUND:
            if (ch == '\033')     /* ESC */
              {
                term->parse_state = TERM_STATE_ESCAPE;
              }
            else if (ch == '\n')
              {
                term_newline(term);
              }
            else if (ch == '\r')
              {
                term->cursor_x = 0;
              }
            else if (ch == '\b')  /* Backspace */
              {
                if (term->cursor_x > 0)
                  {
                    term->cursor_x--;
                  }
              }
            else if (ch == '\t')  /* Tab */
              {
                int next_tab = (term->cursor_x + 8) & ~7;
                if (next_tab >= TERM_COLS) next_tab = TERM_COLS - 1;
                term->cursor_x = next_tab;
              }
            else if (ch == '\a')  /* Bell — ignore */
              {
                /* Could trigger a short beep via audio */
              }
            else if (ch >= 0x20 && ch < 0x7F)  /* Printable */
              {
                term_put_char(term, ch);
              }
            break;

          case TERM_STATE_ESCAPE:
            if (ch == '[')
              {
                term->parse_state = TERM_STATE_CSI;
                term->csi_nparam  = 0;
                memset(term->csi_params, 0, sizeof(term->csi_params));
              }
            else if (ch == ']')
              {
                term->parse_state = TERM_STATE_OSC;
              }
            else if (ch == 'D')  /* Index — scroll up */
              {
                term_scroll_up(term);
                term->parse_state = TERM_STATE_GROUND;
              }
            else if (ch == 'M')  /* Reverse index — scroll down */
              {
                term_scroll_down(term);
                term->parse_state = TERM_STATE_GROUND;
              }
            else if (ch == 'c')  /* RIS — Full reset */
              {
                terminal_clear(term);
                term->parse_state = TERM_STATE_GROUND;
              }
            else
              {
                /* Unknown ESC sequence, return to ground */
                term->parse_state = TERM_STATE_GROUND;
              }
            break;

          case TERM_STATE_CSI:
          case TERM_STATE_CSI_PARAM:
            if (ch >= '0' && ch <= '9')
              {
                /* Accumulate parameter digit */

                if (term->csi_nparam == 0) term->csi_nparam = 1;
                term->csi_params[term->csi_nparam - 1] =
                  term->csi_params[term->csi_nparam - 1] * 10 +
                  (ch - '0');
                term->parse_state = TERM_STATE_CSI_PARAM;
              }
            else if (ch == ';')
              {
                /* Parameter separator */

                if (term->csi_nparam < 8)
                  {
                    term->csi_nparam++;
                  }
                term->parse_state = TERM_STATE_CSI_PARAM;
              }
            else if (ch == '?')
              {
                /* Private mode prefix — store as flag.
                 * We handle ?25h/l for cursor, just continue.
                 */
                term->parse_state = TERM_STATE_CSI_PARAM;
              }
            else if (ch >= 0x40 && ch <= 0x7E)
              {
                /* Final command character */

                term_process_csi(term, ch);
                term->parse_state = TERM_STATE_GROUND;
              }
            else
              {
                /* Unexpected — reset */
                term->parse_state = TERM_STATE_GROUND;
              }
            break;

          case TERM_STATE_OSC:
            /* Consume OSC until ST (ESC \) or BEL */
            if (ch == '\a' || ch == '\\')
              {
                term->parse_state = TERM_STATE_GROUND;
              }
            break;
        }
    }
}

/****************************************************************************
 * Name: terminal_input_key
 *
 * Description:
 *   Translate a keyboard key into ANSI escape sequence and send via
 *   write_cb.
 *
 ****************************************************************************/

void terminal_input_key(terminal_t *term, uint8_t key, uint8_t mods)
{
  const char *seq = NULL;
  char buf[8];
  size_t len = 0;

  if (term->write_cb == NULL)
    {
      return;
    }

  /* Special keys → ANSI sequences */

  switch (key)
    {
      /* LVGL key constants */

      case LV_KEY_UP:
        seq = "\033[A";
        break;

      case LV_KEY_DOWN:
        seq = "\033[B";
        break;

      case LV_KEY_LEFT:
        seq = "\033[D";
        break;

      case LV_KEY_RIGHT:
        seq = "\033[C";
        break;

      case LV_KEY_HOME:
        seq = "\033[H";
        break;

      case LV_KEY_END:
        seq = "\033[F";
        break;

      case LV_KEY_PREV:
        seq = "\033[5~";
        break;

      case LV_KEY_NEXT:
        seq = "\033[6~";
        break;

      case LV_KEY_DEL:
        seq = "\033[3~";
        break;

      case LV_KEY_ESC:
        seq = "\033";
        break;

      case LV_KEY_ENTER:
        buf[0] = '\r';
        len = 1;
        break;

      /* PicoCalc firmware keycodes (for compatibility) */

      case KBD_KEY_UP:
        seq = "\033[A";
        break;

      case KBD_KEY_DOWN:
        seq = "\033[B";
        break;

      case KBD_KEY_LEFT:
        seq = "\033[D";
        break;

      case KBD_KEY_RIGHT:
        seq = "\033[C";
        break;

      case KBD_KEY_HOME:
        seq = "\033[H";
        break;

      case KBD_KEY_END:
        seq = "\033[F";
        break;

      case KBD_KEY_PGUP:
        seq = "\033[5~";
        break;

      case KBD_KEY_PGDN:
        seq = "\033[6~";
        break;

      case KBD_KEY_DELETE:
        seq = "\033[3~";
        break;

      case KBD_KEY_INSERT:
        seq = "\033[2~";
        break;

      case KBD_KEY_ESC:
        seq = "\033";
        break;

      /* Older local keycode mapping compatibility */

      case 0x80:
        seq = "\033[A";
        break;

      case 0x81:
        seq = "\033[B";
        break;

      case 0x82:
        seq = "\033[D";
        break;

      case 0x83:
        seq = "\033[C";
        break;

      case 0x84:
        seq = "\033[H";
        break;

      case 0x85:
        seq = "\033[F";
        break;

      case 0x86:
        seq = "\033[5~";
        break;

      case 0x87:
        seq = "\033[6~";
        break;

      case 0x88:
        seq = "\033[3~";
        break;

      case 0x89:
        seq = "\033[2~";
        break;

      case 0x8A:
        seq = "\033";
        break;

      default:
        /* Regular ASCII key */

        if (key >= 0x20 && key < 0x80)
          {
            /* Ctrl modifier: send control character */

            if ((mods & 0x04) && key >= 'a' && key <= 'z')
              {
                buf[0] = key - 'a' + 1;  /* Ctrl+A = 0x01, etc. */
                len = 1;
              }
            else
              {
                buf[0] = key;
                len = 1;
              }
          }
        else if (key == '\r' || key == '\n')
          {
            buf[0] = '\r';
            len = 1;
          }
        else if (key == '\b' || key == 0x7F)
          {
            buf[0] = 0x7F;   /* DEL for backspace */
            len = 1;
          }
        break;
    }

  if (seq != NULL)
    {
      term->write_cb(seq, strlen(seq), term->write_user);
    }
  else if (len > 0)
    {
      term->write_cb(buf, len, term->write_user);
    }
}

/****************************************************************************
 * Name: terminal_render
 *
 * Description:
 *   Render the terminal grid to the LVGL canvas.
 *   Only redraws if dirty flag is set.
 *   Uses LVGL canvas draw functions with UNSCII-8 monospace font.
 *
 ****************************************************************************/

void terminal_render(terminal_t *term)
{
  if (!term->dirty || term->canvas == NULL)
    {
      return;
    }

  /* Clear canvas to black */

  lv_canvas_fill_bg(term->canvas, lv_color_black(), LV_OPA_COVER);

  /* Begin a draw layer on the canvas for all rendering */

  lv_layer_t layer;
  lv_canvas_init_layer(term->canvas, &layer);

  int soff = term->scroll_offset;

  for (int row = 0; row < TERM_ROWS; row++)
    {
      for (int col = 0; col < TERM_COLS; col++)
        {
          term_cell_t *cell;

          /* When scrolled back, top rows come from scrollback buffer.
           * scrollback[0] = most recently scrolled-out line.
           * scroll_offset rows from scrollback, rest from live grid.
           */

          if (soff > 0 && row < soff && term->scrollback != NULL)
            {
              int sb_idx = (soff - 1 - row) * TERM_COLS + col;
              cell = &term->scrollback[sb_idx];
            }
          else
            {
              int grid_row = row - soff;
              cell = &term->grid[grid_row * TERM_COLS + col];
            }

          /* Skip blank cells with default bg for performance */

          if (cell->ch == ' ' && cell->bg == TERM_COLOR_BLACK &&
              !(cell->attr & TERM_ATTR_INVERSE))
            {
              continue;
            }

          int px = col * TERM_CELL_W;
          int py = row * TERM_CELL_H;

          lv_color_t fg_color = g_ansi_colors[cell->fg];
          lv_color_t bg_color = g_ansi_colors[cell->bg];

          /* Handle inverse attribute */

          if (cell->attr & TERM_ATTR_INVERSE)
            {
              lv_color_t tmp = fg_color;
              fg_color = bg_color;
              bg_color = tmp;
            }

          /* Draw background if non-black */

          if (!lv_color_eq(bg_color, lv_color_black()))
            {
              lv_draw_rect_dsc_t rdsc;
              lv_draw_rect_dsc_init(&rdsc);
              rdsc.bg_color = bg_color;
              rdsc.bg_opa   = LV_OPA_COVER;
              rdsc.radius   = 0;

              lv_area_t rect_area;
              rect_area.x1 = px;
              rect_area.y1 = py;
              rect_area.x2 = px + TERM_CELL_W - 1;
              rect_area.y2 = py + TERM_CELL_H - 1;

              lv_draw_rect(&layer, &rdsc, &rect_area);
            }

          /* Draw the character */

          if (cell->ch > ' ')
            {
              char ch_buf[2] = { cell->ch, '\0' };

              lv_draw_label_dsc_t ldsc;
              lv_draw_label_dsc_init(&ldsc);
              ldsc.font  = &lv_font_unscii_8;
              ldsc.color = fg_color;
              ldsc.text  = ch_buf;

              lv_area_t txt_area;
              txt_area.x1 = px;
              txt_area.y1 = py + 2;
              txt_area.x2 = px + TERM_CELL_W;
              txt_area.y2 = py + TERM_CELL_H;

              lv_draw_label(&layer, &ldsc, &txt_area);
            }
        }
    }

  /* Draw cursor — only when viewing live (not scrolled back) */

  if (soff == 0 &&
      term->cursor_visible &&
      term->cursor_x < TERM_COLS &&
      term->cursor_y < TERM_ROWS)
    {
      int cx = term->cursor_x * TERM_CELL_W;
      int cy = term->cursor_y * TERM_CELL_H;

      /* Block cursor: fill cell with white */

      lv_draw_rect_dsc_t cur_rdsc;
      lv_draw_rect_dsc_init(&cur_rdsc);
      cur_rdsc.bg_color = lv_color_white();
      cur_rdsc.bg_opa   = LV_OPA_COVER;
      cur_rdsc.radius   = 0;

      lv_area_t cur_area;
      cur_area.x1 = cx;
      cur_area.y1 = cy;
      cur_area.x2 = cx + TERM_CELL_W - 1;
      cur_area.y2 = cy + TERM_CELL_H - 1;

      lv_draw_rect(&layer, &cur_rdsc, &cur_area);

      /* Redraw cursor character in black */

      int cur_idx = term->cursor_y * TERM_COLS + term->cursor_x;
      char cur_ch = term->grid[cur_idx].ch;

      if (cur_ch > ' ')
        {
          char ch_buf[2] = { cur_ch, '\0' };

          lv_draw_label_dsc_t cur_ldsc;
          lv_draw_label_dsc_init(&cur_ldsc);
          cur_ldsc.font  = &lv_font_unscii_8;
          cur_ldsc.color = lv_color_black();
          cur_ldsc.text  = ch_buf;

          lv_area_t txt_area;
          txt_area.x1 = cx;
          txt_area.y1 = cy + 2;
          txt_area.x2 = cx + TERM_CELL_W;
          txt_area.y2 = cy + TERM_CELL_H;

          lv_draw_label(&layer, &cur_ldsc, &txt_area);
        }
    }

  /* Finish the layer — processes all draw tasks */

  lv_canvas_finish_layer(term->canvas, &layer);

  term->dirty = false;

  /* Force LVGL to invalidate the canvas area */

  lv_obj_invalidate(term->canvas);
}

/****************************************************************************
 * Name: terminal_scroll
 *
 * Description:
 *   Scroll the terminal view for scrollback navigation.
 *
 ****************************************************************************/

void terminal_scroll(terminal_t *term, int lines)
{
  if (term->scrollback == NULL)
    {
      return;
    }

  term->scroll_offset += lines;

  if (term->scroll_offset < 0)
    {
      term->scroll_offset = 0;
    }

  if (term->scroll_offset > TERM_SCROLLBACK)
    {
      term->scroll_offset = TERM_SCROLLBACK;
    }

  /* When scroll_offset > 0, the render loop will composite
   * scrollback rows at the top and live grid rows below.
   * The scroll_offset represents how many rows we've scrolled
   * back into history. scrollback[0] is the most recent line
   * pushed out of the live grid.
   *
   * Visible rows (0..TERM_ROWS-1):
   *   If row < scroll_offset  → show scrollback[scroll_offset - 1 - row]
   *   If row >= scroll_offset → show grid[(row - scroll_offset) * COLS]
   */

  term->dirty = true;
}

/****************************************************************************
 * Name: terminal_clear
 *
 * Description:
 *   Clear the screen and reset cursor to (0,0).
 *
 ****************************************************************************/

void terminal_clear(terminal_t *term)
{
  for (int i = 0; i < TERM_COLS * TERM_ROWS; i++)
    {
      term_cell_clear(&term->grid[i]);
    }

  term->cursor_x      = 0;
  term->cursor_y      = 0;
  term->cur_fg        = TERM_COLOR_WHITE;
  term->cur_bg        = TERM_COLOR_BLACK;
  term->cur_attr      = 0;
  term->scroll_top    = 0;
  term->scroll_bottom = TERM_ROWS - 1;
  term->parse_state   = TERM_STATE_GROUND;
  term->dirty         = true;
}

/****************************************************************************
 * Name: terminal_resize
 *
 * Description:
 *   Resize the terminal grid. Normally not needed for PicoCalc (fixed
 *   320×320 display).
 *
 ****************************************************************************/

int terminal_resize(terminal_t *term, int cols, int rows)
{
  /* Not implemented for fixed display — return success */

  (void)term;
  (void)cols;
  (void)rows;

  return 0;
}
