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

#ifdef CONFIG_NET
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <netutils/netlib.h>
#  ifdef CONFIG_NETUTILS_DHCPC
#    include <netutils/dhcpc.h>
#  endif
#endif

#ifdef CONFIG_WIRELESS_WAPI
#  include <wireless/wapi.h>
#endif

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

#if defined(CONFIG_NET) && defined(CONFIG_WIRELESS_WAPI)

#define WIFI_IFNAME "wlan0"

static char g_wifi_ip[INET_ADDRSTRLEN] = "0.0.0.0";

static void rp23xx_wifi_refresh_ip(void)
{
  struct in_addr addr;

  if (netlib_get_ipv4addr(WIFI_IFNAME, &addr) < 0 || addr.s_addr == 0)
    {
      strlcpy(g_wifi_ip, "0.0.0.0", sizeof(g_wifi_ip));
      return;
    }

  if (inet_ntop(AF_INET, &addr, g_wifi_ip, sizeof(g_wifi_ip)) == NULL)
    {
      strlcpy(g_wifi_ip, "0.0.0.0", sizeof(g_wifi_ip));
    }
}

#ifdef CONFIG_NETUTILS_DHCPC
static int rp23xx_wifi_dhcp_request(void)
{
  uint8_t mac[6];
  struct dhcpc_state ds;
  void *dhcpc;
  int ret;

  ret = netlib_getmacaddr(WIFI_IFNAME, mac);
  if (ret < 0)
    {
      return ret;
    }

  dhcpc = dhcpc_open(WIFI_IFNAME, mac, sizeof(mac));
  if (dhcpc == NULL)
    {
      return -ENOMEM;
    }

  memset(&ds, 0, sizeof(ds));
  ret = dhcpc_request(dhcpc, &ds);
  if (ret >= 0)
    {
      (void)netlib_set_ipv4addr(WIFI_IFNAME, &ds.ipaddr);
      (void)netlib_set_ipv4netmask(WIFI_IFNAME, &ds.netmask);
      (void)netlib_set_dripv4addr(WIFI_IFNAME, &ds.default_router);
      if (ds.num_dnsaddr > 0)
        {
          (void)netlib_set_ipv4dnsaddr(&ds.dnsaddr[0]);
        }
    }

  dhcpc_close(dhcpc);
  return ret;
}
#endif

int rp23xx_wifi_scan(wifi_scan_result_t *results, int max_results)
{
  struct wapi_list_s aps;
  struct wapi_scan_info_s *ap;
  int count = 0;
  int sock;
  int ret;
  int i;

  if (results == NULL || max_results <= 0)
    {
      return -EINVAL;
    }

  memset(&aps, 0, sizeof(aps));

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    {
      return -errno;
    }

  (void)netlib_ifup(WIFI_IFNAME);

  ret = wapi_scan_init(sock, WIFI_IFNAME, NULL);
  if (ret < 0)
    {
      close(sock);
      return ret;
    }

  for (i = 0; i < 40; i++)
    {
      ret = wapi_scan_stat(sock, WIFI_IFNAME);
      if (ret == 0)
        {
          break;
        }

      if (ret < 0)
        {
          close(sock);
          return ret;
        }

      usleep(100 * 1000);
    }

  ret = wapi_scan_coll(sock, WIFI_IFNAME, &aps);
  close(sock);
  if (ret < 0)
    {
      return ret;
    }

  ap = aps.head.scan;
  while (ap != NULL && count < max_results)
    {
      if (ap->has_essid && ap->essid[0] != '\0')
        {
          strlcpy(results[count].ssid, ap->essid, sizeof(results[count].ssid));
          results[count].rssi = ap->has_rssi ? ap->rssi : -100;
          results[count].auth = (ap->has_encode &&
                                (ap->encode & IW_ENCODE_DISABLED)) ?
                                0 : 2;
          count++;
        }

      ap = ap->next;
    }

  wapi_scan_coll_free(&aps);
  return count;
}

