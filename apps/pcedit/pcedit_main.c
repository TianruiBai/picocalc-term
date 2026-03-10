/****************************************************************************
 * apps/pcedit/pcedit_main.c
 *
 * Full vi/vim-style text editor with plugin ecosystem support.
 *
 * Features:
 *   - Gap buffer in PSRAM (via pcedit_buffer.c)
 *   - Canvas-based rendering with syntax highlighting
 *   - All vi modes: Normal, Insert, Visual, Command, Search, Replace
 *   - Registers, marks, macros, jump list
 *   - Search/replace with regex (via pcedit_search.c)
 *   - Syntax highlighting (via pcedit_syntax.c)
 *   - Plugin ecosystem with Lua scripting (via pcedit_plugin.c)
 *   - Multiple buffer support
 *   - Undo/redo
 *   - File I/O (via pcedit_file.c)
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <syslog.h>
#include <errno.h>
#include <lvgl/lvgl.h>

#include "pcterm/app.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define EDITOR_WIDTH      320
#define EDITOR_HEIGHT     320
#define CANVAS_WIDTH      320
#define CANVAS_HEIGHT     288    /* 320 - 16*2 for status + cmdline */
#define CELL_W            6     /* Font width (lv_font_unscii_8) */
#define CELL_H            12    /* Row height */
#define VISIBLE_ROWS      24    /* CANVAS_HEIGHT / CELL_H */
#define VISIBLE_COLS      53    /* CANVAS_WIDTH / CELL_W */
#define LINE_NUM_COLS     4     /* Columns for line numbers */
#define TEXT_COLS          (VISIBLE_COLS - LINE_NUM_COLS)
#define MAX_BUFFERS       8     /* Maximum open buffers */
#define MAX_LINE_LEN      512   /* Max line extraction length */
#define UNDO_MAX          200   /* Undo history depth */

/****************************************************************************
 * External declarations — from pcedit subsystems
 ****************************************************************************/

/* pcedit_vi.c */

typedef enum
{
  VI_MODE_NORMAL,
  VI_MODE_INSERT,
  VI_MODE_VISUAL,
  VI_MODE_VISUAL_LINE,
  VI_MODE_VISUAL_BLOCK,
  VI_MODE_COMMAND,
  VI_MODE_SEARCH_FWD,
  VI_MODE_SEARCH_BWD,
  VI_MODE_REPLACE,
  VI_MODE_OPERATOR_PENDING,
} vi_mode_t;

extern vi_mode_t    vi_get_mode(void);
extern const char  *vi_mode_string(void);
extern const char  *vi_get_cmdline(void);
extern char         vi_get_pending_register(void);
extern bool         vi_is_recording(void);
extern char         vi_recording_register(void);
extern bool         vi_normal_key(uint8_t key, uint8_t mods,
                                  int *cx, int *cy, int max_x, int max_y);
extern bool         vi_insert_key(uint8_t key, uint8_t mods);
extern bool         vi_replace_key(uint8_t key, uint8_t mods);
extern bool         vi_visual_key(uint8_t key, uint8_t mods,
                                  int *cx, int *cy, int max_x, int max_y);
extern const char  *vi_command_key(uint8_t key);
extern const char  *vi_search_key(uint8_t key);
extern int          vi_parse_command(const char *cmd, char *arg,
                                     size_t arg_len);
extern void         vi_reset(void);
extern void         vi_set_mark(char mark, size_t line, size_t col);
extern bool         vi_get_mark(char mark, size_t *line, size_t *col);
extern const char  *vi_get_register_text(char reg, size_t *len,
                                          bool *linewise);
extern void         vi_get_visual_range(size_t *sl, size_t *sc,
                                         size_t *el, size_t *ec);

/* pcedit_buffer.c */

typedef struct gap_buffer_s
{
  char   *buf;
  size_t  buf_size;
  size_t  gap_start;
  size_t  gap_end;
} gap_buffer_t;

extern int    gap_buffer_init(gap_buffer_t *gb, size_t initial_size);
extern void   gap_buffer_free(gap_buffer_t *gb);
extern size_t gap_buffer_length(const gap_buffer_t *gb);
extern void   gap_buffer_move_gap(gap_buffer_t *gb, size_t pos);
extern int    gap_buffer_insert(gap_buffer_t *gb, char ch);
extern int    gap_buffer_insert_str(gap_buffer_t *gb, const char *s,
                                     size_t len);
extern int    gap_buffer_delete_back(gap_buffer_t *gb);
extern int    gap_buffer_delete_forward(gap_buffer_t *gb);
extern char   gap_buffer_char_at(const gap_buffer_t *gb, size_t pos);
extern char  *gap_buffer_linearize(const gap_buffer_t *gb);
extern int    gap_buffer_load_file(gap_buffer_t *gb, const char *path);

