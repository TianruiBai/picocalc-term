/****************************************************************************
 * apps/pcssh/pcssh.h
 *
 * Shared types and declarations for the SSH application.
 *
 ****************************************************************************/

#ifndef PCSSH_H
#define PCSSH_H

#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef struct ssh_connection_s
{
  char     name[32];        /* Display name */
  char     host[128];       /* Hostname or IP */
  uint16_t port;            /* SSH port (default 22) */
  char     username[64];    /* Username */
  bool     use_key;         /* Key-based auth */
  char     key_path[128];   /* Path to private key on SD card */
} ssh_connection_t;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int  ssh_connections_load(void);
int  ssh_connections_save(void);
int  ssh_connections_count(void);
int  ssh_connections_get(int index, ssh_connection_t *conn);

#endif /* PCSSH_H */
