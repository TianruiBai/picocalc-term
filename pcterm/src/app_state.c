/****************************************************************************
 * pcterm/src/app_state.c
 *
 * App state save/restore implementation.
 * Uses PSRAM as fast cache and SD card for persistent storage.
 *
 * Save flow:
 *   1. Call app's save callback → serialized data in temp buffer
 *   2. Copy to PSRAM cache (fast restore on same session)
 *   3. Write to /mnt/sd/etc/appstate/<name>.state (persistence)
 *
 * Restore flow:
 *   1. Check PSRAM cache (fast)
 *   2. Fallback: read from SD card
 *   3. Call app's restore callback with data
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <syslog.h>

#include "pcterm/appstate.h"
#include "pcterm/app.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PSRAM_CACHE_SLOTS  8     /* Max apps with cached state */

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct state_cache_entry
{
  char    name[32];
  void   *data;
  size_t  size;
  bool    valid;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct state_cache_entry g_cache[PSRAM_CACHE_SLOTS];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static struct state_cache_entry *cache_find(const char *name)
{
  for (int i = 0; i < PSRAM_CACHE_SLOTS; i++)
    {
      if (g_cache[i].valid &&
          strcmp(g_cache[i].name, name) == 0)
        {
          return &g_cache[i];
        }
    }

  return NULL;
}

static struct state_cache_entry *cache_alloc(const char *name)
{
  /* Find existing or empty slot */

  for (int i = 0; i < PSRAM_CACHE_SLOTS; i++)
    {
      if (!g_cache[i].valid)
        {
          strncpy(g_cache[i].name, name, sizeof(g_cache[i].name) - 1);
          return &g_cache[i];
        }
    }

  /* Cache full — evict the first slot (LRU would be better) */

  if (g_cache[0].data != NULL)
    {
      free(g_cache[0].data);
    }

  memset(&g_cache[0], 0, sizeof(g_cache[0]));
  strncpy(g_cache[0].name, name, sizeof(g_cache[0].name) - 1);
  return &g_cache[0];
}

static void sd_path(const char *name, char *path, size_t pathlen)
{
  snprintf(path, pathlen, "%s/%s.state", APPSTATE_DIR, name);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int pc_appstate_save(const char *app_name,
                     int (*save_fn)(void *buf, size_t *len))
{
  /* Allocate temp buffer for serialization */

  size_t len = APPSTATE_MAX_SIZE;
  void *buf = malloc(len);

  if (buf == NULL)
    {
      syslog(LOG_ERR, "APPSTATE: Cannot allocate save buffer\n");
      return PC_ERR_NOMEM;
    }

  /* Call app's serializer */

  int ret = save_fn(buf, &len);
  if (ret != PC_OK)
    {
      free(buf);
      return ret;
    }

  syslog(LOG_INFO, "APPSTATE: Saved %zu bytes for \"%s\"\n",
         len, app_name);

  /* Store in PSRAM cache */

  struct state_cache_entry *entry = cache_find(app_name);
  if (entry == NULL)
    {
      entry = cache_alloc(app_name);
    }
  else if (entry->data != NULL)
    {
      free(entry->data);
    }

  /* Shrink buffer to actual size */

  entry->data = realloc(buf, len);
  if (entry->data == NULL)
    {
      entry->data = buf;  /* realloc failed, keep original */
    }

  entry->size = len;
  entry->valid = true;

  /* Write to SD card for persistence */

  char path[128];
  sd_path(app_name, path, sizeof(path));

  FILE *f = fopen(path, "wb");
  if (f != NULL)
    {
      fwrite(entry->data, 1, entry->size, f);
      fclose(f);
      syslog(LOG_INFO, "APPSTATE: Written to %s\n", path);
    }
  else
    {
      syslog(LOG_WARNING, "APPSTATE: Could not write %s (SD error)\n",
             path);
    }

  return PC_OK;
}

int pc_appstate_restore(const char *app_name,
                        int (*restore_fn)(const void *buf, size_t len))
{
  /* Try PSRAM cache first (fast) */

  struct state_cache_entry *entry = cache_find(app_name);
  if (entry != NULL && entry->data != NULL)
    {
      syslog(LOG_INFO, "APPSTATE: Restoring from PSRAM cache (%zu bytes)\n",
             entry->size);
      return restore_fn(entry->data, entry->size);
    }

  /* Fallback: read from SD card */

  char path[128];
  sd_path(app_name, path, sizeof(path));

  FILE *f = fopen(path, "rb");
  if (f == NULL)
    {
      return PC_ERR_NOENT;
    }

  /* Get file size */

  fseek(f, 0, SEEK_END);
  size_t len = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (len > APPSTATE_MAX_SIZE)
    {
      fclose(f);
      return PC_ERR_TOOBIG;
    }

  void *buf = malloc(len);
  if (buf == NULL)
    {
      fclose(f);
      return PC_ERR_NOMEM;
    }

  fread(buf, 1, len, f);
  fclose(f);

  syslog(LOG_INFO, "APPSTATE: Restoring from SD (%zu bytes)\n", len);

  /* Cache it for next time */

  entry = cache_alloc(app_name);
  entry->data = buf;
  entry->size = len;
  entry->valid = true;

  return restore_fn(buf, len);
}

void pc_appstate_discard(const char *app_name)
{
  /* Remove from PSRAM cache */

  struct state_cache_entry *entry = cache_find(app_name);
  if (entry != NULL)
    {
      if (entry->data != NULL)
        {
          free(entry->data);
        }
      memset(entry, 0, sizeof(*entry));
    }

  /* Remove from SD card */

  char path[128];
  sd_path(app_name, path, sizeof(path));
  unlink(path);

  syslog(LOG_INFO, "APPSTATE: Discarded state for \"%s\"\n", app_name);
}

bool pc_appstate_exists(const char *app_name)
{
  /* Check PSRAM cache */

  if (cache_find(app_name) != NULL)
    {
      return true;
    }

  /* Check SD card */

  char path[128];
  sd_path(app_name, path, sizeof(path));

  struct stat st;
  return (stat(path, &st) == 0);
}

size_t pc_appstate_size(const char *app_name)
{
  struct state_cache_entry *entry = cache_find(app_name);
  if (entry != NULL)
    {
      return entry->size;
    }

  char path[128];
  sd_path(app_name, path, sizeof(path));

  struct stat st;
  if (stat(path, &st) == 0)
    {
      return st.st_size;
    }

  return 0;
}
