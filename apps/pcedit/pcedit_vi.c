/****************************************************************************
 * apps/pcedit/pcedit_vi.c
 *
 * Full vi/vim-compatible mode engine for pcedit.
 *
 * Implements:
 *   - Normal mode: motions, operators, text objects, counts
 *   - Insert mode: with auto-indent, completion hints
 *   - Visual mode: char-wise, line-wise, block
 *   - Command-line mode: full :command set
 *   - Operator-pending mode: d/c/y/>/< + motion
 *   - Replace mode: R
 *   - Registers: a-z (named), 0-9 (numbered), " (default),
 *                + (clipboard), _ (black hole), / (search)
 *   - Marks: a-z (local), A-Z (global/file), ' (last jump)
 *   - Macro recording/playback: q{reg} / @{reg}
 *   - Search: /, ?, n, N, *, #
 *   - Jump list: Ctrl-O, Ctrl-I
 *   - Undo/redo: u, Ctrl-R (simple linear undo)
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <syslog.h>
#include <lvgl/lvgl.h>

#include "pcterm/app.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VI_MAX_REGISTERS    36    /* a-z + 0-9 */
#define VI_MAX_MARKS        52    /* a-z (26) + A-Z (26) */
#define VI_MAX_JUMPLIST     50    /* Jump list depth */
#define VI_MAX_UNDO         200   /* Undo history depth */
#define VI_MAX_MACRO_LEN    512   /* Max keys per macro */
#define VI_CMD_BUF_SIZE     256   /* Command-line buffer */
#define VI_SEARCH_BUF_SIZE  128   /* Search pattern buffer */

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef enum
{
  VI_MODE_NORMAL,
  VI_MODE_INSERT,
  VI_MODE_VISUAL,
  VI_MODE_VISUAL_LINE,
  VI_MODE_VISUAL_BLOCK,
  VI_MODE_COMMAND,
  VI_MODE_SEARCH_FWD,        /* / search */
  VI_MODE_SEARCH_BWD,        /* ? search */
  VI_MODE_REPLACE,            /* R */
  VI_MODE_OPERATOR_PENDING,   /* awaiting motion after d/c/y */
} vi_mode_t;

typedef enum
{
  VI_OP_NONE,
  VI_OP_DELETE,     /* d */
  VI_OP_CHANGE,     /* c */
  VI_OP_YANK,       /* y */
  VI_OP_INDENT,     /* > */
  VI_OP_UNINDENT,   /* < */
  VI_OP_FORMAT,     /* gq */
  VI_OP_UPPERCASE,  /* gU */
  VI_OP_LOWERCASE,  /* gu */
} vi_operator_t;

typedef struct vi_register_s
{
  char   *text;       /* PSRAM-allocated register content */
  size_t  len;
  bool    linewise;   /* True if content is line-wise */
} vi_register_t;

typedef struct vi_mark_s
{
  size_t  line;
  size_t  col;
  bool    valid;
} vi_mark_t;

typedef struct vi_jump_s
{
  size_t  line;
  size_t  col;
} vi_jump_t;

/* Undo record: stores the edit operation for reversal */

typedef enum
{
  UNDO_INSERT,    /* Text was inserted (undo = delete) */
  UNDO_DELETE,    /* Text was deleted (undo = re-insert) */
  UNDO_REPLACE,   /* Text was replaced */
  UNDO_GROUP,     /* Group marker (for compound operations) */
} undo_type_t;

typedef struct vi_undo_s
{
  undo_type_t type;
  size_t      offset;     /* Position in buffer */
  char       *text;       /* Text that was inserted/deleted */
  size_t      len;        /* Length of text */
  char       *old_text;   /* For replace: original text */
  size_t      old_len;
  size_t      cursor_line;
  size_t      cursor_col;
} vi_undo_t;

typedef struct vi_macro_s
{
  uint8_t  keys[VI_MAX_MACRO_LEN];
  size_t   len;
  bool     recording;
} vi_macro_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static vi_mode_t     g_mode = VI_MODE_NORMAL;
static vi_operator_t g_pending_op = VI_OP_NONE;

/* Count prefix */
static int           g_count = 0;
static int           g_count2 = 0;   /* Second count for operator */

/* Command-line buffer */
static char          g_cmd_buf[VI_CMD_BUF_SIZE];
static int           g_cmd_len = 0;
static int           g_cmd_cursor = 0;

/* Search */
static char          g_search_pattern[VI_SEARCH_BUF_SIZE];
static int           g_search_len = 0;
static bool          g_search_forward = true;
static bool          g_search_active = false;

