/****************************************************************************
 * pcterm/src/package_manager.c
 *
 * Package manager for .pcpkg third-party app packages.
 * Handles install, uninstall, registry management, and ELF loading.
 *
 * Package format (.pcpkg):
 *   PCPK magic (4 bytes)
 *   Version    (4 bytes, uint32)
 *   File count (4 bytes, uint32)
 *   Reserved   (4 bytes)
 *   File table: [256-byte name | 4-byte offset | 4-byte size] × count
 *   File data:  concatenated file contents
 *
 * Registry: /mnt/sd/apps/registry.json
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <syslog.h>
#include <fcntl.h>

#include <cJSON.h>

#ifdef CONFIG_ELF
#include <nuttx/binfmt/binfmt.h>
#endif

#include "pcterm/package.h"
#include "pcterm/app.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PCPKG_HEADER_SIZE    16
#define PCPKG_ENTRY_SIZE     264   /* 256 name + 4 offset + 4 size */
#define READ_BUF_SIZE        512

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* File table entry in .pcpkg */

typedef struct pcpkg_file_entry_s
{
  char     name[256];
  uint32_t offset;
  uint32_t size;
} pcpkg_file_entry_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static pcpkg_entry_t  g_installed[64];
static int            g_installed_count = 0;
static bool           g_initialized = false;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pcpkg_load_registry
 *
 * Description:
 *   Load the package registry from /mnt/sd/apps/registry.json.
 *
 ****************************************************************************/

static int pcpkg_load_registry(void)
{
  FILE *f;
  long  fsize;
  char *json_str;

  f = fopen(PCPKG_REGISTRY_PATH, "r");
  if (f == NULL)
    {
      syslog(LOG_WARNING, "pkg: No registry found, starting fresh\n");
      g_installed_count = 0;
      return 0;
    }

  fseek(f, 0, SEEK_END);
  fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (fsize <= 0 || fsize > 65536)
    {
      fclose(f);
      return -EINVAL;
    }

  json_str = (char *)malloc(fsize + 1);
  if (json_str == NULL)
    {
      fclose(f);
      return -ENOMEM;
    }

  fread(json_str, 1, fsize, f);
  json_str[fsize] = '\0';
  fclose(f);

  /* Parse JSON */

  cJSON *root = cJSON_Parse(json_str);
  free(json_str);

  if (root == NULL)
    {
      syslog(LOG_ERR, "pkg: Invalid registry JSON\n");
      return -EINVAL;
    }

  cJSON *packages = cJSON_GetObjectItem(root, "packages");
  if (!cJSON_IsArray(packages))
    {
      cJSON_Delete(root);
      g_installed_count = 0;
      return 0;
    }

  g_installed_count = 0;

  cJSON *pkg;
  cJSON_ArrayForEach(pkg, packages)
    {
      if (g_installed_count >= 64)
        {
          break;
        }

      cJSON *name    = cJSON_GetObjectItem(pkg, "name");
      cJSON *version = cJSON_GetObjectItem(pkg, "version");
      cJSON *path    = cJSON_GetObjectItem(pkg, "install_path");
      cJSON *size    = cJSON_GetObjectItem(pkg, "installed_size");
      cJSON *ts      = cJSON_GetObjectItem(pkg, "install_timestamp");

      pcpkg_entry_t *entry = &g_installed[g_installed_count];

      if (cJSON_IsString(name))
        {
          strncpy(entry->name, name->valuestring, PCPKG_NAME_MAX - 1);
        }
      if (cJSON_IsString(version))
        {
          strncpy(entry->version, version->valuestring,
                  PCPKG_VERSION_MAX - 1);
        }
      if (cJSON_IsString(path))
        {
          strncpy(entry->install_path, path->valuestring, 127);
        }
      if (cJSON_IsNumber(size))
        {
          entry->installed_size = (uint32_t)size->valuedouble;
        }
      if (cJSON_IsNumber(ts))
        {
          entry->install_timestamp = (uint32_t)ts->valuedouble;
        }

      g_installed_count++;
    }

  cJSON_Delete(root);

  syslog(LOG_INFO, "pkg: Loaded registry with %d packages\n",
         g_installed_count);
  return 0;
}

/****************************************************************************
 * Name: pcpkg_save_registry
 *
 * Description:
 *   Save the package registry to /mnt/sd/apps/registry.json.
 *
 ****************************************************************************/