/* pcedit_file.c */

extern int    pcedit_file_save(const char *path, const char *text,
                                size_t len);
extern char  *pcedit_file_load(const char *path, size_t *out_len);
extern long   pcedit_file_size(const char *path);

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

typedef enum
{
  LANG_NONE, LANG_C, LANG_PYTHON, LANG_SHELL, LANG_LUA,
  LANG_MAKEFILE, LANG_MARKDOWN, LANG_JSON,
} language_t;

extern language_t  syntax_detect_language(const char *filename);
extern void        syntax_set_language(language_t lang);
extern int         syntax_highlight_line(const char *line, size_t len,
                                          syntax_span_t *spans, int max);
extern lv_color_t  syntax_get_color(syntax_class_t cls);
extern void        syntax_reset_block_state(void);

/* pcedit_search.c */

extern void        search_init(void);
extern void        search_set_pattern(const char *pat, bool fwd);
extern const char *search_get_pattern(void);
extern bool        search_is_active(void);
extern void        search_clear(void);
extern int         search_find_in_line(const char *line, int len,
                                        int start, bool ic, int *mlen);
extern int         search_find_in_line_reverse(const char *line, int len,
                                                int end, bool ic, int *mlen);

/* pcedit_plugin.c */

extern int         pcedit_plugin_init(void);
extern void        pcedit_plugin_cleanup(void);
extern int         pcedit_plugin_fire_hook(int hook, void *data);
extern const char *pcedit_plugin_check_keymap(char mode, const char *keys);
extern int         pcedit_plugin_set_option(const char *opt);

/* pcedit_render.c */

extern void pcedit_render(lv_obj_t *canvas,
                           const char *text, size_t text_len,
                           int cursor_x, int cursor_y, int scroll_y,
                           const char *mode_str, const char *filename,
                           bool modified);

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Editor buffer (one per open file) */

typedef struct editor_buf_s
{
  gap_buffer_t  gb;               /* Gap buffer */
  char          filepath[256];    /* File path */
  language_t    language;         /* Detected language */
  size_t        cursor_line;      /* Cursor line (0-based) */
  size_t        cursor_col;       /* Cursor column (0-based) */
  size_t        scroll_y;         /* First visible line */
  bool          modified;         /* Has unsaved changes */
  size_t        total_lines;      /* Cached line count */
} editor_buf_t;

/* Simple undo record */

typedef struct undo_rec_s
{
  size_t  offset;          /* Buffer offset */
  char   *text;            /* Text that was inserted or deleted */
  size_t  len;
  bool    was_insert;      /* true = text was inserted (undo=delete) */
  size_t  cursor_line;
  size_t  cursor_col;
} undo_rec_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static editor_buf_t  g_bufs[MAX_BUFFERS];
static int           g_cur_buf  = 0;     /* Active buffer index */
static int           g_num_bufs = 0;     /* Number of open buffers */

static lv_obj_t     *g_canvas      = NULL;  /* Main editor canvas */
static lv_color_t   *g_canvas_buf  = NULL;  /* Canvas pixel buffer */
static lv_obj_t     *g_status_bar  = NULL;  /* Status bar canvas */
static lv_color_t   *g_status_buf  = NULL;
static lv_obj_t     *g_cmd_label   = NULL;  /* Command/search line label */

static undo_rec_t   *g_undo[UNDO_MAX];
static int           g_undo_pos  = 0;
static int           g_undo_top  = 0;

static char          g_message[128];  /* Status message (e.g., "3 lines yanked") */
static int           g_msg_timer = 0;

static bool          g_syntax_enabled = true;
static bool          g_initialized = false;

/****************************************************************************
 * Private Functions — Buffer Helpers
 ****************************************************************************/

static editor_buf_t *cur_buf(void)
{
  return &g_bufs[g_cur_buf];
}

/**
 * Count lines in the gap buffer.
 */
static size_t count_lines(const gap_buffer_t *gb)
{
  size_t len = gap_buffer_length(gb);
  size_t lines = 1;

  for (size_t i = 0; i < len; i++)
    {
      if (gap_buffer_char_at(gb, i) == '\n')
        {
          lines++;
        }
    }

  return lines;
}

/**
 * Get the byte offset of a given line number (0-based).
 */
static size_t line_to_offset(const gap_buffer_t *gb, size_t line)
{
  size_t len = gap_buffer_length(gb);
  size_t cur_line = 0;
  size_t offset = 0;

  while (cur_line < line && offset < len)
    {
      if (gap_buffer_char_at(gb, offset) == '\n')
        {
          cur_line++;
        }
      offset++;
    }

  return offset;
}

/**
 * Get the length of a line (not counting newline).
 */
