/****************************************************************************
 * apps/pcfiles/pcfiles_main.c
 *
 * PicoCalc File Explorer — browse files on SD card and flash.
 *
 * Features:
 *   - Navigate directories with arrow keys and Enter
 *   - View file info (size, modified date)
 *   - Copy, move, rename, delete files
 *   - Quick-switch between SD card (/mnt/sd) and flash (/data)
 *   - Open text files in pcedit, audio in pcaudio, etc.
 *   - Preview file contents (first 1KB for text files)
 *
 * UI Layout (320×300):
 *   [Path bar]                              (top, 24px)
 *   [File list]                             (middle, scrollable)
 *   [Status bar: size | count | free space] (bottom, 24px)
 *   [Action buttons row]                    (bottom, 28px)
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
#include <time.h>

#include <lvgl/lvgl.h>

#include "pcterm/app.h"

/* Forward declarations for file operations (pcfiles_ops.c) */

extern int pcfiles_copy(const char *src, const char *dst);
extern int pcfiles_move(const char *src, const char *dst);
extern int pcfiles_delete(const char *path);
extern int pcfiles_mkdir(const char *path);
extern int pcfiles_rename(const char *old_path, const char *new_name);

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PCFILES_MAX_ENTRIES   256
#define PCFILES_PATH_MAX      256
#define PCFILES_NAME_MAX      128
#define PCFILES_PREVIEW_SIZE  1024

/* Root mount points */

#define PCFILES_SD_ROOT       "/mnt/sd"
#define PCFILES_FLASH_ROOT    "/data"

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Directory entry info */

typedef struct pcfiles_entry_s
{
  char     name[PCFILES_NAME_MAX];
  bool     is_dir;
  uint32_t size;
  time_t   mtime;
} pcfiles_entry_t;

/* Sort mode */

typedef enum
{
  SORT_NAME_ASC = 0,
  SORT_NAME_DESC,
  SORT_SIZE_ASC,
  SORT_SIZE_DESC,
  SORT_DATE_ASC,
  SORT_DATE_DESC,
} pcfiles_sort_t;

/* File explorer state */

typedef struct pcfiles_state_s
{
  char               cwd[PCFILES_PATH_MAX];
  pcfiles_entry_t    entries[PCFILES_MAX_ENTRIES];
  int                entry_count;
  int                selected;
  pcfiles_sort_t     sort_mode;
  bool               show_hidden;

  /* Clipboard for copy/move */

  char               clipboard_path[PCFILES_PATH_MAX];
  bool               clipboard_cut;  /* true = move, false = copy */
  bool               clipboard_valid;

  /* LVGL widgets */

  lv_obj_t          *screen;
  lv_obj_t          *path_label;
  lv_obj_t          *file_list;
  lv_obj_t          *status_label;
  lv_obj_t          *preview_win;
  lv_obj_t          *btn_bar;
} pcfiles_state_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static pcfiles_state_t g_state;

/* Forward declarations for static functions */

static void pcfiles_show_preview(const pcfiles_entry_t *entry);
static void pcfiles_close_preview_cb(lv_event_t *e);
static void pcfiles_copy_cb(lv_event_t *e);
static void pcfiles_cut_cb(lv_event_t *e);
static void pcfiles_paste_cb(lv_event_t *e);
static void pcfiles_delete_cb(lv_event_t *e);
static void pcfiles_mkdir_cb(lv_event_t *e);
static void pcfiles_switch_root_cb(lv_event_t *e);
static void pcfiles_toggle_hidden_cb(lv_event_t *e);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pcfiles_format_size
 *
 * Description:
 *   Format a file size as human-readable string.
 *
 ****************************************************************************/

static void pcfiles_format_size(uint32_t size, char *buf, size_t buflen)
{
  if (size < 1024)
    {
      snprintf(buf, buflen, "%luB", (unsigned long)size);
    }
  else if (size < 1024 * 1024)
    {
      snprintf(buf, buflen, "%.1fKB", (double)size / 1024.0);
    }
  else
    {
      snprintf(buf, buflen, "%.1fMB", (double)size / (1024.0 * 1024.0));
    }
}

/****************************************************************************
 * Name: pcfiles_entry_compare
 *
 * Description:
 *   Compare two entries for qsort. Directories always come first.
 *
 ****************************************************************************/