static int pcpkg_save_registry(void)
{
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "version", 1);

  cJSON *packages = cJSON_CreateArray();

  for (int i = 0; i < g_installed_count; i++)
    {
      cJSON *pkg = cJSON_CreateObject();
      cJSON_AddStringToObject(pkg, "name", g_installed[i].name);
      cJSON_AddStringToObject(pkg, "version", g_installed[i].version);
      cJSON_AddStringToObject(pkg, "install_path",
                              g_installed[i].install_path);
      cJSON_AddNumberToObject(pkg, "installed_size",
                              g_installed[i].installed_size);
      cJSON_AddNumberToObject(pkg, "install_timestamp",
                              g_installed[i].install_timestamp);
      cJSON_AddItemToArray(packages, pkg);
    }

  cJSON_AddItemToObject(root, "packages", packages);

  char *json_str = cJSON_Print(root);
  cJSON_Delete(root);

  if (json_str == NULL)
    {
      return -ENOMEM;
    }

  FILE *f = fopen(PCPKG_REGISTRY_PATH, "w");
  if (f == NULL)
    {
      free(json_str);
      return -EIO;
    }

  fputs(json_str, f);
  fclose(f);
  free(json_str);

  syslog(LOG_INFO, "pkg: Registry saved (%d packages)\n",
         g_installed_count);
  return 0;
}

/****************************************************************************
 * Name: pcpkg_mkdir_p
 *
 * Description:
 *   Create a directory (and parents) like mkdir -p.
 *
 ****************************************************************************/

static int pcpkg_mkdir_p(const char *path)
{
  char tmp[256];
  char *p = NULL;
  size_t len;

  strncpy(tmp, path, sizeof(tmp) - 1);
  len = strlen(tmp);

  if (tmp[len - 1] == '/')
    {
      tmp[len - 1] = '\0';
    }

  for (p = tmp + 1; *p; p++)
    {
      if (*p == '/')
        {
          *p = '\0';
          mkdir(tmp, 0755);
          *p = '/';
        }
    }

  mkdir(tmp, 0755);
  return 0;
}

/****************************************************************************
 * Name: pcpkg_rmdir_recursive
 *
 * Description:
 *   Recursively remove a directory and all its contents.
 *
 ****************************************************************************/

static int pcpkg_rmdir_recursive(const char *path)
{
  DIR           *dir;
  struct dirent *entry;
  char           fullpath[256];

  dir = opendir(path);
  if (dir == NULL)
    {
      return unlink(path);  /* May be a file */
    }

  while ((entry = readdir(dir)) != NULL)
    {
      if (strcmp(entry->d_name, ".") == 0 ||
          strcmp(entry->d_name, "..") == 0)
        {
          continue;
        }

      snprintf(fullpath, sizeof(fullpath), "%s/%s",
               path, entry->d_name);

      struct stat st;
      if (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode))
        {
          pcpkg_rmdir_recursive(fullpath);
        }
      else
        {
          unlink(fullpath);
        }
    }

  closedir(dir);
  return rmdir(path);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pcpkg_init
 *
 * Description:
 *   Initialize the package manager and load the registry.
 *
 ****************************************************************************/

int pcpkg_init(void)
{
  int ret;

  /* Ensure /mnt/sd/apps directory exists */

  mkdir(PCPKG_APPS_DIR, 0755);

  ret = pcpkg_load_registry();
  if (ret < 0)
    {
      syslog(LOG_ERR, "pkg: Failed to load registry: %d\n", ret);
      return ret;
    }

  g_initialized = true;
  return 0;
}

/****************************************************************************
 * Name: pcpkg_install
 *
 * Description:
 *   Install a .pcpkg file. Extracts files to /mnt/sd/apps/<name>/
 *   and updates the registry.
 *
 ****************************************************************************/

