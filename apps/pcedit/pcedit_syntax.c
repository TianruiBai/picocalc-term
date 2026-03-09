/****************************************************************************
 * apps/pcedit/pcedit_syntax.c
 *
 * Syntax highlighting engine for the vi/vim editor.
 *
 * Supports built-in highlighting for:
 *   - C/C++ (.c, .h, .cpp, .hpp)
 *   - Python (.py)
 *   - Shell/Bash (.sh)
 *   - Lua (.lua)
 *   - Makefile
 *   - Markdown (.md)
 *   - JSON (.json)
 *   - Plain text (no highlighting)
 *
 * Colors are LVGL RGB565 values.  Highlighting is done per-line via
 * a span array returned to the renderer.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <lvgl/lvgl.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SYNTAX_MAX_SPANS       64    /* Max highlight spans per line */
#define SYNTAX_MAX_KEYWORDS    128   /* Max keywords per language */

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef enum
{
  SYN_NORMAL,
  SYN_KEYWORD,        /* Language keywords (if, while, for, etc.) */
  SYN_TYPE,           /* Type keywords (int, char, bool, etc.) */
  SYN_STRING,         /* String literals */
  SYN_CHAR,           /* Character literals */
  SYN_NUMBER,         /* Numeric literals */
  SYN_COMMENT,        /* Comments */
  SYN_PREPROC,        /* Preprocessor directives */
  SYN_FUNCTION,       /* Function calls */
  SYN_OPERATOR,       /* Operators */
  SYN_BRACKET,        /* Brackets/parens */
  SYN_CONSTANT,       /* Constants (TRUE, FALSE, NULL, etc.) */
  SYN_SPECIAL,        /* Special (escape sequences, etc.) */
} syntax_class_t;

typedef struct syntax_span_s
{
  uint16_t        start;    /* Start column */
  uint16_t        end;      /* End column (exclusive) */
  syntax_class_t  cls;      /* Syntax class */
} syntax_span_t;

typedef enum
{
  LANG_NONE,
  LANG_C,
  LANG_PYTHON,
  LANG_SHELL,
  LANG_LUA,
  LANG_MAKEFILE,
  LANG_MARKDOWN,
  LANG_JSON,
} language_t;

/****************************************************************************
 * Color Scheme
 ****************************************************************************/

typedef struct colorscheme_s
{
  lv_color_t normal;
  lv_color_t keyword;
  lv_color_t type;
  lv_color_t string;
  lv_color_t character;
  lv_color_t number;
  lv_color_t comment;
  lv_color_t preproc;
  lv_color_t function;
  lv_color_t operator_;
  lv_color_t bracket;
  lv_color_t constant;
  lv_color_t special;
  lv_color_t line_number;
  lv_color_t cursor_line_bg;
  lv_color_t visual_bg;
  lv_color_t search_bg;
  lv_color_t status_bg;
} colorscheme_t;

/* Default dark colorscheme */

static const colorscheme_t g_scheme_default =
{
  .normal         = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),  /* White */
  .keyword        = LV_COLOR_MAKE(0xFF, 0x82, 0x00),  /* Orange */
  .type           = LV_COLOR_MAKE(0x00, 0xFF, 0x00),  /* Green */
  .string         = LV_COLOR_MAKE(0xFF, 0xA6, 0x00),  /* Amber */
  .character      = LV_COLOR_MAKE(0xFF, 0xA6, 0x00),
  .number         = LV_COLOR_MAKE(0x00, 0xFF, 0xFF),  /* Cyan */
  .comment        = LV_COLOR_MAKE(0x7B, 0x7D, 0x7B),  /* Gray */
  .preproc        = LV_COLOR_MAKE(0xFF, 0x00, 0xFF),  /* Magenta */
  .function       = LV_COLOR_MAKE(0x00, 0x00, 0xFF),  /* Blue */
  .operator_      = LV_COLOR_MAKE(0xFF, 0xFF, 0x00),  /* Yellow */
  .bracket        = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),  /* White */
  .constant       = LV_COLOR_MAKE(0x00, 0xFF, 0xFF),  /* Cyan */
  .special        = LV_COLOR_MAKE(0xFF, 0x00, 0xFF),  /* Magenta */
  .line_number    = LV_COLOR_MAKE(0x6B, 0x6D, 0x6B),  /* Dim gray */
  .cursor_line_bg = LV_COLOR_MAKE(0x31, 0x34, 0x31),  /* Dark gray */
  .visual_bg      = LV_COLOR_MAKE(0x29, 0x2C, 0x29),  /* Dark blue */
  .search_bg      = LV_COLOR_MAKE(0x42, 0x41, 0x00),  /* Dark yellow */
  .status_bg      = LV_COLOR_MAKE(0x31, 0x30, 0x31),  /* Dark gray */
};

