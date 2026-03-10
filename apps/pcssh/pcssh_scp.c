/****************************************************************************
 * apps/pcssh/pcssh_scp.c
 *
 * SCP file transfer (over SSH).
 * Simple file upload/download using wolfSSH SCP channel.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/stat.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#ifdef CONFIG_CRYPTO_WOLFSSH
#  include <wolfssh/ssh.h>
#  include <wolfssh/settings.h>
#endif

#include "pcterm/app.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_CRYPTO_WOLFSSH
static int scp_tcp_connect(const char *host, uint16_t port)
{
  struct addrinfo hints, *res;
  char port_str[8];
  int sock;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  snprintf(port_str, sizeof(port_str), "%d", port);

  if (getaddrinfo(host, port_str, &hints, &res) != 0)
    {
      return -1;
    }

  sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sock < 0)
    {
      freeaddrinfo(res);
      return -1;
    }

  if (connect(sock, res->ai_addr, res->ai_addrlen) < 0)
    {
      close(sock);
      freeaddrinfo(res);
      return -1;
    }

  freeaddrinfo(res);
  return sock;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int scp_download(const char *host, uint16_t port,
                 const char *username, const char *password,
                 const char *remote_path, const char *local_path)
{
  syslog(LOG_INFO, "SCP: Download %s:%s -> %s\n",
         host, remote_path, local_path);

#ifdef CONFIG_CRYPTO_WOLFSSH
  int sock = scp_tcp_connect(host, port);
  if (sock < 0)
    {
      syslog(LOG_ERR, "SCP: TCP connect failed\n");
      return -1;
    }

  WOLFSSH_CTX *ctx = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_CLIENT, NULL);
  if (ctx == NULL)
    {
      close(sock);
      return -1;
    }

  WOLFSSH *ssh = wolfSSH_new(ctx);
  if (ssh == NULL)
    {
      wolfSSH_CTX_free(ctx);
      close(sock);
      return -1;
    }

  wolfSSH_set_fd(ssh, sock);
  wolfSSH_SetUsername(ssh, username);

  if (wolfSSH_connect(ssh) != WS_SUCCESS)
    {
      syslog(LOG_ERR, "SCP: SSH handshake failed\n");
      wolfSSH_free(ssh);
      wolfSSH_CTX_free(ctx);
      close(sock);
      return -1;
    }

  /* Request SCP receive */

  int ret = wolfSSH_SCP_connect(ssh, (byte *)remote_path);
  if (ret != WS_SCP_COMPLETE && ret != WS_SUCCESS)
    {
      syslog(LOG_ERR, "SCP: SCP connect failed: %d\n", ret);
      wolfSSH_free(ssh);
      wolfSSH_CTX_free(ctx);
      close(sock);
      return -1;
    }

  FILE *f = fopen(local_path, "wb");
  if (f == NULL)
    {
      wolfSSH_free(ssh);
      wolfSSH_CTX_free(ctx);
      close(sock);
      return -1;
    }

  uint8_t buf[4096];
  int n;

  while ((n = wolfSSH_stream_read(ssh, buf, sizeof(buf))) > 0)
    {
      fwrite(buf, 1, n, f);
    }

  fclose(f);
  wolfSSH_shutdown(ssh);
  wolfSSH_free(ssh);
  wolfSSH_CTX_free(ctx);
  close(sock);

  syslog(LOG_INFO, "SCP: Download complete\n");
  return 0;

#else
  syslog(LOG_WARNING, "SCP: wolfSSH not available\n");
  return -1;
#endif
}

int scp_upload(const char *host, uint16_t port,
               const char *username, const char *password,
               const char *local_path, const char *remote_path)
{
  syslog(LOG_INFO, "SCP: Upload %s -> %s:%s\n",
         local_path, host, remote_path);

#ifdef CONFIG_CRYPTO_WOLFSSH
  /* Get file size for SCP protocol */

  struct stat st;
  if (stat(local_path, &st) != 0)
    {
      syslog(LOG_ERR, "SCP: Cannot stat %s\n", local_path);
      return -1;
    }

  FILE *f = fopen(local_path, "rb");
  if (f == NULL)
    {
      return -1;
    }

  int sock = scp_tcp_connect(host, port);
  if (sock < 0)
    {
      fclose(f);
      return -1;
    }

  WOLFSSH_CTX *ctx = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_CLIENT, NULL);
  WOLFSSH *ssh = wolfSSH_new(ctx);
  wolfSSH_set_fd(ssh, sock);
  wolfSSH_SetUsername(ssh, username);

  if (wolfSSH_connect(ssh) != WS_SUCCESS)
    {
      fclose(f);
      wolfSSH_free(ssh);
      wolfSSH_CTX_free(ctx);
      close(sock);
      return -1;
    }

  /* Send file via SCP */

  uint8_t buf[4096];
  size_t remaining = st.st_size;

  while (remaining > 0)
    {
      size_t chunk = remaining < sizeof(buf) ? remaining : sizeof(buf);
      size_t got = fread(buf, 1, chunk, f);
      if (got == 0) break;

      int sent = wolfSSH_stream_send(ssh, buf, got);
      if (sent <= 0) break;

      remaining -= sent;
    }

  fclose(f);
  wolfSSH_shutdown(ssh);
  wolfSSH_free(ssh);
  wolfSSH_CTX_free(ctx);
  close(sock);

  syslog(LOG_INFO, "SCP: Upload complete\n");
  return 0;

#else
  syslog(LOG_WARNING, "SCP: wolfSSH not available\n");
  return -1;
#endif
}
