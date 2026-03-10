/****************************************************************************
 * pcterm/include/pcterm/user.h
 *
 * User account management for PicoCalc-Term.
 * A single-user device: one active account stored in config.
 * The login screen is shown on boot when login_enabled=true and a
 * password hash is set.
 *
 ****************************************************************************/

#ifndef __PCTERM_USER_H
#define __PCTERM_USER_H

#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define USER_MAX_NAME    32
#define USER_MAX_HASH    65   /* 32-byte hash → 64 hex chars + NUL */

/* Linux-like filesystem paths (all on internal flash, always available) */

#define USER_HOME_DIR    "/flash/home/picocalc"
#define USER_PASSWD_FILE "/flash/etc/passwd"

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * One-way hash of a password. Writes at most USER_MAX_HASH bytes to out.
 * Uses a simple djb2-based hash combined with a fixed salt string.
 * Suitable for a personal device; not cryptographically strong.
 */
void user_hash_password(const char *password, char out[USER_MAX_HASH]);

/**
 * Verify that 'password' matches the stored hash.
 * Returns true on match.
 */
bool user_verify_password(const char *password, const char *stored_hash);

/**
 * Set username + password for the active account.
 * Stores the hash in pc_config and saves config to SD.
 * Also writes a Unix-style /flash/etc/passwd entry.
 * Pass password=NULL or "" to clear the password (disable login screen).
 *
 * Returns 0 on success, negative on error.
 */
int user_set_credentials(const char *username, const char *password);

/**
 * Write (or refresh) /flash/etc/passwd with the current username.
 * Called automatically by user_set_credentials(); exposed so boot code
 * can ensure the file exists on first run.
 */
void user_write_passwd(void);

/**
 * Get the current username from config. Returns a pointer to the config
 * buf (valid until next config_load); never NULL (falls back to "user").
 */
const char *user_get_name(void);

#ifdef __cplusplus
}
#endif

#endif /* __PCTERM_USER_H */
