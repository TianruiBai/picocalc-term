/****************************************************************************
 * apps/pcssh/pcssh_client.c
 *
 * SSH client session management using wolfSSH.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* wolfSSH is optional — guarded by CONFIG_CRYPTO_WOLFSSH */

#ifdef CONFIG_CRYPTO_WOLFSSH
#include <wolfssh/ssh.h>
#include <wolfssh/wolfssh.h>
#endif

#include "pcterm/app.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef struct ssh_session_s
{
  int           sock_fd;        /* TCP socket */
#ifdef CONFIG_CRYPTO_WOLFSSH
  WOLFSSH      *ssh;            /* wolfSSH session handle */
  WOLFSSH_CTX  *ctx;            /* wolfSSH context */
#endif
  char          host[128];
  uint16_t      port;
  char          username[64];
  bool          connected;
  bool          authenticated;
} ssh_session_t;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * Initialize the SSH subsystem (call once).
 */
int ssh_client_init(void)
{
#ifdef CONFIG_CRYPTO_WOLFSSH
  int ret = wolfSSH_Init();
  if (ret != WS_SUCCESS)
    {
      syslog(LOG_ERR, "SSH: wolfSSH_Init failed: %d\n", ret);
      return -1;
    }
#endif
  syslog(LOG_INFO, "SSH: Client initialized\n");
  return 0;
}

/**
 * Connect to an SSH server.
 *
 * @param host      Hostname or IP
 * @param port      Port number (default 22)
 * @param username  Username for authentication
 * @param password  Password (NULL for key-based auth)
 * @return SSH session, or NULL on failure
 */
ssh_session_t *ssh_connect(const char *host, uint16_t port,
                           const char *username, const char *password)
{
  ssh_session_t *sess;
  struct addrinfo hints, *res;
  int ret;

  sess = (ssh_session_t *)pc_app_psram_alloc(sizeof(ssh_session_t));
  if (sess == NULL) return NULL;

  memset(sess, 0, sizeof(ssh_session_t));
  strncpy(sess->host, host, sizeof(sess->host) - 1);
  strncpy(sess->username, username, sizeof(sess->username) - 1);
  sess->port = (port > 0) ? port : 22;

  /* DNS resolve */

  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  char port_str[8];
  snprintf(port_str, sizeof(port_str), "%d", sess->port);

  ret = getaddrinfo(host, port_str, &hints, &res);
  if (ret != 0)
    {
      syslog(LOG_ERR, "SSH: DNS failed for %s: %d\n", host, ret);
      pc_app_psram_free(sess);
      return NULL;
    }

  /* TCP connect */

  sess->sock_fd = socket(res->ai_family, res->ai_socktype,
                          res->ai_protocol);
  if (sess->sock_fd < 0)
    {
      freeaddrinfo(res);
      pc_app_psram_free(sess);
      return NULL;
    }

  if (connect(sess->sock_fd, res->ai_addr, res->ai_addrlen) < 0)
    {
      syslog(LOG_ERR, "SSH: TCP connect to %s:%d failed\n",
             host, sess->port);
      close(sess->sock_fd);
      freeaddrinfo(res);
      pc_app_psram_free(sess);
      return NULL;
    }

  freeaddrinfo(res);
  sess->connected = true;

  syslog(LOG_INFO, "SSH: TCP connected to %s:%d\n", host, sess->port);

  /* wolfSSH handshake */

#ifdef CONFIG_CRYPTO_WOLFSSH
  sess->ctx = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_CLIENT, NULL);
  if (sess->ctx == NULL)
    {
      syslog(LOG_ERR, "SSH: CTX_new failed\n");
      close(sess->sock_fd);
      pc_app_psram_free(sess);
      return NULL;
    }

  sess->ssh = wolfSSH_new(sess->ctx);
  if (sess->ssh == NULL)
    {
      syslog(LOG_ERR, "SSH: wolfSSH_new failed\n");
      wolfSSH_CTX_free(sess->ctx);
      close(sess->sock_fd);
      pc_app_psram_free(sess);
      return NULL;
    }

  wolfSSH_set_fd(sess->ssh, sess->sock_fd);

  /* Set username for auth */

  wolfSSH_SetUsername(sess->ssh, username);

  /* If password provided, set it for userauth */

  if (password != NULL)
    {
      wolfSSH_SetUserAuthCtx(sess->ssh, (void *)password);
    }

  ret = wolfSSH_connect(sess->ssh);
  if (ret != WS_SUCCESS)
    {
      syslog(LOG_ERR, "SSH: wolfSSH_connect failed: %d\n",
             wolfSSH_get_error(sess->ssh));
      wolfSSH_free(sess->ssh);
      wolfSSH_CTX_free(sess->ctx);
      close(sess->sock_fd);
      pc_app_psram_free(sess);
      return NULL;
    }

  sess->authenticated = true;
  syslog(LOG_INFO, "SSH: Authenticated as %s@%s\n", username, host);
#else
  syslog(LOG_WARNING, "SSH: wolfSSH not available, raw TCP only\n");
#endif

  return sess;
}

/**
 * Send data to the SSH server (from keyboard).
 */
int ssh_send(ssh_session_t *sess, const char *data, size_t len)
{
  if (sess == NULL || !sess->connected) return -1;

#ifdef CONFIG_CRYPTO_WOLFSSH
  if (sess->ssh)
    {
      return wolfSSH_stream_send(sess->ssh, (uint8_t *)data, len);
    }
#endif

  /* Fallback: raw TCP for testing */

  return send(sess->sock_fd, data, len, 0);
}

/**
 * Receive data from the SSH server (for terminal display).
 *
 * @param sess  SSH session
 * @param buf   Output buffer
 * @param len   Buffer size
 * @return Bytes received, 0 on no data, -1 on error
 */
int ssh_recv(ssh_session_t *sess, char *buf, size_t len)
{
  if (sess == NULL || !sess->connected) return -1;

#ifdef CONFIG_CRYPTO_WOLFSSH
  if (sess->ssh)
    {
      return wolfSSH_stream_read(sess->ssh, (uint8_t *)buf, len);
    }
#endif

  /* Fallback: raw TCP recv with non-blocking */

  return recv(sess->sock_fd, buf, len, MSG_DONTWAIT);
}

/**
 * Disconnect and free the SSH session.
 */
void ssh_disconnect(ssh_session_t *sess)
{
  if (sess == NULL) return;

#ifdef CONFIG_CRYPTO_WOLFSSH
  if (sess->ssh)
    {
      wolfSSH_shutdown(sess->ssh);
      wolfSSH_free(sess->ssh);
    }

  if (sess->ctx)
    {
      wolfSSH_CTX_free(sess->ctx);
    }
#endif

  if (sess->sock_fd >= 0) close(sess->sock_fd);

  syslog(LOG_INFO, "SSH: Disconnected from %s\n", sess->host);
  pc_app_psram_free(sess);
}

/**
 * Cleanup the SSH subsystem.
 */
void ssh_client_cleanup(void)
{
#ifdef CONFIG_CRYPTO_WOLFSSH
  wolfSSH_Cleanup();
#endif
}
