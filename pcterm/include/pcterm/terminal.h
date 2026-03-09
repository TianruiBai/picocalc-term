/****************************************************************************
 * pcterm/include/pcterm/terminal.h
 *
 * Shared VT100/ANSI terminal emulator widget.
 * Used by pcterm (local shell) and pcssh (SSH client) apps.
 *
 * Supports a subset of ANSI/VT100 escape sequences sufficient for
 * vi, top, htop, and typical shell usage.
 *
 ****************************************************************************/

#ifndef __PCTERM_TERMINAL_H
#define __PCTERM_TERMINAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <lvgl/lvgl.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Terminal grid dimensions
 * 320px width / 6px per char (UNSCII-8 at 6px wide) = 53 columns
 * 300px height / 12px per row (8px char + 4px spacing) = 25 rows
 */

#define TERM_COLS            53
#define TERM_ROWS            25
#define TERM_CELL_W          6     /* Pixels per character width */
#define TERM_CELL_H          12    /* Pixels per character height */

#define TERM_SCROLLBACK      100   /* Scrollback buffer rows */

/* ANSI color indices */

#define TERM_COLOR_BLACK     0
#define TERM_COLOR_RED       1
#define TERM_COLOR_GREEN     2
#define TERM_COLOR_YELLOW    3
#define TERM_COLOR_BLUE      4
#define TERM_COLOR_MAGENTA   5
#define TERM_COLOR_CYAN      6
#define TERM_COLOR_WHITE     7

/* Cell attributes */

#define TERM_ATTR_BOLD       (1 << 0)
#define TERM_ATTR_UNDERLINE  (1 << 1)
#define TERM_ATTR_INVERSE    (1 << 2)
#define TERM_ATTR_BLINK      (1 << 3)

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Single character cell in the terminal grid */

typedef struct term_cell_s
{
  char    ch;           /* ASCII character */
  uint8_t fg;           /* Foreground color (0-7) */
  uint8_t bg;           /* Background color (0-7) */
  uint8_t attr;         /* TERM_ATTR_* bitfield */
} term_cell_t;

/* Terminal parser state machine */

typedef enum
{
  TERM_STATE_GROUND,     /* Normal character input */
  TERM_STATE_ESCAPE,     /* Received ESC (0x1B) */
  TERM_STATE_CSI,        /* Received ESC [ */
  TERM_STATE_CSI_PARAM,  /* Reading CSI parameters */
  TERM_STATE_OSC,        /* Operating system command */
} term_parse_state_t;

/* Terminal context (allocated in PSRAM) */

typedef struct terminal_s
{
  /* Grid */
  term_cell_t  *grid;              /* Active grid: TERM_COLS × TERM_ROWS */
  term_cell_t  *scrollback;        /* Scrollback: TERM_COLS × TERM_SCROLLBACK */
  int           scroll_offset;     /* Current scrollback position */

  /* Cursor */
  int           cursor_x;          /* Column (0-based) */
  int           cursor_y;          /* Row (0-based) */
  bool          cursor_visible;

  /* Current attributes (for new characters) */
  uint8_t       cur_fg;
  uint8_t       cur_bg;
  uint8_t       cur_attr;

  /* Parser state */
  term_parse_state_t parse_state;
  int           csi_params[8];     /* CSI parameter buffer */
  int           csi_nparam;        /* Number of CSI parameters */

  /* Scroll region */
  int           scroll_top;        /* Top row of scroll region */
  int           scroll_bottom;     /* Bottom row of scroll region */

  /* LVGL rendering */
  lv_obj_t     *canvas;           /* LVGL canvas for rendering */
  lv_color_t   *canvas_buf;       /* Canvas pixel buffer */
  bool          dirty;             /* Needs re-render */

  /* Data callback — called when terminal has data to send
   * (user typed a key, or terminal sends a response sequence) */
  void         (*write_cb)(const char *data, size_t len, void *user);
  void         *write_user;
} terminal_t;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a new terminal widget.
 *
 * @param parent    LVGL parent container
 * @param write_cb  Callback for outgoing data (NULL if not needed yet)
 * @param user      User context for write_cb
 * @return Allocated terminal, or NULL on failure
 */
terminal_t *terminal_create(lv_obj_t *parent,
                            void (*write_cb)(const char *, size_t, void *),
                            void *user);

/**
 * Destroy a terminal widget and free all memory.
 */
void terminal_destroy(terminal_t *term);

/**
 * Feed data into the terminal (from shell or SSH).
 * Parses ANSI escape sequences and updates the grid.
 *
 * @param term  Terminal context
 * @param data  Input data (may contain escape sequences)
 * @param len   Length of data
 */
void terminal_feed(terminal_t *term, const char *data, size_t len);

/**
 * Process a keyboard input and send to the connected process.
 * Translates special keys (arrows, Home, etc.) to ANSI sequences.
 *
 * @param term  Terminal context
 * @param key   Key code from keyboard driver
 * @param mods  Modifier flags (shift, ctrl, etc.)
 */
void terminal_input_key(terminal_t *term, uint8_t key, uint8_t mods);

/**
 * Render the terminal grid to the LVGL canvas.
 * Only redraws if the dirty flag is set.
 */
void terminal_render(terminal_t *term);

/**
 * Scroll the terminal view (for scrollback).
 *
 * @param term   Terminal context
 * @param lines  Lines to scroll (negative = up, positive = down)
 */
void terminal_scroll(terminal_t *term, int lines);

/**
 * Clear the terminal screen and reset cursor to (0,0).
 */
void terminal_clear(terminal_t *term);

/**
 * Resize the terminal grid (if display resolution changes).
 * Normally not needed for PicoCalc (fixed 320×320).
 */
int terminal_resize(terminal_t *term, int cols, int rows);

#ifdef __cplusplus
}
#endif

#endif /* __PCTERM_TERMINAL_H */
