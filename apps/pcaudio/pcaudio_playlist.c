/****************************************************************************
 * apps/pcaudio/pcaudio_playlist.c
 *
 * Playlist management: directory scan, m3u parsing, shuffle, repeat.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <syslog.h>

#include "pcterm/app.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PLAYLIST_MAX_ENTRIES   256
#define MUSIC_DIR              "/mnt/sd/music"

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef struct playlist_s
{
  char    **paths;        /* Array of file paths */
  int       count;
  int       current;      /* Currently playing index */
  bool      shuffle;
  bool      repeat;
} playlist_t;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool is_audio_file(const char *name)
{
  const char *ext = strrchr(name, '.');
  if (ext == NULL) return false;
  return (strcasecmp(ext, ".mp3") == 0 || strcasecmp(ext, ".wav") == 0);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

playlist_t *playlist_create(void)
{
  playlist_t *pl = (playlist_t *)pc_app_psram_alloc(sizeof(playlist_t));
  if (pl == NULL) return NULL;

  memset(pl, 0, sizeof(playlist_t));

  pl->paths = (char **)pc_app_psram_alloc(
    sizeof(char *) * PLAYLIST_MAX_ENTRIES);
  if (pl->paths == NULL)
    {
      pc_app_psram_free(pl);
      return NULL;
    }

  memset(pl->paths, 0, sizeof(char *) * PLAYLIST_MAX_ENTRIES);
  return pl;
}

void playlist_destroy(playlist_t *pl)
{
  if (pl == NULL) return;

  for (int i = 0; i < pl->count; i++)
    {
      if (pl->paths[i]) pc_app_psram_free(pl->paths[i]);
    }

  pc_app_psram_free(pl->paths);
  pc_app_psram_free(pl);
}

/**
 * Scan a directory for audio files and populate the playlist.
 */
int playlist_scan_dir(playlist_t *pl, const char *dir_path)
{
  DIR           *dir;
  struct dirent *entry;

  dir = opendir(dir_path ? dir_path : MUSIC_DIR);
  if (dir == NULL)
    {
      syslog(LOG_WARNING, "AUDIO: Cannot open music directory\n");
      return -1;
    }

  while ((entry = readdir(dir)) != NULL && pl->count < PLAYLIST_MAX_ENTRIES)
    {
      if (!is_audio_file(entry->d_name))
        {
          continue;
        }

      char fullpath[256];
      snprintf(fullpath, sizeof(fullpath), "%s/%s",
               dir_path ? dir_path : MUSIC_DIR, entry->d_name);

      size_t plen = strlen(fullpath);
      char *path = (char *)pc_app_psram_alloc(plen + 1);

      if (path)
        {
          memcpy(path, fullpath, plen + 1);
          pl->paths[pl->count++] = path;
        }
    }

  closedir(dir);

  syslog(LOG_INFO, "AUDIO: Playlist: %d tracks from %s\n",
         pl->count, dir_path ? dir_path : MUSIC_DIR);
  return pl->count;
}

/**
 * Parse an m3u playlist file.
 */
int playlist_load_m3u(playlist_t *pl, const char *m3u_path)
{
  FILE *f = fopen(m3u_path, "r");
  if (f == NULL) return -1;

  char line[256];

  while (fgets(line, sizeof(line), f) != NULL &&
         pl->count < PLAYLIST_MAX_ENTRIES)
    {
      /* Strip trailing newline */

      size_t len = strlen(line);
      while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        {
          line[--len] = '\0';
        }

      /* Skip comments and empty lines */

      if (len == 0 || line[0] == '#')
        {
          continue;
        }

      char *path = (char *)pc_app_psram_alloc(len + 1);
      if (path)
        {
          memcpy(path, line, len + 1);
          pl->paths[pl->count++] = path;
        }
    }

  fclose(f);
  syslog(LOG_INFO, "AUDIO: Loaded %d tracks from %s\n",
         pl->count, m3u_path);
  return pl->count;
}

const char *playlist_current(const playlist_t *pl)
{
  if (pl == NULL || pl->count == 0) return NULL;
  return pl->paths[pl->current];
}

const char *playlist_next(playlist_t *pl)
{
  if (pl == NULL || pl->count == 0) return NULL;

  pl->current++;
  if (pl->current >= pl->count)
    {
      pl->current = pl->repeat ? 0 : pl->count - 1;
      if (!pl->repeat) return NULL;
    }

  return pl->paths[pl->current];
}

const char *playlist_prev(playlist_t *pl)
{
  if (pl == NULL || pl->count == 0) return NULL;

  pl->current--;
  if (pl->current < 0)
    {
      pl->current = pl->repeat ? pl->count - 1 : 0;
    }

  return pl->paths[pl->current];
}
