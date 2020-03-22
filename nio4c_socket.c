/*
 *  nio4c_socket.c
 *
 *  copyright (c) 2019 Xiongfei Shi
 *
 *  author: Xiongfei Shi <jenson.shixf(a)gmail.com>
 *  license: Apache-2.0
 *
 *  https://github.com/shixiongfei/nio4c
 */

#include "nio4c.h"
#include "internal.h"

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IOEthernetInterface.h>
#include <IOKit/network/IONetworkInterface.h>

#define TCP_KEEPIDLE TCP_KEEPALIVE
#endif

#if defined(__linux__)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <net/ethernet.h>
#include <net/if_arp.h>
#endif

#ifdef _WIN32
#include <stdio.h>
#include <mstcpip.h>
#include <process.h>

#ifdef _MSC_VER
static const struct in6_addr in6addr_any = { {{0}} };
#endif

#define socklen_t int
#define socket_errno() WSAGetLastError()

#define SOCKERR_EAGAIN WSAEWOULDBLOCK
#define SOCKERR_EINPROGRESS WSAEINPROGRESS
#else
#define SOCKET int
#define socket_errno() errno

#define SOCKERR_EAGAIN EAGAIN
#define SOCKERR_EINPROGRESS EINPROGRESS
#endif

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 64
#endif

#ifdef _MSC_VER
#define strncasecmp _strnicmp
#endif

#ifndef IPV6_ADD_MEMBERSHIP
#ifdef IPV6_JOIN_GROUP
#define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
#endif
#endif

#ifndef IPV6_DROP_MEMBERSHIP
#ifdef IPV6_LEAVE_GROUP
#define IPV6_DROP_MEMBERSHIP IPV6_LEAVE_GROUP
#endif
#endif

const char *nio_gethostname(void) {
  static char buffer[MAXHOSTNAMELEN] = { 0 };

  if (!buffer[0]) {
    if (0 != gethostname(buffer, sizeof(buffer))) {
      strcpy(buffer, "localhost");
    }
  }
  return buffer;
}

#if defined(__APPLE__)
static kern_return_t find_ethernet_interfaces(io_iterator_t *matchingServices,
  int primary_only) {
  CFMutableDictionaryRef matchingDict;

  matchingDict = IOServiceMatching(kIOEthernetInterfaceClass);

  if (primary_only && (NULL != matchingDict)) {
    CFMutableDictionaryRef propertyMatchDict;

    propertyMatchDict = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);

    if (NULL != propertyMatchDict) {
      CFDictionarySetValue(propertyMatchDict, CFSTR(kIOPrimaryInterface),
        kCFBooleanTrue);
      CFDictionarySetValue(matchingDict, CFSTR(kIOPropertyMatchKey),
        propertyMatchDict);
      CFRelease(propertyMatchDict);
    }
  }

  return IOServiceGetMatchingServices(kIOMasterPortDefault, matchingDict,
    matchingServices);
}

int nio_gethwaddr(niohwaddr_t *hwaddr, int count) {
  io_iterator_t intfIterator;
  io_object_t intfService;
  io_object_t controllerService;
  int num = 0;

  if (KERN_SUCCESS != find_ethernet_interfaces(&intfIterator, 1))
    if (KERN_SUCCESS != find_ethernet_interfaces(&intfIterator, 0))
      return 0;

  while ((num < count) && !!(intfService = IOIteratorNext(intfIterator))) {
    CFTypeRef MACAddressAsCFData;

    if (KERN_SUCCESS != IORegistryEntryGetParentEntry(
      intfService, kIOServicePlane, &controllerService))
      continue;

    MACAddressAsCFData = IORegistryEntryCreateCFProperty(
      controllerService, CFSTR(kIOMACAddress), kCFAllocatorDefault, 0);

    if (MACAddressAsCFData) {
      CFDataGetBytes(MACAddressAsCFData, CFRangeMake(0, kIOEthernetAddressSize),
        hwaddr[num++].hwaddr);
      CFRelease(MACAddressAsCFData);
    }

    IOObjectRelease(controllerService);
  }

  IOObjectRelease(intfIterator);

  return num;
}
#elif defined(_WIN32)
static int is_physical_adapter(const char *name) {
  char buffer[256] = { 0 };
  DWORD dwType = REG_SZ, dwDataLen;
  HKEY hNetKey = NULL;

  sprintf(buffer,
    "SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-"
    "BFC1-08002BE10318}\\%s\\Connection",
    name);

  if (ERROR_SUCCESS !=
    RegOpenKeyExA(HKEY_LOCAL_MACHINE, buffer, 0, KEY_READ, &hNetKey))
    return 0;

  dwDataLen = sizeof(buffer);

  if (ERROR_SUCCESS == RegQueryValueExA(hNetKey, "MediaSubType", 0, &dwType,
    (LPBYTE)buffer, &dwDataLen)) {
    DWORD dwMediaSubType = *(DWORD *)buffer;

    /* 0x01 local network, 0x02 wifi network. */
    if ((0x01 != dwMediaSubType) && (0x02 != dwMediaSubType)) {
      RegCloseKey(hNetKey);
      return 0;
    }
  }

  dwDataLen = sizeof(buffer);

  if (ERROR_SUCCESS != RegQueryValueExA(hNetKey, "PnpInstanceID", 0, &dwType,
    (LPBYTE)buffer, &dwDataLen)) {
    RegCloseKey(hNetKey);
    return 0;
  }

  RegCloseKey(hNetKey);

  return 0 == strncasecmp(buffer, "PCI", 3);
}

