/****************************************************************************
 * apps/pcedit/pcedit_search.c
 *
 * Search and substitution engine for the vi/vim editor.
 *
 * Supports:
 *   - Forward search (/)
 *   - Backward search (?)
 *   - Next/previous match (n/N)
 *   - Word under cursor search (* / #)
 *   - Substitution (:s/old/new/flags, :%s/old/new/g)
 *   - Incremental search highlighting
 *   - All-match highlighting (hlsearch)
 *   - Basic regex: . ^ $ * [] + ? | \b \d \w \s
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SEARCH_PATTERN_MAX   128
#define SEARCH_REPLACE_MAX   128
#define SEARCH_MATCH_MAX     256   /* Max highlighted matches per screen */

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef struct search_match_s
{
  int row;
  int col;
  int length;
} search_match_t;

typedef struct search_state_s
{
  char  pattern[SEARCH_PATTERN_MAX];
  char  replace[SEARCH_REPLACE_MAX];
  bool  forward;      /* true = / , false = ? */
  bool  active;       /* Is there an active search? */
  bool  wrap_scan;    /* Wrap around buffer */

  /* Last match position */

  int   match_row;
  int   match_col;
  int   match_len;

  /* All matches for hlsearch */

  search_match_t matches[SEARCH_MATCH_MAX];
  int            match_count;
} search_state_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static search_state_t g_search;

/****************************************************************************
 * Private Functions — Simple Pattern Matcher
 *
 *   We implement a simplified regex that covers the most common cases
 *   without pulling in a full regex library.
 *
 *   Supported:
 *     .       any character
 *     ^       start of line (in pattern position 0)
 *     $       end of line (at pattern end)
 *     *       zero or more of previous
 *     +       one or more of previous
 *     ?       zero or one of previous
 *     [abc]   character class
 *     [^abc]  negated class
 *     \d      digit
 *     \w      word char
 *     \s      whitespace
 *     \b      word boundary
 *     \\      literal backslash
 *
 *   No grouping, alternation, or backreferences.
 ****************************************************************************/

/****************************************************************************
 * Name: match_class
 *
 * Description:
 *   Match a character class [...].  Returns the number of pattern chars
 *   consumed (including the leading [) or 0 on failure.
 *
 ****************************************************************************/

static int match_class(const char *pat, char ch, bool *matched)
{
  bool negate = false;
  int i = 1; /* skip '[' */
  *matched = false;

  if (pat[i] == '^')
    {
      negate = true;
      i++;
    }

  bool found = false;

  while (pat[i] && pat[i] != ']')
    {
      /* Range: a-z */

      if (pat[i + 1] == '-' && pat[i + 2] && pat[i + 2] != ']')
        {
          if (ch >= pat[i] && ch <= pat[i + 2])
            {
              found = true;
            }

          i += 3;
        }
      else
        {
          if (ch == pat[i])
            {
              found = true;
            }

          i++;
        }
    }

  if (pat[i] == ']')
    {
      i++; /* consume ] */
    }

  *matched = negate ? !found : found;
  return i;
}

/****************************************************************************
 * Name: match_one
 *
 * Description:
 *   Match a single pattern element against a character.
 *   Returns the number of pattern characters consumed, or 0 on no match.
 *   Sets *consumed to pattern bytes used.
 *
 ****************************************************************************/

static bool match_one(const char *pat, char ch, char prev, char next,
                      int *pat_consumed)
{
  if (*pat == '.')
    {
      *pat_consumed = 1;
      return ch != '\0';
    }

  if (*pat == '\\' && pat[1])
    {
      *pat_consumed = 2;

      switch (pat[1])
        {
          case 'd': return isdigit((unsigned char)ch);
          case 'w': return isalnum((unsigned char)ch) || ch == '_';
          case 's': return isspace((unsigned char)ch);
          case 'b':
            {
              bool pw = (prev == 0) ? false :
                        (isalnum((unsigned char)prev) || prev == '_');
              bool nw = isalnum((unsigned char)ch) || ch == '_';
              return pw != nw;
            }
          default:
            return ch == pat[1]; /* literal escape */
        }
    }

  if (*pat == '[')
    {
      bool matched;
      *pat_consumed = match_class(pat, ch, &matched);
      return matched;
    }

  *pat_consumed = 1;
  return ch == *pat;
}

/****************************************************************************
 * Name: search_match_at
 *
 * Description:
 *   Try to match the pattern starting at text[pos].
 *   Returns length of match, or -1 on no match.
 *
 ****************************************************************************/