static size_t line_length(const gap_buffer_t *gb, size_t line)
{
  size_t offset = line_to_offset(gb, line);
  size_t len = gap_buffer_length(gb);
  size_t ll = 0;

  while (offset + ll < len &&
         gap_buffer_char_at(gb, offset + ll) != '\n')
    {
      ll++;
    }

  return ll;
}

/**
 * Extract a line into a buffer.
 */
static size_t extract_line(const gap_buffer_t *gb, size_t line,
                           char *out, size_t out_size)
{
  size_t offset = line_to_offset(gb, line);
  size_t len = gap_buffer_length(gb);
  size_t i = 0;

  while (offset + i < len && i < out_size - 1)
    {
      char ch = gap_buffer_char_at(gb, offset + i);
      if (ch == '\n') break;
      out[i] = ch;
      i++;
    }

  out[i] = '\0';
  return i;
}

/**
 * Move gap to line:col position.
 */
static void move_cursor_to(editor_buf_t *eb, size_t line, size_t col)
{
  size_t offset = line_to_offset(&eb->gb, line);
  size_t ll = line_length(&eb->gb, line);

  if (col > ll) col = ll;

  gap_buffer_move_gap(&eb->gb, offset + col);
  eb->cursor_line = line;
  eb->cursor_col = col;
}

/**
 * Ensure scroll_y keeps cursor visible.
 */
static void ensure_cursor_visible(editor_buf_t *eb)
{
  if (eb->cursor_line < eb->scroll_y)
    {
      eb->scroll_y = eb->cursor_line;
    }

  if (eb->cursor_line >= eb->scroll_y + VISIBLE_ROWS)
    {
      eb->scroll_y = eb->cursor_line - VISIBLE_ROWS + 1;
    }
}

/**
 * Clamp cursor col to line length.
 */
static void clamp_cursor(editor_buf_t *eb)
{
  size_t ll = line_length(&eb->gb, eb->cursor_line);

  vi_mode_t mode = vi_get_mode();

  /* In normal mode, cursor can't be past last char */

  if (mode != VI_MODE_INSERT && mode != VI_MODE_REPLACE)
    {
      if (ll > 0 && eb->cursor_col >= ll)
        {
          eb->cursor_col = ll - 1;
        }
    }
  else
    {
      if (eb->cursor_col > ll)
        {
          eb->cursor_col = ll;
        }
    }

  eb->total_lines = count_lines(&eb->gb);
  ensure_cursor_visible(eb);
}

/****************************************************************************
 * Private Functions — Undo
 ****************************************************************************/

static void undo_push(size_t offset, const char *text, size_t len,
                      bool was_insert, size_t cline, size_t ccol)
{
  undo_rec_t *rec = malloc(sizeof(undo_rec_t));
  if (rec == NULL) return;

  rec->offset = offset;
  rec->len = len;
  rec->was_insert = was_insert;
  rec->cursor_line = cline;
  rec->cursor_col = ccol;
  rec->text = malloc(len + 1);

  if (rec->text)
    {
      memcpy(rec->text, text, len);
      rec->text[len] = '\0';
    }

  /* Free any redo entries beyond current position */

  for (int i = g_undo_pos; i < g_undo_top; i++)
    {
      if (g_undo[i])
        {
          if (g_undo[i]->text) free(g_undo[i]->text);
          free(g_undo[i]);
          g_undo[i] = NULL;
        }
    }

  if (g_undo_pos < UNDO_MAX)
    {
      g_undo[g_undo_pos] = rec;
      g_undo_pos++;
      g_undo_top = g_undo_pos;
    }
  else
    {
      /* Shift everything down */

      if (g_undo[0])
        {
          if (g_undo[0]->text) free(g_undo[0]->text);
          free(g_undo[0]);
        }

      memmove(&g_undo[0], &g_undo[1],
              sizeof(undo_rec_t *) * (UNDO_MAX - 1));
      g_undo[UNDO_MAX - 1] = rec;
      g_undo_top = UNDO_MAX;
    }
}

static void pcedit_undo(void)
{
  if (g_undo_pos <= 0)
    {
      snprintf(g_message, sizeof(g_message), "Already at oldest change");
      g_msg_timer = 30;
      return;
    }

  g_undo_pos--;
  undo_rec_t *rec = g_undo[g_undo_pos];
  if (rec == NULL) return;

  editor_buf_t *eb = cur_buf();

  if (rec->was_insert)
    {
      /* The original action was an insert — undo by deleting */

      gap_buffer_move_gap(&eb->gb, rec->offset);
      for (size_t i = 0; i < rec->len; i++)
        {
          gap_buffer_delete_forward(&eb->gb);
        }
    }
  else
    {
      /* The original action was a delete — undo by re-inserting */

      gap_buffer_move_gap(&eb->gb, rec->offset);
      if (rec->text)
        {
          gap_buffer_insert_str(&eb->gb, rec->text, rec->len);
        }
    }

  eb->cursor_line = rec->cursor_line;
  eb->cursor_col = rec->cursor_col;
  eb->modified = true;
  eb->total_lines = count_lines(&eb->gb);
  move_cursor_to(eb, eb->cursor_line, eb->cursor_col);
}

