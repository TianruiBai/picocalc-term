/****************************************************************************
 * apps/pcedit/pcedit_buffer.c
 *
 * Gap buffer implementation for the text editor.
 * Allocated in PSRAM for large file support (up to ~4MB text).
 *
 * Gap buffer: [text before cursor][   GAP   ][text after cursor]
 * Inserts at cursor are O(1), movement is O(gap_size) worst case.
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

#define GAP_INITIAL_SIZE   4096
#define GAP_GROW_SIZE      4096

/****************************************************************************
 * Public Types (also in pcedit_main.c)
 ****************************************************************************/

typedef struct gap_buffer_s
{
  char   *buf;           /* Buffer in PSRAM */
  size_t  buf_size;      /* Total buffer size */
  size_t  gap_start;     /* Gap start index */
  size_t  gap_end;       /* Gap end index (exclusive) */
} gap_buffer_t;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int gap_buffer_init(gap_buffer_t *gb, size_t initial_size)
{
  gb->buf_size  = initial_size + GAP_INITIAL_SIZE;
  gb->buf       = (char *)pc_app_psram_alloc(gb->buf_size);

  if (gb->buf == NULL)
    {
      return -1;
    }

  gb->gap_start = 0;
  gb->gap_end   = GAP_INITIAL_SIZE;

  memset(gb->buf, 0, gb->buf_size);
  return 0;
}

void gap_buffer_free(gap_buffer_t *gb)
{
  if (gb->buf)
    {
      pc_app_psram_free(gb->buf);
      gb->buf = NULL;
    }
}

/**
 * Get the total text length (excluding gap).
 */
size_t gap_buffer_length(const gap_buffer_t *gb)
{
  return gb->buf_size - (gb->gap_end - gb->gap_start);
}

/**
 * Get the gap size.
 */
size_t gap_buffer_gap_size(const gap_buffer_t *gb)
{
  return gb->gap_end - gb->gap_start;
}

/**
 * Move the gap to a specific position in the text.
 */
void gap_buffer_move_gap(gap_buffer_t *gb, size_t pos)
{
  if (pos == gb->gap_start)
    {
      return;  /* Already there */
    }

  size_t gap_size = gb->gap_end - gb->gap_start;

  if (pos < gb->gap_start)
    {
      /* Move gap left: shift chars [pos, gap_start) to after gap */
      size_t count = gb->gap_start - pos;
      memmove(gb->buf + gb->gap_end - count,
              gb->buf + pos, count);
      gb->gap_start = pos;
      gb->gap_end   = pos + gap_size;
    }
  else
    {
      /* Move gap right: shift chars [gap_end, gap_end + delta) to gap_start */
      size_t count = pos - gb->gap_start;
      memmove(gb->buf + gb->gap_start,
              gb->buf + gb->gap_end, count);
      gb->gap_start = pos;
      gb->gap_end   = pos + gap_size;
    }
}

/**
 * Ensure the gap is at least min_size bytes.
 */
static int gap_buffer_grow(gap_buffer_t *gb, size_t min_size)
{
  size_t cur_gap = gb->gap_end - gb->gap_start;

  if (cur_gap >= min_size)
    {
      return 0;
    }

  size_t add = ((min_size - cur_gap) + GAP_GROW_SIZE - 1) / GAP_GROW_SIZE
               * GAP_GROW_SIZE;
  size_t new_size = gb->buf_size + add;

  char *new_buf = (char *)pc_app_psram_alloc(new_size);
  if (new_buf == NULL)
    {
      return -1;
    }

  /* Copy text before gap */
  memcpy(new_buf, gb->buf, gb->gap_start);

  /* Copy text after gap to new position */
  size_t after_len = gb->buf_size - gb->gap_end;
  memcpy(new_buf + new_size - after_len,
         gb->buf + gb->gap_end, after_len);

  pc_app_psram_free(gb->buf);
  gb->buf      = new_buf;
  gb->gap_end  = new_size - after_len;
  gb->buf_size = new_size;

  return 0;
}

/**
 * Insert a single character at the current gap position.
 */
int gap_buffer_insert(gap_buffer_t *gb, char ch)
{
  if (gap_buffer_gap_size(gb) < 1)
    {
      if (gap_buffer_grow(gb, GAP_GROW_SIZE) < 0)
        {
          return -1;
        }
    }

  gb->buf[gb->gap_start++] = ch;
  return 0;
}

/**
 * Insert a string at the current gap position.
 */
int gap_buffer_insert_str(gap_buffer_t *gb, const char *str, size_t len)
{
  if (gap_buffer_gap_size(gb) < len)
    {
      if (gap_buffer_grow(gb, len) < 0)
        {
          return -1;
        }
    }

  memcpy(gb->buf + gb->gap_start, str, len);
  gb->gap_start += len;
  return 0;
}

/**
 * Delete one character before the gap (backspace).
 */
int gap_buffer_delete_back(gap_buffer_t *gb)
{
  if (gb->gap_start == 0)
    {
      return -1;  /* Nothing to delete */
    }

  gb->gap_start--;
  return 0;
}

/**
 * Delete one character after the gap (forward delete).
 */
int gap_buffer_delete_forward(gap_buffer_t *gb)
{
  if (gb->gap_end >= gb->buf_size)
    {
      return -1;
    }

  gb->gap_end++;
  return 0;
}

/**
 * Get character at logical position (skipping the gap).
 */
char gap_buffer_char_at(const gap_buffer_t *gb, size_t pos)
{
  if (pos < gb->gap_start)
    {
      return gb->buf[pos];
    }
  else
    {
      return gb->buf[pos + (gb->gap_end - gb->gap_start)];
    }
}

/**
 * Linearize the buffer into a contiguous string.
 * Caller must free the returned pointer with pc_app_psram_free().
 */
char *gap_buffer_linearize(const gap_buffer_t *gb)
{
  size_t len = gap_buffer_length(gb);
  char *out = (char *)pc_app_psram_alloc(len + 1);

  if (out == NULL)
    {
      return NULL;
    }

  memcpy(out, gb->buf, gb->gap_start);
  memcpy(out + gb->gap_start, gb->buf + gb->gap_end,
         gb->buf_size - gb->gap_end);
  out[len] = '\0';

  return out;
}

/**
 * Load file contents into the gap buffer.
 */
int gap_buffer_load_file(gap_buffer_t *gb, const char *path)
{
  FILE *f = fopen(path, "r");
  if (f == NULL)
    {
      return -1;
    }

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (fsize < 0)
    {
      fclose(f);
      return -1;
    }

  /* Reinit buffer to hold the file */

  gap_buffer_free(gb);
  if (gap_buffer_init(gb, fsize) < 0)
    {
      fclose(f);
      return -1;
    }

  /* Read directly into the buffer before the gap */

  size_t read_len = fread(gb->buf, 1, fsize, f);
  fclose(f);

  gb->gap_start = read_len;

  syslog(LOG_INFO, "PCEDIT: Loaded %zu bytes from %s\n", read_len, path);
  return 0;
}
