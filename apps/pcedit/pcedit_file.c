/****************************************************************************
 * apps/pcedit/pcedit_file.c
 *
 * File I/O operations for the editor.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <syslog.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * Save text to a file.
 *
 * @param path  File path on SD card
 * @param text  Text data to write
 * @param len   Length of text
 * @return 0 on success, negative error on failure
 */
int pcedit_file_save(const char *path, const char *text, size_t len)
{
  FILE *f;

  if (path == NULL || text == NULL)
    {
      return -EINVAL;
    }

  f = fopen(path, "w");
  if (f == NULL)
    {
      syslog(LOG_ERR, "PCEDIT: Cannot write to %s\n", path);
      return -EIO;
    }

  size_t written = fwrite(text, 1, len, f);
  fclose(f);

  if (written != len)
    {
      syslog(LOG_ERR, "PCEDIT: Short write (%zu/%zu) to %s\n",
             written, len, path);
      return -EIO;
    }

  syslog(LOG_INFO, "PCEDIT: Saved %zu bytes to %s\n", len, path);
  return 0;
}

/**
 * Load a file into a dynamically allocated buffer.
 * Caller owns the returned pointer (must free with pc_app_psram_free).
 *
 * @param path     File path on SD card
 * @param out_len  Output: file data length
 * @return Allocated buffer, or NULL on failure
 */
char *pcedit_file_load(const char *path, size_t *out_len)
{
  FILE *f;
  long  fsize;
  char *buf;

  if (path == NULL || out_len == NULL)
    {
      return NULL;
    }

  f = fopen(path, "r");
  if (f == NULL)
    {
      syslog(LOG_ERR, "PCEDIT: Cannot open %s\n", path);
      return NULL;
    }

  fseek(f, 0, SEEK_END);
  fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (fsize < 0 || fsize > (4 * 1024 * 1024))  /* 4MB limit */
    {
      syslog(LOG_ERR, "PCEDIT: File too large: %ld bytes\n", fsize);
      fclose(f);
      return NULL;
    }

  /* Import pc_app_psram_alloc from the OS framework */

  extern void *pc_app_psram_alloc(size_t size);
  buf = (char *)pc_app_psram_alloc(fsize + 1);

  if (buf == NULL)
    {
      fclose(f);
      return NULL;
    }

  size_t nread = fread(buf, 1, fsize, f);
  fclose(f);

  buf[nread] = '\0';
  *out_len = nread;

  syslog(LOG_INFO, "PCEDIT: Loaded %zu bytes from %s\n", nread, path);
  return buf;
}

/**
 * Check if a file exists and get its size.
 *
 * @param path  File path
 * @return File size in bytes, or -1 if not found
 */
long pcedit_file_size(const char *path)
{
  struct stat st;

  if (stat(path, &st) != 0)
    {
      return -1;
    }

  return (long)st.st_size;
}