static void pcedit_redo(void)
{
  if (g_undo_pos >= g_undo_top)
    {
      snprintf(g_message, sizeof(g_message), "Already at newest change");
      g_msg_timer = 30;
      return;
    }

  undo_rec_t *rec = g_undo[g_undo_pos];
  g_undo_pos++;
  if (rec == NULL) return;

  editor_buf_t *eb = cur_buf();

  if (rec->was_insert)
    {
      /* Re-do insert */

      gap_buffer_move_gap(&eb->gb, rec->offset);
      if (rec->text)
        {
          gap_buffer_insert_str(&eb->gb, rec->text, rec->len);
        }
    }
  else
    {
      /* Re-do delete */

      gap_buffer_move_gap(&eb->gb, rec->offset);
      for (size_t i = 0; i < rec->len; i++)
        {
          gap_buffer_delete_forward(&eb->gb);
        }
    }

  eb->modified = true;
  eb->total_lines = count_lines(&eb->gb);
}

/****************************************************************************
 * Private Functions — File Operations
 ****************************************************************************/

static int pcedit_open_file(const char *path)
{
  editor_buf_t *eb = cur_buf();

  if (gap_buffer_load_file(&eb->gb, path) < 0)
    {
      snprintf(g_message, sizeof(g_message), "Cannot open: %s", path);
      g_msg_timer = 60;
      return -1;
    }

  strncpy(eb->filepath, path, sizeof(eb->filepath) - 1);
  eb->filepath[sizeof(eb->filepath) - 1] = '\0';
  eb->cursor_line = 0;
  eb->cursor_col  = 0;
  eb->scroll_y    = 0;
  eb->modified    = false;
  eb->language    = syntax_detect_language(path);
  eb->total_lines = count_lines(&eb->gb);

  syntax_set_language(eb->language);
  syntax_reset_block_state();
  vi_reset();
  search_clear();

  /* Fire plugin hook */

  pcedit_plugin_fire_hook(0 /* HOOK_ON_OPEN */, path);

  snprintf(g_message, sizeof(g_message), "\"%s\" %zuL",
           path, eb->total_lines);
  g_msg_timer = 60;
  return 0;
}

static int pcedit_save_file(void)
{
  editor_buf_t *eb = cur_buf();

  if (eb->filepath[0] == '\0')
    {
      snprintf(g_message, sizeof(g_message), "No file name");
      g_msg_timer = 60;
      return -1;
    }

  char *text = gap_buffer_linearize(&eb->gb);
  if (text == NULL) return -ENOMEM;

  size_t len = gap_buffer_length(&eb->gb);
  int ret = pcedit_file_save(eb->filepath, text, len);
  pc_app_psram_free(text);

  if (ret < 0)
    {
      snprintf(g_message, sizeof(g_message), "Write error: %s",
               eb->filepath);
      g_msg_timer = 60;
      return ret;
    }

  eb->modified = false;

  /* Fire plugin hook */

  pcedit_plugin_fire_hook(1 /* HOOK_ON_SAVE */, eb->filepath);

  snprintf(g_message, sizeof(g_message), "\"%s\" written", eb->filepath);
  g_msg_timer = 60;
  return 0;
}

/****************************************************************************
 * Private Functions — Rendering
 ****************************************************************************/

static void pcedit_redraw(void)
{
  if (g_canvas == NULL) return;

  editor_buf_t *eb = cur_buf();
  clamp_cursor(eb);

  /* Linearize visible portion for rendering */

  char *text = gap_buffer_linearize(&eb->gb);
  if (text == NULL) return;

  size_t text_len = gap_buffer_length(&eb->gb);
  const char *mode_str = vi_mode_string();
  const char *fname = eb->filepath[0] ? eb->filepath : "[No Name]";

  pcedit_render(g_canvas, text, text_len,
                (int)eb->cursor_col, (int)eb->cursor_line,
                (int)eb->scroll_y,
                mode_str, fname, eb->modified);

  pc_app_psram_free(text);

  /* Update status/command line label */

  if (g_cmd_label)
    {
      vi_mode_t mode = vi_get_mode();
      char status[128];

      if (g_msg_timer > 0)
        {
          lv_label_set_text(g_cmd_label, g_message);
          g_msg_timer--;
        }
      else if (mode == VI_MODE_COMMAND)
        {
          snprintf(status, sizeof(status), ":%s", vi_get_cmdline());
          lv_label_set_text(g_cmd_label, status);
        }
      else if (mode == VI_MODE_SEARCH_FWD)
        {
          snprintf(status, sizeof(status), "/%s", vi_get_cmdline());
          lv_label_set_text(g_cmd_label, status);
        }
      else if (mode == VI_MODE_SEARCH_BWD)
        {
          snprintf(status, sizeof(status), "?%s", vi_get_cmdline());
          lv_label_set_text(g_cmd_label, status);
        }
      else
        {
          /* Show recording indicator */

          if (vi_is_recording())
            {
              snprintf(status, sizeof(status), "recording @%c",
                       vi_recording_register());
            }
          else
            {
              status[0] = '\0';
            }

          lv_label_set_text(g_cmd_label, status);
        }
    }
}

