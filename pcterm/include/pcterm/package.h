/****************************************************************************
 * pcterm/include/pcterm/package.h
 *
 * Package manager API for .pcpkg third-party app packages.
 *
 * Package structure (TAR-like):
 *   manifest.json    — metadata (name, version, author, min_ram, etc.)
 *   app.elf          — ARM ELF binary
 *   icon.bin         — 32×32 RGB565 icon (optional)
 *   assets/          — additional files (optional)
 *
 * Registry: /mnt/sd/apps/registry.json
 * Install dir: /mnt/sd/apps/<package_name>/
 *
 ****************************************************************************/

#ifndef __PCTERM_PACKAGE_H
#define __PCTERM_PACKAGE_H

#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PCPKG_MAGIC          "PCPK"
#define PCPKG_REGISTRY_PATH  "/mnt/sd/apps/registry.json"
#define PCPKG_APPS_DIR       "/mnt/sd/apps"
#define PCPKG_STAGING_DIR    "/mnt/sd/apps/.staging"
#define PCPKG_CATALOG_PATH   "/mnt/sd/apps/catalog.json"
#define PCPKG_DEFAULT_REPO   "https://picocalc.dev/repo/catalog.json"

#define PCPKG_NAME_MAX       32
#define PCPKG_VERSION_MAX    16
#define PCPKG_AUTHOR_MAX     64
#define PCPKG_DESC_MAX       256
#define PCPKG_URL_MAX        256

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Package manifest (parsed from manifest.json) */

typedef struct pcpkg_manifest_s
{
  char     name[PCPKG_NAME_MAX];
  char     version[PCPKG_VERSION_MAX];
  char     author[PCPKG_AUTHOR_MAX];
  char     description[PCPKG_DESC_MAX];
  char     category[16];
  uint32_t min_ram;
  uint32_t flags;
  bool     requires_network;
} pcpkg_manifest_t;

/* Installed package entry (from registry.json) */

typedef struct pcpkg_entry_s
{
  char     name[PCPKG_NAME_MAX];
  char     version[PCPKG_VERSION_MAX];
  char     install_path[128];
  uint32_t installed_size;         /* Bytes on SD card */
  uint32_t install_timestamp;      /* Unix timestamp */
} pcpkg_entry_t;

/* Remote app catalog entry (from repository catalog.json) */

typedef struct pcpkg_catalog_entry_s
{
  char     name[PCPKG_NAME_MAX];
  char     version[PCPKG_VERSION_MAX];
  char     author[PCPKG_AUTHOR_MAX];
  char     description[PCPKG_DESC_MAX];
  char     category[16];
  char     download_url[PCPKG_URL_MAX];
  uint32_t size_bytes;             /* Download size */
  uint32_t min_ram;                /* Minimum PSRAM required */
  bool     requires_network;
} pcpkg_catalog_entry_t;

/* App catalog (list of available remote apps) */

typedef struct pcpkg_catalog_s
{
  int                    count;
  pcpkg_catalog_entry_t  entries[64];
  char                   repo_url[PCPKG_URL_MAX];
  uint32_t               fetch_timestamp;
} pcpkg_catalog_t;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the package manager. Loads registry.json.
 */
int pcpkg_init(void);

/**
 * Install a .pcpkg file from the given path.
 * Extracts to /mnt/sd/apps/<name>/ and updates registry.
 *
 * @param pcpkg_path  Path to .pcpkg file on SD card
 * @return PC_OK on success, negative error code on failure
 */
int pcpkg_install(const char *pcpkg_path);

/**
 * Uninstall a package by name.
 * Removes the app directory and registry entry.
 *
 * @param name  Package name (e.g. "mygame")
 * @return PC_OK on success, PC_ERR_NOENT if not installed
 */
int pcpkg_uninstall(const char *name);

/**
 * Get the list of installed packages.
 *
 * @param entries  Output array (caller-allocated)
 * @param max      Maximum entries to return
 * @return Number of entries written, or negative error
 */
int pcpkg_list(pcpkg_entry_t *entries, int max);

/**
 * Get the number of installed packages.
 */
int pcpkg_count(void);

/**
 * Read the manifest of an installed package.
 *
 * @param name      Package name
 * @param manifest  Output manifest struct
 * @return PC_OK on success
 */
int pcpkg_get_manifest(const char *name, pcpkg_manifest_t *manifest);

/**
 * Launch an installed third-party app by name.
 * Loads app.elf from the package directory.
 *
 * @param name  Package name
 * @return PC_OK on success (app is now running)
 */
int pcpkg_launch(const char *name);

/**
 * Parse a manifest.json file.
 *
 * @param json_path Path to manifest.json
 * @param manifest  Output manifest struct
 * @return PC_OK on success
 */
int pcpkg_parse_manifest(const char *json_path, pcpkg_manifest_t *manifest);

/* --- App Store / Remote Download --- */

/**
 * Fetch the app catalog from a remote repository URL.
 * Downloads catalog.json and parses it into pcpkg_catalog_t.
 *
 * @param repo_url  URL to catalog.json (NULL uses PCPKG_DEFAULT_REPO)
 * @param catalog   Output catalog struct (caller-allocated)
 * @return PC_OK on success, PC_ERR_NET on network failure
 */
int pcpkg_fetch_catalog(const char *repo_url, pcpkg_catalog_t *catalog);

/**
 * Load a previously cached catalog from SD card.
 * Falls back to this if no network is available.
 *
 * @param catalog  Output catalog struct
 * @return PC_OK on success, PC_ERR_NOENT if no cached catalog
 */
int pcpkg_load_cached_catalog(pcpkg_catalog_t *catalog);

/**
 * Download and install a package from a remote URL.
 * Downloads .pcpkg to staging directory, then calls pcpkg_install().
 *
 * @param url   URL to .pcpkg file
 * @param name  Package name (for progress logging)
 * @return PC_OK on success
 */
int pcpkg_download_and_install(const char *url, const char *name);

/**
 * Scan /mnt/sd/apps/ for .pcpkg files and install any found.
 * This is the "sideload from SD card" workflow.
 *
 * @return Number of packages installed, or negative on error
 */
int pcpkg_scan_and_install_sd(void);

/**
 * Check if a package has a newer version available in the catalog.
 *
 * @param name     Package name
 * @param catalog  Catalog to check against
 * @return true if update available, false otherwise
 */
bool pcpkg_update_available(const char *name,
                            const pcpkg_catalog_t *catalog);

#ifdef __cplusplus
}
#endif

#endif /* __PCTERM_PACKAGE_H */