int nio_gethwaddr(niohwaddr_t *hwaddr, int count) {
  IP_ADAPTER_ADDRESSES *pAdapterAddr = NULL;
  ULONG ulOutBufLen = 0;
  ULONG ulFlags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
    GAA_FLAG_SKIP_DNS_SERVER;
  int num = 0;

  if (ERROR_BUFFER_OVERFLOW == GetAdaptersAddresses(AF_UNSPEC, ulFlags, NULL,
    pAdapterAddr,
    &ulOutBufLen)) {
    pAdapterAddr = (IP_ADAPTER_ADDRESSES *)nio_malloc(ulOutBufLen);
    if (!pAdapterAddr)
      return -1;
  }
  if (NO_ERROR == GetAdaptersAddresses(AF_UNSPEC, ulFlags, NULL, pAdapterAddr,
    &ulOutBufLen)) {
    IP_ADAPTER_ADDRESSES *pAdapter;

    for (pAdapter = pAdapterAddr; (NULL != pAdapter) && (num < count);
      pAdapter = pAdapter->Next) {
      MIB_IFROW mib;

      memset(&mib, 0, sizeof(MIB_IFROW));

      if (pAdapter->IfIndex > 0)
        mib.dwIndex = pAdapter->IfIndex;
      else if (pAdapter->Ipv6IfIndex > 0)
        mib.dwIndex = pAdapter->Ipv6IfIndex;
      else
        continue;

      if (NO_ERROR != GetIfEntry(&mib))
        continue;

      if ((IF_TYPE_ETHERNET_CSMACD == pAdapter->IfType) ||
        (IF_TYPE_IEEE80211 == pAdapter->IfType))
        if (is_physical_adapter(pAdapter->AdapterName))
          memcpy(hwaddr[num++].hwaddr, pAdapter->PhysicalAddress,
            pAdapter->PhysicalAddressLength);
    }
  }
  if (pAdapterAddr)
    nio_free(pAdapterAddr);

  return num;
}
#else
int nio_gethwaddr(niohwaddr_t *hwaddr, int count) {
  struct ifaddrs *ifaddr;
  struct ifaddrs *ifa;
  int num = 0;

  if (0 != getifaddrs(&ifaddr))
    return 0;

  for (ifa = ifaddr; (NULL != ifa) && (num < count); ifa = ifa->ifa_next) {
    if ((ifa->ifa_addr) && (AF_LINK == ifa->ifa_addr->sa_family)) {
      struct ifreq ifr;
      int fd = socket(AF_INET, SOCK_DGRAM, 0);
#ifdef __linux__
      strcpy(ifr.ifr_name, ifa->ifa_name);
      ioctl(fd, SIOCGIFHWADDR, &ifr);
      close(fd);

      if ((ARPHRD_ETHER == ifr.ifr_hwaddr.sa_family) ||
        (ARPHRD_IEEE80211 == ifr.ifr_hwaddr.sa_family)) {
        int hwlen = ETHER_ADDR_LEN;
        memcpy(hwaddr[num++].hwaddr, ifr.ifr_hwaddr.sa_data, hwlen);
      }
#else
      struct ifmediareq ifmed;

      memset(&ifmed, 0, sizeof(struct ifmediareq));
      strcpy(ifmed.ifm_name, ifa->ifa_name);
      ioctl(fd, SIOCGIFMEDIA, (caddr_t)&ifmed);
      close(fd);

      if ((IFM_ETHER == IFM_TYPE(ifmed.ifm_active)) ||
        (IFM_IEEE80211 == IFM_TYPE(ifmed.ifm_active))) {
        struct sockaddr_dl *sdl = (struct sockaddr_dl *)ifa->ifa_addr;

        if (S_HWADDR_LEN == sdl->sdl_alen)
          memcpy(hwaddr[num++].hwaddr, LLADDR(sdl), sdl->sdl_alen);
      }
#endif
    }
  }

  freeifaddrs(ifaddr);

  return num;
}
#endif