int pcpkg_install(const char *pcpkg_path)
{
  uint8_t header[PCPKG_HEADER_SIZE];
  int     fd;
  int     ret = 0;

  if (!g_initialized)
    {
      return PC_ERR_GENERIC;
    }

  fd = open(pcpkg_path, O_RDONLY);
  if (fd < 0)
    {
      syslog(LOG_ERR, "pkg: Cannot open %s\n", pcpkg_path);
      return PC_ERR_NOENT;
    }

  /* Read and validate header */

  if (read(fd, header, PCPKG_HEADER_SIZE) != PCPKG_HEADER_SIZE)
    {
      close(fd);
      return PC_ERR_GENERIC;
    }

  if (memcmp(header, PCPKG_MAGIC, 4) != 0)
    {
      syslog(LOG_ERR, "pkg: Invalid magic in %s\n", pcpkg_path);
      close(fd);
      return PC_ERR_GENERIC;
    }

  uint32_t version;
  uint32_t file_count;

  memcpy(&version, &header[4], 4);
  memcpy(&file_count, &header[8], 4);

  if (file_count == 0 || file_count > 128)
    {
      syslog(LOG_ERR, "pkg: Invalid file count %lu\n",
             (unsigned long)file_count);
      close(fd);
      return PC_ERR_GENERIC;
    }

  /* Read file table */

  pcpkg_file_entry_t *entries;
  entries = (pcpkg_file_entry_t *)malloc(
    sizeof(pcpkg_file_entry_t) * file_count);
  if (entries == NULL)
    {
      close(fd);
      return PC_ERR_NOMEM;
    }

  for (uint32_t i = 0; i < file_count; i++)
    {
      uint8_t entry_buf[PCPKG_ENTRY_SIZE];

      if (read(fd, entry_buf, PCPKG_ENTRY_SIZE) != PCPKG_ENTRY_SIZE)
        {
          free(entries);
          close(fd);
          return PC_ERR_GENERIC;
        }

      memcpy(entries[i].name, entry_buf, 256);
      entries[i].name[255] = '\0';
      memcpy(&entries[i].offset, &entry_buf[256], 4);
      memcpy(&entries[i].size, &entry_buf[260], 4);
    }

  /* First file must be manifest.json — parse it to get the package name */

  pcpkg_manifest_t manifest;
  memset(&manifest, 0, sizeof(manifest));

  /* Read manifest from the .pcpkg to a temp location, then parse */

  char install_dir[128];
  bool manifest_found = false;

  for (uint32_t i = 0; i < file_count; i++)
    {
      if (strcmp(entries[i].name, "manifest.json") == 0)
        {
          /* Read manifest data */

          char *mdata = (char *)malloc(entries[i].size + 1);
          if (mdata == NULL)
            {
              free(entries);
              close(fd);
              return PC_ERR_NOMEM;
            }

          lseek(fd, entries[i].offset, SEEK_SET);
          read(fd, mdata, entries[i].size);
          mdata[entries[i].size] = '\0';

          /* Parse manifest JSON */

          cJSON *mroot = cJSON_Parse(mdata);
          free(mdata);

          if (mroot == NULL)
            {
              syslog(LOG_ERR, "pkg: Invalid manifest.json\n");
              free(entries);
              close(fd);
              return PC_ERR_GENERIC;
            }

          cJSON *name_j = cJSON_GetObjectItem(mroot, "name");
          cJSON *ver_j  = cJSON_GetObjectItem(mroot, "version");
          cJSON *auth_j = cJSON_GetObjectItem(mroot, "author");
          cJSON *desc_j = cJSON_GetObjectItem(mroot, "description");
          cJSON *cat_j  = cJSON_GetObjectItem(mroot, "category");
          cJSON *ram_j  = cJSON_GetObjectItem(mroot, "min_ram");

          if (cJSON_IsString(name_j))
            {
              strncpy(manifest.name, name_j->valuestring,
                      PCPKG_NAME_MAX - 1);
            }
          if (cJSON_IsString(ver_j))
            {
              strncpy(manifest.version, ver_j->valuestring,
                      PCPKG_VERSION_MAX - 1);
            }
          if (cJSON_IsString(auth_j))
            {
              strncpy(manifest.author, auth_j->valuestring,
                      PCPKG_AUTHOR_MAX - 1);
            }
          if (cJSON_IsString(desc_j))
            {
              strncpy(manifest.description, desc_j->valuestring,
                      PCPKG_DESC_MAX - 1);
            }
          if (cJSON_IsString(cat_j))
            {
              strncpy(manifest.category, cat_j->valuestring, 15);
            }
          if (cJSON_IsNumber(ram_j))
            {
              manifest.min_ram = (uint32_t)ram_j->valuedouble;
            }

          cJSON_Delete(mroot);
          manifest_found = true;
          break;
        }
    }

  if (!manifest_found || manifest.name[0] == '\0')
    {
      syslog(LOG_ERR, "pkg: Package has no valid manifest\n");
      free(entries);
      close(fd);
      return PC_ERR_GENERIC;
    }

  /* Check if already installed — uninstall first */

  for (int i = 0; i < g_installed_count; i++)
    {
      if (strcmp(g_installed[i].name, manifest.name) == 0)
        {
          syslog(LOG_INFO, "pkg: Upgrading \"%s\"\n", manifest.name);
          pcpkg_uninstall(manifest.name);
          break;
        }
    }

  /* Create install directory */

  snprintf(install_dir, sizeof(install_dir), "%s/%s",
           PCPKG_APPS_DIR, manifest.name);
  pcpkg_mkdir_p(install_dir);

  /* Extract all files */

  uint32_t total_size = 0;
  uint8_t  *buf = (uint8_t *)malloc(READ_BUF_SIZE);

  if (buf == NULL)
    {
      free(entries);
      close(fd);
      return PC_ERR_NOMEM;
    }

  for (uint32_t i = 0; i < file_count; i++)
    {
      char filepath[256];
      snprintf(filepath, sizeof(filepath), "%s/%s",
               install_dir, entries[i].name);

      /* Create parent directories if needed */

      char *last_slash = strrchr(filepath, '/');
      if (last_slash != NULL)
        {
          *last_slash = '\0';
          pcpkg_mkdir_p(filepath);
          *last_slash = '/';
        }

      /* Extract file */

      int out_fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (out_fd < 0)
        {
          syslog(LOG_ERR, "pkg: Cannot create %s\n", filepath);
          continue;
        }

      lseek(fd, entries[i].offset, SEEK_SET);

      uint32_t remaining = entries[i].size;
      while (remaining > 0)
        {
          uint32_t chunk = (remaining > READ_BUF_SIZE)
                           ? READ_BUF_SIZE : remaining;
          ssize_t n = read(fd, buf, chunk);
          if (n <= 0) break;
          write(out_fd, buf, n);
          remaining -= n;
        }

      close(out_fd);
      total_size += entries[i].size;

      syslog(LOG_DEBUG, "pkg: Extracted %s (%lu bytes)\n",
             entries[i].name, (unsigned long)entries[i].size);
    }

  free(buf);
  free(entries);
  close(fd);

  /* Add to registry */

  if (g_installed_count < 64)
    {
      pcpkg_entry_t *entry = &g_installed[g_installed_count];
      strncpy(entry->name, manifest.name, PCPKG_NAME_MAX - 1);
      strncpy(entry->version, manifest.version, PCPKG_VERSION_MAX - 1);
      strncpy(entry->install_path, install_dir, 127);
      entry->installed_size = total_size;
      entry->install_timestamp = (uint32_t)time(NULL);
      g_installed_count++;
    }

  pcpkg_save_registry();

  syslog(LOG_INFO, "pkg: Installed \"%s\" v%s (%lu bytes)\n",
         manifest.name, manifest.version, (unsigned long)total_size);

  return PC_OK;
}