static int pcfiles_entry_compare(const void *a, const void *b)
{
  const pcfiles_entry_t *ea = (const pcfiles_entry_t *)a;
  const pcfiles_entry_t *eb = (const pcfiles_entry_t *)b;

  /* Directories first */

  if (ea->is_dir && !eb->is_dir) return -1;
  if (!ea->is_dir && eb->is_dir) return  1;

  switch (g_state.sort_mode)
    {
      case SORT_NAME_ASC:
        return strcasecmp(ea->name, eb->name);
      case SORT_NAME_DESC:
        return strcasecmp(eb->name, ea->name);
      case SORT_SIZE_ASC:
        return (int)(ea->size - eb->size);
      case SORT_SIZE_DESC:
        return (int)(eb->size - ea->size);
      case SORT_DATE_ASC:
        return (int)(ea->mtime - eb->mtime);
      case SORT_DATE_DESC:
        return (int)(eb->mtime - ea->mtime);
      default:
        return strcasecmp(ea->name, eb->name);
    }
}

/****************************************************************************
 * Name: pcfiles_scan_dir
 *
 * Description:
 *   Read the current directory and populate the entries array.
 *
 ****************************************************************************/

static int pcfiles_scan_dir(void)
{
  DIR           *dir;
  struct dirent *ent;
  struct stat    st;
  char           fullpath[PCFILES_PATH_MAX];

  g_state.entry_count = 0;

  dir = opendir(g_state.cwd);
  if (dir == NULL)
    {
      syslog(LOG_ERR, "PCFILES: Cannot open %s: %d\n",
             g_state.cwd, errno);
      return -errno;
    }

  /* Add ".." entry unless at a root mount point */

  if (strcmp(g_state.cwd, PCFILES_SD_ROOT) != 0 &&
      strcmp(g_state.cwd, PCFILES_FLASH_ROOT) != 0 &&
      strcmp(g_state.cwd, "/") != 0)
    {
      pcfiles_entry_t *e = &g_state.entries[g_state.entry_count];
      strncpy(e->name, "..", PCFILES_NAME_MAX - 1);
      e->is_dir = true;
      e->size   = 0;
      e->mtime  = 0;
      g_state.entry_count++;
    }

  while ((ent = readdir(dir)) != NULL &&
         g_state.entry_count < PCFILES_MAX_ENTRIES)
    {
      /* Skip . and .. */

      if (strcmp(ent->d_name, ".") == 0 ||
          strcmp(ent->d_name, "..") == 0)
        {
          continue;
        }

      /* Skip hidden files unless show_hidden is set */

      if (!g_state.show_hidden && ent->d_name[0] == '.')
        {
          continue;
        }

      snprintf(fullpath, sizeof(fullpath), "%s/%s",
               g_state.cwd, ent->d_name);

      pcfiles_entry_t *e = &g_state.entries[g_state.entry_count];
      strncpy(e->name, ent->d_name, PCFILES_NAME_MAX - 1);
      e->name[PCFILES_NAME_MAX - 1] = '\0';

      if (stat(fullpath, &st) == 0)
        {
          e->is_dir = S_ISDIR(st.st_mode);
          e->size   = (uint32_t)st.st_size;
          e->mtime  = st.st_mtime;
        }
      else
        {
          e->is_dir = false;
          e->size   = 0;
          e->mtime  = 0;
        }

      g_state.entry_count++;
    }

  closedir(dir);

  /* Sort entries (skip ".." entry at index 0 if present) */

  int sort_start = 0;
  if (g_state.entry_count > 0 &&
      strcmp(g_state.entries[0].name, "..") == 0)
    {
      sort_start = 1;
    }

  if (g_state.entry_count - sort_start > 1)
    {
      qsort(&g_state.entries[sort_start],
            g_state.entry_count - sort_start,
            sizeof(pcfiles_entry_t),
            pcfiles_entry_compare);
    }

  return g_state.entry_count;
}

/****************************************************************************
 * Name: pcfiles_refresh_ui
 *
 * Description:
 *   Rebuild the LVGL file list from current entries.
 *
 ****************************************************************************/