int nio_resolvehost(niosockaddr_t *addr_list, int count, int af,
  const char *hostname, unsigned short port) {
  int retval = 0;

  if (hostname) {
    /* IP Address Resolve Host */
    struct addrinfo hints;
    struct addrinfo *servinfo = NULL;
    char port_str[8] = { 0 };

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    sprintf(port_str, "%d", port);

    if (0 == getaddrinfo(hostname, port_str, &hints, &servinfo)) {
      struct addrinfo *aip;

      for (aip = servinfo; (NULL != aip) && (retval < count);
        aip = aip->ai_next) {
        if ((AF_INET == af) && (AF_INET == aip->ai_family)) {
          struct sockaddr_in *ipv4 = (struct sockaddr_in *)aip->ai_addr;
          ipv4->sin_port = htons(port);
          memcpy(&addr_list[retval++].saddr, ipv4, sizeof(struct sockaddr_in));
        }
        if ((AF_INET6 == af) && (AF_INET6 == aip->ai_family)) {
          struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)aip->ai_addr;
          ipv6->sin6_port = htons(port);
          memcpy(&addr_list[retval++].saddr, ipv6, sizeof(struct sockaddr_in6));
        }
      }

      freeaddrinfo(servinfo);
    }
  }
  else {
    if (AF_INET == af) {
      /* IP Address Any IPv4 */
      struct sockaddr_in addr;

      memset(&addr, 0, sizeof(addr));

      addr.sin_family = AF_INET;
      addr.sin_port = htons(port);
      addr.sin_addr.s_addr = htonl(INADDR_ANY);

      memcpy(&addr_list[retval++].saddr, &addr, sizeof(struct sockaddr_in));
    }
    if (AF_INET6 == af) {
      /* IP Address Any IPv6 */
      struct sockaddr_in6 addr;

      memset(&addr, 0, sizeof(addr));

      addr.sin6_family = AF_INET6;
      addr.sin6_port = htons(port);
      addr.sin6_addr = in6addr_any;

      memcpy(&addr_list[retval++].saddr, &addr, sizeof(struct sockaddr_in6));
    }
  }

  return retval;
}

int nio_hostaddr(niosockaddr_t *addr, const char *hostname,
  unsigned short port) {
  if (nio_resolvehost(addr, 1, AF_INET, hostname, port) > 0)
    return 0;
  if (nio_resolvehost(addr, 1, AF_INET6, hostname, port) > 0)
    return 0;
  return -1;
}

/* addr is in_addr or in6_addr */
static const char *nio_inetntop(int af, const void *addr, char *strbuf,
  int strbuf_size) {
#ifndef _WIN32
  return inet_ntop(af, addr, strbuf, strbuf_size);
#else
  struct sockaddr_storage ss;
  unsigned long s = strbuf_size;
  wchar_t wch_addr[INET6_ADDRSTRLEN + 1] = { 0 };

  ZeroMemory(&ss, sizeof(ss));
  ss.ss_family = af;

  switch (af) {
  case AF_INET:
    ((struct sockaddr_in *)&ss)->sin_addr = *(struct in_addr *)addr;
    break;
  case AF_INET6:
    ((struct sockaddr_in6 *)&ss)->sin6_addr = *(struct in6_addr *)addr;
    break;
  default:
    return NULL;
  }

  /* cannot direclty use &size because of strict aliasing rules */
  if (0 != WSAAddressToStringW((struct sockaddr *)&ss, sizeof(ss), NULL,
    wch_addr, &s))
    return NULL;

  WideCharToMultiByte(CP_UTF8, 0, wch_addr, -1, strbuf, s, NULL, NULL);

  return strbuf;
#endif /* _WIN32 */
}

#if 0
static int nio_inetpton(int af, const char *strbuf, void *addr) {
#ifndef _WIN32
  return inet_pton(af, strbuf, addr);
#else
  struct sockaddr_storage ss;
  int size = sizeof(ss);
  wchar_t src_copy[INET6_ADDRSTRLEN + 1] = { 0 };

  ZeroMemory(&ss, sizeof(ss));

  MultiByteToWideChar(CP_UTF8, 0, strbuf, -1, src_copy, INET6_ADDRSTRLEN + 1);

  if (0 ==
    WSAStringToAddressW(src_copy, af, NULL, (struct sockaddr *)&ss, &size)) {
    switch (af) {
    case AF_INET:
      *(struct in_addr *)addr = ((struct sockaddr_in *)&ss)->sin_addr;
      return 1;
    case AF_INET6:
      *(struct in6_addr *)addr = ((struct sockaddr_in6 *)&ss)->sin6_addr;
      return 1;
    default:
      return -1;
    }
  }
  return 0;
#endif /* _WIN32 */
}
#endif

