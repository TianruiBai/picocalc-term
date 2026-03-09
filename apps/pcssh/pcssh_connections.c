/****************************************************************************
 * apps/pcssh/pcssh_connections.c
 *
 * Saved SSH connections manager.
 * Stores connection profiles in /mnt/sd/ssh/connections.json.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <cJSON.h>
#include "pcterm/app.h"
#include "pcssh.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CONNECTIONS_FILE  "/mnt/sd/ssh/connections.json"
#define MAX_CONNECTIONS   16

/****************************************************************************
 * Private Types (struct now defined in pcssh.h)
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

static ssh_connection_t g_connections[MAX_CONNECTIONS];
static int              g_count = 0;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * Load saved connections from JSON file.
 */
int ssh_connections_load(void)
{
  FILE *f = fopen(CONNECTIONS_FILE, "r");
  if (f == NULL)
    {
      g_count = 0;
      return 0;
    }

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *json = (char *)malloc(fsize + 1);
  if (json == NULL)
    {
      fclose(f);
      return -1;
    }

  fread(json, 1, fsize, f);
  json[fsize] = '\0';
  fclose(f);

  cJSON *root = cJSON_Parse(json);
  free(json);

  if (root == NULL) return -1;

  cJSON *arr = cJSON_GetObjectItem(root, "connections");
  if (!cJSON_IsArray(arr))
    {
      cJSON_Delete(root);
      return 0;
    }

  g_count = 0;
  cJSON *item;
  cJSON_ArrayForEach(item, arr)
    {
      if (g_count >= MAX_CONNECTIONS) break;

      ssh_connection_t *c = &g_connections[g_count];

      cJSON *name = cJSON_GetObjectItem(item, "name");
      cJSON *host = cJSON_GetObjectItem(item, "host");
      cJSON *port = cJSON_GetObjectItem(item, "port");
      cJSON *user = cJSON_GetObjectItem(item, "username");

      if (cJSON_IsString(name))
        strncpy(c->name, name->valuestring, 31);
      if (cJSON_IsString(host))
        strncpy(c->host, host->valuestring, 127);
      if (cJSON_IsNumber(port))
        c->port = (uint16_t)port->valuedouble;
      else
        c->port = 22;
      if (cJSON_IsString(user))
        strncpy(c->username, user->valuestring, 63);

      g_count++;
    }

  cJSON_Delete(root);

  syslog(LOG_INFO, "SSH: Loaded %d saved connections\n", g_count);
  return g_count;
}

/**
 * Save connections to JSON file.
 */
int ssh_connections_save(void)
{
  cJSON *root = cJSON_CreateObject();
  cJSON *arr  = cJSON_CreateArray();

  for (int i = 0; i < g_count; i++)
    {
      cJSON *item = cJSON_CreateObject();
      cJSON_AddStringToObject(item, "name", g_connections[i].name);
      cJSON_AddStringToObject(item, "host", g_connections[i].host);
      cJSON_AddNumberToObject(item, "port", g_connections[i].port);
      cJSON_AddStringToObject(item, "username", g_connections[i].username);
      cJSON_AddItemToArray(arr, item);
    }

  cJSON_AddItemToObject(root, "connections", arr);

  char *json = cJSON_Print(root);
  cJSON_Delete(root);

  if (json == NULL) return -1;

  FILE *f = fopen(CONNECTIONS_FILE, "w");
  if (f == NULL)
    {
      free(json);
      return -1;
    }

  fputs(json, f);
  fclose(f);
  free(json);

  return 0;
}

/**
 * Add a new connection.
 */
int ssh_connections_add(const ssh_connection_t *conn)
{
  if (g_count >= MAX_CONNECTIONS) return -1;

  memcpy(&g_connections[g_count], conn, sizeof(ssh_connection_t));
  g_count++;

  ssh_connections_save();
  return 0;
}

/**
 * Remove a connection by index.
 */
int ssh_connections_remove(int index)
{
  if (index < 0 || index >= g_count) return -1;

  for (int i = index; i < g_count - 1; i++)
    {
      memcpy(&g_connections[i], &g_connections[i + 1],
             sizeof(ssh_connection_t));
    }

  g_count--;
  ssh_connections_save();
  return 0;
}

/**
 * Get a single connection by index.
 */
int ssh_connections_get(int index, ssh_connection_t *conn)
{
  if (index < 0 || index >= g_count || conn == NULL) return -1;
  memcpy(conn, &g_connections[index], sizeof(ssh_connection_t));
  return 0;
}

int ssh_connections_count(void)
{
  return g_count;
}
