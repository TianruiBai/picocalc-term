/****************************************************************************
 * apps/pcssh/pcssh_sftp.c
 *
 * SFTP file browser using wolfSSH SFTP subsystem.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#ifdef CONFIG_CRYPTO_WOLFSSH
#  include <wolfssh/ssh.h>
#  include <wolfssh/settings.h>
#  include <wolfssh/wolfsftp.h>
#endif

#include "pcterm/app.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef struct sftp_entry_s
{
  char     name[128];
  bool     is_dir;
  uint32_t size;
  uint32_t mtime;
} sftp_entry_t;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int sftp_list_dir(void *session, const char *remote_path,
                  sftp_entry_t *entries, int max_entries)
{
  syslog(LOG_INFO, "SFTP: List %s\n", remote_path);

#ifdef CONFIG_CRYPTO_WOLFSSH
  WOLFSSH *ssh = (WOLFSSH *)session;

  if (ssh == NULL)
    {
      return -1;
    }

  /* TODO: wolfSSH SFTP API has changed in newer versions.
   * wolfSSH_SFTP_ReadDir now requires (ssh, handle, handleSz).
   * Need to open directory first with wolfSSH_SFTP_OpenDir,
   * then read entries, then close. Stubbed for now.
   */

  syslog(LOG_WARNING, "SFTP: ReadDir not yet updated for wolfSSH API\n");
  (void)entries;
  (void)max_entries;
  return -1;

#else
  syslog(LOG_WARNING, "SFTP: wolfSSH not available\n");
  (void)session;
  (void)entries;
  (void)max_entries;
  return -1;
#endif
}

int sftp_download(void *session, const char *remote_path,
                  const char *local_path)
{
  syslog(LOG_INFO, "SFTP: Download %s -> %s\n", remote_path, local_path);

#ifdef CONFIG_CRYPTO_WOLFSSH
  WOLFSSH *ssh = (WOLFSSH *)session;

  if (ssh == NULL)
    {
      return -1;
    }

  /* Open remote file */

  byte handle[WOLFSSH_MAX_HANDLE];
  word32 handle_sz = sizeof(handle);

  int ret = wolfSSH_SFTP_Open(ssh, (char *)remote_path,
                              WOLFSSH_FXF_READ, NULL,
                              handle, &handle_sz);
  if (ret != WS_SUCCESS)
    {
      syslog(LOG_ERR, "SFTP: Open failed: %d\n", ret);
      return -1;
    }

  FILE *f = fopen(local_path, "wb");
  if (f == NULL)
    {
      wolfSSH_SFTP_Close(ssh, handle, handle_sz);
      return -1;
    }

  uint8_t buf[4096];
  word64 offset = 0;
  int n;

  while ((n = wolfSSH_SFTP_Read(ssh, handle, handle_sz,
                                 &offset, buf, sizeof(buf))) > 0)
    {
      fwrite(buf, 1, n, f);
      offset += n;
    }

  fclose(f);
  wolfSSH_SFTP_Close(ssh, handle, handle_sz);
  return 0;

#else
  (void)session;
  return -1;
#endif
}

int sftp_upload(void *session, const char *local_path,
                const char *remote_path)
{
  syslog(LOG_INFO, "SFTP: Upload %s -> %s\n", local_path, remote_path);

#ifdef CONFIG_CRYPTO_WOLFSSH
  WOLFSSH *ssh = (WOLFSSH *)session;

  if (ssh == NULL)
    {
      return -1;
    }

  FILE *f = fopen(local_path, "rb");
  if (f == NULL)
    {
      return -1;
    }

  byte handle[WOLFSSH_MAX_HANDLE];
  word32 handle_sz = sizeof(handle);

  int ret = wolfSSH_SFTP_Open(ssh, (char *)remote_path,
                              WOLFSSH_FXF_WRITE | WOLFSSH_FXF_CREAT
                              | WOLFSSH_FXF_TRUNC,
                              NULL, handle, &handle_sz);
  if (ret != WS_SUCCESS)
    {
      fclose(f);
      return -1;
    }

  uint8_t buf[4096];
  word64 offset = 0;
  size_t got;

  while ((got = fread(buf, 1, sizeof(buf), f)) > 0)
    {
      int sent = wolfSSH_SFTP_Write(ssh, handle, handle_sz,
                                     &offset, buf, got);
      if (sent <= 0) break;
      offset += sent;
    }

  fclose(f);
  wolfSSH_SFTP_Close(ssh, handle, handle_sz);
  return 0;

#else
  (void)session;
  return -1;
#endif
}
