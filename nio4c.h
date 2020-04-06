/*
 *  nio4c.h
 *
 *  copyright (c) 2019, 2020 Xiongfei Shi
 *
 *  author: Xiongfei Shi <jenson.shixf(a)gmail.com>
 *  license: Apache-2.0
 *
 *  https://github.com/shixiongfei/nio4c
 */

#ifndef __NIO4C_H__
#define __NIO4C_H__

#ifndef _WIN32
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __linux__
#define AF_LINK AF_PACKET
#else /* __linux__ */
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#endif /* __linux__ */

#define INVALID_SOCKET (~0)
#define SOCKET_ERROR (-1)
#else /* _WIN32 */
#include <WS2tcpip.h>
#include <WinSock2.h>
#include <Windows.h>

#include <IPHlpApi.h>
#include <fcntl.h>
#include <io.h>
#endif /* _WIN32 */

#ifndef SHUT_RD
#define SHUT_RD SD_RECEIVE
#endif

#ifndef SHUT_WR
#define SHUT_WR SD_SEND
#endif

#ifndef SHUT_RDWR
#define SHUT_RDWR SD_BOTH
#endif

#ifndef _WIN32
#define NIO_EXPORT __attribute__((visibility("default")))
#define NIO_IMPORT __attribute__((visibility("default")))
#else
#define NIO_EXPORT __declspec(dllexport)
#define NIO_IMPORT __declspec(dllimport)
#endif

#if defined(NIO_STATIC)
#define NIO_API extern
#elif defined(NIO_BUILD_DLL)
#define NIO_API NIO_EXPORT
#else
#define NIO_API NIO_IMPORT
#endif

#define NIO_QUOTEX(x) #x
#define NIO_QUOTE(x) NIO_QUOTEX(x)

#define NIO_MAJOR 0
#define NIO_MINOR 1
#define NIO_PATCH 1

#define NIO_VERMAJOR NIO_QUOTE(NIO_MAJOR)
#define NIO_VERMINOR NIO_QUOTE(NIO_MINOR)
#define NIO_VERPATCH NIO_QUOTE(NIO_PATCH)

#define NIO_VERNUM ((NIO_MAJOR * 100) + NIO_MINOR)
#define NIO_VERFULL ((NIO_VERNUM * 100) + NIO_PATCH)
#define NIO_VERSION (NIO_VERMAJOR "." NIO_VERMINOR "." NIO_VERPATCH)

#define NIO_HI8(x) ((unsigned char)((0xFF00U & (unsigned short)(x)) >> 8))
#define NIO_LO8(x) ((unsigned char)(0x00FFU & (unsigned short)(x)))

#define NIO_HI16(x) ((unsigned short)((0xFFFF0000UL & (unsigned int)(x)) >> 16))
#define NIO_LO16(x) ((unsigned short)(0x0000FFFFUL & (unsigned int)(x)))

#define NIO_HI32(x)                                                            \
  ((unsigned int)((0xFFFFFFFF00000000ULL & (uint64_t)(x)) >> 32))
#define NIO_LO32(x) ((unsigned int)(0x00000000FFFFFFFFULL & (uint64_t)(x)))

#define NIO_SWAP16(x)                                                          \
  (((0xFF00U & (unsigned short)(x)) >> 8) |                                    \
   ((0x00FFU & (unsigned short)(x)) << 8))

#define NIO_SWAP32(x)                                                          \
  (((0xFF000000UL & (unsigned int)(x)) >> 24) |                                \
   ((0x00FF0000UL & (unsigned int)(x)) >> 8) |                                 \
   ((0x0000FF00UL & (unsigned int)(x)) << 8) |                                 \
   ((0x000000FFUL & (unsigned int)(x)) << 24))

#define NIO_SWAP64(x)                                                          \
  (((0xFF00000000000000ULL & (uint64_t)(x)) >> 56) |                           \
   ((0x00FF000000000000ULL & (uint64_t)(x)) >> 40) |                           \
   ((0x0000FF0000000000ULL & (uint64_t)(x)) >> 24) |                           \
   ((0x000000FF00000000ULL & (uint64_t)(x)) >> 8) |                            \
   ((0x00000000FF000000ULL & (uint64_t)(x)) << 8) |                            \
   ((0x0000000000FF0000ULL & (uint64_t)(x)) << 24) |                           \
   ((0x000000000000FF00ULL & (uint64_t)(x)) << 40) |                           \
   ((0x00000000000000FFULL & (uint64_t)(x)) << 56))

