/****************************************************************************
 * pcterm/include/pcterm/hostname.h
 *
 * eUX OS — Hostname management.
 * Hostname is stored on the internal flash at /flash/etc/hostname and used in:
 *   - NuttShell prompt: user@<hostname>$
 *   - Status bar display
 *   - Network identity (mDNS future)
 *   - SSH client identification
 *
 ****************************************************************************/

#ifndef __PCTERM_HOSTNAME_H
#define __PCTERM_HOSTNAME_H

#include <stddef.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define HOSTNAME_PATH       "/flash/etc/hostname"
#define HOSTNAME_MAX_LEN    31       /* Max hostname length (excl. NUL) */
#define HOSTNAME_DEFAULT    "eux"

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Load hostname from /flash/etc/hostname.
 * Falls back to HOSTNAME_DEFAULT if file is missing or invalid.
 * Also sets the NuttX kernel hostname via sethostname().
 *
 * @return 0 on success
 */
int hostname_init(void);

/**
 * Get the current hostname.
 * Returns a pointer to the static hostname buffer (thread-safe, read-only).
 */
const char *hostname_get(void);

/**
 * Set a new hostname.
 * Updates the in-memory value, NuttX kernel hostname, and SD card file.
 *
 * @param name  New hostname (max HOSTNAME_MAX_LEN characters, alphanumeric + hyphens)
 * @return 0 on success, -EINVAL if name is invalid
 */
int hostname_set(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* __PCTERM_HOSTNAME_H */