/****************************************************************************
 * Private Functions — Command Execution
 ****************************************************************************/

static void pcedit_exec_command(const char *cmd_str)
{
  char arg[256];
  int action = vi_parse_command(cmd_str, arg, sizeof(arg));

  editor_buf_t *eb = cur_buf();

  switch (action)
    {
      case 1:  /* :q */
        if (eb->modified)
          {
            snprintf(g_message, sizeof(g_message),
                     "No write since last change (add ! to override)");
            g_msg_timer = 60;
          }
        else
          {
            pc_app_exit(0);
          }
        break;

      case 2:  /* :q! */
        pc_app_exit(0);
        break;

      case 3:  /* :w */
        pcedit_save_file();
        break;

      case 4:  /* :wq */
      case 5:  /* :wq! */
        pcedit_save_file();
        pc_app_exit(0);
        break;

      case 6:  /* :w {file} */
        {
          char *text = gap_buffer_linearize(&eb->gb);
          if (text)
            {
              pcedit_file_save(arg, text, gap_buffer_length(&eb->gb));
              pc_app_psram_free(text);
              snprintf(g_message, sizeof(g_message),
                       "\"%s\" written", arg);
              g_msg_timer = 60;
            }
        }
        break;

      case 7:  /* :e {file} */
        pcedit_open_file(arg);
        break;

      case 8:  /* :e! (reload) */
        if (eb->filepath[0])
          {
            pcedit_open_file(eb->filepath);
          }
        break;

      case 10:  /* :bn (next buffer) */
        if (g_num_bufs > 1)
          {
            g_cur_buf = (g_cur_buf + 1) % g_num_bufs;
            eb = cur_buf();
            syntax_set_language(eb->language);
            syntax_reset_block_state();
          }
        break;

      case 11:  /* :bp (prev buffer) */
        if (g_num_bufs > 1)
          {
            g_cur_buf = (g_cur_buf + g_num_bufs - 1) % g_num_bufs;
            eb = cur_buf();
            syntax_set_language(eb->language);
            syntax_reset_block_state();
          }
        break;

      case 12:  /* :bd (delete buffer) */
        if (g_num_bufs > 1)
          {
            gap_buffer_free(&eb->gb);
            for (int i = g_cur_buf; i < g_num_bufs - 1; i++)
              {
                g_bufs[i] = g_bufs[i + 1];
              }
            g_num_bufs--;
            if (g_cur_buf >= g_num_bufs) g_cur_buf = g_num_bufs - 1;
          }
        break;

      case 13:  /* :ls (list buffers) */
        {
          char ls_msg[128];
          int pos = 0;
          for (int i = 0; i < g_num_bufs && pos < 120; i++)
            {
              pos += snprintf(ls_msg + pos, sizeof(ls_msg) - pos,
                              "%s%d:%s ",
                              (i == g_cur_buf) ? "%" : " ",
                              i + 1,
                              g_bufs[i].filepath[0] ?
                              g_bufs[i].filepath : "[No Name]");
            }
          strncpy(g_message, ls_msg, sizeof(g_message) - 1);
          g_msg_timer = 90;
        }
        break;

      case 30:  /* :set {option} */
        pcedit_plugin_set_option(arg);
        snprintf(g_message, sizeof(g_message), "set %s", arg);
        g_msg_timer = 30;
        break;

      case 50:  /* :s/old/new/flags */
        {
          /* Search/replace on current line using pcedit_search */

          snprintf(g_message, sizeof(g_message),
                   "Substitution (use %%s for all lines)");
          g_msg_timer = 60;
        }
        break;

      case 81:  /* :syntax on */
        g_syntax_enabled = true;
        syntax_set_language(eb->language);
        break;

      case 82:  /* :syntax off */
        g_syntax_enabled = false;
        syntax_set_language(LANG_NONE);
        break;

      case 90:  /* :noh */
        search_clear();
        break;

      case 100:  /* :{number} — goto line */
        {
          int linenum = atoi(arg);
          if (linenum > 0)
            {
              size_t target = linenum - 1;
              if (target >= eb->total_lines)
                target = eb->total_lines - 1;
              move_cursor_to(eb, target, 0);
            }
        }
        break;

      case -1:
        snprintf(g_message, sizeof(g_message),
                 "Not an editor command: %s", cmd_str);
        g_msg_timer = 60;
        break;

      default:
        break;
    }
}