#define NIO_LILENDIAN 1234
#define NIO_BIGENDIAN 4321

#ifndef NIO_BYTEORDER
#ifdef __linux__
#include <endian.h>
#define NIO_BYTEORDER __BYTE_ORDER
#else
#if defined(__hppa__) || defined(__m68k__) || defined(mc68000) ||              \
    defined(_M_M68K) || (defined(__MIPS__) && defined(__MISPEB__)) ||          \
    defined(__ppc__) || defined(__POWERPC__) || defined(_M_PPC) ||             \
    defined(__sparc__)
#define NIO_BYTEORDER NIO_BIGENDIAN
#else
#define NIO_BYTEORDER NIO_LILENDIAN
#endif
#endif /* __linux__ */
#endif /* NIO_BYTEORDER */

#if NIO_BYTEORDER == NIO_LILENDIAN
#define NIO_LE16(X) (X)
#define NIO_LE32(X) (X)
#define NIO_LE64(X) (X)
#define NIO_BE16(X) NIO_SWAP16(X)
#define NIO_BE32(X) NIO_SWAP32(X)
#define NIO_BE64(X) NIO_SWAP64(X)
#else
#define NIO_LE16(X) NIO_SWAP16(X)
#define NIO_LE32(X) NIO_SWAP32(X)
#define NIO_LE64(X) NIO_SWAP64(X)
#define NIO_BE16(X) (X)
#define NIO_BE32(X) (X)
#define NIO_BE64(X) (X)
#endif

#define NIO_MTUMAXSIZE 1500
#define NIO_MTUMINSIZE 400

#define NIO_ADDRSTRLEN 46
#define NIO_HWADDRLEN 6

#define NIO_NIL 0
#define NIO_READ 1
#define NIO_WRITE 2
#define NIO_READWRITE (NIO_READ | NIO_WRITE)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct niosocket_s {
  int sockfd;
} niosocket_t;

typedef struct niosockaddr_s {
  struct sockaddr_storage saddr;
} niosockaddr_t;

typedef struct nioipstr_s {
  char addr[NIO_ADDRSTRLEN];
  unsigned short port;
} nioipstr_t;

typedef struct niohwaddr_s {
  unsigned char hwaddr[NIO_HWADDRLEN];
} niohwaddr_t;

NIO_API void nio_setalloc(void *(*allocator)(void *, size_t));

NIO_API void nio_initialize(void);
NIO_API void nio_finalize(void);

NIO_API unsigned short nio_checksum(const void *buffer, int len);
NIO_API const char *nio_gethostname(void);
NIO_API int nio_gethwaddr(niohwaddr_t *hwaddr, int count);

NIO_API int nio_resolvehost(niosockaddr_t *addr_list, int count, int af,
                            const char *hostname, unsigned short port);
NIO_API int nio_hostaddr(niosockaddr_t *addr, const char *hostname,
                         unsigned short port);
NIO_API int nio_ipstr(nioipstr_t *ipstr, const niosockaddr_t *addr);

NIO_API int nio_isnat(const niosockaddr_t *addr);
NIO_API int nio_ipmasklen(const niosockaddr_t *addr);

NIO_API int nio_inprogress(void);

NIO_API int nio_createtcp(niosocket_t *s, int af);
NIO_API int nio_createtcp4(niosocket_t *s);
NIO_API int nio_createtcp6(niosocket_t *s);

NIO_API int nio_createudp(niosocket_t *s, int af);
NIO_API int nio_createudp4(niosocket_t *s);
NIO_API int nio_createudp6(niosocket_t *s);

NIO_API void nio_destroysocket(niosocket_t *s);

#define nio_sockfd(s) ((s)->sockfd)

#ifndef _WIN32
#define nio_ioctlsocket ioctl
#else
#define nio_ioctlsocket ioctlsocket
#endif

NIO_API int nio_bind(niosocket_t *s, const niosockaddr_t *addr);
NIO_API int nio_listen(niosocket_t *s, int backlog);
NIO_API int nio_connect(niosocket_t *s, const niosockaddr_t *addr);
NIO_API int nio_accept(niosocket_t *s, niosocket_t *client,
                       niosockaddr_t *addr);