int rp23xx_wifi_connect(const char *ssid, const char *passphrase)
{
  int sock;
  int ret;

  if (ssid == NULL || ssid[0] == '\0')
    {
      return -EINVAL;
    }

  (void)netlib_ifup(WIFI_IFNAME);

  if (passphrase != NULL && passphrase[0] != '\0')
    {
      struct wpa_wconfig_s cfg;

      memset(&cfg, 0, sizeof(cfg));
      cfg.sta_mode = WAPI_MODE_MANAGED;
      cfg.auth_wpa = IW_AUTH_WPA_VERSION_WPA2;
      cfg.cipher_mode = IW_AUTH_CIPHER_CCMP;
      cfg.alg = WPA_ALG_CCMP;
      cfg.flag = WAPI_FREQ_AUTO;
      cfg.ifname = WIFI_IFNAME;
      cfg.ssid = ssid;
      cfg.passphrase = passphrase;
      cfg.ssidlen = strnlen(ssid, 32);
      cfg.phraselen = strnlen(passphrase, 63);

      ret = wpa_driver_wext_associate(&cfg);
      if (ret < 0)
        {
          return ret;
        }
    }
  else
    {
      sock = socket(AF_INET, SOCK_DGRAM, 0);
      if (sock < 0)
        {
          return -errno;
        }

      ret = wapi_set_mode(sock, WIFI_IFNAME, WAPI_MODE_MANAGED);
      if (ret >= 0)
        {
          ret = wapi_set_essid(sock, WIFI_IFNAME, ssid, WAPI_ESSID_ON);
        }

      close(sock);
      if (ret < 0)
        {
          return ret;
        }
    }

#ifdef CONFIG_NETUTILS_DHCPC
  (void)rp23xx_wifi_dhcp_request();
#endif

  rp23xx_wifi_refresh_ip();
  return 0;
}

int rp23xx_wifi_status(void)
{
  char essid[WAPI_ESSID_MAX_SIZE + 1];
  enum wapi_essid_flag_e flag;
  int sock;
  int ret;

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    {
      return -errno;
    }

  memset(essid, 0, sizeof(essid));
  ret = wapi_get_essid(sock, WIFI_IFNAME, essid, &flag);
  close(sock);
  if (ret < 0)
    {
      return ret;
    }

  if (flag != WAPI_ESSID_OFF && essid[0] != '\0')
    {
      rp23xx_wifi_refresh_ip();
      return 1;
    }

  strlcpy(g_wifi_ip, "0.0.0.0", sizeof(g_wifi_ip));
  return 0;
}

const char *rp23xx_wifi_ip(void)
{
  rp23xx_wifi_refresh_ip();
  return g_wifi_ip;
}

int rp23xx_wifi_disconnect(void)
{
  int sock;

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    {
      return -errno;
    }

  wpa_driver_wext_disconnect(sock, WIFI_IFNAME);
  close(sock);

  strlcpy(g_wifi_ip, "0.0.0.0", sizeof(g_wifi_ip));
  return 0;
}

#else

int rp23xx_wifi_scan(wifi_scan_result_t *results, int max_results)
{
  (void)results;
  (void)max_results;
  syslog(LOG_WARNING, "stub: rp23xx_wifi_scan\n");
  return 0;
}

int rp23xx_wifi_connect(const char *ssid, const char *passphrase)
{
  (void)ssid;
  (void)passphrase;
  syslog(LOG_WARNING, "stub: rp23xx_wifi_connect\n");
  return -ENOSYS;
}

int rp23xx_wifi_status(void)
{
  return 0;
}

const char *rp23xx_wifi_ip(void)
{
  return "0.0.0.0";
}

int rp23xx_wifi_disconnect(void)
{
  syslog(LOG_WARNING, "stub: rp23xx_wifi_disconnect\n");
  return -ENOSYS;
}

#endif

/* ======================================================================= */
/*  SPI callback stubs                                                     */
/*  These are now provided by the board library (rp23xx_sdcard.c).         */
/*  Only provide weak fallbacks if the board library doesn't include them. */
/* ======================================================================= */

#ifdef CONFIG_SPI_CALLBACK
__attribute__((weak))
int rp23xx_spi0register(struct spi_dev_s *dev,
                        spi_mediachange_t callback, void *arg)
{
  (void)dev; (void)callback; (void)arg;
  return -ENOSYS;
}

__attribute__((weak))
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