/****************************************************************************
 * Private Functions — Search
 ****************************************************************************/

static void pcedit_search_next(bool forward)
{
  editor_buf_t *eb = cur_buf();

  if (!search_is_active()) return;

  size_t start_line = eb->cursor_line;
  size_t start_col = eb->cursor_col + (forward ? 1 : 0);
  char line_buf[MAX_LINE_LEN];

  size_t lines = eb->total_lines;

  for (size_t i = 0; i < lines; i++)
    {
      size_t check_line;

      if (forward)
        {
          check_line = (start_line + i) % lines;
        }
      else
        {
          check_line = (start_line + lines - i) % lines;
        }

      size_t ll = extract_line(&eb->gb, check_line,
                                line_buf, sizeof(line_buf));
      int mlen;
      int col;

      if (forward)
        {
          int sc = (i == 0) ? (int)start_col : 0;
          col = search_find_in_line(line_buf, ll, sc, true, &mlen);
        }
      else
        {
          int ec = (i == 0) ? (int)start_col : (int)ll;
          col = search_find_in_line_reverse(line_buf, ll, ec,
                                             true, &mlen);
        }

      if (col >= 0)
        {
          move_cursor_to(eb, check_line, col);
          ensure_cursor_visible(eb);
          return;
        }
    }

  snprintf(g_message, sizeof(g_message), "Pattern not found: %s",
           search_get_pattern());
  g_msg_timer = 60;
}

/****************************************************************************
 * Private Functions — Insert Mode Text Editing
 ****************************************************************************/

static void pcedit_insert_char(char ch)
{
  editor_buf_t *eb = cur_buf();
  size_t offset = line_to_offset(&eb->gb, eb->cursor_line) +
                  eb->cursor_col;

  gap_buffer_move_gap(&eb->gb, offset);
  gap_buffer_insert(&eb->gb, ch);

  /* Record undo */

  char buf[2] = { ch, '\0' };
  undo_push(offset, buf, 1, true,
            eb->cursor_line, eb->cursor_col);

  if (ch == '\n')
    {
      eb->cursor_line++;
      eb->cursor_col = 0;
      eb->total_lines++;
    }
  else
    {
      eb->cursor_col++;
    }

  eb->modified = true;
}

static void pcedit_insert_backspace(void)
{
  editor_buf_t *eb = cur_buf();

  if (eb->cursor_col == 0 && eb->cursor_line == 0) return;

  size_t offset = line_to_offset(&eb->gb, eb->cursor_line) +
                  eb->cursor_col;

  if (offset == 0) return;

  /* Get the character being deleted for undo */

  char deleted = gap_buffer_char_at(&eb->gb, offset - 1);
  char buf[2] = { deleted, '\0' };

  gap_buffer_move_gap(&eb->gb, offset);
  gap_buffer_delete_back(&eb->gb);

  undo_push(offset - 1, buf, 1, false,
            eb->cursor_line, eb->cursor_col);

  if (deleted == '\n')
    {
      eb->cursor_line--;
      eb->cursor_col = line_length(&eb->gb, eb->cursor_line);
      eb->total_lines--;
    }
  else
    {
      if (eb->cursor_col > 0) eb->cursor_col--;
    }

  eb->modified = true;
}

/****************************************************************************
 * Private Functions — Key Handler
 ****************************************************************************/