NIO_API int nio_shutdown(niosocket_t *s, int how);
NIO_API int nio_peeraddr(niosocket_t *s, niosockaddr_t *addr);
NIO_API int nio_peeripstr(niosocket_t *s, nioipstr_t *addr);
NIO_API int nio_sockaddr(niosocket_t *s, niosockaddr_t *addr);
NIO_API int nio_sockipstr(niosocket_t *s, nioipstr_t *addr);

NIO_API int nio_socketnonblock(niosocket_t *s, int on);
NIO_API int nio_reuseaddr(niosocket_t *s, int on);
NIO_API int nio_tcpnodelay(niosocket_t *s, int on);
NIO_API int nio_tcpkeepalive(niosocket_t *s, int on);
NIO_API int nio_tcpkeepvalues(niosocket_t *s, int idle, int interval,
                              int count);
NIO_API int nio_udpbroadcast(niosocket_t *s, int on);

NIO_API int nio_socketreadable(niosocket_t *s, unsigned int timedout);
NIO_API int nio_socketwritable(niosocket_t *s, unsigned int timedout);

NIO_API int nio_send(niosocket_t *s, const void *buffer, int len);
NIO_API int nio_recv(niosocket_t *s, void *buffer, int len);
NIO_API int nio_sendto(niosocket_t *s, const niosockaddr_t *addr,
                       const void *buffer, int len);
NIO_API int nio_recvfrom(niosocket_t *s, niosockaddr_t *addr, void *buffer,
                         int len);
NIO_API int nio_sendall(niosocket_t *s, const void *buffer, int len);
NIO_API int nio_recvall(niosocket_t *s, void *buffer, int len);

/* multiaddr: 224.0.0.0 ~ 239.255.255.255, FF00::/8 */
NIO_API int nio_addmembership(niosocket_t *s, const niosockaddr_t *multiaddr);
NIO_API int nio_dropmembership(niosocket_t *s, const niosockaddr_t *multiaddr);
NIO_API int nio_multicastloop(niosocket_t *s, const niosockaddr_t *multiaddr,
                              int on);

NIO_API int nio_pipe(niosocket_t socks[2]);
NIO_API int nio_popen(niosocket_t *s, const char *cmdline);

typedef struct nioselector_s nioselector_t;
typedef struct niomonitor_s niomonitor_t;

NIO_API nioselector_t *nio_selector(void);

NIO_API void selector_destroy(nioselector_t *selector);
NIO_API const char *selector_backend(nioselector_t *selector);
NIO_API niomonitor_t *selector_register(nioselector_t *selector,
                                        niosocket_t *io, int interest,
                                        void *ud);
NIO_API niomonitor_t *selector_deregister(nioselector_t *selector,
                                          niosocket_t *io);
NIO_API int selector_select(nioselector_t *selector, niomonitor_t **monitors,
                            int count, unsigned int millisec);
NIO_API int selector_wakeup(nioselector_t *selector);
NIO_API int selector_close(nioselector_t *selector);
NIO_API int selector_registered(nioselector_t *selector, niosocket_t *io);
NIO_API int selector_closed(nioselector_t *selector);
NIO_API int selector_empty(nioselector_t *selector);

NIO_API void monitor_destroy(niomonitor_t *monitor);
NIO_API void *monitor_userdata(niomonitor_t *monitor);
NIO_API niosocket_t *monitor_io(niomonitor_t *monitor);
NIO_API int monitor_close(niomonitor_t *monitor, int deregister);
NIO_API int monitor_getinterests(niomonitor_t *monitor);
NIO_API int monitor_setinterests(niomonitor_t *monitor, int interests);
NIO_API int monitor_addinterest(niomonitor_t *monitor, int interest);
NIO_API int monitor_removeinterest(niomonitor_t *monitor, int interest);
NIO_API int monitor_readable(niomonitor_t *monitor);
NIO_API int monitor_writable(niomonitor_t *monitor);
NIO_API int monitor_exception(niomonitor_t *monitor);
NIO_API int monitor_closed(niomonitor_t *monitor);

#ifdef __cplusplus
};
#endif

#endif /* __NIO4C_H__ */
