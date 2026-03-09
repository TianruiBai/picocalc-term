/****************************************************************************
 * apps/pcweb/pcweb_http.c
 *
 * HTTP/HTTPS client for the web browser.
 * Minimal GET-only client using NuttX sockets + wolfSSL/mbedTLS for HTTPS.
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
#include <unistd.h>

#ifdef CONFIG_CRYPTO_WOLFSSL
#  include <wolfssl/ssl.h>
#endif

#include "pcterm/app.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define HTTP_MAX_HEADER    4096
#define HTTP_RECV_BUF      1024
#define HTTP_MAX_REDIRECTS 5
#define HTTP_TIMEOUT_SEC   10

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef struct http_response_s
{
  int      status_code;
  char     content_type[64];
  char    *body;            /* PSRAM-allocated */
  size_t   body_len;
  char     redirect_url[256];
} http_response_t;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int parse_url(const char *url, char *host, size_t host_len,
                     char *path, size_t path_len,
                     uint16_t *port, bool *is_https)
{
  *is_https = false;
  *port = 80;

  if (strncmp(url, "https://", 8) == 0)
    {
      *is_https = true;
      *port = 443;
      url += 8;
    }
  else if (strncmp(url, "http://", 7) == 0)
    {
      url += 7;
    }

  const char *slash = strchr(url, '/');
  const char *colon = strchr(url, ':');

  if (colon && (!slash || colon < slash))
    {
      size_t hlen = colon - url;
      if (hlen >= host_len) hlen = host_len - 1;
      memcpy(host, url, hlen);
      host[hlen] = '\0';
      *port = (uint16_t)atoi(colon + 1);
    }
  else if (slash)
    {
      size_t hlen = slash - url;
      if (hlen >= host_len) hlen = host_len - 1;
      memcpy(host, url, hlen);
      host[hlen] = '\0';
    }
  else
    {
      strncpy(host, url, host_len - 1);
      host[host_len - 1] = '\0';
    }

  if (slash)
    {
      strncpy(path, slash, path_len - 1);
      path[path_len - 1] = '\0';
    }
  else
    {
      strncpy(path, "/", path_len - 1);
    }

  return 0;
}

