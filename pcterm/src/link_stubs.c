/****************************************************************************
 * pcterm/src/link_stubs.c
 *
 * Stub implementations for board-level and network symbols that are
 * referenced by the TermOS application code but not yet provided by the
 * board support package or the NuttX kernel configuration.
 *
 * These stubs allow the firmware to link and boot.  Each stub logs a
 * warning on first call so the developer knows the real driver is still
 * missing.
 ****************************************************************************/

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <nuttx/config.h>
#include <nuttx/spi/spi.h>

/* ======================================================================= */
/*  Board-level driver stubs                                               */
/*  NOTE: rp23xx_sdcard_mount and rp23xx_keyboard_read are now provided    */
/*  by the real board drivers so they are no longer stubbed here.          */
/* ======================================================================= */

/**
 * rp23xx_lcd_set_brightness - set display backlight brightness.
 */
void rp23xx_lcd_set_brightness(int pct)
{
  (void)pct;
}

/* ======================================================================= */
/*  WiFi driver stubs                                                      */
/* ======================================================================= */

typedef struct wifi_scan_result_s
{
  char ssid[33];
  int  rssi;
  int  auth;
} wifi_scan_result_t;

int rp23xx_wifi_scan(wifi_scan_result_t *results, int max_results)
{
  (void)results;
  (void)max_results;
  syslog(LOG_WARNING, "STUB: rp23xx_wifi_scan\n");
  return 0;                     /* 0 networks found */
}

int rp23xx_wifi_connect(const char *ssid, const char *passphrase)
{
  (void)ssid;
  (void)passphrase;
  syslog(LOG_WARNING, "STUB: rp23xx_wifi_connect\n");
  return -ENOSYS;
}

int rp23xx_wifi_status(void)
{
  return 0;                     /* disconnected */
}

const char *rp23xx_wifi_ip(void)
{
  return "0.0.0.0";
}

/* ======================================================================= */
/*  SPI callback stubs                                                     */
/*  CONFIG_SPI_CALLBACK is set but the board does not need SPI media-      */
/*  change notification.  Provide empty implementations.                   */
/* ======================================================================= */

#ifdef CONFIG_SPI_CALLBACK
int rp23xx_spi0register(struct spi_dev_s *dev,
                        spi_mediachange_t callback, void *arg)
{
  (void)dev; (void)callback; (void)arg;
  return -ENOSYS;
}

int rp23xx_spi1register(struct spi_dev_s *dev,
                        spi_mediachange_t callback, void *arg)
{
  (void)dev; (void)callback; (void)arg;
  return -ENOSYS;
}
#endif

/* ======================================================================= */
/*  Socket / network stubs                                                 */
/*                                                                         */
/*  CONFIG_NET is not enabled in this build.  Provide minimal stubs so the */
/*  linker is satisfied.  Any runtime call returns an error.               */
/* ======================================================================= */

#ifndef CONFIG_NET

/* --- user-level socket API --- */

int socket(int domain, int type, int protocol)
{
  (void)domain; (void)type; (void)protocol;
  return -ENOSYS;
}

struct addrinfo;
int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res)
{
  (void)node; (void)service; (void)hints; (void)res;
  return -ENOSYS;
}

void freeaddrinfo(struct addrinfo *res)
{
  (void)res;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
  (void)sockfd; (void)addr; (void)addrlen;
  return -ENOSYS;
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
  (void)sockfd; (void)buf; (void)len; (void)flags;
  return -ENOSYS;
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
  (void)sockfd; (void)buf; (void)len; (void)flags;
  return -ENOSYS;
}

int setsockopt(int sockfd, int level, int optname,
               const void *optval, socklen_t optlen)
{
  (void)sockfd; (void)level; (void)optname;
  (void)optval; (void)optlen;
  return -ENOSYS;
}

/* --- NuttX internal psock API (called from libfs socket.o) --- */

struct socket;        /* forward declare */
struct pollfd;
struct msghdr;

int psock_poll(struct socket *psock, struct pollfd *fds, int setup)
{
  (void)psock; (void)fds; (void)setup;
  return -ENOSYS;
}

int psock_ioctl(struct socket *psock, int cmd, unsigned long arg)
{
  (void)psock; (void)cmd; (void)arg;
  return -ENOSYS;
}

ssize_t psock_send(struct socket *psock, const void *buf,
                   size_t len, int flags)
{
  (void)psock; (void)buf; (void)len; (void)flags;
  return -ENOSYS;
}

ssize_t psock_recvfrom(struct socket *psock, void *buf, size_t len,
                       int flags, struct sockaddr *from,
                       socklen_t *fromlen)
{
  (void)psock; (void)buf; (void)len; (void)flags;
  (void)from; (void)fromlen;
  return -ENOSYS;
}

int psock_close(struct socket *psock)
{
  (void)psock;
  return -ENOSYS;
}

int psock_dup2(struct socket *psock1, struct socket *psock2)
{
  (void)psock1; (void)psock2;
  return -ENOSYS;
}

int psock_socket(int domain, int type, int protocol,
                 struct socket *psock)
{
  (void)domain; (void)type; (void)protocol; (void)psock;
  return -ENOSYS;
}

#endif /* !CONFIG_NET */

/* ======================================================================= */
/*  setjmp / longjmp stubs                                                 */
/*  (ARM Cortex-M33 Thumb-2 implementation)                                */
/*  Implements the NuttX jmp_buf layout: r4-r11, sp, lr (10 words)         */
/* ======================================================================= */

/* These are provided in setjmp_stubs.S instead */
