/****************************************************************************
 * pcterm/src/hostname.c
 *
 * Hostname management implementation.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>

#include "pcterm/hostname.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static char g_hostname[HOSTNAME_MAX_LEN + 1] = HOSTNAME_DEFAULT;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/**
 * Validate a hostname string.
 * Rules: 1-31 chars, alphanumeric + hyphens, no leading/trailing hyphen.
 */
static bool hostname_validate(const char *name)
{
  size_t len = strlen(name);

  if (len == 0 || len > HOSTNAME_MAX_LEN)
    {
      return false;
    }

  if (name[0] == '-' || name[len - 1] == '-')
    {
      return false;
    }

  for (size_t i = 0; i < len; i++)
    {
      if (!isalnum((unsigned char)name[i]) && name[i] != '-')
        {
          return false;
        }
    }

  return true;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int hostname_init(void)
{
  FILE *f = fopen(HOSTNAME_PATH, "r");

  if (f != NULL)
    {
      char buf[HOSTNAME_MAX_LEN + 2];

      if (fgets(buf, sizeof(buf), f) != NULL)
        {
          /* Strip trailing newline */

          size_t len = strlen(buf);
          while (len > 0 &&
                 (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
            {
              buf[--len] = '\0';
            }

          if (hostname_validate(buf))
            {
              strncpy(g_hostname, buf, HOSTNAME_MAX_LEN);
              g_hostname[HOSTNAME_MAX_LEN] = '\0';
            }
          else
            {
              syslog(LOG_WARNING,
                     "hostname: Invalid hostname in %s, using default\n",
                     HOSTNAME_PATH);
            }
        }

      fclose(f);
    }
  else
    {
      syslog(LOG_INFO, "hostname: No %s, using default \"%s\"\n",
             HOSTNAME_PATH, HOSTNAME_DEFAULT);

      /* Create the file with default */

      f = fopen(HOSTNAME_PATH, "w");
      if (f != NULL)
        {
          fprintf(f, "%s\n", HOSTNAME_DEFAULT);
          fclose(f);
        }
    }

  /* Set NuttX kernel hostname */

  sethostname(g_hostname, strlen(g_hostname));

  return 0;
}

const char *hostname_get(void)
{
  return g_hostname;
}

int hostname_set(const char *name)
{
  if (!hostname_validate(name))
    {
      return -EINVAL;
    }

  strncpy(g_hostname, name, HOSTNAME_MAX_LEN);
  g_hostname[HOSTNAME_MAX_LEN] = '\0';

  /* Update NuttX kernel hostname */

  sethostname(g_hostname, strlen(g_hostname));

  /* Persist to SD card */

  FILE *f = fopen(HOSTNAME_PATH, "w");
  if (f != NULL)
    {
      fprintf(f, "%s\n", g_hostname);
      fclose(f);
    }

  syslog(LOG_INFO, "hostname: Set to \"%s\"\n", g_hostname);

  return 0;
}
