/*
 *  nio4c.h
 *
 *  copyright (c) 2019 Xiongfei Shi
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

void nio_setalloc(void *(*alloc)(size_t), void (*release)(void *));

void nio_initialize(void);
void nio_finalize(void);

unsigned short nio_checksum(const void *buffer, int len);
const char *nio_gethostname(void);
int nio_gethwaddr(niohwaddr_t *hwaddr, int count);

int nio_resolvehost(niosockaddr_t *addr_list, int count, int af,
  const char *hostname, unsigned short port);
int nio_hostaddr(niosockaddr_t *addr, const char *hostname,
  unsigned short port);
int nio_ipstr(nioipstr_t *ipstr, const niosockaddr_t *addr);

int nio_isnat(const niosockaddr_t *addr);
int nio_ipmasklen(const niosockaddr_t *addr);

int nio_inprogress(void);

int nio_createtcp(niosocket_t *s, int af);
int nio_createtcp4(niosocket_t *s);
int nio_createtcp6(niosocket_t *s);

int nio_createudp(niosocket_t *s, int af);
int nio_createudp4(niosocket_t *s);
int nio_createudp6(niosocket_t *s);

void nio_destroysocket(niosocket_t *s);

#define nio_sockfd(s) ((s)->sockfd)

#ifndef _WIN32
#define nio_ioctlsocket ioctl
#else
#define nio_ioctlsocket ioctlsocket
#endif

int nio_bind(niosocket_t *s, const niosockaddr_t *addr);
int nio_listen(niosocket_t *s, int backlog);
int nio_connect(niosocket_t *s, const niosockaddr_t *addr);
int nio_accept(niosocket_t *s, niosocket_t *client, niosockaddr_t *addr);
int nio_shutdown(niosocket_t *s, int how);
int nio_peeraddr(niosocket_t *s, niosockaddr_t *addr);
int nio_peeripstr(niosocket_t *s, nioipstr_t *addr);
int nio_sockaddr(niosocket_t *s, niosockaddr_t *addr);
int nio_sockipstr(niosocket_t *s, nioipstr_t *addr);

int nio_socketnonblock(niosocket_t *s, int on);
int nio_reuseaddr(niosocket_t *s, int on);
int nio_tcpnodelay(niosocket_t *s, int on);
int nio_tcpkeepalive(niosocket_t *s, int on);
int nio_tcpkeepvalues(niosocket_t *s, int idle, int interval, int count);
int nio_udpbroadcast(niosocket_t *s, int on);

int nio_socketreadable(niosocket_t *s, unsigned int timedout);
int nio_socketwritable(niosocket_t *s, unsigned int timedout);

int nio_send(niosocket_t *s, const void *buffer, int len);
int nio_recv(niosocket_t *s, void *buffer, int len);
int nio_sendto(niosocket_t *s, const niosockaddr_t *addr,
  const void *buffer, int len);
int nio_recvfrom(niosocket_t *s, niosockaddr_t *addr, void *buffer, int len);
int nio_sendall(niosocket_t *s, const void *buffer, int len);
int nio_recvall(niosocket_t *s, void *buffer, int len);

/* multiaddr: 224.0.0.0 ~ 239.255.255.255, FF00::/8 */
int nio_addmembership(niosocket_t *s, const niosockaddr_t *multiaddr);
int nio_dropmembership(niosocket_t *s, const niosockaddr_t *multiaddr);
int nio_multicastloop(niosocket_t *s, const niosockaddr_t *multiaddr, int on);

int nio_pipe(niosocket_t socks[2]);
int nio_popen(niosocket_t *s, const char *cmdline);

typedef struct nioselector_s nioselector_t;
typedef struct niomonitor_s niomonitor_t;

nioselector_t *nio_selector(void);

void selector_destroy(nioselector_t *selector);
const char *selector_backend(nioselector_t *selector);
niomonitor_t *selector_register(nioselector_t *selector, niosocket_t *io,
  int interest, void *ud);
niomonitor_t *selector_deregister(nioselector_t *selector, niosocket_t *io);
int selector_select(nioselector_t *selector, niomonitor_t **monitors, int count,
  unsigned int millisec);
int selector_wakeup(nioselector_t *selector);
int selector_close(nioselector_t *selector);
int selector_registered(nioselector_t *selector, niosocket_t *io);
int selector_closed(nioselector_t *selector);
int selector_empty(nioselector_t *selector);

void monitor_destroy(niomonitor_t *monitor);
void *monitor_userdata(niomonitor_t *monitor);
niosocket_t *monitor_io(niomonitor_t *monitor);
int monitor_close(niomonitor_t *monitor, int deregister);
int monitor_getinterests(niomonitor_t *monitor);
int monitor_setinterests(niomonitor_t *monitor, int interests);
int monitor_addinterest(niomonitor_t *monitor, int interest);
int monitor_removeinterest(niomonitor_t *monitor, int interest);
int monitor_readable(niomonitor_t *monitor);
int monitor_writable(niomonitor_t *monitor);
int monitor_closed(niomonitor_t *monitor);

#ifdef __cplusplus
};
#endif

#endif  /* __NIO4C_H__ */