static void pcfiles_refresh_ui(void)
{
  /* Update path bar */

  lv_label_set_text(g_state.path_label, g_state.cwd);

  /* Rebuild file list */

  lv_obj_clean(g_state.file_list);

  if (g_state.entry_count == 0)
    {
      lv_list_add_text(g_state.file_list, "(empty directory)");
    }
  else
    {
      for (int i = 0; i < g_state.entry_count; i++)
        {
          pcfiles_entry_t *e = &g_state.entries[i];
          char label_buf[160];

          if (e->is_dir)
            {
              snprintf(label_buf, sizeof(label_buf), "%s/", e->name);
            }
          else
            {
              char size_str[16];
              pcfiles_format_size(e->size, size_str, sizeof(size_str));
              snprintf(label_buf, sizeof(label_buf), "%-20s %8s",
                       e->name, size_str);
            }

          const char *icon = e->is_dir ? LV_SYMBOL_DIRECTORY
                                       : LV_SYMBOL_FILE;

          lv_obj_t *btn = lv_list_add_btn(g_state.file_list,
                                           icon, label_buf);

          /* Highlight selected */

          if (i == g_state.selected)
            {
              lv_obj_add_state(btn, LV_STATE_FOCUSED);
            }
        }
    }

  /* Update status bar */

  char status_buf[80];
  int dirs = 0;
  int files = 0;
  uint32_t total_size = 0;

  for (int i = 0; i < g_state.entry_count; i++)
    {
      if (strcmp(g_state.entries[i].name, "..") == 0) continue;
      if (g_state.entries[i].is_dir)
        {
          dirs++;
        }
      else
        {
          files++;
          total_size += g_state.entries[i].size;
        }
    }

  char total_str[16];
  pcfiles_format_size(total_size, total_str, sizeof(total_str));

  snprintf(status_buf, sizeof(status_buf),
           "%d dirs, %d files (%s)%s",
           dirs, files, total_str,
           g_state.clipboard_valid ? "  [Clipboard]" : "");

  lv_label_set_text(g_state.status_label, status_buf);
}

/****************************************************************************
 * Name: pcfiles_navigate
 *
 * Description:
 *   Change directory and refresh the view.
 *
 ****************************************************************************/

static void pcfiles_navigate(const char *path)
{
  strncpy(g_state.cwd, path, PCFILES_PATH_MAX - 1);
  g_state.cwd[PCFILES_PATH_MAX - 1] = '\0';
  g_state.selected = 0;

  pcfiles_scan_dir();
  pcfiles_refresh_ui();
}

/****************************************************************************
 * Name: pcfiles_navigate_parent
 *
 * Description:
 *   Go up one directory level.
 *
 ****************************************************************************/

static void pcfiles_navigate_parent(void)
{
  char *last_slash = strrchr(g_state.cwd, '/');
  if (last_slash != NULL && last_slash != g_state.cwd)
    {
      *last_slash = '\0';
    }

  g_state.selected = 0;
  pcfiles_scan_dir();
  pcfiles_refresh_ui();
}

/****************************************************************************
 * Name: pcfiles_enter_selected
 *
 * Description:
 *   Enter the selected directory or open the selected file.
 *
 ****************************************************************************/

static void pcfiles_enter_selected(void)
{
  if (g_state.selected < 0 ||
      g_state.selected >= g_state.entry_count)
    {
      return;
    }

  pcfiles_entry_t *e = &g_state.entries[g_state.selected];

  if (strcmp(e->name, "..") == 0)
    {
      pcfiles_navigate_parent();
      return;
    }

  if (e->is_dir)
    {
      /* Enter directory */

      char new_path[PCFILES_PATH_MAX];
      snprintf(new_path, sizeof(new_path), "%s/%s",
               g_state.cwd, e->name);
      pcfiles_navigate(new_path);
      return;
    }

  /* File selected — show preview window */

  pcfiles_show_preview(e);
}

/****************************************************************************
 * Name: pcfiles_show_preview
 *
 * Description:
 *   Show a preview/info window for the selected file.
 *
 ****************************************************************************/