int nio_ipstr(nioipstr_t *ipstr, const niosockaddr_t *addr) {
  if (AF_INET == addr->saddr.ss_family) {
    struct sockaddr_in *ss = (struct sockaddr_in *)(&addr->saddr);

    nio_inetntop(AF_INET, &ss->sin_addr, ipstr->addr, sizeof(ipstr->addr));
    ipstr->port = ntohs(ss->sin_port);
  }
  else if (AF_INET6 == addr->saddr.ss_family) {
    struct sockaddr_in6 *ss = (struct sockaddr_in6 *)(&addr->saddr);

    nio_inetntop(AF_INET6, &ss->sin6_addr, ipstr->addr, sizeof(ipstr->addr));
    ipstr->port = ntohs(ss->sin6_port);
  }
  else
    return -1;
  return 0;
}

static unsigned int ipv4_submask(unsigned int prefixlen) {
  return htonl(~((1 << (32 - prefixlen)) - 1));
}

int nio_isnat(const niosockaddr_t *addr) {
  struct sockaddr_in *ss_addr = (struct sockaddr_in *)(&addr->saddr);
  /* Class A: 10.0.0.0 ~ 10.255.255.255 */
  if (htonl(0x0A000000) == (ss_addr->sin_addr.s_addr & ipv4_submask(8)))
    return 1;
  /* Class B: 172.16.0.0 ~ 172.31.255.255 */
  if (htonl(0xAC100000) == (ss_addr->sin_addr.s_addr & ipv4_submask(12)))
    return 1;
  /* Class C: 192.168.0.0 ~ 192.168.255.255 */
  if (htonl(0xC0A80000) == (ss_addr->sin_addr.s_addr & ipv4_submask(16)))
    return 1;
  /* Invalid Address: 169.254.0.0 ~ 169.254.255.255 */
  if (htonl(0xA9FE0000) == (ss_addr->sin_addr.s_addr & ipv4_submask(16)))
    return 1;
  return 0;
}

static int ipv4_masklen(struct sockaddr_in *s) {
  int len = 0;
  unsigned char *pnt = (unsigned char *)&s->sin_addr;
  unsigned char *end = pnt + 4;

  while ((pnt < end) && (0xff == (*pnt))) {
    len += 8;
    pnt++;
  }

  if (pnt < end) {
    unsigned char val = *pnt;

    while (val) {
      len += 1;
      val <<= 1;
    }
  }

  return len;
}

static int ipv6_masklen(struct sockaddr_in6 *s) {
  int len = 0;
  unsigned char *pnt = (unsigned char *)&s->sin6_addr;

  while ((0xff == (*pnt)) && (len < 128)) {
    len += 8;
    pnt++;
  }

  if (len < 128) {
    unsigned char val = *pnt;

    while (val) {
      len += 1;
      val <<= 1;
    }
  }

  return len;
}

int nio_ipmasklen(const niosockaddr_t *addr) {
  return (AF_INET == addr->saddr.ss_family)
    ? ipv4_masklen((struct sockaddr_in *)(&addr->saddr))
    : ipv6_masklen((struct sockaddr_in6 *)(&addr->saddr));
}

int nio_inprogress(void) {
  int errcode = socket_errno();
  return SOCKERR_EAGAIN == errcode || SOCKERR_EINPROGRESS == errcode;
}

static int nio_createsocket(niosocket_t *s, int af, int type, int protocol) {
  if (!s) return -1;

#ifndef _WIN32
  s->sockfd = socket(af, type, protocol);
#else
  s->sockfd = (int)WSASocket(af, type, protocol, NULL, 0, 0);
#endif
  return (INVALID_SOCKET != s->sockfd) ? 0 : -1;
}

int nio_createtcp(niosocket_t *s, int af) {
  if (AF_INET == af)
    return nio_createtcp4(s);
  if (AF_INET6 == af)
    return nio_createtcp6(s);
  return -1;
}