static const colorscheme_t *g_scheme = &g_scheme_default;

/****************************************************************************
 * Keyword Tables
 ****************************************************************************/

static const char *g_c_keywords[] =
{
  "auto", "break", "case", "continue", "default", "do", "else",
  "extern", "for", "goto", "if", "inline", "register", "return",
  "sizeof", "static", "struct", "switch", "typedef", "union",
  "volatile", "while", "const", "enum", NULL
};

static const char *g_c_types[] =
{
  "void", "char", "short", "int", "long", "float", "double",
  "signed", "unsigned", "bool", "int8_t", "int16_t", "int32_t",
  "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
  "size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t",
  "FILE", "DIR", NULL
};

static const char *g_c_constants[] =
{
  "NULL", "true", "false", "TRUE", "FALSE", "OK", "ERROR",
  "EINVAL", "ENOMEM", "EIO", "ENOENT", "EBUSY", NULL
};

static const char *g_python_keywords[] =
{
  "and", "as", "assert", "async", "await", "break", "class",
  "continue", "def", "del", "elif", "else", "except", "finally",
  "for", "from", "global", "if", "import", "in", "is", "lambda",
  "nonlocal", "not", "or", "pass", "raise", "return", "try",
  "while", "with", "yield", NULL
};

static const char *g_python_types[] =
{
  "int", "float", "str", "list", "dict", "tuple", "set",
  "bool", "bytes", "None", "True", "False", "self", NULL
};

static const char *g_lua_keywords[] =
{
  "and", "break", "do", "else", "elseif", "end", "for",
  "function", "goto", "if", "in", "local", "not", "or",
  "repeat", "return", "then", "until", "while", NULL
};

static const char *g_lua_constants[] =
{
  "nil", "true", "false", NULL
};