static void pcfiles_show_preview(const pcfiles_entry_t *entry)
{
  char fullpath[PCFILES_PATH_MAX];
  snprintf(fullpath, sizeof(fullpath), "%s/%s",
           g_state.cwd, entry->name);

  /* Create a popup window */

  if (g_state.preview_win != NULL)
    {
      lv_obj_delete(g_state.preview_win);
    }

  g_state.preview_win = lv_win_create(g_state.screen);
  lv_win_add_title(g_state.preview_win, entry->name);
  lv_obj_set_size(g_state.preview_win, 300, 250);
  lv_obj_center(g_state.preview_win);

  lv_obj_t *content = lv_win_get_content(g_state.preview_win);
  lv_obj_set_style_pad_all(content, 4, 0);

  /* File info line */

  char info_buf[128];
  char size_str[16];
  pcfiles_format_size(entry->size, size_str, sizeof(size_str));

  struct tm *tm_info = localtime(&entry->mtime);
  char date_str[32] = "unknown";
  if (tm_info != NULL)
    {
      strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M", tm_info);
    }

  snprintf(info_buf, sizeof(info_buf),
           "Size: %s\nModified: %s", size_str, date_str);

  lv_obj_t *info_label = lv_label_create(content);
  lv_label_set_text(info_label, info_buf);
  lv_obj_set_style_text_font(info_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(info_label,
                              lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_width(info_label, 280);

  /* Try to preview text files */

  const char *ext = strrchr(entry->name, '.');
  bool is_text = false;

  if (ext != NULL)
    {
      if (strcasecmp(ext, ".txt") == 0 ||
          strcasecmp(ext, ".c") == 0 ||
          strcasecmp(ext, ".h") == 0 ||
          strcasecmp(ext, ".md") == 0 ||
          strcasecmp(ext, ".csv") == 0 ||
          strcasecmp(ext, ".json") == 0 ||
          strcasecmp(ext, ".sh") == 0 ||
          strcasecmp(ext, ".cfg") == 0 ||
          strcasecmp(ext, ".conf") == 0 ||
          strcasecmp(ext, ".log") == 0 ||
          strcasecmp(ext, ".ini") == 0)
        {
          is_text = true;
        }
    }

  if (is_text && entry->size > 0)
    {
      int fd = open(fullpath, O_RDONLY);
      if (fd >= 0)
        {
          size_t preview_len = (entry->size < PCFILES_PREVIEW_SIZE)
                               ? entry->size : PCFILES_PREVIEW_SIZE;
          char *preview_buf = (char *)malloc(preview_len + 1);
          if (preview_buf != NULL)
            {
              ssize_t n = read(fd, preview_buf, preview_len);
              if (n > 0)
                {
                  preview_buf[n] = '\0';

                  lv_obj_t *sep = lv_label_create(content);
                  lv_label_set_text(sep, "--- Preview ---");
                  lv_obj_set_style_text_color(
                    sep, lv_palette_main(LV_PALETTE_CYAN), 0);

                  lv_obj_t *preview_label = lv_label_create(content);
                  lv_label_set_text(preview_label, preview_buf);
                  lv_obj_set_style_text_font(preview_label,
                                             &lv_font_montserrat_12, 0);
                  lv_obj_set_width(preview_label, 280);
                  lv_label_set_long_mode(preview_label,
                                         LV_LABEL_LONG_WRAP);
                }
              free(preview_buf);
            }
          close(fd);
        }
    }

  /* Close button */

  lv_obj_t *close_btn = lv_win_add_button(g_state.preview_win,
                                            LV_SYMBOL_CLOSE, 24);
  lv_obj_add_event_cb(close_btn, pcfiles_close_preview_cb,
                      LV_EVENT_CLICKED, NULL);
}

/****************************************************************************
 * Name: pcfiles_close_preview_cb
 *
 * Description:
 *   LVGL callback to close the preview window.
 *
 ****************************************************************************/

static void pcfiles_close_preview_cb(lv_event_t *e)
{
  (void)e;

  if (g_state.preview_win != NULL)
    {
      lv_obj_delete(g_state.preview_win);
      g_state.preview_win = NULL;
    }
}

/****************************************************************************
 * Name: pcfiles_copy_cb / pcfiles_cut_cb / pcfiles_paste_cb /
 *       pcfiles_delete_cb / pcfiles_mkdir_cb / pcfiles_switch_root_cb
 *
 * Description:
 *   Button callbacks for file operations.
 *
 ****************************************************************************/

static void pcfiles_copy_cb(lv_event_t *e)
{
  (void)e;

  if (g_state.selected < 0 ||
      g_state.selected >= g_state.entry_count)
    {
      return;
    }

  pcfiles_entry_t *entry = &g_state.entries[g_state.selected];
  if (strcmp(entry->name, "..") == 0) return;

  snprintf(g_state.clipboard_path, PCFILES_PATH_MAX,
           "%s/%s", g_state.cwd, entry->name);
  g_state.clipboard_cut   = false;
  g_state.clipboard_valid = true;

  lv_label_set_text_fmt(g_state.status_label,
                        "Copied: %s", entry->name);
}

static void pcfiles_cut_cb(lv_event_t *e)
{
  (void)e;

  if (g_state.selected < 0 ||
      g_state.selected >= g_state.entry_count)
    {
      return;
    }

  pcfiles_entry_t *entry = &g_state.entries[g_state.selected];
  if (strcmp(entry->name, "..") == 0) return;

  snprintf(g_state.clipboard_path, PCFILES_PATH_MAX,
           "%s/%s", g_state.cwd, entry->name);
  g_state.clipboard_cut   = true;
  g_state.clipboard_valid = true;

  lv_label_set_text_fmt(g_state.status_label,
                        "Cut: %s", entry->name);
}

static void pcfiles_paste_cb(lv_event_t *e)
{
  (void)e;

  if (!g_state.clipboard_valid)
    {
      return;
    }

  /* Extract filename from clipboard path */

  const char *src_name = strrchr(g_state.clipboard_path, '/');
  if (src_name == NULL) return;
  src_name++;  /* Skip slash */

  char dst_path[PCFILES_PATH_MAX];
  snprintf(dst_path, sizeof(dst_path), "%s/%s",
           g_state.cwd, src_name);

  int ret;
  if (g_state.clipboard_cut)
    {
      ret = pcfiles_move(g_state.clipboard_path, dst_path);
      if (ret == 0)
        {
          g_state.clipboard_valid = false;
        }
    }
  else
    {
      ret = pcfiles_copy(g_state.clipboard_path, dst_path);
    }

  if (ret == 0)
    {
      lv_label_set_text_fmt(g_state.status_label,
                            "%s: %s",
                            g_state.clipboard_cut ? "Moved" : "Pasted",
                            src_name);
    }
  else
    {
      lv_label_set_text_fmt(g_state.status_label,
                            "Error: %d", ret);
    }

  pcfiles_scan_dir();
  pcfiles_refresh_ui();
}

static void pcfiles_delete_cb(lv_event_t *e)
{
  (void)e;

  if (g_state.selected < 0 ||
      g_state.selected >= g_state.entry_count)
    {
      return;
    }

  pcfiles_entry_t *entry = &g_state.entries[g_state.selected];
  if (strcmp(entry->name, "..") == 0) return;

  char fullpath[PCFILES_PATH_MAX];
  snprintf(fullpath, sizeof(fullpath), "%s/%s",
           g_state.cwd, entry->name);

  int ret = pcfiles_delete(fullpath);

  if (ret == 0)
    {
      lv_label_set_text_fmt(g_state.status_label,
                            "Deleted: %s", entry->name);
    }
  else
    {
      lv_label_set_text_fmt(g_state.status_label,
                            "Delete failed: %d", ret);
    }

  if (g_state.selected >= g_state.entry_count - 1)
    {
      g_state.selected = g_state.entry_count - 2;
      if (g_state.selected < 0) g_state.selected = 0;
    }

  pcfiles_scan_dir();
  pcfiles_refresh_ui();
}

static void pcfiles_mkdir_cb(lv_event_t *e)
{
  (void)e;

  /* Create a "New Folder" directory with incrementing number */

  char new_dir[PCFILES_PATH_MAX];
  int  n = 1;

  do
    {
      if (n == 1)
        {
          snprintf(new_dir, sizeof(new_dir), "%s/New Folder",
                   g_state.cwd);
        }
      else
        {
          snprintf(new_dir, sizeof(new_dir), "%s/New Folder %d",
                   g_state.cwd, n);
        }
      n++;
    }
  while (n < 100 && access(new_dir, F_OK) == 0);

  int ret = pcfiles_mkdir(new_dir);

  if (ret == 0)
    {
      lv_label_set_text(g_state.status_label, "Folder created");
    }
  else
    {
      lv_label_set_text_fmt(g_state.status_label,
                            "mkdir failed: %d", ret);
    }

  pcfiles_scan_dir();
  pcfiles_refresh_ui();
}

static void pcfiles_switch_root_cb(lv_event_t *e)
{
  (void)e;

  /* Toggle between SD card and flash root */

  if (strncmp(g_state.cwd, PCFILES_SD_ROOT,
              strlen(PCFILES_SD_ROOT)) == 0)
    {
      pcfiles_navigate(PCFILES_FLASH_ROOT);
    }
  else
    {
      pcfiles_navigate(PCFILES_SD_ROOT);
    }
}

static void pcfiles_toggle_hidden_cb(lv_event_t *e)
{
  (void)e;

  g_state.show_hidden = !g_state.show_hidden;
  pcfiles_scan_dir();
  pcfiles_refresh_ui();
}

/****************************************************************************
 * Name: pcfiles_handle_key
 *
 * Description:
 *   Keyboard handler for navigation.
 *
 ****************************************************************************/

static void pcfiles_handle_key(lv_event_t *e)
{
  uint32_t key = lv_event_get_key(e);

  /* Close preview window if open */

  if (g_state.preview_win != NULL)
    {
      if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
        {
          lv_obj_delete(g_state.preview_win);
          g_state.preview_win = NULL;
        }
      return;
    }

  int old_sel = g_state.selected;

  switch (key)
    {
      case LV_KEY_UP:
        if (g_state.selected > 0)
          {
            g_state.selected--;
          }
        break;

      case LV_KEY_DOWN:
        if (g_state.selected < g_state.entry_count - 1)
          {
            g_state.selected++;
          }
        break;

      case LV_KEY_ENTER:
        pcfiles_enter_selected();
        return;  /* UI already refreshed */

      case LV_KEY_BACKSPACE:
      case LV_KEY_ESC:
        pcfiles_navigate_parent();
        return;

      case 'c':  /* Copy shortcut */
        pcfiles_copy_cb(NULL);
        return;

      case 'x':  /* Cut shortcut */
        pcfiles_cut_cb(NULL);
        return;

      case 'v':  /* Paste shortcut */
        pcfiles_paste_cb(NULL);
        return;

      case 'd':  /* Delete shortcut */
        pcfiles_delete_cb(NULL);
        return;

      case 'h':  /* Toggle hidden */
        pcfiles_toggle_hidden_cb(NULL);
        return;

      default:
        return;
    }

  if (old_sel != g_state.selected)
    {
      pcfiles_refresh_ui();
    }
}

/****************************************************************************
 * Name: pcfiles_create_ui
 *
 * Description:
 *   Build the file explorer LVGL UI.
 *
 ****************************************************************************/

static void pcfiles_create_ui(void)
{
  g_state.screen = pc_app_get_screen();

  /* Path bar (top) */

  g_state.path_label = lv_label_create(g_state.screen);
  lv_label_set_text(g_state.path_label, g_state.cwd);
  lv_obj_set_style_text_font(g_state.path_label,
                             &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(g_state.path_label,
                              lv_palette_main(LV_PALETTE_CYAN), 0);
  lv_obj_set_style_bg_color(g_state.path_label,
                            lv_color_hex(0x0A0A1E), 0);
  lv_obj_set_style_bg_opa(g_state.path_label, LV_OPA_COVER, 0);
  lv_obj_set_size(g_state.path_label, 320, 20);
  lv_obj_set_style_pad_left(g_state.path_label, 4, 0);
  lv_obj_align(g_state.path_label, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_label_set_long_mode(g_state.path_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

  /* File list (middle) */

  g_state.file_list = lv_list_create(g_state.screen);
  lv_obj_set_size(g_state.file_list, 320, 210);
  lv_obj_set_pos(g_state.file_list, 0, 20);
  lv_obj_set_style_bg_color(g_state.file_list, lv_color_black(), 0);
  lv_obj_set_style_border_width(g_state.file_list, 0, 0);
  lv_obj_set_style_radius(g_state.file_list, 0, 0);
  lv_obj_set_style_pad_all(g_state.file_list, 2, 0);

  /* Status bar (below file list) */

  g_state.status_label = lv_label_create(g_state.screen);
  lv_label_set_text(g_state.status_label, "");
  lv_obj_set_style_text_font(g_state.status_label,
                             &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(g_state.status_label,
                              lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_size(g_state.status_label, 320, 18);
  lv_obj_set_pos(g_state.status_label, 4, 232);

  /* Button bar (bottom) */

  g_state.btn_bar = lv_obj_create(g_state.screen);
  lv_obj_set_size(g_state.btn_bar, 320, 48);
  lv_obj_set_pos(g_state.btn_bar, 0, 252);
  lv_obj_set_style_bg_color(g_state.btn_bar,
                            lv_color_hex(0x0A0A1E), 0);
  lv_obj_set_style_bg_opa(g_state.btn_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(g_state.btn_bar, 2, 0);
  lv_obj_set_style_border_width(g_state.btn_bar, 0, 0);
  lv_obj_clear_flag(g_state.btn_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(g_state.btn_bar, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(g_state.btn_bar,
                        LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  /* Action buttons */

  static const struct {
    const char *label;
    lv_event_cb_t cb;
  } buttons[] = {
    { LV_SYMBOL_COPY   " Cp",    pcfiles_copy_cb },
    { LV_SYMBOL_CUT    " Cut",   pcfiles_cut_cb },
    { LV_SYMBOL_PASTE  " Paste", pcfiles_paste_cb },
    { LV_SYMBOL_TRASH  " Del",   pcfiles_delete_cb },
    { LV_SYMBOL_PLUS   " Dir",   pcfiles_mkdir_cb },
    { LV_SYMBOL_SD_CARD " Swap", pcfiles_switch_root_cb },
  };

  for (int i = 0; i < 6; i++)
    {
      lv_obj_t *btn = lv_button_create(g_state.btn_bar);
      lv_obj_set_size(btn, 50, 20);
      lv_obj_add_event_cb(btn, buttons[i].cb,
                          LV_EVENT_CLICKED, NULL);

      lv_obj_t *lbl = lv_label_create(btn);
      lv_label_set_text(lbl, buttons[i].label);
      lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
      lv_obj_center(lbl);
    }

  /* Register keyboard handler */

  lv_group_t *grp = lv_group_get_default();
  if (grp != NULL)
    {
      lv_group_add_obj(grp, g_state.file_list);
    }

  lv_obj_add_event_cb(g_state.screen, pcfiles_handle_key,
                      LV_EVENT_KEY, NULL);

  g_state.preview_win = NULL;
}

/****************************************************************************
 * Name: pcfiles_main
 *
 * Description:
 *   File explorer app entry point.
 *
 ****************************************************************************/

static int pcfiles_main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  syslog(LOG_INFO, "PCFILES: Starting file explorer\n");

  /* Initialize state */

  memset(&g_state, 0, sizeof(g_state));
  strncpy(g_state.cwd, PCFILES_SD_ROOT, PCFILES_PATH_MAX - 1);
  g_state.sort_mode   = SORT_NAME_ASC;
  g_state.show_hidden = false;

  /* Build UI */

  pcfiles_create_ui();

  /* Initial directory scan */

  pcfiles_scan_dir();
  pcfiles_refresh_ui();

  /* Run event loop — pump LVGL timers and check for Fn+ESC exit.
   * The user can also exit via Ctrl+Q which calls pc_app_exit().
   */

  extern bool lv_port_indev_exit_requested(void);

  while (1)
    {
      lv_timer_handler();

      if (lv_port_indev_exit_requested())
        {
          syslog(LOG_INFO, "PCFILES: Fn+ESC exit\n");
          pc_app_exit(0);
        }

      usleep(30000);  /* ~33fps */
    }

  /* Never reached — app exits via pc_app_exit() or pc_app_yield() */

  return 0;
}

/****************************************************************************
 * Public Data — App Registration
 ****************************************************************************/

const pc_app_t g_pcfiles_app =
{
  .info =
    {
      .name         = "pcfiles",
      .display_name = "Files",
      .version      = "1.0.0",
      .category     = "system",
      .min_ram      = 32768,      /* 32KB for entries + buffers */
      .flags        = PC_APP_FLAG_BUILTIN,
    },
  .main    = pcfiles_main,
  .save    = NULL,
  .restore = NULL,
};
