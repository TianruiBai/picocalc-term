/****************************************************************************
 * apps/pcweb/pcweb_nav.c
 *
 * Navigation: URL history (back/forward), bookmarks management.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <cJSON.h>
#include "pcterm/app.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define HISTORY_MAX        32
#define BOOKMARKS_FILE     "/flash/home/picocalc/.bookmarks.json"
#define BOOKMARKS_MAX      32

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef struct bookmark_s
{
  char title[64];
  char url[256];
} bookmark_t;

typedef struct web_nav_s
{
  /* History (circular buffer) */

  char      *history[HISTORY_MAX];
  int        history_pos;     /* Current position */
  int        history_count;   /* Total entries */
  int        history_back;    /* Entries we can go back */

  /* Bookmarks */

  bookmark_t bookmarks[BOOKMARKS_MAX];
  int        bookmark_count;
} web_nav_t;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

web_nav_t *web_nav_create(void)
{
  web_nav_t *nav;

  nav = (web_nav_t *)pc_app_psram_alloc(sizeof(web_nav_t));
  if (nav == NULL) return NULL;

  memset(nav, 0, sizeof(web_nav_t));
  return nav;
}

void web_nav_destroy(web_nav_t *nav)
{
  if (nav == NULL) return;

  for (int i = 0; i < HISTORY_MAX; i++)
    {
      if (nav->history[i]) pc_app_psram_free(nav->history[i]);
    }

  pc_app_psram_free(nav);
}

/**
 * Navigate to a new URL (adds to history).
 */
void web_nav_goto(web_nav_t *nav, const char *url)
{
  /* Clear forward history */

  int next = (nav->history_pos + 1) % HISTORY_MAX;
  while (next != (nav->history_pos + nav->history_count) % HISTORY_MAX)
    {
      if (nav->history[next])
        {
          pc_app_psram_free(nav->history[next]);
          nav->history[next] = NULL;
        }
      next = (next + 1) % HISTORY_MAX;
    }

  /* Add new entry */

  nav->history_pos = (nav->history_pos + 1) % HISTORY_MAX;

  if (nav->history[nav->history_pos])
    {
      pc_app_psram_free(nav->history[nav->history_pos]);
    }

  size_t ulen = strlen(url);
  nav->history[nav->history_pos] = (char *)pc_app_psram_alloc(ulen + 1);
  if (nav->history[nav->history_pos])
    {
      memcpy(nav->history[nav->history_pos], url, ulen + 1);
    }

  if (nav->history_count < HISTORY_MAX) nav->history_count++;
  nav->history_back = 0;
}

/**
 * Go back in history.
 * @return URL to navigate to, or NULL if at beginning
 */
const char *web_nav_back(web_nav_t *nav)
{
  if (nav->history_back >= nav->history_count - 1) return NULL;

  nav->history_back++;
  int idx = (nav->history_pos - nav->history_back + HISTORY_MAX)
            % HISTORY_MAX;
  return nav->history[idx];
}

/**
 * Go forward in history.
 */
const char *web_nav_forward(web_nav_t *nav)
{
  if (nav->history_back <= 0) return NULL;

  nav->history_back--;
  int idx = (nav->history_pos - nav->history_back + HISTORY_MAX)
            % HISTORY_MAX;
  return nav->history[idx];
}

/**
 * Get current URL.
 */
const char *web_nav_current(web_nav_t *nav)
{
  int idx = (nav->history_pos - nav->history_back + HISTORY_MAX)
            % HISTORY_MAX;
  return nav->history[idx];
}

/**
 * Load bookmarks from JSON file.
 */
int web_nav_load_bookmarks(web_nav_t *nav)
{
  FILE *f = fopen(BOOKMARKS_FILE, "r");
  if (f == NULL) return 0;

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *json = (char *)malloc(fsize + 1);
  if (json == NULL) { fclose(f); return -1; }

  fread(json, 1, fsize, f);
  json[fsize] = '\0';
  fclose(f);

  cJSON *root = cJSON_Parse(json);
  free(json);
  if (root == NULL) return -1;

  cJSON *arr = cJSON_GetObjectItem(root, "bookmarks");
  if (!cJSON_IsArray(arr)) { cJSON_Delete(root); return 0; }

  nav->bookmark_count = 0;
  cJSON *item;
  cJSON_ArrayForEach(item, arr)
    {
      if (nav->bookmark_count >= BOOKMARKS_MAX) break;

      cJSON *title = cJSON_GetObjectItem(item, "title");
      cJSON *url   = cJSON_GetObjectItem(item, "url");

      bookmark_t *b = &nav->bookmarks[nav->bookmark_count];

      if (cJSON_IsString(title))
        strncpy(b->title, title->valuestring, 63);
      if (cJSON_IsString(url))
        strncpy(b->url, url->valuestring, 255);

      nav->bookmark_count++;
    }

  cJSON_Delete(root);
  syslog(LOG_INFO, "PCWEB: Loaded %d bookmarks\n", nav->bookmark_count);
  return nav->bookmark_count;
}

/**
 * Save bookmarks to JSON file.
 */
int web_nav_save_bookmarks(web_nav_t *nav)
{
  cJSON *root = cJSON_CreateObject();
  cJSON *arr  = cJSON_CreateArray();

  for (int i = 0; i < nav->bookmark_count; i++)
    {
      cJSON *item = cJSON_CreateObject();
      cJSON_AddStringToObject(item, "title", nav->bookmarks[i].title);
      cJSON_AddStringToObject(item, "url", nav->bookmarks[i].url);
      cJSON_AddItemToArray(arr, item);
    }

  cJSON_AddItemToObject(root, "bookmarks", arr);

  char *json = cJSON_Print(root);
  cJSON_Delete(root);
  if (json == NULL) return -1;

  FILE *f = fopen(BOOKMARKS_FILE, "w");
  if (f == NULL) { free(json); return -1; }

  fputs(json, f);
  fclose(f);
  free(json);
  return 0;
}

/**
 * Add current page as a bookmark.
 */
int web_nav_add_bookmark(web_nav_t *nav, const char *title, const char *url)
{
  if (nav->bookmark_count >= BOOKMARKS_MAX) return -1;

  bookmark_t *b = &nav->bookmarks[nav->bookmark_count];
  strncpy(b->title, title, 63);
  strncpy(b->url, url, 255);
  nav->bookmark_count++;

  web_nav_save_bookmarks(nav);
  return 0;
}