static const char *g_shell_keywords[] =
{
  "if", "then", "else", "elif", "fi", "for", "while", "do",
  "done", "case", "esac", "in", "function", "return", "exit",
  "export", "local", "readonly", "shift", "source", NULL
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static language_t g_language = LANG_NONE;
static bool       g_in_block_comment = false;  /* Multi-line comment state */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool is_keyword(const char *word, size_t len,
                       const char **table)
{
  if (table == NULL) return false;

  for (int i = 0; table[i] != NULL; i++)
    {
      if (strlen(table[i]) == len &&
          strncmp(word, table[i], len) == 0)
        {
          return true;
        }
    }

  return false;
}

static bool is_word_char(char c)
{
  return isalnum((unsigned char)c) || c == '_';
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: syntax_detect_language
 *
 * Description:
 *   Detect the programming language from the file extension.
 *
 ****************************************************************************/

language_t syntax_detect_language(const char *filename)
{
  if (filename == NULL) return LANG_NONE;

  const char *ext = strrchr(filename, '.');
  const char *base = strrchr(filename, '/');
  if (base) base++; else base = filename;

  if (ext)
    {
      if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0 ||
          strcmp(ext, ".cpp") == 0 || strcmp(ext, ".hpp") == 0 ||
          strcmp(ext, ".cc") == 0 || strcmp(ext, ".cxx") == 0)
        {
          return LANG_C;
        }

      if (strcmp(ext, ".py") == 0 || strcmp(ext, ".pyw") == 0)
        {
          return LANG_PYTHON;
        }

      if (strcmp(ext, ".sh") == 0 || strcmp(ext, ".bash") == 0)
        {
          return LANG_SHELL;
        }

      if (strcmp(ext, ".lua") == 0)
        {
          return LANG_LUA;
        }

      if (strcmp(ext, ".md") == 0 || strcmp(ext, ".markdown") == 0)
        {
          return LANG_MARKDOWN;
        }

      if (strcmp(ext, ".json") == 0)
        {
          return LANG_JSON;
        }
    }

  if (strcmp(base, "Makefile") == 0 || strcmp(base, "makefile") == 0 ||
      strcmp(base, "GNUmakefile") == 0)
    {
      return LANG_MAKEFILE;
    }

  return LANG_NONE;
}

/****************************************************************************
 * Name: syntax_set_language
 ****************************************************************************/

void syntax_set_language(language_t lang)
{
  g_language = lang;
  g_in_block_comment = false;
}

/****************************************************************************
 * Name: syntax_highlight_line
 *
 * Description:
 *   Highlight a single line of text.
 *   Returns the number of spans written to the output array.
 *
 *   This handles:
 *     - Keywords, types, constants
 *     - String/char literals (with escape detection)
 *     - Single-line comments (// # --)
 *     - Multi-line comments (/* *\/ for C)
 *     - Number literals
 *     - Preprocessor directives
 *     - Function calls (word followed by '(')
 *
 ****************************************************************************/

int syntax_highlight_line(const char *line, size_t len,
                          syntax_span_t *spans, int max_spans)
{
  if (g_language == LANG_NONE || line == NULL || len == 0)
    {
      return 0;
    }

  int span_count = 0;
  size_t i = 0;

  const char **kw_table = NULL;
  const char **type_table = NULL;
  const char **const_table = NULL;
  const char *line_comment = NULL;
  bool has_block_comment = false;

  switch (g_language)
    {
      case LANG_C:
        kw_table = g_c_keywords;
        type_table = g_c_types;
        const_table = g_c_constants;
        line_comment = "//";
        has_block_comment = true;
        break;

      case LANG_PYTHON:
        kw_table = g_python_keywords;
        type_table = g_python_types;
        line_comment = "#";
        break;

      case LANG_SHELL:
        kw_table = g_shell_keywords;
        line_comment = "#";
        break;

      case LANG_LUA:
        kw_table = g_lua_keywords;
        const_table = g_lua_constants;
        line_comment = "--";
        break;

      default:
        break;
    }

  /* Check if we're continuing a block comment */

  if (g_in_block_comment && has_block_comment)
    {
      const char *end = strstr(line, "*/");
      if (end)
        {
          size_t end_pos = (end - line) + 2;
          if (span_count < max_spans)
            {
              spans[span_count].start = 0;
              spans[span_count].end = end_pos;
              spans[span_count].cls = SYN_COMMENT;
              span_count++;
            }

          g_in_block_comment = false;
          i = end_pos;
        }
      else
        {
          /* Entire line is comment */

          if (span_count < max_spans)
            {
              spans[span_count].start = 0;
              spans[span_count].end = len;
              spans[span_count].cls = SYN_COMMENT;
              span_count++;
            }

          return span_count;
        }
    }

  while (i < len && span_count < max_spans)
    {
      char ch = line[i];

      /* Skip whitespace */

      if (ch == ' ' || ch == '\t')
        {
          i++;
          continue;
        }

      /* Preprocessor directives (C/C++) */

      if (ch == '#' && g_language == LANG_C)
        {
          /* Check if it's at line start (after whitespace) */

          bool at_start = true;
          for (size_t j = 0; j < i; j++)
            {
              if (line[j] != ' ' && line[j] != '\t')
                {
                  at_start = false;
                  break;
                }
            }

          if (at_start)
            {
              spans[span_count].start = i;
              spans[span_count].end = len;
              spans[span_count].cls = SYN_PREPROC;
              span_count++;
              return span_count;
            }
        }

      /* Line comment */

      if (line_comment)
        {
          size_t lc_len = strlen(line_comment);
          if (i + lc_len <= len &&
              strncmp(line + i, line_comment, lc_len) == 0)
            {
              spans[span_count].start = i;
              spans[span_count].end = len;
              spans[span_count].cls = SYN_COMMENT;
              span_count++;
              return span_count;
            }
        }

      /* Block comment start (C) */

      if (has_block_comment && i + 1 < len &&
          line[i] == '/' && line[i + 1] == '*')
        {
          size_t start = i;
          const char *end = strstr(line + i + 2, "*/");

          if (end)
            {
              size_t end_pos = (end - line) + 2;
              spans[span_count].start = start;
              spans[span_count].end = end_pos;
              spans[span_count].cls = SYN_COMMENT;
              span_count++;
              i = end_pos;
            }
          else
            {
              /* Block comment extends beyond line */

              spans[span_count].start = start;
              spans[span_count].end = len;
              spans[span_count].cls = SYN_COMMENT;
              span_count++;
              g_in_block_comment = true;
              return span_count;
            }

          continue;
        }

      /* String literals */

      if (ch == '"' || ch == '\'')
        {
          char quote = ch;
          size_t start = i;
          i++;

          /* For C, single quotes are char literals */

          syntax_class_t cls = (quote == '\'' && g_language == LANG_C) ?
                               SYN_CHAR : SYN_STRING;

          while (i < len)
            {
              if (line[i] == '\\' && i + 1 < len)
                {
                  i += 2;  /* Skip escape sequence */
                }
              else if (line[i] == quote)
                {
                  i++;
                  break;
                }
              else
                {
                  i++;
                }
            }

          spans[span_count].start = start;
          spans[span_count].end = i;
          spans[span_count].cls = cls;
          span_count++;
          continue;
        }

      /* Number literals */

      if (isdigit((unsigned char)ch) ||
          (ch == '.' && i + 1 < len && isdigit((unsigned char)line[i + 1])))
        {
          size_t start = i;
          bool is_hex = false;

          if (ch == '0' && i + 1 < len &&
              (line[i + 1] == 'x' || line[i + 1] == 'X'))
            {
              is_hex = true;
              i += 2;
            }

          while (i < len)
            {
              if (is_hex && isxdigit((unsigned char)line[i]))
                {
                  i++;
                }
              else if (isdigit((unsigned char)line[i]) ||
                       line[i] == '.' || line[i] == 'e' ||
                       line[i] == 'E' || line[i] == 'f' ||
                       line[i] == 'L' || line[i] == 'u' ||
                       line[i] == 'U')
                {
                  i++;
                }
              else
                {
                  break;
                }
            }

          spans[span_count].start = start;
          spans[span_count].end = i;
          spans[span_count].cls = SYN_NUMBER;
          span_count++;
          continue;
        }

      /* Word (keyword, type, constant, function) */

      if (is_word_char(ch))
        {
          size_t start = i;

          while (i < len && is_word_char(line[i]))
            {
              i++;
            }

          size_t wlen = i - start;

          /* Check if followed by '(' — function call */

          size_t peek = i;
          while (peek < len && line[peek] == ' ') peek++;

          if (peek < len && line[peek] == '(')
            {
              spans[span_count].start = start;
              spans[span_count].end = i;
              spans[span_count].cls = SYN_FUNCTION;
              span_count++;
              continue;
            }

          /* Check keyword tables */

          if (is_keyword(line + start, wlen, kw_table))
            {
              spans[span_count].start = start;
              spans[span_count].end = i;
              spans[span_count].cls = SYN_KEYWORD;
              span_count++;
            }
          else if (is_keyword(line + start, wlen, type_table))
            {
              spans[span_count].start = start;
              spans[span_count].end = i;
              spans[span_count].cls = SYN_TYPE;
              span_count++;
            }
          else if (is_keyword(line + start, wlen, const_table))
            {
              spans[span_count].start = start;
              spans[span_count].end = i;
              spans[span_count].cls = SYN_CONSTANT;
              span_count++;
            }

          continue;
        }

      /* Operators */

      if (strchr("+-*/%=!<>&|^~?:", ch))
        {
          size_t start = i;
          i++;

          /* Multi-char operators */

          if (i < len)
            {
              char next = line[i];
              if ((ch == '=' && next == '=') ||
                  (ch == '!' && next == '=') ||
                  (ch == '<' && next == '=') ||
                  (ch == '>' && next == '=') ||
                  (ch == '&' && next == '&') ||
                  (ch == '|' && next == '|') ||
                  (ch == '+' && next == '+') ||
                  (ch == '-' && next == '-') ||
                  (ch == '-' && next == '>') ||
                  (ch == '<' && next == '<') ||
                  (ch == '>' && next == '>'))
                {
                  i++;
                }
            }

          spans[span_count].start = start;
          spans[span_count].end = i;
          spans[span_count].cls = SYN_OPERATOR;
          span_count++;
          continue;
        }

      /* Brackets */

      if (strchr("(){}[]", ch))
        {
          spans[span_count].start = i;
          spans[span_count].end = i + 1;
          spans[span_count].cls = SYN_BRACKET;
          span_count++;
          i++;
          continue;
        }

      /* Everything else */

      i++;
    }

  return span_count;
}

/****************************************************************************
 * Name: syntax_get_color
 *
 * Description:
 *   Get the LVGL color for a syntax class.
 *
 ****************************************************************************/

lv_color_t syntax_get_color(syntax_class_t cls)
{
  switch (cls)
    {
      case SYN_KEYWORD:     return g_scheme->keyword;
      case SYN_TYPE:        return g_scheme->type;
      case SYN_STRING:      return g_scheme->string;
      case SYN_CHAR:        return g_scheme->character;
      case SYN_NUMBER:      return g_scheme->number;
      case SYN_COMMENT:     return g_scheme->comment;
      case SYN_PREPROC:     return g_scheme->preproc;
      case SYN_FUNCTION:    return g_scheme->function;
      case SYN_OPERATOR:    return g_scheme->operator_;
      case SYN_BRACKET:     return g_scheme->bracket;
      case SYN_CONSTANT:    return g_scheme->constant;
      case SYN_SPECIAL:     return g_scheme->special;
      default:              return g_scheme->normal;
    }
}

/****************************************************************************
 * Name: syntax_get_scheme
 *
 * Description:
 *   Get the current colorscheme.
 *
 ****************************************************************************/

const colorscheme_t *syntax_get_scheme(void)
{
  return g_scheme;
}

/****************************************************************************
 * Name: syntax_reset_block_state
 *
 * Description:
 *   Reset block comment tracking (call when jumping to a new location).
 *
 ****************************************************************************/

void syntax_reset_block_state(void)
{
  g_in_block_comment = false;
}