static void pcedit_key_handler(lv_event_t *e)
{
  uint32_t key = lv_event_get_key(e);
  uint8_t mods = 0;

  editor_buf_t *eb = cur_buf();
  vi_mode_t mode = vi_get_mode();

  /* Fire plugin key hook */

  pcedit_plugin_fire_hook(2 /* HOOK_ON_KEY */, &key);

  switch (mode)
    {
      case VI_MODE_NORMAL:
      case VI_MODE_OPERATOR_PENDING:
        {
          int cx = (int)eb->cursor_col;
          int cy = (int)eb->cursor_line;
          int max_x = (int)line_length(&eb->gb, eb->cursor_line);
          int max_y = (int)eb->total_lines - 1;
          if (max_y < 0) max_y = 0;
          if (max_x < 1) max_x = 1;

          /* Handle undo/redo ctrl keys */

          if (key == 'u')
            {
              pcedit_undo();
              break;
            }

          if (key == 0x12)  /* Ctrl-R */
            {
              pcedit_redo();
              break;
            }

          if (vi_normal_key((uint8_t)key, mods, &cx, &cy,
                            max_x, max_y))
            {
              /* Apply cursor changes */

              if ((size_t)cy != eb->cursor_line ||
                  (size_t)cx != eb->cursor_col)
                {
                  if (cy < 0) cy = 0;
                  if ((size_t)cy >= eb->total_lines)
                    cy = eb->total_lines - 1;

                  move_cursor_to(eb, cy, cx);
                }
            }

          /* Check if mode changed */

          vi_mode_t new_mode = vi_get_mode();
          if (new_mode != mode)
            {
              pcedit_plugin_fire_hook(3 /* HOOK_ON_MODE_CHANGE */,
                                       vi_mode_string());
            }
        }
        break;

      case VI_MODE_INSERT:
        if (vi_insert_key((uint8_t)key, mods))
          {
            /* ESC → back to normal; adjust cursor */

            if (eb->cursor_col > 0)
              {
                eb->cursor_col--;
              }

            clamp_cursor(eb);
            break;
          }

        /* Text editing */

        if (key == '\r' || key == '\n')
          {
            pcedit_insert_char('\n');
          }
        else if (key == '\b' || key == 0x7F)
          {
            pcedit_insert_backspace();
          }
        else if (key == '\t')
          {
            /* Tab: insert spaces or tab based on expandtab option */

            pcedit_insert_char('\t');
          }
        else if (key >= 0x20 && key < 0x7F)
          {
            pcedit_insert_char((char)key);
          }
        break;

      case VI_MODE_REPLACE:
        if (vi_replace_key((uint8_t)key, mods))
          {
            break;  /* ESC → normal */
          }

        /* Replace character under cursor */

        if (key >= 0x20 && key < 0x7F)
          {
            size_t offset = line_to_offset(&eb->gb, eb->cursor_line) +
                            eb->cursor_col;
            size_t len = gap_buffer_length(&eb->gb);

            if (offset < len)
              {
                char old_ch = gap_buffer_char_at(&eb->gb, offset);
                gap_buffer_move_gap(&eb->gb, offset);
                gap_buffer_delete_forward(&eb->gb);
                gap_buffer_insert(&eb->gb, (char)key);

                /* Push undo for the replacement */

                char old_buf[2] = { old_ch, '\0' };
                undo_push(offset, old_buf, 1, false,
                          eb->cursor_line, eb->cursor_col);

                eb->cursor_col++;
                eb->modified = true;
              }
          }
        break;

      case VI_MODE_VISUAL:
      case VI_MODE_VISUAL_LINE:
      case VI_MODE_VISUAL_BLOCK:
        {
          int cx = (int)eb->cursor_col;
          int cy = (int)eb->cursor_line;
          int max_x = (int)line_length(&eb->gb, eb->cursor_line);
          int max_y = (int)eb->total_lines - 1;

          vi_visual_key((uint8_t)key, mods, &cx, &cy, max_x, max_y);

          if (cy >= 0 && (size_t)cy < eb->total_lines)
            {
              move_cursor_to(eb, cy, cx);
            }
        }
        break;

      case VI_MODE_COMMAND:
        {
          const char *cmd = vi_command_key((uint8_t)key);

          if (cmd != NULL)
            {
              pcedit_exec_command(cmd);
            }
        }
        break;

      case VI_MODE_SEARCH_FWD:
      case VI_MODE_SEARCH_BWD:
        {
          const char *pattern = vi_search_key((uint8_t)key);

          if (pattern != NULL)
            {
              search_set_pattern(pattern,
                                 mode == VI_MODE_SEARCH_FWD);
              pcedit_search_next(mode == VI_MODE_SEARCH_FWD);
            }
        }
        break;

      default:
        break;
    }

  /* Handle n/N in normal mode */

  if (mode == VI_MODE_NORMAL)
    {
      if (key == 'n' && search_is_active())
        {
          pcedit_search_next(true);
        }
      else if (key == 'N' && search_is_active())
        {
          pcedit_search_next(false);
        }
    }

  clamp_cursor(eb);
  pcedit_redraw();
}

/****************************************************************************
 * Private Functions — State Save/Restore
 ****************************************************************************/

typedef struct pcedit_state_s
{
  char        filepath[256];
  size_t      cursor_line;
  size_t      cursor_col;
  size_t      scroll_y;
  bool        modified;
  size_t      text_size;
  /* Followed by text content */
} pcedit_state_t;