/****************************************************************************
 * Name: pcpkg_uninstall
 *
 * Description:
 *   Uninstall a package by name.
 *
 ****************************************************************************/

int pcpkg_uninstall(const char *name)
{
  int idx = -1;

  for (int i = 0; i < g_installed_count; i++)
    {
      if (strcmp(g_installed[i].name, name) == 0)
        {
          idx = i;
          break;
        }
    }

  if (idx < 0)
    {
      syslog(LOG_ERR, "pkg: \"%s\" is not installed\n", name);
      return PC_ERR_NOENT;
    }

  /* Remove install directory */

  pcpkg_rmdir_recursive(g_installed[idx].install_path);

  /* Remove from registry array */

  for (int i = idx; i < g_installed_count - 1; i++)
    {
      memcpy(&g_installed[i], &g_installed[i + 1],
             sizeof(pcpkg_entry_t));
    }

  g_installed_count--;
  pcpkg_save_registry();

  syslog(LOG_INFO, "pkg: Uninstalled \"%s\"\n", name);
  return PC_OK;
}

/****************************************************************************
 * Name: pcpkg_list
 *
 * Description:
 *   Get the list of installed packages.
 *
 ****************************************************************************/

int pcpkg_list(pcpkg_entry_t *entries, int max)
{
  int count = (g_installed_count < max) ? g_installed_count : max;

  memcpy(entries, g_installed, sizeof(pcpkg_entry_t) * count);

  return count;
}

/****************************************************************************
 * Name: pcpkg_count
 *
 * Description:
 *   Get the number of installed packages.
 *
 ****************************************************************************/

int pcpkg_count(void)
{
  return g_installed_count;
}

/****************************************************************************
 * Name: pcpkg_get_manifest
 *
 * Description:
 *   Read the manifest of an installed package.
 *
 ****************************************************************************/

int pcpkg_get_manifest(const char *name, pcpkg_manifest_t *manifest)
{
  int idx = -1;

  for (int i = 0; i < g_installed_count; i++)
    {
      if (strcmp(g_installed[i].name, name) == 0)
        {
          idx = i;
          break;
        }
    }

  if (idx < 0)
    {
      return PC_ERR_NOENT;
    }

  char manifest_path[256];
  snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json",
           g_installed[idx].install_path);

  return pcpkg_parse_manifest(manifest_path, manifest);
}

/****************************************************************************
 * Name: pcpkg_parse_manifest
 *
 * Description:
 *   Parse a manifest.json file.
 *
 ****************************************************************************/