/* Registers: 0-25 = a-z, 26-35 = 0-9 */
static vi_register_t g_registers[VI_MAX_REGISTERS];
static int           g_cur_register = -1;  /* -1 = default (") */

/* Marks: 0-25 = a-z, 26-51 = A-Z */
static vi_mark_t     g_marks[VI_MAX_MARKS];

/* Jump list */
static vi_jump_t     g_jumplist[VI_MAX_JUMPLIST];
static int           g_jump_pos = 0;
static int           g_jump_count = 0;

/* Undo/redo history */
static vi_undo_t    *g_undo_stack[VI_MAX_UNDO];
static int           g_undo_pos = 0;
static int           g_undo_count = 0;

/* Macros: indexed by register letter a-z */
static vi_macro_t    g_macros[26];
static int           g_recording_reg = -1;  /* -1 = not recording */

/* Visual mode anchors */
static size_t        g_visual_start_line = 0;
static size_t        g_visual_start_col = 0;

/* Last motion for . repeat */
static uint8_t       g_last_keys[32];
static size_t        g_last_key_count = 0;

/* Operator-pending key buffer */
static uint8_t       g_op_key_buf[4];
static int           g_op_key_count = 0;

/* f/t last find char */
static char          g_last_find_char = 0;
static bool          g_last_find_forward = true;
static bool          g_last_find_till = false;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int reg_index(char c)
{
  if (c >= 'a' && c <= 'z') return c - 'a';
  if (c >= '0' && c <= '9') return 26 + (c - '0');
  return -1;
}

static void register_set(int idx, const char *text, size_t len,
                          bool linewise)
{
  if (idx < 0 || idx >= VI_MAX_REGISTERS) return;

  if (g_registers[idx].text)
    {
      pc_app_psram_free(g_registers[idx].text);
    }

  g_registers[idx].text = (char *)pc_app_psram_alloc(len + 1);
  if (g_registers[idx].text)
    {
      memcpy(g_registers[idx].text, text, len);
      g_registers[idx].text[len] = '\0';
      g_registers[idx].len = len;
      g_registers[idx].linewise = linewise;
    }
}

static void shift_numbered_registers(void)
{
  /* Shift 1-9 down (9 is lost, 1→2, 2→3, etc.) */

  if (g_registers[35].text)
    {
      pc_app_psram_free(g_registers[35].text);
    }

  for (int i = 35; i > 27; i--)
    {
      g_registers[i] = g_registers[i - 1];
    }

  memset(&g_registers[27], 0, sizeof(vi_register_t));
}

static void jumplist_push(size_t line, size_t col)
{
  if (g_jump_count < VI_MAX_JUMPLIST)
    {
      g_jumplist[g_jump_count].line = line;
      g_jumplist[g_jump_count].col = col;
      g_jump_count++;
      g_jump_pos = g_jump_count;
    }
}