int nio_createtcp4(niosocket_t *s) {
  return nio_createsocket(s, AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

int nio_createtcp6(niosocket_t *s) {
  return nio_createsocket(s, AF_INET6, SOCK_STREAM, IPPROTO_TCP);
}

int nio_createudp(niosocket_t *s, int af) {
  if (AF_INET == af)
    return nio_createudp4(s);
  if (AF_INET6 == af)
    return nio_createudp6(s);
  return -1;
}

int nio_createudp4(niosocket_t *s) {
  return nio_createsocket(s, AF_INET, SOCK_DGRAM, IPPROTO_IP);
}

int nio_createudp6(niosocket_t *s) {
  return nio_createsocket(s, AF_INET6, SOCK_DGRAM, IPPROTO_IP);
}

void nio_destroysocket(niosocket_t *s) {
  if (!s || INVALID_SOCKET == s->sockfd)
    return;

#ifndef _WIN32
  close(s->sockfd);
#else
  closesocket(s->sockfd);
#endif
  s->sockfd = INVALID_SOCKET;
}

#define sockaddr_len(a)                                                        \
  ((NULL == (a)) ? 0                                                           \
  : (AF_INET == (a)->ss_family) ? sizeof(struct sockaddr_in)                   \
  : (AF_INET6 == (a)->ss_family) ? sizeof(struct sockaddr_in6) : 0)

int nio_bind(niosocket_t *s, const niosockaddr_t *addr) {
  return bind(s->sockfd, (struct sockaddr *)(&addr->saddr),
    sockaddr_len(&addr->saddr));
}

int nio_listen(niosocket_t *s, int backlog) {
  return listen(s->sockfd, backlog);
}

int nio_connect(niosocket_t *s, const niosockaddr_t *addr) {
#ifndef _WIN32
  return connect(s->sockfd, (struct sockaddr *)(&addr->saddr),
    sockaddr_len(&addr->saddr));
#else
  return WSAConnect(s->sockfd, (struct sockaddr *)(&addr->saddr),
    sockaddr_len(&addr->saddr), NULL, NULL, NULL, NULL);
#endif
}

int nio_accept(niosocket_t *s, niosocket_t *client, niosockaddr_t *addr) {
  struct sockaddr_storage c_addr;
  socklen_t ca_len = sizeof(c_addr);

#ifndef _WIN32
  client->sockfd = accept(s->sockfd, (struct sockaddr *)&c_addr, &ca_len);
#else
  client->sockfd =
    (int)WSAAccept(s->sockfd, (struct sockaddr *)&c_addr, &ca_len, NULL, 0);
#endif

  if (addr) memcpy(&addr->saddr, &c_addr, sizeof(c_addr));

  return (INVALID_SOCKET != client->sockfd) ? 0 : -1;
}

int nio_shutdown(niosocket_t *s, int how) {
  return shutdown(s->sockfd, how);
}

int nio_peeraddr(niosocket_t *s, niosockaddr_t *addr) {
  if (addr) {
    socklen_t size = sizeof(struct sockaddr_storage);
    return getpeername(s->sockfd, (struct sockaddr *)(&addr->saddr), &size);
  }
  return -1;
}

int nio_peeripstr(niosocket_t *s, nioipstr_t *addr) {
  niosockaddr_t taddr;

  if (nio_peeraddr(s, &taddr) < 0)
    return -1;
  return nio_ipstr(addr, &taddr);
}

int nio_sockaddr(niosocket_t *s, niosockaddr_t *addr) {
  if (addr) {
    socklen_t size = sizeof(struct sockaddr_storage);
    return getsockname(s->sockfd, (struct sockaddr *)(&addr->saddr), &size);
  }
  return -1;
}

int nio_sockipstr(niosocket_t *s, nioipstr_t *addr) {
  niosockaddr_t taddr;

  if (nio_sockaddr(s, &taddr) < 0)
    return -1;
  return nio_ipstr(addr, &taddr);
}

int nio_socketnonblock(niosocket_t *s, int on) {
#ifdef O_NONBLOCK
  int flags = fcntl(s->sockfd, F_GETFL, 0);
  if (on) {
    flags |= O_NONBLOCK;
  }
  else {
    flags &= ~O_NONBLOCK;
  }
  return 0 == fcntl(s->sockfd, F_SETFL, flags) ? 0 : -1;
#else
  unsigned long mode = (unsigned long)on;
  return 0 == nio_ioctlsocket(s->sockfd, FIONBIO, &mode) ? 0 : -1;
#endif
}

int nio_reuseaddr(niosocket_t *s, int on) {
  setsockopt(s->sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));
  return 0;
}

int nio_tcpnodelay(niosocket_t *s, int on) {
  setsockopt(s->sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&on, sizeof(on));
  return 0;
}

int nio_tcpkeepalive(niosocket_t *s, int on) {
  setsockopt(s->sockfd, SOL_SOCKET, SO_KEEPALIVE, (char *)&on, sizeof(on));
  return 0;
}

int nio_tcpkeepvalues(niosocket_t *s, int idle, int interval, int count) {
#ifdef _WIN32
  DWORD dw = 0;
  struct tcp_keepalive kl, ko;

  kl.onoff = 1;
  kl.keepalivetime = idle * 1000;
  kl.keepaliveinterval = interval * 1000;

  WSAIoctl(s->sockfd, SIO_KEEPALIVE_VALS, &kl, sizeof(kl), &ko, sizeof(ko), &dw,
    NULL, NULL);
#else
  setsockopt(s->sockfd, IPPROTO_TCP, TCP_KEEPIDLE, (char *)&idle, sizeof(idle));
  setsockopt(s->sockfd, IPPROTO_TCP, TCP_KEEPINTVL, (char *)&interval,
    sizeof(interval));
  setsockopt(s->sockfd, IPPROTO_TCP, TCP_KEEPCNT, (char *)&count,
    sizeof(count));
#endif
  return 0;
}

int nio_udpbroadcast(niosocket_t *s, int on) {
  setsockopt(s->sockfd, SOL_SOCKET, SO_BROADCAST, (char *)&on, sizeof(on));
  return 0;
}

int nio_socketreadable(niosocket_t *s, unsigned int timedout) {
  struct timeval tv = { 0, 0 };
  fd_set fds, fd_error;
  int retval;

  FD_ZERO(&fds);
  FD_ZERO(&fd_error);
  FD_SET(s->sockfd, &fds);
  FD_SET(s->sockfd, &fd_error);

  if (timedout > 0) {
    tv.tv_sec = (long)(timedout * 0.001);
    tv.tv_usec = (long)((timedout % 1000) * 1000);
  }

  retval = select(s->sockfd + 1, &fds, NULL, &fd_error, &tv);
  if (retval < 0) return -1;

  if (retval > 0) {
    if (FD_ISSET(s->sockfd, &fd_error))
      return -1;

    if (FD_ISSET(s->sockfd, &fds))
      return 1;
  }
  return 0;
}

int nio_socketwritable(niosocket_t *s, unsigned int timedout) {
  struct timeval tv = { 0, 0 };
  fd_set fds, fd_error;
  int retval;

  FD_ZERO(&fds);
  FD_ZERO(&fd_error);
  FD_SET(s->sockfd, &fds);
  FD_SET(s->sockfd, &fd_error);

  if (timedout > 0) {
    tv.tv_sec = (long)(timedout * 0.001);
    tv.tv_usec = (long)((timedout % 1000) * 1000);
  }

  retval = select(s->sockfd + 1, NULL, &fds, &fd_error, &tv);
  if (retval < 0) return -1;

  if (retval > 0) {
    if (FD_ISSET(s->sockfd, &fd_error))
      return -1;

    if (FD_ISSET(s->sockfd, &fds))
      return 1;
  }
  return 0;
}

int nio_send(niosocket_t *s, const void *buffer, int len) {
#ifndef _WIN32
  return send(s->sockfd, (const char *)buffer, len, 0);
#else
  DWORD num = 0;
  WSABUF wsa_buf = { (ULONG)len, (CHAR *)buffer };

  if (SOCKET_ERROR == WSASend(s->sockfd, &wsa_buf, 1, &num, 0, NULL, NULL))
    return -1;
  return num;
#endif
}

int nio_recv(niosocket_t *s, void *buffer, int len) {
#ifndef _WIN32
  return recv(s->sockfd, (char *)buffer, len, 0);
#else
  DWORD num = 0, flag = 0;
  WSABUF wsa_buf = { (ULONG)len, (CHAR *)buffer };

  if (SOCKET_ERROR == WSARecv(s->sockfd, &wsa_buf, 1, &num, &flag, NULL, NULL))
    return -1;
  return (int)num;
#endif
}

int nio_sendto(niosocket_t *s, const niosockaddr_t *addr,
  const void *buffer, int len) {
#ifndef _WIN32
  return sendto(s->sockfd, (const char *)buffer, len, 0,
    (struct sockaddr *)(&addr->saddr), sockaddr_len(&addr->saddr));
#else
  DWORD num = 0;
  WSABUF wsa_buf = { (ULONG)len, (CHAR *)buffer };

  if (SOCKET_ERROR == WSASendTo(s->sockfd, &wsa_buf, 1, &num, 0,
    (struct sockaddr *)(&addr->saddr), sockaddr_len(&addr->saddr), NULL, NULL))
    return -1;
  return (int)num;
#endif
}

int nio_recvfrom(niosocket_t *s, niosockaddr_t *addr, void *buffer, int len) {
  struct sockaddr_storage ss;
  socklen_t size = sizeof(ss);
  int num = 0;

#ifndef _WIN32
  num = recvfrom(s->sockfd, (char *)buffer, len, 0, (struct sockaddr *)&ss,
    &size);
#else
  DWORD flag = 0, rd_num = 0;
  WSABUF wsa_buf = { (ULONG)len, (CHAR *)buffer };

  if (SOCKET_ERROR == WSARecvFrom(s->sockfd, &wsa_buf, 1, &rd_num, &flag,
    (struct sockaddr *)&ss, &size, NULL, NULL))
    return -1;

  num = (int)rd_num;
#endif

  if (addr)
    memcpy(&addr->saddr, &ss, sizeof(ss));

  return num;
}

int nio_sendall(niosocket_t *s, const void *buffer, int len) {
  unsigned char *lptr = (unsigned char *)buffer;
  int sent = 0;
  int retval = 0;

  while (sent < len) {
    retval = nio_send(s, lptr, len - sent);

    if (retval >= 0) {
      sent += retval;
      lptr += retval;
    }
    else {
      if (!nio_inprogress())
        return -1;
    }

    if (sent != len)
      if (nio_socketwritable(s, UINT_MAX) < 0)
        return -1;
  }

  return sent;
}

int nio_recvall(niosocket_t *s, void *buffer, int len) {
  unsigned char *lptr = (unsigned char *)buffer;
  int total = 0;
  int retval = 0;

  while (total < len) {
    retval = nio_recv(s, lptr, len - total);

    if (retval > 0) {
      total += retval;
      lptr += retval;
    }
    else {
      if (0 == retval)
        return total; /* disconnected */
      else {
        if (!nio_inprogress())
          return -1;
      }
    }

    if (total != len)
      if (nio_socketreadable(s, UINT_MAX) < 0)
        return -1;
  }

  return total;
}

static void ip4_mreq(struct ip_mreq *mreq4,
  const struct sockaddr_storage *ipaddr) {
  struct sockaddr_in *ss_addr = (struct sockaddr_in *)ipaddr;

  memset(mreq4, 0, sizeof(struct ip_mreq));

  mreq4->imr_multiaddr.s_addr = ss_addr->sin_addr.s_addr;
  mreq4->imr_interface.s_addr = htonl(INADDR_ANY);
}

static void ip6_mreq(struct ipv6_mreq *mreq6,
  const struct sockaddr_storage *ipaddr) {
  struct sockaddr_in6 *ss_addr = (struct sockaddr_in6 *)ipaddr;

  memset(mreq6, 0, sizeof(struct ipv6_mreq));

  memcpy(&mreq6->ipv6mr_multiaddr, &ss_addr->sin6_addr,
    sizeof(struct in6_addr));
  mreq6->ipv6mr_interface = 0;
}

int nio_addmembership(niosocket_t *s, const niosockaddr_t *multiaddr) {
  if (AF_INET == multiaddr->saddr.ss_family) {
    struct ip_mreq mreq;

    ip4_mreq(&mreq, &multiaddr->saddr);
    setsockopt(s->sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq,
      sizeof(mreq));

    return 0;
  }

  if (AF_INET6 == multiaddr->saddr.ss_family) {
    struct ipv6_mreq mreq;

    ip6_mreq(&mreq, &multiaddr->saddr);
    setsockopt(s->sockfd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, (char *)&mreq,
      sizeof(mreq));

    return 0;
  }

  return -1;
}

int nio_dropmembership(niosocket_t *s, const niosockaddr_t *multiaddr) {
  if (AF_INET == multiaddr->saddr.ss_family) {
    struct ip_mreq mreq;

    ip4_mreq(&mreq, &multiaddr->saddr);
    setsockopt(s->sockfd, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char *)&mreq,
      sizeof(mreq));

    return 0;
  }

  if (AF_INET6 == multiaddr->saddr.ss_family) {
    struct ipv6_mreq mreq;

    ip6_mreq(&mreq, &multiaddr->saddr);
    setsockopt(s->sockfd, IPPROTO_IPV6, IPV6_DROP_MEMBERSHIP, (char *)&mreq,
      sizeof(mreq));

    return 0;
  }

  return -1;
}