int pcpkg_parse_manifest(const char *json_path, pcpkg_manifest_t *manifest)
{
  FILE *f;
  long  fsize;
  char *json_str;

  f = fopen(json_path, "r");
  if (f == NULL)
    {
      return PC_ERR_NOENT;
    }

  fseek(f, 0, SEEK_END);
  fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (fsize <= 0 || fsize > 4096)
    {
      fclose(f);
      return PC_ERR_GENERIC;
    }

  json_str = (char *)malloc(fsize + 1);
  if (json_str == NULL)
    {
      fclose(f);
      return PC_ERR_NOMEM;
    }

  fread(json_str, 1, fsize, f);
  json_str[fsize] = '\0';
  fclose(f);

  cJSON *root = cJSON_Parse(json_str);
  free(json_str);

  if (root == NULL)
    {
      return PC_ERR_GENERIC;
    }

  memset(manifest, 0, sizeof(pcpkg_manifest_t));

  cJSON *name_j = cJSON_GetObjectItem(root, "name");
  cJSON *ver_j  = cJSON_GetObjectItem(root, "version");
  cJSON *auth_j = cJSON_GetObjectItem(root, "author");
  cJSON *desc_j = cJSON_GetObjectItem(root, "description");
  cJSON *cat_j  = cJSON_GetObjectItem(root, "category");
  cJSON *ram_j  = cJSON_GetObjectItem(root, "min_ram");
  cJSON *net_j  = cJSON_GetObjectItem(root, "requires_network");

  if (cJSON_IsString(name_j))
    strncpy(manifest->name, name_j->valuestring, PCPKG_NAME_MAX - 1);
  if (cJSON_IsString(ver_j))
    strncpy(manifest->version, ver_j->valuestring, PCPKG_VERSION_MAX - 1);
  if (cJSON_IsString(auth_j))
    strncpy(manifest->author, auth_j->valuestring, PCPKG_AUTHOR_MAX - 1);
  if (cJSON_IsString(desc_j))
    strncpy(manifest->description, desc_j->valuestring, PCPKG_DESC_MAX - 1);
  if (cJSON_IsString(cat_j))
    strncpy(manifest->category, cat_j->valuestring, 15);
  if (cJSON_IsNumber(ram_j))
    manifest->min_ram = (uint32_t)ram_j->valuedouble;
  if (cJSON_IsBool(net_j))
    manifest->requires_network = cJSON_IsTrue(net_j);

  cJSON_Delete(root);
  return PC_OK;
}

/****************************************************************************
 * Name: pcpkg_launch
 *
 * Description:
 *   Launch an installed third-party app by loading its ELF binary.
 *
 ****************************************************************************/

int pcpkg_launch(const char *name)
{
  int idx = -1;

  for (int i = 0; i < g_installed_count; i++)
    {
      if (strcmp(g_installed[i].name, name) == 0)
        {
          idx = i;
          break;
        }
    }

  if (idx < 0)
    {
      return PC_ERR_NOENT;
    }

  char elf_path[256];
  snprintf(elf_path, sizeof(elf_path), "%s/app.elf",
           g_installed[idx].install_path);

  /* Check if ELF file exists */

  struct stat st;
  if (stat(elf_path, &st) != 0)
    {
      syslog(LOG_ERR, "pkg: Missing app.elf for \"%s\"\n", name);
      return PC_ERR_NOENT;
    }

  syslog(LOG_INFO, "pkg: Loading ELF \"%s\" (%lu bytes)\n",
         elf_path, (unsigned long)st.st_size);

#ifdef CONFIG_ELF
  /* Use NuttX ELF binary format loader to load and execute
   * the third-party app's ELF binary.
   */

  {
    struct binary_s bin;
    int ret;

    memset(&bin, 0, sizeof(bin));

    /* The app's ELF imports symbols from our app API (pc_app_*).
     * NuttX provides the symbol table through the binfmt subsystem.
     * The default symtab is set via boardctl(BOARDIOC_OS_SYMTAB).
     */

    ret = load_module(&bin, elf_path, NULL, 0);
    if (ret < 0)
      {
        syslog(LOG_ERR, "pkg: load_module failed: %d\n", ret);
        return PC_ERR_GENERIC;
      }

    ret = exec_module(&bin, elf_path, NULL, NULL, NULL, NULL, false);

    syslog(LOG_INFO, "pkg: ELF \"%s\" exited with code %d\n",
           name, ret);

    unload_module(&bin);
    return (ret >= 0) ? PC_OK : PC_ERR_GENERIC;
  }
#else
  syslog(LOG_WARNING, "pkg: ELF loader not enabled (CONFIG_ELF=n)\n");
  return PC_ERR_GENERIC;
#endif
}

/****************************************************************************
 * Name: pcpkg_fetch_catalog
 *
 * Description:
 *   Fetch the app catalog from a remote repository.
 *   Downloads catalog.json from the given URL (or default repo),
 *   parses it, saves a local cache to SD card.
 *
 ****************************************************************************/

/* HTTP response type (defined in pcweb_http.c) */

typedef struct http_response_s
{
  int      status_code;
  char     content_type[64];
  char    *body;            /* PSRAM-allocated */
  size_t   body_len;
  char     redirect_url[256];
} http_response_t;

