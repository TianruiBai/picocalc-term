/****************************************************************************
 * apps/pcfiles/pcfiles_ops.c
 *
 * File operations for the PicoCalc File Explorer.
 * Copy, move, delete, mkdir, rename — all with error handling.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define COPY_BUF_SIZE  4096

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pcfiles_copy_file
 *
 * Description:
 *   Copy a single file from src to dst.
 *
 ****************************************************************************/

static int pcfiles_copy_file(const char *src, const char *dst)
{
  int     src_fd;
  int     dst_fd;
  uint8_t *buf;
  ssize_t n;
  int     ret = 0;

  src_fd = open(src, O_RDONLY);
  if (src_fd < 0)
    {
      syslog(LOG_ERR, "PCFILES: Cannot open source %s: %d\n",
             src, errno);
      return -errno;
    }

  dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (dst_fd < 0)
    {
      syslog(LOG_ERR, "PCFILES: Cannot create dest %s: %d\n",
             dst, errno);
      close(src_fd);
      return -errno;
    }

  buf = (uint8_t *)malloc(COPY_BUF_SIZE);
  if (buf == NULL)
    {
      close(src_fd);
      close(dst_fd);
      return -ENOMEM;
    }

  while ((n = read(src_fd, buf, COPY_BUF_SIZE)) > 0)
    {
      ssize_t written = 0;
      while (written < n)
        {
          ssize_t w = write(dst_fd, buf + written, n - written);
          if (w < 0)
            {
              ret = -errno;
              goto cleanup;
            }
          written += w;
        }
    }

  if (n < 0)
    {
      ret = -errno;
    }

cleanup:
  free(buf);
  close(src_fd);
  close(dst_fd);

  if (ret < 0)
    {
      /* Clean up partial dest file on error */

      unlink(dst);
    }

  return ret;
}

/****************************************************************************
 * Name: pcfiles_delete_recursive
 *
 * Description:
 *   Recursively delete a file or directory.
 *
 ****************************************************************************/

static int pcfiles_delete_recursive(const char *path)
{
  struct stat st;

  if (stat(path, &st) != 0)
    {
      return -errno;
    }

  if (!S_ISDIR(st.st_mode))
    {
      /* It's a file — just unlink */

      if (unlink(path) != 0)
        {
          return -errno;
        }
      return 0;
    }

  /* It's a directory — recurse */

  DIR           *dir;
  struct dirent *ent;
  char           fullpath[256];
  int            ret = 0;

  dir = opendir(path);
  if (dir == NULL)
    {
      return -errno;
    }

  while ((ent = readdir(dir)) != NULL)
    {
      if (strcmp(ent->d_name, ".") == 0 ||
          strcmp(ent->d_name, "..") == 0)
        {
          continue;
        }

      snprintf(fullpath, sizeof(fullpath), "%s/%s",
               path, ent->d_name);

      ret = pcfiles_delete_recursive(fullpath);
      if (ret < 0)
        {
          syslog(LOG_ERR, "PCFILES: Failed to delete %s: %d\n",
                 fullpath, ret);
          /* Continue trying other files */
        }
    }

  closedir(dir);

  if (rmdir(path) != 0)
    {
      return -errno;
    }

  return ret;
}

/****************************************************************************
 * Name: pcfiles_copy_dir_recursive
 *
 * Description:
 *   Recursively copy a directory.
 *
 ****************************************************************************/

static int pcfiles_copy_dir_recursive(const char *src, const char *dst)
{
  DIR           *dir;
  struct dirent *ent;
  struct stat    st;
  char           src_path[256];
  char           dst_path[256];
  int            ret;

  /* Create destination directory */

  if (mkdir(dst, 0755) != 0 && errno != EEXIST)
    {
      return -errno;
    }

  dir = opendir(src);
  if (dir == NULL)
    {
      return -errno;
    }

  while ((ent = readdir(dir)) != NULL)
    {
      if (strcmp(ent->d_name, ".") == 0 ||
          strcmp(ent->d_name, "..") == 0)
        {
          continue;
        }

      snprintf(src_path, sizeof(src_path), "%s/%s",
               src, ent->d_name);
      snprintf(dst_path, sizeof(dst_path), "%s/%s",
               dst, ent->d_name);

      if (stat(src_path, &st) != 0)
        {
          continue;
        }

      if (S_ISDIR(st.st_mode))
        {
          ret = pcfiles_copy_dir_recursive(src_path, dst_path);
        }
      else
        {
          ret = pcfiles_copy_file(src_path, dst_path);
        }

      if (ret < 0)
        {
          syslog(LOG_ERR, "PCFILES: Copy error %s -> %s: %d\n",
                 src_path, dst_path, ret);
        }
    }

  closedir(dir);
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pcfiles_copy
 *
 * Description:
 *   Copy a file or directory from src to dst.
 *
 ****************************************************************************/

int pcfiles_copy(const char *src, const char *dst)
{
  struct stat st;

  syslog(LOG_INFO, "PCFILES: Copy %s -> %s\n", src, dst);

  if (stat(src, &st) != 0)
    {
      return -errno;
    }

  if (S_ISDIR(st.st_mode))
    {
      return pcfiles_copy_dir_recursive(src, dst);
    }
  else
    {
      return pcfiles_copy_file(src, dst);
    }
}

/****************************************************************************
 * Name: pcfiles_move
 *
 * Description:
 *   Move (rename) a file or directory. Falls back to copy+delete
 *   if rename fails (cross-filesystem move).
 *
 ****************************************************************************/

int pcfiles_move(const char *src, const char *dst)
{
  syslog(LOG_INFO, "PCFILES: Move %s -> %s\n", src, dst);

  /* Try rename first (fast, same filesystem) */

  if (rename(src, dst) == 0)
    {
      return 0;
    }

  /* rename failed — try copy + delete (cross-filesystem) */

  if (errno == EXDEV)
    {
      int ret = pcfiles_copy(src, dst);
      if (ret != 0)
        {
          return ret;
        }

      return pcfiles_delete_recursive(src);
    }

  return -errno;
}

/****************************************************************************
 * Name: pcfiles_delete
 *
 * Description:
 *   Delete a file or directory (recursive).
 *
 ****************************************************************************/

int pcfiles_delete(const char *path)
{
  syslog(LOG_INFO, "PCFILES: Delete %s\n", path);
  return pcfiles_delete_recursive(path);
}

/****************************************************************************
 * Name: pcfiles_mkdir
 *
 * Description:
 *   Create a new directory.
 *
 ****************************************************************************/

int pcfiles_mkdir(const char *path)
{
  syslog(LOG_INFO, "PCFILES: Mkdir %s\n", path);

  if (mkdir(path, 0755) != 0)
    {
      return -errno;
    }

  return 0;
}

/****************************************************************************
 * Name: pcfiles_rename
 *
 * Description:
 *   Rename a file or directory.
 *   old_path is the full current path; new_name is just the new filename.
 *
 ****************************************************************************/

int pcfiles_rename(const char *old_path, const char *new_name)
{
  char new_path[256];
  char dir[256];

  syslog(LOG_INFO, "PCFILES: Rename %s -> %s\n", old_path, new_name);

  /* Get directory portion of old_path */

  strncpy(dir, old_path, sizeof(dir) - 1);
  char *last_slash = strrchr(dir, '/');
  if (last_slash != NULL)
    {
      *(last_slash + 1) = '\0';
    }

  snprintf(new_path, sizeof(new_path), "%s%s", dir, new_name);

  if (rename(old_path, new_path) != 0)
    {
      return -errno;
    }

  return 0;
}