static int parse_headers(const char *header_buf, http_response_t *resp)
{
  /* Status line: HTTP/1.x NNN ... */

  const char *space = strchr(header_buf, ' ');
  if (space)
    {
      resp->status_code = atoi(space + 1);
    }

  /* Content-Type */

  const char *ct = strstr(header_buf, "Content-Type: ");
  if (ct == NULL) ct = strstr(header_buf, "content-type: ");

  if (ct)
    {
      ct += 14;
      const char *eol = strstr(ct, "\r\n");
      size_t len = eol ? (size_t)(eol - ct) : strlen(ct);
      if (len >= sizeof(resp->content_type))
        len = sizeof(resp->content_type) - 1;
      memcpy(resp->content_type, ct, len);
      resp->content_type[len] = '\0';
    }

  /* Location (for redirects) */

  const char *loc = strstr(header_buf, "Location: ");
  if (loc == NULL) loc = strstr(header_buf, "location: ");

  if (loc)
    {
      loc += 10;
      const char *eol = strstr(loc, "\r\n");
      size_t len = eol ? (size_t)(eol - loc) : strlen(loc);
      if (len >= sizeof(resp->redirect_url))
        len = sizeof(resp->redirect_url) - 1;
      memcpy(resp->redirect_url, loc, len);
      resp->redirect_url[len] = '\0';
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * Perform an HTTP GET request.
 *
 * @param url   Full URL (http:// or https://)
 * @param resp  Output response struct
 * @return 0 on success
 */
int http_get(const char *url, http_response_t *resp)
{
  char host[128], path[256];
  uint16_t port;
  bool is_https;
  int sock;
  struct addrinfo hints, *res;
  int ret;

  memset(resp, 0, sizeof(http_response_t));

  parse_url(url, host, sizeof(host), path, sizeof(path),
            &port, &is_https);

  syslog(LOG_INFO, "HTTP: GET %s%s:%d%s\n",
         is_https ? "https://" : "http://", host, port, path);

  /* DNS resolve */

  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  char port_str[8];
  snprintf(port_str, sizeof(port_str), "%d", port);

  ret = getaddrinfo(host, port_str, &hints, &res);
  if (ret != 0)
    {
      syslog(LOG_ERR, "HTTP: DNS failed: %d\n", ret);
      return -1;
    }

  /* TCP connect */

  sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sock < 0)
    {
      freeaddrinfo(res);
      return -1;
    }

  /* Set timeout */

  struct timeval tv;
  tv.tv_sec  = HTTP_TIMEOUT_SEC;
  tv.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  if (connect(sock, res->ai_addr, res->ai_addrlen) < 0)
    {
      syslog(LOG_ERR, "HTTP: Connect failed\n");
      close(sock);
      freeaddrinfo(res);
      return -1;
    }

  freeaddrinfo(res);

  /* TLS handshake for HTTPS */

#ifdef CONFIG_CRYPTO_WOLFSSL
  WOLFSSL_CTX *ssl_ctx = NULL;
  WOLFSSL     *ssl     = NULL;
#endif

  if (is_https)
    {
#ifdef CONFIG_CRYPTO_WOLFSSL
      ssl_ctx = wolfSSL_CTX_new(wolfTLSv1_2_client_method());
      if (ssl_ctx == NULL)
        {
          syslog(LOG_ERR, "HTTP: wolfSSL_CTX_new failed\n");
          close(sock);
          return -1;
        }

      /* Don't verify server cert (no CA store on device) */

      wolfSSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, NULL);

      ssl = wolfSSL_new(ssl_ctx);
      if (ssl == NULL)
        {
          syslog(LOG_ERR, "HTTP: wolfSSL_new failed\n");
          wolfSSL_CTX_free(ssl_ctx);
          close(sock);
          return -1;
        }

      wolfSSL_set_fd(ssl, sock);

      ret = wolfSSL_connect(ssl);
      if (ret != SSL_SUCCESS)
        {
          int err = wolfSSL_get_error(ssl, ret);
          syslog(LOG_ERR, "HTTP: TLS handshake failed: %d\n", err);
          wolfSSL_free(ssl);
          wolfSSL_CTX_free(ssl_ctx);
          close(sock);
          return -1;
        }

      syslog(LOG_INFO, "HTTP: TLS connected to %s\n", host);
#else
      syslog(LOG_WARNING, "HTTP: HTTPS not available (wolfSSL disabled)\n");
      close(sock);
      return -1;
#endif
    }

  /* Send HTTP GET request */

  char request[512];
  int req_len = snprintf(request, sizeof(request),
    "GET %s HTTP/1.0\r\n"
    "Host: %s\r\n"
    "User-Agent: PicoCalc-Term/0.1\r\n"
    "Accept: text/html, text/plain, image/bmp, image/png, image/*\r\n"
    "Connection: close\r\n"
    "\r\n",
    path, host);

#ifdef CONFIG_CRYPTO_WOLFSSL
  if (is_https && ssl)
    {
      wolfSSL_write(ssl, request, req_len);
    }
  else
#endif
    {
      send(sock, request, req_len, 0);
    }

  /* Receive response */

  size_t buf_size  = 32768;  /* Start with 32KB */
  char *buf = (char *)pc_app_psram_alloc(buf_size);
  if (buf == NULL)
    {
#ifdef CONFIG_CRYPTO_WOLFSSL
      if (ssl) { wolfSSL_free(ssl); wolfSSL_CTX_free(ssl_ctx); }
#endif
      close(sock);
      return -1;
    }

  size_t total = 0;

  while (1)
    {
      int n;

#ifdef CONFIG_CRYPTO_WOLFSSL
      if (is_https && ssl)
        {
          n = wolfSSL_read(ssl, buf + total, buf_size - total - 1);
        }
      else
#endif
        {
          n = recv(sock, buf + total, buf_size - total - 1, 0);
        }

      if (n <= 0) break;
      total += n;

      /* Grow buffer if needed */

      if (total > buf_size - HTTP_RECV_BUF)
        {
          size_t new_size = buf_size * 2;
          if (new_size > 512 * 1024) break;  /* 512KB limit */

          char *new_buf = (char *)pc_app_psram_alloc(new_size);
          if (new_buf == NULL) break;

          memcpy(new_buf, buf, total);
          pc_app_psram_free(buf);
          buf = new_buf;
          buf_size = new_size;
        }
    }

#ifdef CONFIG_CRYPTO_WOLFSSL
  if (ssl)
    {
      wolfSSL_shutdown(ssl);
      wolfSSL_free(ssl);
      wolfSSL_CTX_free(ssl_ctx);
    }
#endif

  close(sock);
  buf[total] = '\0';

  /* Split headers and body */

  char *body_start = strstr(buf, "\r\n\r\n");
  if (body_start)
    {
      *body_start = '\0';
      parse_headers(buf, resp);
      body_start += 4;

      resp->body_len = total - (body_start - buf);
      resp->body = (char *)pc_app_psram_alloc(resp->body_len + 1);
      if (resp->body)
        {
          memcpy(resp->body, body_start, resp->body_len);
          resp->body[resp->body_len] = '\0';
        }
    }

  pc_app_psram_free(buf);

  syslog(LOG_INFO, "HTTP: Response %d, %zu bytes, type=%s\n",
         resp->status_code, resp->body_len, resp->content_type);

  return 0;
}

/**
 * Free response body.
 */
void http_response_free(http_response_t *resp)
{
  if (resp->body)
    {
      pc_app_psram_free(resp->body);
      resp->body = NULL;
      resp->body_len = 0;
    }
}