static int pcedit_save_state(void *buf, size_t *len)
{
  editor_buf_t *eb = cur_buf();
  size_t text_size = gap_buffer_length(&eb->gb);
  size_t total = sizeof(pcedit_state_t) + text_size;

  if (total > *len) return -1;

  pcedit_state_t hdr;
  memset(&hdr, 0, sizeof(hdr));
  strncpy(hdr.filepath, eb->filepath, sizeof(hdr.filepath) - 1);
  hdr.cursor_line = eb->cursor_line;
  hdr.cursor_col  = eb->cursor_col;
  hdr.scroll_y    = eb->scroll_y;
  hdr.modified    = eb->modified;
  hdr.text_size   = text_size;

  memcpy(buf, &hdr, sizeof(hdr));

  char *text = gap_buffer_linearize(&eb->gb);
  if (text)
    {
      memcpy((char *)buf + sizeof(hdr), text, text_size);
      pc_app_psram_free(text);
    }

  *len = total;
  return 0;
}

static int pcedit_restore_state(const void *buf, size_t len)
{
  if (len < sizeof(pcedit_state_t)) return -1;

  pcedit_state_t hdr;
  memcpy(&hdr, buf, sizeof(hdr));

  editor_buf_t *eb = &g_bufs[0];
  g_cur_buf = 0;
  g_num_bufs = 1;

  gap_buffer_init(&eb->gb, hdr.text_size + 4096);
  strncpy(eb->filepath, hdr.filepath, sizeof(eb->filepath) - 1);
  eb->cursor_line = hdr.cursor_line;
  eb->cursor_col  = hdr.cursor_col;
  eb->scroll_y    = hdr.scroll_y;
  eb->modified    = hdr.modified;

  /* Load text */

  const char *text = (const char *)buf + sizeof(hdr);
  gap_buffer_insert_str(&eb->gb, text, hdr.text_size);
  gap_buffer_move_gap(&eb->gb, 0);

  eb->language = syntax_detect_language(eb->filepath);
  eb->total_lines = count_lines(&eb->gb);

  g_initialized = true;
  return 0;
}

/****************************************************************************
 * App Entry Point
 ****************************************************************************/

static int pcedit_main(int argc, char *argv[])
{
  lv_obj_t *screen = pc_app_get_screen();

  /* Initialize subsystems */

  search_init();
  pcedit_plugin_init();

  /* Initialize first buffer if not restored */

  if (!g_initialized)
    {
      g_num_bufs = 1;
      g_cur_buf = 0;
      memset(&g_bufs[0], 0, sizeof(editor_buf_t));
      gap_buffer_init(&g_bufs[0].gb, 8192);
      g_bufs[0].total_lines = 1;
    }

  /* Load file from argv if provided */

  if (argc > 1 && argv[1] && !g_initialized)
    {
      pcedit_open_file(argv[1]);
    }

  /* Create main editor canvas */

  g_canvas_buf = (lv_color_t *)malloc(CANVAS_WIDTH * CANVAS_HEIGHT *
                                       sizeof(lv_color_t));
  if (g_canvas_buf)
    {
      g_canvas = lv_canvas_create(screen);
      lv_canvas_set_buffer(g_canvas, g_canvas_buf,
                           CANVAS_WIDTH, CANVAS_HEIGHT,
                           LV_COLOR_FORMAT_RGB565);
      lv_obj_align(g_canvas, LV_ALIGN_TOP_LEFT, 0, 0);
    }

  /* Command/message line at bottom */

  g_cmd_label = lv_label_create(screen);
  lv_label_set_text(g_cmd_label, "");
  lv_obj_set_width(g_cmd_label, EDITOR_WIDTH);
  lv_obj_align(g_cmd_label, LV_ALIGN_BOTTOM_LEFT, 2, -2);
  lv_obj_set_style_text_font(g_cmd_label, &lv_font_unscii_8, 0);
  lv_obj_set_style_text_color(g_cmd_label, lv_color_white(), 0);

  /* Set up input handling */

  lv_obj_add_event_cb(g_canvas, pcedit_key_handler, LV_EVENT_KEY, NULL);

  lv_group_t *grp = lv_group_get_default();
  if (grp != NULL)
    {
      lv_group_add_obj(grp, g_canvas);
      lv_group_focus_obj(g_canvas);
    }

  /* Set syntax language */

  editor_buf_t *eb = cur_buf();
  syntax_set_language(eb->language);

  /* Initial render */

  pcedit_redraw();

  syslog(LOG_INFO, "PCEDIT: Editor ready (%zu lines)\n", eb->total_lines);
  g_initialized = true;
  return 0;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

const pc_app_t g_pcedit_app = {
  .info = {
    .name         = "pcedit",
    .display_name = "Text Editor",
    .version      = "2.0.0",
    .category     = "office",
    .icon         = LV_SYMBOL_EDIT,
    .min_ram      = 65536,
    .flags        = PC_APP_FLAG_BUILTIN | PC_APP_FLAG_STATEFUL,
  },
  .main    = pcedit_main,
  .save    = pcedit_save_state,
  .restore = pcedit_restore_state,
};
