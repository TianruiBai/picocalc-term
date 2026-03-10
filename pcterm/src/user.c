/****************************************************************************
 * pcterm/src/user.c
 *
 * User account management — password hashing and credential storage.
 *
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>

#include "pcterm/user.h"
#include "pcterm/config.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Static salt mixed into every hash so offline dictionary attacks need
 * to be per-device (the salt IS embedded in firmware, which is acceptable
 * for a single-user embedded device). */

#define HASH_SALT  "picocalc-v1:"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: user_hash_password
 *
 * Description:
 *   Compute a 32-byte (64 hex char) hash of password using a salted
 *   djb2 accumulator folded into 32 bytes. Simple but sufficient for a
 *   personal device with physical SD-card access as the threat model.
 *
 ****************************************************************************/

void user_hash_password(const char *password, char out[USER_MAX_HASH])
{
  uint8_t  digest[32];
  uint32_t state[8];
  int      i;
  int      pos;
  const char *p;

  /* Initialise state with constants derived from djb2 primes */

  state[0] = 0x6b8b4567u;
  state[1] = 0x327b23c6u;
  state[2] = 0x643c9869u;
  state[3] = 0x66334873u;
  state[4] = 0x74b0dc51u;
  state[5] = 0x19495cffu;
  state[6] = 0x2ae8944au;
  state[7] = 0x625558ecu;

  /* Mix salt */

  for (p = HASH_SALT; *p; p++)
    {
      for (i = 0; i < 8; i++)
        {
          state[i] = state[i] * 33u + (uint8_t)*p + (uint32_t)i;
        }
    }

  /* Mix password */

  pos = 0;
  for (p = password ? password : ""; *p; p++)
    {
      state[pos % 8] = state[pos % 8] * 33u ^ (uint8_t)*p;
      pos++;
    }

  /* Additional diffusion rounds */

  for (i = 0; i < 64; i++)
    {
      int a = i % 8;
      int b = (i + 3) % 8;
      state[a] ^= state[b] * 0x9e3779b9u;
      state[a]  = (state[a] << 7) | (state[a] >> 25);
    }

  /* Pack state into 32-byte digest */

  for (i = 0; i < 8; i++)
    {
      digest[i * 4 + 0] = (state[i] >> 24) & 0xff;
      digest[i * 4 + 1] = (state[i] >> 16) & 0xff;
      digest[i * 4 + 2] = (state[i] >>  8) & 0xff;
      digest[i * 4 + 3] = (state[i]      ) & 0xff;
    }

  /* Encode as lowercase hex */

  for (i = 0; i < 32; i++)
    {
      static const char hex[] = "0123456789abcdef";
      out[i * 2]     = hex[(digest[i] >> 4) & 0x0f];
      out[i * 2 + 1] = hex[ digest[i]       & 0x0f];
    }

  out[64] = '\0';
}

/****************************************************************************
 * Name: user_verify_password
 ****************************************************************************/

bool user_verify_password(const char *password, const char *stored_hash)
{
  char computed[USER_MAX_HASH];

  if (!stored_hash || stored_hash[0] == '\0')
    {
      return true;   /* No password set — always allow */
    }

  user_hash_password(password, computed);
  return (strcmp(computed, stored_hash) == 0);
}

/****************************************************************************
 * Name: user_set_credentials
 ****************************************************************************/

int user_set_credentials(const char *username, const char *password)
{
  pc_config_t *cfg = pc_config_get();

  if (username && username[0] != '\0')
    {
      strncpy(cfg->login_user, username, sizeof(cfg->login_user) - 1);
      cfg->login_user[sizeof(cfg->login_user) - 1] = '\0';
    }

  if (password && password[0] != '\0')
    {
      user_hash_password(password, cfg->login_hash);
      cfg->login_enabled = true;
    }
  else
    {
      /* Clear password — disable login requirement */

      cfg->login_hash[0] = '\0';
      cfg->login_enabled = false;
    }

  /* Write Unix-style /etc/passwd for Linux compatibility */

  user_write_passwd();

  return pc_config_save(cfg);
}

/****************************************************************************
 * Name: user_write_passwd
 *
 * Description:
 *   Write a minimal Unix /etc/passwd line to /flash/etc/passwd.
 *   Format: username:x:1000:1000:User:/home/user:/bin/sh
 *   The 'x' shadows the password (actual hash is in settings.json).
 *   Called on credential change and can be called at boot to ensure the
 *   file exists.
 *
 ****************************************************************************/

void user_write_passwd(void)
{
  const char *username = user_get_name();
  char home_dir[80];
  FILE *f;

  snprintf(home_dir, sizeof(home_dir), "/flash/home/%s", username);

  /* Ensure directory exists (may be first boot) */

  mkdir("/flash/etc", 0755);

  f = fopen(USER_PASSWD_FILE, "w");
  if (f == NULL)
    {
      return;  /* Flash may not be mounted yet — skip silently */
    }

  /* Write Unix passwd format:
   *   name : password-placeholder : uid : gid : GECOS : home : shell
   * UID/GID 1000 is the conventional first user on Linux.
   * '/bin/sh' maps to NuttShell on NuttX.
   */

  fprintf(f, "%s:x:1000:1000:%s:%s:/bin/sh\n",
          username, username, home_dir);

  fclose(f);
}

/****************************************************************************
 * Name: user_get_name
 ****************************************************************************/

const char *user_get_name(void)
{
  const pc_config_t *cfg = pc_config_get();

  if (cfg->login_user[0] != '\0')
    {
      return cfg->login_user;
    }

  return "picocalc";
}