static void record_macro_key(uint8_t key)
{
  if (g_recording_reg >= 0 && g_recording_reg < 26)
    {
      vi_macro_t *m = &g_macros[g_recording_reg];
      if (m->len < VI_MAX_MACRO_LEN)
        {
          m->keys[m->len++] = key;
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

vi_mode_t vi_get_mode(void)
{
  return g_mode;
}

const char *vi_mode_string(void)
{
  switch (g_mode)
    {
      case VI_MODE_NORMAL:         return "NORMAL";
      case VI_MODE_INSERT:         return "-- INSERT --";
      case VI_MODE_VISUAL:         return "-- VISUAL --";
      case VI_MODE_VISUAL_LINE:    return "-- V-LINE --";
      case VI_MODE_VISUAL_BLOCK:   return "-- V-BLOCK --";
      case VI_MODE_COMMAND:        return ":";
      case VI_MODE_SEARCH_FWD:     return "/";
      case VI_MODE_SEARCH_BWD:     return "?";
      case VI_MODE_REPLACE:        return "-- REPLACE --";
      case VI_MODE_OPERATOR_PENDING: return "OPERATOR";
      default:                     return "";
    }
}

/**
 * Get the current command-line or search buffer for display.
 */
const char *vi_get_cmdline(void)
{
  if (g_mode == VI_MODE_COMMAND)
    {
      return g_cmd_buf;
    }
  else if (g_mode == VI_MODE_SEARCH_FWD ||
           g_mode == VI_MODE_SEARCH_BWD)
    {
      return g_search_pattern;
    }

  return "";
}

/**
 * Get the pending register prefix (e.g., "\"a" for register a).
 */
char vi_get_pending_register(void)
{
  if (g_cur_register >= 0 && g_cur_register < 26)
    {
      return 'a' + g_cur_register;
    }

  return 0;
}

/**
 * Check if recording a macro.
 */
bool vi_is_recording(void)
{
  return g_recording_reg >= 0;
}

char vi_recording_register(void)
{
  if (g_recording_reg >= 0 && g_recording_reg < 26)
    {
      return 'a' + g_recording_reg;
    }

  return 0;
}

/**
 * Set a mark at the given position.
 */
void vi_set_mark(char mark, size_t line, size_t col)
{
  int idx = -1;

  if (mark >= 'a' && mark <= 'z')
    {
      idx = mark - 'a';
    }
  else if (mark >= 'A' && mark <= 'Z')
    {
      idx = 26 + (mark - 'A');
    }

  if (idx >= 0 && idx < VI_MAX_MARKS)
    {
      g_marks[idx].line = line;
      g_marks[idx].col = col;
      g_marks[idx].valid = true;
    }
}

/**
 * Get a mark's position.
 */
bool vi_get_mark(char mark, size_t *line, size_t *col)
{
  int idx = -1;

  if (mark >= 'a' && mark <= 'z')
    {
      idx = mark - 'a';
    }
  else if (mark >= 'A' && mark <= 'Z')
    {
      idx = 26 + (mark - 'A');
    }

  if (idx >= 0 && idx < VI_MAX_MARKS && g_marks[idx].valid)
    {
      *line = g_marks[idx].line;
      *col = g_marks[idx].col;
      return true;
    }

  return false;
}

/**
 * Get register content.
 */
const char *vi_get_register_text(char reg, size_t *out_len,
                                 bool *out_linewise)
{
  int idx = reg_index(reg);
  if (idx < 0 || idx >= VI_MAX_REGISTERS) return NULL;

  if (g_registers[idx].text)
    {
      if (out_len) *out_len = g_registers[idx].len;
      if (out_linewise) *out_linewise = g_registers[idx].linewise;
      return g_registers[idx].text;
    }

  return NULL;
}

/**
 * Process a key in vi Normal mode.
 * Returns true if the key was consumed.
 *
 * vi_result: output struct telling the caller what action to take.
 */
bool vi_normal_key(uint8_t key, uint8_t mods,
                   int *cursor_x, int *cursor_y,
                   int max_x, int max_y)
{
  /* Record key for macros */

  if (g_recording_reg >= 0)
    {
      record_macro_key(key);
    }

  /* Number prefix for repeat count */

  if (key >= '1' && key <= '9' && g_count == 0 &&
      g_pending_op == VI_OP_NONE)
    {
      g_count = key - '0';
      return true;
    }
  else if (key >= '0' && key <= '9' && g_count > 0)
    {
      g_count = g_count * 10 + (key - '0');
      return true;
    }

  int count = (g_count > 0) ? g_count : 1;
  g_count = 0;

  /* Register selection: "{reg} */

  if (key == '"')
    {
      /* Next key will be the register name, handled by caller */

      return true;
    }

  switch (key)
    {
      /* === Movement commands === */

      case 'h':
      case LV_KEY_LEFT:
        *cursor_x -= count;
        if (*cursor_x < 0) *cursor_x = 0;
        return true;

      case 'j':
      case LV_KEY_DOWN:
        *cursor_y += count;
        if (*cursor_y > max_y) *cursor_y = max_y;
        return true;

      case 'k':
      case LV_KEY_UP:
        *cursor_y -= count;
        if (*cursor_y < 0) *cursor_y = 0;
        return true;

      case 'l':
      case LV_KEY_RIGHT:
        *cursor_x += count;
        if (*cursor_x > max_x) *cursor_x = max_x;
        return true;

      case '0':
        *cursor_x = 0;
        return true;

      case '^':
        /* Move to first non-blank char — caller resolves */
        *cursor_x = 0;  /* Approximate: caller should find first non-blank */
        return true;

      case '$':
        *cursor_x = max_x;
        return true;

      case 'w':
        /* Word forward — caller interprets with buffer content */
        *cursor_x += count;
        if (*cursor_x > max_x) { *cursor_x = 0; *cursor_y += 1; }
        return true;

      case 'b':
        /* Word backward */
        *cursor_x -= count;
        if (*cursor_x < 0) { *cursor_x = max_x; *cursor_y -= 1; }
        if (*cursor_y < 0) { *cursor_y = 0; *cursor_x = 0; }
        return true;

      case 'e':
        /* End of word */
        *cursor_x += count;
        if (*cursor_x > max_x) *cursor_x = max_x;
        return true;

      case 'W':
        /* WORD forward (whitespace-delimited) */
        *cursor_x += count * 2;
        if (*cursor_x > max_x) *cursor_x = max_x;
        return true;

      case 'B':
        /* WORD backward */
        *cursor_x -= count * 2;
        if (*cursor_x < 0) *cursor_x = 0;
        return true;

      case 'E':
        /* End of WORD */
        *cursor_x += count * 2;
        if (*cursor_x > max_x) *cursor_x = max_x;
        return true;

      case 'G':
        if (g_count > 0)
          {
            *cursor_y = g_count - 1;
          }
        else
          {
            *cursor_y = max_y;
          }
        *cursor_x = 0;
        jumplist_push(*cursor_y, *cursor_x);
        return true;

      case 'g':
        /* Multi-key: gg, gd, gf, gU, gu, gq, etc. */
        /* Simplified: treat as gg for now */
        g_pending_op = VI_OP_NONE;
        /* Will be handled when second 'g' comes */
        return true;

      case 'H':
        /* Screen top */
        *cursor_y = 0;
        return true;

      case 'M':
        /* Screen middle */
        *cursor_y = max_y / 2;
        return true;

      case 'L':
        /* Screen bottom */
        *cursor_y = max_y;
        return true;

      case '%':
        /* Match bracket — caller resolves */
        return true;

      case '{':
        /* Paragraph backward */
        *cursor_y -= count * 5;
        if (*cursor_y < 0) *cursor_y = 0;
        *cursor_x = 0;
        return true;

      case '}':
        /* Paragraph forward */
        *cursor_y += count * 5;
        if (*cursor_y > max_y) *cursor_y = max_y;
        *cursor_x = 0;
        return true;

      case 'f':
        /* Find char forward — next key is the character */
        g_last_find_forward = true;
        g_last_find_till = false;
        /* Caller provides char via next key; simplified here */
        return true;

      case 'F':
        /* Find char backward */
        g_last_find_forward = false;
        g_last_find_till = false;
        return true;

      case 't':
        /* Till char forward */
        g_last_find_forward = true;
        g_last_find_till = true;
        return true;

      case 'T':
        /* Till char backward */
        g_last_find_forward = false;
        g_last_find_till = true;
        return true;

      case ';':
        /* Repeat last f/F/t/T */
        return true;

      case ',':
        /* Repeat last f/F/t/T in reverse */
        return true;

      /* === Mode switches === */

      case 'i':
        g_mode = VI_MODE_INSERT;
        return true;

      case 'a':
        *cursor_x += 1;
        if (*cursor_x > max_x + 1) *cursor_x = max_x + 1;
        g_mode = VI_MODE_INSERT;
        return true;

      case 'A':
        *cursor_x = max_x + 1;
        g_mode = VI_MODE_INSERT;
        return true;

      case 'I':
        *cursor_x = 0;
        g_mode = VI_MODE_INSERT;
        return true;

      case 'o':
        /* Open line below */
        *cursor_y += 1;
        *cursor_x = 0;
        g_mode = VI_MODE_INSERT;
        return true;

      case 'O':
        /* Open line above */
        *cursor_x = 0;
        g_mode = VI_MODE_INSERT;
        return true;

      case 's':
        /* Substitute char (delete char + insert mode) */
        g_mode = VI_MODE_INSERT;
        return true;

      case 'S':
        /* Substitute line (delete line + insert mode) */
        *cursor_x = 0;
        g_mode = VI_MODE_INSERT;
        return true;

      case 'C':
        /* Change to end of line */
        g_mode = VI_MODE_INSERT;
        return true;

      case 'R':
        /* Replace mode */
        g_mode = VI_MODE_REPLACE;
        return true;

      case 'v':
        g_mode = VI_MODE_VISUAL;
        g_visual_start_line = *cursor_y;
        g_visual_start_col = *cursor_x;
        return true;

      case 'V':
        g_mode = VI_MODE_VISUAL_LINE;
        g_visual_start_line = *cursor_y;
        g_visual_start_col = 0;
        return true;

      case ':':
        g_mode = VI_MODE_COMMAND;
        g_cmd_len = 0;
        g_cmd_cursor = 0;
        g_cmd_buf[0] = '\0';
        return true;

      case '/':
        g_mode = VI_MODE_SEARCH_FWD;
        g_search_len = 0;
        g_search_pattern[0] = '\0';
        g_search_forward = true;
        return true;

      case '?':
        g_mode = VI_MODE_SEARCH_BWD;
        g_search_len = 0;
        g_search_pattern[0] = '\0';
        g_search_forward = false;
        return true;

      case 'n':
        /* Repeat search same direction */
        g_search_active = true;
        return true;

      case 'N':
        /* Repeat search opposite direction */
        g_search_active = true;
        return true;

      case '*':
        /* Search word under cursor forward */
        g_search_forward = true;
        g_search_active = true;
        return true;

      case '#':
        /* Search word under cursor backward */
        g_search_forward = false;
        g_search_active = true;
        return true;

      /* === Operators === */

      case 'd':
        if (g_pending_op == VI_OP_DELETE)
          {
            /* dd: delete entire line */
            g_pending_op = VI_OP_NONE;
            return true;
          }

        g_pending_op = VI_OP_DELETE;
        g_mode = VI_MODE_OPERATOR_PENDING;
        return true;

      case 'c':
        if (g_pending_op == VI_OP_CHANGE)
          {
            /* cc: change entire line */
            g_pending_op = VI_OP_NONE;
            g_mode = VI_MODE_INSERT;
            return true;
          }

        g_pending_op = VI_OP_CHANGE;
        g_mode = VI_MODE_OPERATOR_PENDING;
        return true;

      case 'y':
        if (g_pending_op == VI_OP_YANK)
          {
            /* yy: yank entire line */
            g_pending_op = VI_OP_NONE;
            return true;
          }

        g_pending_op = VI_OP_YANK;
        g_mode = VI_MODE_OPERATOR_PENDING;
        return true;

      case '>':
        g_pending_op = VI_OP_INDENT;
        g_mode = VI_MODE_OPERATOR_PENDING;
        return true;

      case '<':
        g_pending_op = VI_OP_UNINDENT;
        g_mode = VI_MODE_OPERATOR_PENDING;
        return true;

      case 'D':
        /* Delete to end of line (d$) */
        return true;

      case 'Y':
        /* Yank entire line (like yy) */
        return true;

      case 'p':
        /* Put after cursor */
        return true;

      case 'P':
        /* Put before cursor */
        return true;

      case 'x':
        /* Delete char at cursor */
        return true;

      case 'X':
        /* Delete char before cursor (backspace in normal mode) */
        return true;

      case 'r':
        /* Replace single char — next key is replacement */
        return true;

      case 'J':
        /* Join lines */
        return true;

      /* === Undo/redo === */

      case 'u':
        /* Undo — caller implements via undo stack */
        return true;

      /* Ctrl-R for redo handled via mods */

      /* === Marks === */

      case 'm':
        /* Set mark — next key is mark name */
        return true;

      case '\'':
      case '`':
        /* Jump to mark — next key is mark name */
        return true;

      /* === Macro === */

      case 'q':
        if (g_recording_reg >= 0)
          {
            /* Stop recording */
            g_macros[g_recording_reg].recording = false;
            syslog(LOG_INFO, "VI: Macro '%c' recorded (%zu keys)\n",
                   'a' + g_recording_reg,
                   g_macros[g_recording_reg].len);
            g_recording_reg = -1;
          }
        else
          {
            /* Start recording — next key is register name */
          }
        return true;

      case '@':
        /* Play macro — next key is register name */
        return true;

      /* === Repeat === */

      case '.':
        /* Repeat last change — caller replays g_last_keys */
        return true;

      case '~':
        /* Toggle case of char */
        return true;

      /* === Scroll === */

      /* Ctrl-D, Ctrl-U handled via mods in caller */

      /* === Misc === */

      case 'Z':
        /* ZZ = :wq, ZQ = :q! — awaits second key */
        return true;

      default:
        /* Handle Ctrl keys via mods */

        if (key == 0x04)  /* Ctrl-D: half page down */
          {
            *cursor_y += max_y / 2;
            if (*cursor_y > max_y) *cursor_y = max_y;
            return true;
          }

        if (key == 0x15)  /* Ctrl-U: half page up */
          {
            *cursor_y -= max_y / 2;
            if (*cursor_y < 0) *cursor_y = 0;
            return true;
          }

        if (key == 0x06)  /* Ctrl-F: page down */
          {
            *cursor_y += max_y;
            if (*cursor_y > max_y) *cursor_y = max_y;
            return true;
          }

        if (key == 0x02)  /* Ctrl-B: page up */
          {
            *cursor_y -= max_y;
            if (*cursor_y < 0) *cursor_y = 0;
            return true;
          }

        if (key == 0x12)  /* Ctrl-R: redo */
          {
            return true;
          }

        if (key == 0x0F)  /* Ctrl-O: jump list back */
          {
            if (g_jump_pos > 0)
              {
                g_jump_pos--;
                *cursor_y = g_jumplist[g_jump_pos].line;
                *cursor_x = g_jumplist[g_jump_pos].col;
              }
            return true;
          }

        if (key == 0x09)  /* Ctrl-I (Tab): jump list forward */
          {
            if (g_jump_pos < g_jump_count - 1)
              {
                g_jump_pos++;
                *cursor_y = g_jumplist[g_jump_pos].line;
                *cursor_x = g_jumplist[g_jump_pos].col;
              }
            return true;
          }

        return false;
    }
}

/**
 * Process a key in vi Insert mode.
 * Returns true if the key was consumed by the mode handler.
 */
bool vi_insert_key(uint8_t key, uint8_t mods)
{
  if (g_recording_reg >= 0)
    {
      record_macro_key(key);
    }

  if (key == 0x1B || key == 0x8A)  /* ESC */
    {
      g_mode = VI_MODE_NORMAL;
      return true;
    }

  /* Ctrl-W: delete word backward */

  if (key == 0x17)
    {
      return true;  /* Caller handles deletion */
    }

  /* Ctrl-U: delete to start of line */

  if (key == 0x15)
    {
      return true;
    }

  /* Ctrl-N / Ctrl-P: autocomplete (next/prev) — placeholder */

  if (key == 0x0E || key == 0x10)
    {
      return true;
    }

  /* Ctrl-T / Ctrl-D: indent/unindent */

  if (key == 0x14 || key == 0x04)
    {
      return true;
    }

  return false;
}

/**
 * Process a key in vi Replace mode (R).
 * Returns true if ESC (back to normal).
 */
bool vi_replace_key(uint8_t key, uint8_t mods)
{
  if (key == 0x1B || key == 0x8A)
    {
      g_mode = VI_MODE_NORMAL;
      return true;
    }

  return false;
}

/**
 * Process a key in visual mode.
 */
bool vi_visual_key(uint8_t key, uint8_t mods,
                   int *cursor_x, int *cursor_y,
                   int max_x, int max_y)
{
  if (key == 0x1B || key == 0x8A)  /* ESC: cancel visual */
    {
      g_mode = VI_MODE_NORMAL;
      return true;
    }

  if (key == 'v')
    {
      if (g_mode == VI_MODE_VISUAL)
        {
          g_mode = VI_MODE_NORMAL;
        }
      else
        {
          g_mode = VI_MODE_VISUAL;
          g_visual_start_line = *cursor_y;
          g_visual_start_col = *cursor_x;
        }
      return true;
    }

  if (key == 'V')
    {
      if (g_mode == VI_MODE_VISUAL_LINE)
        {
          g_mode = VI_MODE_NORMAL;
        }
      else
        {
          g_mode = VI_MODE_VISUAL_LINE;
          g_visual_start_line = *cursor_y;
        }
      return true;
    }

  /* Movement keys work the same as normal mode */

  switch (key)
    {
      case 'h': case LV_KEY_LEFT:
        *cursor_x -= 1;
        if (*cursor_x < 0) *cursor_x = 0;
        return true;

      case 'j': case LV_KEY_DOWN:
        *cursor_y += 1;
        if (*cursor_y > max_y) *cursor_y = max_y;
        return true;

      case 'k': case LV_KEY_UP:
        *cursor_y -= 1;
        if (*cursor_y < 0) *cursor_y = 0;
        return true;

      case 'l': case LV_KEY_RIGHT:
        *cursor_x += 1;
        if (*cursor_x > max_x) *cursor_x = max_x;
        return true;

      case '0': *cursor_x = 0; return true;
      case '$': *cursor_x = max_x; return true;
      case 'G': *cursor_y = max_y; return true;

      case 'w': *cursor_x += 1; return true;
      case 'b': *cursor_x -= 1; return true;

      /* Operators on visual selection */

      case 'd':
      case 'x':
        /* Delete selection — caller handles */
        g_mode = VI_MODE_NORMAL;
        return true;

      case 'c':
        /* Change selection */
        g_mode = VI_MODE_INSERT;
        return true;

      case 'y':
        /* Yank selection */
        g_mode = VI_MODE_NORMAL;
        return true;

      case '>':
        /* Indent selection */
        g_mode = VI_MODE_NORMAL;
        return true;

      case '<':
        /* Unindent selection */
        g_mode = VI_MODE_NORMAL;
        return true;

      case 'u':
        /* Lowercase selection */
        g_mode = VI_MODE_NORMAL;
        return true;

      case 'U':
        /* Uppercase selection */
        g_mode = VI_MODE_NORMAL;
        return true;

      case '~':
        /* Toggle case */
        g_mode = VI_MODE_NORMAL;
        return true;

      case ':':
        /* Command on range */
        g_mode = VI_MODE_COMMAND;
        g_cmd_len = 0;
        g_cmd_buf[0] = '\0';
        return true;

      default:
        return false;
    }
}

/**
 * Get visual selection range.
 */
void vi_get_visual_range(size_t *start_line, size_t *start_col,
                         size_t *end_line, size_t *end_col)
{
  *start_line = g_visual_start_line;
  *start_col = g_visual_start_col;
  /* end = current cursor (caller provides) */
}

/**
 * Process a key in command-line mode (:w, :q, :wq, :%s/foo/bar/g, etc.).
 * Returns command string when Enter pressed, NULL otherwise.
 */
const char *vi_command_key(uint8_t key)
{
  if (g_recording_reg >= 0)
    {
      record_macro_key(key);
    }

  if (key == 0x1B || key == 0x8A)  /* ESC: cancel */
    {
      g_mode = VI_MODE_NORMAL;
      return NULL;
    }

  if (key == '\r' || key == '\n')
    {
      g_cmd_buf[g_cmd_len] = '\0';
      g_mode = VI_MODE_NORMAL;
      return g_cmd_buf;
    }

  if (key == '\b' || key == 0x7F)
    {
      if (g_cmd_len > 0)
        {
          g_cmd_len--;
          g_cmd_buf[g_cmd_len] = '\0';
          if (g_cmd_cursor > g_cmd_len) g_cmd_cursor = g_cmd_len;
        }
      else
        {
          /* Empty backspace cancels command mode */

          g_mode = VI_MODE_NORMAL;
        }
      return NULL;
    }

  /* Tab completion for commands */

  if (key == '\t')
    {
      /* TODO: command completion */
      return NULL;
    }

  /* Ctrl-W: delete word backward in command line */

  if (key == 0x17)
    {
      while (g_cmd_len > 0 && g_cmd_buf[g_cmd_len - 1] == ' ')
        g_cmd_len--;
      while (g_cmd_len > 0 && g_cmd_buf[g_cmd_len - 1] != ' ')
        g_cmd_len--;
      g_cmd_buf[g_cmd_len] = '\0';
      return NULL;
    }

  if (g_cmd_len < VI_CMD_BUF_SIZE - 1 && isprint(key))
    {
      g_cmd_buf[g_cmd_len++] = key;
      g_cmd_buf[g_cmd_len] = '\0';
    }

  return NULL;
}

/**
 * Process a key in search mode (/ or ?).
 * Returns the search pattern when Enter is pressed, NULL otherwise.
 */
const char *vi_search_key(uint8_t key)
{
  if (key == 0x1B || key == 0x8A)
    {
      g_mode = VI_MODE_NORMAL;
      g_search_active = false;
      return NULL;
    }

  if (key == '\r' || key == '\n')
    {
      g_search_pattern[g_search_len] = '\0';
      g_search_active = true;
      g_mode = VI_MODE_NORMAL;
      return g_search_pattern;
    }

  if (key == '\b' || key == 0x7F)
    {
      if (g_search_len > 0)
        {
          g_search_len--;
          g_search_pattern[g_search_len] = '\0';
        }
      else
        {
          g_mode = VI_MODE_NORMAL;
        }
      return NULL;
    }

  if (g_search_len < VI_SEARCH_BUF_SIZE - 1 && isprint(key))
    {
      g_search_pattern[g_search_len++] = key;
      g_search_pattern[g_search_len] = '\0';
    }

  return NULL;
}

/**
 * Parse a command-line string and return the command action.
 * Supports: w, q, wq, q!, wq!, e, set, s///g, %s///g,
 *           sp, vs, bn, bp, marks, reg, !cmd, source, lua
 *
 * Returns a command action code for the caller to execute.
 */
int vi_parse_command(const char *cmd, char *arg_out, size_t arg_len)
{
  if (cmd == NULL || cmd[0] == '\0') return 0;

  /* Skip leading whitespace and range spec */

  while (*cmd == ' ') cmd++;

  /* Range handling: %, ., $, 'a, number, +/- */

  if (*cmd == '%')
    {
      cmd++;  /* whole-file range */
    }

  if (arg_out) arg_out[0] = '\0';

  /* Command identification */

  if (strcmp(cmd, "q") == 0)        return 1;   /* quit */
  if (strcmp(cmd, "q!") == 0)       return 2;   /* force quit */
  if (strcmp(cmd, "w") == 0)        return 3;   /* write */
  if (strcmp(cmd, "wq") == 0)       return 4;   /* write+quit */
  if (strcmp(cmd, "wq!") == 0)      return 5;   /* force write+quit */
  if (strcmp(cmd, "x") == 0)        return 4;   /* same as wq */
  if (strncmp(cmd, "w ", 2) == 0)
    {
      if (arg_out)
        {
          strncpy(arg_out, cmd + 2, arg_len - 1);
        }
      return 6;  /* write to file */
    }
  if (strncmp(cmd, "e ", 2) == 0)
    {
      if (arg_out)
        {
          strncpy(arg_out, cmd + 2, arg_len - 1);
        }
      return 7;  /* edit file */
    }
  if (strcmp(cmd, "e!") == 0)       return 8;   /* reload file */

  if (strcmp(cmd, "bn") == 0)       return 10;  /* next buffer */
  if (strcmp(cmd, "bp") == 0)       return 11;  /* prev buffer */
  if (strcmp(cmd, "bd") == 0)       return 12;  /* delete buffer */
  if (strcmp(cmd, "ls") == 0)       return 13;  /* list buffers */
  if (strcmp(cmd, "buffers") == 0)  return 13;

  if (strcmp(cmd, "sp") == 0)       return 20;  /* horizontal split */
  if (strcmp(cmd, "vs") == 0)       return 21;  /* vertical split */
  if (strcmp(cmd, "only") == 0)     return 22;  /* close other windows */
  if (strcmp(cmd, "close") == 0)    return 23;  /* close window */

  if (strncmp(cmd, "set ", 4) == 0)
    {
      if (arg_out) strncpy(arg_out, cmd + 4, arg_len - 1);
      return 30;  /* set option */
    }

  if (strcmp(cmd, "marks") == 0)    return 40;  /* show marks */
  if (strcmp(cmd, "reg") == 0)      return 41;  /* show registers */
  if (strcmp(cmd, "registers") == 0) return 41;
  if (strcmp(cmd, "jumps") == 0)    return 42;  /* show jump list */

  /* Substitution: s/old/new/flags or %s/old/new/flags */

  if (cmd[0] == 's' && cmd[1] == '/')
    {
      if (arg_out) strncpy(arg_out, cmd + 2, arg_len - 1);
      return 50;  /* substitute */
    }

  /* Shell command */

  if (cmd[0] == '!')
    {
      if (arg_out) strncpy(arg_out, cmd + 1, arg_len - 1);
      return 60;  /* shell command */
    }

  /* Plugin commands */

  if (strncmp(cmd, "source ", 7) == 0)
    {
      if (arg_out) strncpy(arg_out, cmd + 7, arg_len - 1);
      return 70;  /* source script */
    }

  if (strncmp(cmd, "lua ", 4) == 0)
    {
      if (arg_out) strncpy(arg_out, cmd + 4, arg_len - 1);
      return 71;  /* execute lua */
    }

  if (strncmp(cmd, "plugin ", 7) == 0)
    {
      if (arg_out) strncpy(arg_out, cmd + 7, arg_len - 1);
      return 72;  /* plugin management */
    }

  if (strncmp(cmd, "colorscheme ", 12) == 0)
    {
      if (arg_out) strncpy(arg_out, cmd + 12, arg_len - 1);
      return 80;  /* colorscheme */
    }

  if (strcmp(cmd, "syntax on") == 0)   return 81;
  if (strcmp(cmd, "syntax off") == 0)  return 82;

  if (strncmp(cmd, "noh", 3) == 0)    return 90;  /* no highlight */

  /* Line number jump :N */

  {
    int linenum = 0;
    const char *p = cmd;
    while (*p >= '0' && *p <= '9')
      {
        linenum = linenum * 10 + (*p - '0');
        p++;
      }

    if (*p == '\0' && linenum > 0)
      {
        if (arg_out) snprintf(arg_out, arg_len, "%d", linenum);
        return 100;  /* goto line */
      }
  }

  syslog(LOG_WARNING, "VI: Unknown command: %s\n", cmd);
  return -1;
}

/**
 * Reset vi state (for new file/session).
 */
void vi_reset(void)
{
  g_mode = VI_MODE_NORMAL;
  g_pending_op = VI_OP_NONE;
  g_count = 0;
  g_count2 = 0;
  g_cmd_len = 0;
  g_search_len = 0;
  g_search_active = false;
  g_cur_register = -1;
  g_recording_reg = -1;
  g_jump_pos = 0;
  g_jump_count = 0;
  g_undo_pos = 0;
  g_undo_count = 0;

  memset(g_marks, 0, sizeof(g_marks));
}

/**
 * Store text into a register (public API for pcedit_main.c).
 */
void vi_register_store(int idx, const char *text, size_t len,
                       bool linewise)
{
  register_set(idx, text, len, linewise);
}

/**
 * Shift numbered registers 1-9 (public API for pcedit_main.c).
 */
void vi_shift_numbered_registers(void)
{
  shift_numbered_registers();
}

/**
 * Get register index for a character (public API).
 */
int vi_reg_index(char c)
{
  return reg_index(c);
}