int nio_multicastloop(niosocket_t *s, const niosockaddr_t *multiaddr, int on) {
  if (AF_INET == multiaddr->saddr.ss_family) {
    setsockopt(s->sockfd, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&on,
      sizeof(on));
    return 0;
  }

  if (AF_INET6 == multiaddr->saddr.ss_family) {
    setsockopt(s->sockfd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, (char *)&on,
      sizeof(on));
    return 0;
  }

  return -1;
}

int nio_pipe(niosocket_t socks[2]) {
#ifndef _WIN32
  SOCKET sv[2] = { INVALID_SOCKET };

  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
    return -1;

  socks[0].sockfd = sv[0];
  socks[1].sockfd = sv[1];

  return 0;
#else
  niosockaddr_t loopback;
  niosocket_t listener = { INVALID_SOCKET };

  nio_hostaddr(&loopback, "localhost", 0);
  nio_createtcp(&listener, loopback.saddr.ss_family);

  if (nio_bind(&listener, &loopback) < 0)
    goto pipe_error;

  nio_sockaddr(&listener, &loopback);

  if (nio_listen(&listener, 1) < 0)
    goto pipe_error;

  nio_createtcp(&socks[0], loopback.saddr.ss_family);

  if (nio_connect(&socks[0], &loopback))
    goto pipe_error;

  if (nio_accept(&listener, &socks[1], NULL) < 0)
    goto pipe_error;

  nio_destroysocket(&listener);

  return 0;

pipe_error:
  nio_destroysocket(&listener);
  nio_destroysocket(&socks[0]);
  nio_destroysocket(&socks[1]);
  return -1;
#endif
}