extern int http_get(const char *url, http_response_t *response);
extern void http_response_free(http_response_t *response);

int pcpkg_fetch_catalog(const char *repo_url, pcpkg_catalog_t *catalog)
{
  http_response_t resp;
  const char *url;
  int ret;

  if (catalog == NULL)
    {
      return PC_ERR_INVAL;
    }

  memset(catalog, 0, sizeof(pcpkg_catalog_t));
  url = (repo_url != NULL) ? repo_url : PCPKG_DEFAULT_REPO;

  strncpy(catalog->repo_url, url, PCPKG_URL_MAX - 1);

  syslog(LOG_INFO, "pkg: Fetching catalog from %s\n", url);

  memset(&resp, 0, sizeof(resp));
  ret = http_get(url, &resp);

  if (ret < 0 || resp.status_code != 200 || resp.body == NULL)
    {
      syslog(LOG_ERR, "pkg: Catalog fetch failed (ret=%d, status=%d)\n",
             ret, resp.status_code);
      http_response_free(&resp);

      /* Try loading cached catalog as fallback */

      return pcpkg_load_cached_catalog(catalog);
    }

  /* Parse JSON catalog */

  cJSON *root = cJSON_Parse(resp.body);
  if (root == NULL)
    {
      syslog(LOG_ERR, "pkg: Invalid catalog JSON\n");
      http_response_free(&resp);
      return PC_ERR_GENERIC;
    }

  /* Save raw JSON to SD cache before parsing */

  mkdir(PCPKG_APPS_DIR, 0755);
  FILE *cache_f = fopen(PCPKG_CATALOG_PATH, "w");
  if (cache_f != NULL)
    {
      fwrite(resp.body, 1, resp.body_len, cache_f);
      fclose(cache_f);
    }

  http_response_free(&resp);

  /* Parse packages array */

  cJSON *packages = cJSON_GetObjectItem(root, "packages");
  if (!cJSON_IsArray(packages))
    {
      cJSON_Delete(root);
      return PC_ERR_GENERIC;
    }

  catalog->count = 0;
  catalog->fetch_timestamp = (uint32_t)time(NULL);

  cJSON *pkg;
  cJSON_ArrayForEach(pkg, packages)
    {
      if (catalog->count >= 64)
        {
          break;
        }

      pcpkg_catalog_entry_t *e = &catalog->entries[catalog->count];

      cJSON *name_j = cJSON_GetObjectItem(pkg, "name");
      cJSON *ver_j  = cJSON_GetObjectItem(pkg, "version");
      cJSON *auth_j = cJSON_GetObjectItem(pkg, "author");
      cJSON *desc_j = cJSON_GetObjectItem(pkg, "description");
      cJSON *cat_j  = cJSON_GetObjectItem(pkg, "category");
      cJSON *url_j  = cJSON_GetObjectItem(pkg, "download_url");
      cJSON *size_j = cJSON_GetObjectItem(pkg, "size_bytes");
      cJSON *ram_j  = cJSON_GetObjectItem(pkg, "min_ram");
      cJSON *net_j  = cJSON_GetObjectItem(pkg, "requires_network");

      if (cJSON_IsString(name_j))
        strncpy(e->name, name_j->valuestring, PCPKG_NAME_MAX - 1);
      if (cJSON_IsString(ver_j))
        strncpy(e->version, ver_j->valuestring, PCPKG_VERSION_MAX - 1);
      if (cJSON_IsString(auth_j))
        strncpy(e->author, auth_j->valuestring, PCPKG_AUTHOR_MAX - 1);
      if (cJSON_IsString(desc_j))
        strncpy(e->description, desc_j->valuestring, PCPKG_DESC_MAX - 1);
      if (cJSON_IsString(cat_j))
        strncpy(e->category, cat_j->valuestring, 15);
      if (cJSON_IsString(url_j))
        strncpy(e->download_url, url_j->valuestring, PCPKG_URL_MAX - 1);
      if (cJSON_IsNumber(size_j))
        e->size_bytes = (uint32_t)size_j->valuedouble;
      if (cJSON_IsNumber(ram_j))
        e->min_ram = (uint32_t)ram_j->valuedouble;
      if (cJSON_IsBool(net_j))
        e->requires_network = cJSON_IsTrue(net_j);

      catalog->count++;
    }

  cJSON_Delete(root);

  syslog(LOG_INFO, "pkg: Catalog loaded with %d apps\n", catalog->count);
  return PC_OK;
}

/****************************************************************************
 * Name: pcpkg_load_cached_catalog
 *
 * Description:
 *   Load a previously cached catalog from SD card.
 *
 ****************************************************************************/