static int search_match_at(const char *pattern, const char *text,
                           int pos, int text_len, bool ignore_case)
{
  const char *pat = pattern;
  int ti = pos;
  int anchor_start = false;

  /* Handle ^ anchor */

  if (*pat == '^')
    {
      if (pos != 0) return -1;
      pat++;
      anchor_start = true;
    }

  int start_ti = ti;

  while (*pat)
    {
      /* Handle $ anchor at end of pattern */

      if (*pat == '$' && pat[1] == '\0')
        {
          if (ti == text_len)
            {
              return ti - start_ti;
            }

          return -1;
        }

      /* Check for quantifiers after next element */

      int elem_len;
      const char *elem = pat;

      /* Determine element length */

      if (*pat == '\\' && pat[1])
        {
          elem_len = 2;
        }
      else if (*pat == '[')
        {
          bool dummy;
          elem_len = match_class(pat, 'a', &dummy);
        }
      else
        {
          elem_len = 1;
        }

      char quant = pat[elem_len];

      if (quant == '*' || quant == '+' || quant == '?')
        {
          /* Greedy match */

          int min_reps = (quant == '+') ? 1 : 0;
          int max_reps = (quant == '?') ? 1 : text_len - ti;
          int count = 0;

          while (count < max_reps && ti + count < text_len)
            {
              int pcons;
              char tc = text[ti + count];
              char prev_c = (ti + count > 0) ? text[ti + count - 1] : 0;
              char next_c = (ti + count + 1 < text_len) ?
                            text[ti + count + 1] : 0;

              if (ignore_case)
                {
                  tc = tolower((unsigned char)tc);
                }

              if (!match_one(elem, tc, prev_c, next_c, &pcons))
                {
                  break;
                }

              count++;
            }

          /* Try from longest to shortest (greedy) */

          pat += elem_len + 1;

          for (int c = count; c >= min_reps; c--)
            {
              int sub = search_match_at(pat, text, ti + c, text_len,
                                        ignore_case);
              if (sub >= 0)
                {
                  return (ti + c - start_ti) + sub;
                }
            }

          return -1;
        }
      else
        {
          /* Simple match */

          if (ti >= text_len) return -1;

          int pcons;
          char tc = text[ti];
          char prev_c = (ti > 0) ? text[ti - 1] : 0;
          char next_c = (ti + 1 < text_len) ? text[ti + 1] : 0;

          if (ignore_case)
            {
              tc = tolower((unsigned char)tc);
            }

          if (!match_one(elem, tc, prev_c, next_c, &pcons))
            {
              return -1;
            }

          pat += pcons;
          ti++;
        }
    }

  return ti - start_ti;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: search_init
 ****************************************************************************/

void search_init(void)
{
  memset(&g_search, 0, sizeof(g_search));
  g_search.wrap_scan = true;
  g_search.match_row = -1;
  g_search.match_col = -1;
}

/****************************************************************************
 * Name: search_set_pattern
 *
 * Description:
 *   Set the search pattern and direction.
 *
 ****************************************************************************/

void search_set_pattern(const char *pattern, bool forward)
{
  strncpy(g_search.pattern, pattern, SEARCH_PATTERN_MAX - 1);
  g_search.pattern[SEARCH_PATTERN_MAX - 1] = '\0';
  g_search.forward = forward;
  g_search.active = (pattern[0] != '\0');
  g_search.match_count = 0;
}

/****************************************************************************
 * Name: search_get_pattern
 ****************************************************************************/

const char *search_get_pattern(void)
{
  return g_search.pattern;
}

/****************************************************************************
 * Name: search_is_active
 ****************************************************************************/

bool search_is_active(void)
{
  return g_search.active;
}

/****************************************************************************
 * Name: search_clear
 ****************************************************************************/

void search_clear(void)
{
  g_search.active = false;
  g_search.match_count = 0;
}

/****************************************************************************
 * Name: search_find_in_line
 *
 * Description:
 *   Find the first match of g_search.pattern in `line` starting from
 *   column `start_col`.  If found, returns the column and sets *match_len.
 *   Returns -1 if no match.
 *
 ****************************************************************************/

int search_find_in_line(const char *line, int line_len,
                        int start_col, bool ignore_case,
                        int *match_len)
{
  if (!g_search.active || g_search.pattern[0] == '\0')
    {
      return -1;
    }

  /* Lowercase copy if ignore_case */

  char lower_pat[SEARCH_PATTERN_MAX];
  const char *pat = g_search.pattern;

  if (ignore_case)
    {
      int k;
      for (k = 0; pat[k] && k < SEARCH_PATTERN_MAX - 1; k++)
        {
          lower_pat[k] = tolower((unsigned char)pat[k]);
        }

      lower_pat[k] = '\0';
      pat = lower_pat;
    }

  for (int col = start_col; col < line_len; col++)
    {
      int mlen = search_match_at(pat, line, col, line_len, ignore_case);
      if (mlen > 0)
        {
          if (match_len) *match_len = mlen;
          return col;
        }
    }

  return -1;
}

/****************************************************************************
 * Name: search_find_in_line_reverse
 *
 * Description:
 *   Find the last match of g_search.pattern in `line` at or before
 *   column `end_col`.  Returns the column and sets *match_len.
 *   Returns -1 if no match.
 *
 ****************************************************************************/

int search_find_in_line_reverse(const char *line, int line_len,
                                int end_col, bool ignore_case,
                                int *match_len)
{
  if (!g_search.active || g_search.pattern[0] == '\0')
    {
      return -1;
    }

  char lower_pat[SEARCH_PATTERN_MAX];
  const char *pat = g_search.pattern;

  if (ignore_case)
    {
      int k;
      for (k = 0; pat[k] && k < SEARCH_PATTERN_MAX - 1; k++)
        {
          lower_pat[k] = tolower((unsigned char)pat[k]);
        }

      lower_pat[k] = '\0';
      pat = lower_pat;
    }

  int last_col = -1;
  int last_len = 0;

  for (int col = 0; col <= end_col && col < line_len; col++)
    {
      int mlen = search_match_at(pat, line, col, line_len, ignore_case);
      if (mlen > 0)
        {
          last_col = col;
          last_len = mlen;
        }
    }

  if (last_col >= 0 && match_len)
    {
      *match_len = last_len;
    }

  return last_col;
}

/****************************************************************************
 * Name: search_parse_substitute
 *
 * Description:
 *   Parse a :s command string.
 *   Format: [range]s/pattern/replacement/[flags]
 *
 *   Supported flags: g (global), i (ignore case), c (confirm)
 *
 *   Returns true if parsed successfully.
 *
 ****************************************************************************/

typedef struct subst_cmd_s
{
  int   range_start;    /* -1 = current line */
  int   range_end;      /* -1 = current line */
  char  pattern[SEARCH_PATTERN_MAX];
  char  replacement[SEARCH_REPLACE_MAX];
  bool  global;
  bool  ignore_case;
  bool  confirm;
} subst_cmd_t;

bool search_parse_substitute(const char *cmd, int current_line,
                             int total_lines, subst_cmd_t *out)
{
  memset(out, 0, sizeof(*out));
  out->range_start = current_line;
  out->range_end = current_line;

  const char *p = cmd;

  /* Parse range: %, .,$ , N,M , etc. */

  if (*p == '%')
    {
      out->range_start = 0;
      out->range_end = total_lines - 1;
      p++;
    }
  else if (*p == '.')
    {
      out->range_start = current_line;
      p++;

      if (*p == ',')
        {
          p++;

          if (*p == '$')
            {
              out->range_end = total_lines - 1;
              p++;
            }
          else if (isdigit((unsigned char)*p))
            {
              out->range_end = atoi(p) - 1;
              while (isdigit((unsigned char)*p)) p++;
            }
        }
      else
        {
          out->range_end = current_line;
        }
    }
  else if (isdigit((unsigned char)*p))
    {
      out->range_start = atoi(p) - 1;
      while (isdigit((unsigned char)*p)) p++;

      if (*p == ',')
        {
          p++;

          if (*p == '$')
            {
              out->range_end = total_lines - 1;
              p++;
            }
          else if (isdigit((unsigned char)*p))
            {
              out->range_end = atoi(p) - 1;
              while (isdigit((unsigned char)*p)) p++;
            }
        }
      else
        {
          out->range_end = out->range_start;
        }
    }

  /* Expect 's' */

  if (*p != 's') return false;
  p++;

  /* Delimiter */

  char delim = *p;
  if (delim == '\0') return false;
  p++;

  /* Pattern */

  int pi = 0;
  while (*p && *p != delim && pi < SEARCH_PATTERN_MAX - 1)
    {
      if (*p == '\\' && p[1] == delim)
        {
          out->pattern[pi++] = delim;
          p += 2;
        }
      else
        {
          out->pattern[pi++] = *p++;
        }
    }

  out->pattern[pi] = '\0';

  if (*p == delim) p++;

  /* Replacement */

  int ri = 0;
  while (*p && *p != delim && ri < SEARCH_REPLACE_MAX - 1)
    {
      if (*p == '\\' && p[1] == delim)
        {
          out->replacement[ri++] = delim;
          p += 2;
        }
      else
        {
          out->replacement[ri++] = *p++;
        }
    }

  out->replacement[ri] = '\0';

  if (*p == delim) p++;

  /* Flags */

  while (*p)
    {
      switch (*p)
        {
          case 'g': out->global = true; break;
          case 'i': out->ignore_case = true; break;
          case 'c': out->confirm = true; break;
          default: break;
        }

      p++;
    }

  /* Also set the search pattern so n/N work */

  if (out->pattern[0])
    {
      search_set_pattern(out->pattern, true);
    }

  return true;
}

/****************************************************************************
 * Name: search_substitute_line
 *
 * Description:
 *   Perform substitution on a single line.  The caller provides a buffer
 *   of at least `buf_size` bytes for the result.
 *
 *   Returns the number of substitutions made.
 *
 ****************************************************************************/

int search_substitute_line(const char *line, int line_len,
                           const subst_cmd_t *sub,
                           char *out_buf, int buf_size)
{
  int subs = 0;
  int oi = 0;
  int ti = 0;

  char lower_pat[SEARCH_PATTERN_MAX];
  const char *pat = sub->pattern;

  if (sub->ignore_case)
    {
      int k;
      for (k = 0; pat[k] && k < SEARCH_PATTERN_MAX - 1; k++)
        {
          lower_pat[k] = tolower((unsigned char)pat[k]);
        }

      lower_pat[k] = '\0';
      pat = lower_pat;
    }

  while (ti < line_len && oi < buf_size - 1)
    {
      int mlen = search_match_at(pat, line, ti, line_len,
                                 sub->ignore_case);

      if (mlen > 0)
        {
          /* Copy replacement */

          int rlen = strlen(sub->replacement);
          for (int r = 0; r < rlen && oi < buf_size - 1; r++)
            {
              out_buf[oi++] = sub->replacement[r];
            }

          ti += mlen;
          subs++;

          if (!sub->global) 
            {
              /* Copy rest of line */

              while (ti < line_len && oi < buf_size - 1)
                {
                  out_buf[oi++] = line[ti++];
                }

              break;
            }
        }
      else
        {
          out_buf[oi++] = line[ti++];
        }
    }

  out_buf[oi] = '\0';
  return subs;
}

/****************************************************************************
 * Name: search_collect_matches
 *
 * Description:
 *   Collect all matches in a range of lines for hlsearch highlighting.
 *   The caller provides a callback to get line text.
 *
 *   typedef const char *(*get_line_fn)(int row, int *len, void *ctx);
 *
 ****************************************************************************/

typedef const char *(*search_get_line_fn)(int row, int *len, void *ctx);

int search_collect_matches(int start_row, int end_row,
                           bool ignore_case,
                           search_get_line_fn get_line, void *ctx)
{
  g_search.match_count = 0;

  if (!g_search.active) return 0;

  for (int row = start_row; row <= end_row; row++)
    {
      int line_len;
      const char *line = get_line(row, &line_len, ctx);
      if (line == NULL) continue;

      int col = 0;

      while (col < line_len &&
             g_search.match_count < SEARCH_MATCH_MAX)
        {
          int mlen;
          int mcol = search_find_in_line(line, line_len, col,
                                         ignore_case, &mlen);

          if (mcol < 0) break;

          g_search.matches[g_search.match_count].row = row;
          g_search.matches[g_search.match_count].col = mcol;
          g_search.matches[g_search.match_count].length = mlen;
          g_search.match_count++;

          col = mcol + (mlen > 0 ? mlen : 1);
        }
    }

  return g_search.match_count;
}

/****************************************************************************
 * Name: search_get_matches
 ****************************************************************************/

const search_match_t *search_get_matches(int *count)
{
  if (count) *count = g_search.match_count;
  return g_search.matches;
}

/****************************************************************************
 * Name: search_is_match_at
 *
 * Description:
 *   Check if there's a highlighted match at the given row, col.
 *   If so, returns the match length.  Otherwise returns 0.
 *
 ****************************************************************************/

int search_is_match_at(int row, int col)
{
  for (int i = 0; i < g_search.match_count; i++)
    {
      const search_match_t *m = &g_search.matches[i];
      if (m->row == row && col >= m->col && col < m->col + m->length)
        {
          return m->length;
        }
    }

  return 0;
}