#ifndef _WIN32
int nio_popen(niosocket_t *s, const char *cmdline) {
  niosocket_t socks[2];
  pid_t pid;

  if (nio_pipe(socks) < 0)
    return -1;

  pid = fork();

  if (pid < 0)
    return -1;

  if (pid > 0) {
    nio_destroysocket(&socks[1]);
    nio_socketnonblock(&socks[0], 1);
    *s = socks[0];
    return pid;
  }

  if (0 == pid) {
    nio_destroysocket(&socks[0]);

    dup2(socks[1].sockfd, STDIN_FILENO);
    dup2(socks[1].sockfd, STDOUT_FILENO);
    dup2(socks[1].sockfd, STDERR_FILENO);

    nio_destroysocket(&socks[1]);

    execl("/bin/sh", "sh", "-c", cmdline, (char *)0);
    exit(0);
  }

  nio_destroysocket(&socks[0]);
  nio_destroysocket(&socks[1]);
  return -1;
}
#else
typedef struct PopenProc_s {
  PROCESS_INFORMATION proc_info;
  niosocket_t pipe_socket;
} PopenProc;

static unsigned int __stdcall popen_waitforprocess(void *arg) {
  PopenProc *proc_info = (PopenProc *)arg;
  DWORD wait_ret;

  do {
    wait_ret = WaitForSingleObject(proc_info->proc_info.hProcess, INFINITE);
  } while (WAIT_TIMEOUT == wait_ret);

  CloseHandle(proc_info->proc_info.hProcess);
  CloseHandle(proc_info->proc_info.hThread);

  nio_destroysocket(&proc_info->pipe_socket);
  nio_free(proc_info);

  return 0;
}