int pcpkg_load_cached_catalog(pcpkg_catalog_t *catalog)
{
  FILE *f;
  long  fsize;
  char *json_str;

  if (catalog == NULL)
    {
      return PC_ERR_INVAL;
    }

  f = fopen(PCPKG_CATALOG_PATH, "r");
  if (f == NULL)
    {
      syslog(LOG_WARNING, "pkg: No cached catalog found\n");
      return PC_ERR_NOENT;
    }

  fseek(f, 0, SEEK_END);
  fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (fsize <= 0 || fsize > 131072)
    {
      fclose(f);
      return PC_ERR_GENERIC;
    }

  json_str = (char *)malloc(fsize + 1);
  if (json_str == NULL)
    {
      fclose(f);
      return PC_ERR_NOMEM;
    }

  fread(json_str, 1, fsize, f);
  json_str[fsize] = '\0';
  fclose(f);

  cJSON *root = cJSON_Parse(json_str);
  free(json_str);

  if (root == NULL)
    {
      return PC_ERR_GENERIC;
    }

  memset(catalog, 0, sizeof(pcpkg_catalog_t));

  cJSON *packages = cJSON_GetObjectItem(root, "packages");
  if (!cJSON_IsArray(packages))
    {
      cJSON_Delete(root);
      return PC_ERR_GENERIC;
    }

  catalog->count = 0;

  cJSON *pkg;
  cJSON_ArrayForEach(pkg, packages)
    {
      if (catalog->count >= 64) break;

      pcpkg_catalog_entry_t *e = &catalog->entries[catalog->count];

      cJSON *name_j = cJSON_GetObjectItem(pkg, "name");
      cJSON *ver_j  = cJSON_GetObjectItem(pkg, "version");
      cJSON *auth_j = cJSON_GetObjectItem(pkg, "author");
      cJSON *desc_j = cJSON_GetObjectItem(pkg, "description");
      cJSON *cat_j  = cJSON_GetObjectItem(pkg, "category");
      cJSON *url_j  = cJSON_GetObjectItem(pkg, "download_url");
      cJSON *size_j = cJSON_GetObjectItem(pkg, "size_bytes");
      cJSON *ram_j  = cJSON_GetObjectItem(pkg, "min_ram");

      if (cJSON_IsString(name_j))
        strncpy(e->name, name_j->valuestring, PCPKG_NAME_MAX - 1);
      if (cJSON_IsString(ver_j))
        strncpy(e->version, ver_j->valuestring, PCPKG_VERSION_MAX - 1);
      if (cJSON_IsString(auth_j))
        strncpy(e->author, auth_j->valuestring, PCPKG_AUTHOR_MAX - 1);
      if (cJSON_IsString(desc_j))
        strncpy(e->description, desc_j->valuestring, PCPKG_DESC_MAX - 1);
      if (cJSON_IsString(cat_j))
        strncpy(e->category, cat_j->valuestring, 15);
      if (cJSON_IsString(url_j))
        strncpy(e->download_url, url_j->valuestring, PCPKG_URL_MAX - 1);
      if (cJSON_IsNumber(size_j))
        e->size_bytes = (uint32_t)size_j->valuedouble;
      if (cJSON_IsNumber(ram_j))
        e->min_ram = (uint32_t)ram_j->valuedouble;

      catalog->count++;
    }

  cJSON_Delete(root);

  syslog(LOG_INFO, "pkg: Loaded cached catalog (%d apps)\n",
         catalog->count);
  return PC_OK;
}

/****************************************************************************
 * Name: pcpkg_download_and_install
 *
 * Description:
 *   Download a .pcpkg from a URL and install it.
 *   Uses staging directory to avoid corrupt partial downloads.
 *
 ****************************************************************************/

int pcpkg_download_and_install(const char *url, const char *name)
{
  http_response_t resp;
  int ret;

  if (url == NULL || name == NULL)
    {
      return PC_ERR_INVAL;
    }

  syslog(LOG_INFO, "pkg: Downloading \"%s\" from %s\n", name, url);

  /* Ensure staging directory exists */

  pcpkg_mkdir_p(PCPKG_STAGING_DIR);

  /* Download the package */

  memset(&resp, 0, sizeof(resp));
  ret = http_get(url, &resp);

  if (ret < 0 || resp.status_code != 200 || resp.body == NULL)
    {
      syslog(LOG_ERR, "pkg: Download failed for \"%s\" (ret=%d)\n",
             name, ret);
      http_response_free(&resp);
      return PC_ERR_NET;
    }

  /* Validate minimum size (header must be >= 16 bytes) */

  if (resp.body_len < PCPKG_HEADER_SIZE)
    {
      syslog(LOG_ERR, "pkg: Downloaded file too small (%lu bytes)\n",
             (unsigned long)resp.body_len);
      http_response_free(&resp);
      return PC_ERR_GENERIC;
    }

  /* Validate magic bytes */

  if (memcmp(resp.body, PCPKG_MAGIC, 4) != 0)
    {
      syslog(LOG_ERR, "pkg: Downloaded file has invalid magic\n");
      http_response_free(&resp);
      return PC_ERR_GENERIC;
    }

  /* Write to staging file */

  char staging_path[128];
  snprintf(staging_path, sizeof(staging_path),
           "%s/%s.pcpkg", PCPKG_STAGING_DIR, name);

  int fd = open(staging_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    {
      syslog(LOG_ERR, "pkg: Cannot create staging file %s\n", staging_path);
      http_response_free(&resp);
      return PC_ERR_IO;
    }

  ssize_t written = write(fd, resp.body, resp.body_len);
  close(fd);
  http_response_free(&resp);

  if (written != (ssize_t)resp.body_len)
    {
      syslog(LOG_ERR, "pkg: Staging write failed\n");
      unlink(staging_path);
      return PC_ERR_IO;
    }

  syslog(LOG_INFO, "pkg: Downloaded %lu bytes to staging\n",
         (unsigned long)written);

  /* Install from staging file */

  ret = pcpkg_install(staging_path);

  /* Clean up staging file regardless of install result */

  unlink(staging_path);

  if (ret == PC_OK)
    {
      syslog(LOG_INFO, "pkg: \"%s\" downloaded and installed OK\n", name);
    }
  else
    {
      syslog(LOG_ERR, "pkg: Install of \"%s\" failed: %d\n", name, ret);
    }

  return ret;
}

/****************************************************************************
 * Name: pcpkg_scan_and_install_sd
 *
 * Description:
 *   Scan /mnt/sd/apps/ for .pcpkg files and install them.
 *   This is the "sideload" workflow: copy .pcpkg to SD card via
 *   USB or SCP, then run this function to install them.
 *   After successful install, the .pcpkg file is renamed to .installed
 *   to prevent re-install on next scan.
 *
 ****************************************************************************/

int pcpkg_scan_and_install_sd(void)
{
  DIR           *dir;
  struct dirent *ent;
  int            installed = 0;

  dir = opendir(PCPKG_APPS_DIR);
  if (dir == NULL)
    {
      syslog(LOG_ERR, "pkg: Cannot open %s\n", PCPKG_APPS_DIR);
      return PC_ERR_IO;
    }

  while ((ent = readdir(dir)) != NULL)
    {
      size_t nlen = strlen(ent->d_name);
      if (nlen <= 6)
        {
          continue;
        }

      /* Check for .pcpkg extension */

      if (strcmp(ent->d_name + nlen - 6, ".pcpkg") != 0)
        {
          continue;
        }

      char path[256];
      snprintf(path, sizeof(path), "%s/%s", PCPKG_APPS_DIR, ent->d_name);

      syslog(LOG_INFO, "pkg: Found sideloaded package: %s\n", ent->d_name);

      int ret = pcpkg_install(path);
      if (ret == PC_OK)
        {
          /* Rename to .installed to prevent re-install */

          char done_path[256];
          snprintf(done_path, sizeof(done_path), "%s.installed", path);
          rename(path, done_path);
          installed++;
        }
      else
        {
          syslog(LOG_ERR, "pkg: Failed to install %s: %d\n",
                 ent->d_name, ret);
        }
    }

  closedir(dir);

  syslog(LOG_INFO, "pkg: SD scan complete, installed %d packages\n",
         installed);
  return installed;
}

/****************************************************************************
 * Name: pcpkg_update_available
 *
 * Description:
 *   Check if a newer version of a package exists in the catalog.
 *   Simple string comparison of version numbers.
 *
 ****************************************************************************/

bool pcpkg_update_available(const char *name,
                            const pcpkg_catalog_t *catalog)
{
  if (name == NULL || catalog == NULL)
    {
      return false;
    }

  /* Find the installed version */

  char installed_ver[PCPKG_VERSION_MAX] = "";

  for (int i = 0; i < g_installed_count; i++)
    {
      if (strcmp(g_installed[i].name, name) == 0)
        {
          strncpy(installed_ver, g_installed[i].version,
                  PCPKG_VERSION_MAX - 1);
          break;
        }
    }

  if (installed_ver[0] == '\0')
    {
      return false;  /* Not installed */
    }

  /* Find in catalog */

  for (int i = 0; i < catalog->count; i++)
    {
      if (strcmp(catalog->entries[i].name, name) == 0)
        {
          /* Simple lexicographic version comparison.
           * Works for semver if all components are same width.
           * TODO: proper semver comparison.
           */

          return strcmp(catalog->entries[i].version,
                        installed_ver) > 0;
        }
    }

  return false;
}