int nio_popen(niosocket_t *s, const char *cmdline) {
  niosocket_t socks[2];
  PopenProc *procinfo;
  STARTUPINFOA siStartInfo;
  char command[MAX_PATH] = { 0 };
  char *cmdexe;

  if (nio_pipe(socks) < 0)
    return -1;

  procinfo = (PopenProc *)nio_malloc(sizeof(PopenProc));

  if (!procinfo)
    return -1;

  cmdexe = getenv("COMSPEC");

  if (!cmdexe)
    cmdexe = "cmd.exe";

  ZeroMemory(&procinfo->proc_info, sizeof(PROCESS_INFORMATION));
  ZeroMemory(&siStartInfo, sizeof(siStartInfo));

  siStartInfo.cb = sizeof(STARTUPINFOW);
  siStartInfo.hStdError = (HANDLE)(SOCKET)socks[1].sockfd;
  siStartInfo.hStdOutput = (HANDLE)(SOCKET)socks[1].sockfd;
  siStartInfo.hStdInput = (HANDLE)(SOCKET)socks[1].sockfd;
  siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

  snprintf(command, MAX_PATH, "%s /C \"%s\"", cmdexe, cmdline);

  if (CreateProcessA(cmdexe, command, NULL, NULL, TRUE, 0, NULL, NULL,
    &siStartInfo, &procinfo->proc_info)) {
    int pid;
    unsigned int tid;
    HANDLE h;

    pid = (int)procinfo->proc_info.dwProcessId;
    procinfo->pipe_socket = socks[1];
    nio_socketnonblock(&socks[0], 1);

    h = (HANDLE)_beginthreadex(NULL, 0, popen_waitforprocess, procinfo, 0,
      &tid);
    CloseHandle(h);

    *s = socks[0];
    return pid;
  }

  nio_destroysocket(&socks[0]);
  nio_destroysocket(&socks[1]);
  return -1;
}
#endif
