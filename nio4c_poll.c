/*
 *  nio4c_poll.c
 *
 *  copyright (c) 2019, 2020 Xiongfei Shi
 *
 *  author: Xiongfei Shi <xiongfei.shi(a)icloud.com>
 *  license: Apache-2.0
 *
 *  https://github.com/shixiongfei/nio4c
 */

#include "nio4c_internal.h"

#if defined(__linux__)
#include <sys/epoll.h>
#include <unistd.h>

typedef struct nioepoll_s {
  niopoll_t np;
  int fd;
} nioepoll_t;

static const char *nioepoll_backend(niopoll_t *p) { return "epoll"; }

static void nioepoll_destroy(niopoll_t *p) {
  nioepoll_t *ep = nio_entry(p, nioepoll_t, np);
  close(ep->fd);
  nio_free(ep);
}

static int nioepoll_register(niopoll_t *p, int fd, void *userdata) {
  nioepoll_t *ep = nio_entry(p, nioepoll_t, np);
  struct epoll_event ev;

  ev.events = 0;
  ev.data.ptr = userdata;

  return (epoll_ctl(ep->fd, EPOLL_CTL_ADD, fd, &ev) < 0) ? -1 : 0;
}

static int nioepoll_deregister(niopoll_t *p, int fd) {
  nioepoll_t *ep = nio_entry(p, nioepoll_t, np);
  return (epoll_ctl(ep->fd, EPOLL_CTL_DEL, fd, NULL) < 0) ? -1 : 0;
}

static int nioepoll_ioevent(niopoll_t *p, int fd, int readable, int writeable,
                            void *userdata) {
  nioepoll_t *ep = nio_entry(p, nioepoll_t, np);
  struct epoll_event ev;

  ev.events = (readable ? EPOLLIN : 0) | (writeable ? EPOLLOUT : 0);
  ev.data.ptr = userdata;

  return (epoll_ctl(ep->fd, EPOLL_CTL_MOD, fd, &ev) < 0) ? -1 : 0;
}

static int nioepoll_wait(niopoll_t *p, nioevent_t *evt, int count,
                         int timeout) {
  nioepoll_t *ep = nio_entry(p, nioepoll_t, np);
  struct epoll_event ev[count];
  int ready, i;

  ready = epoll_wait(ep->fd, ev, count, timeout);

  for (i = 0; i < ready; ++i) {
    evt[i].fd = ev[i].data.fd;
    evt[i].userdata = ev[i].data.ptr;

    evt[i].error = !!(ev[i].events & EPOLLERR);
    evt[i].readable = !!(ev[i].events & (EPOLLIN | EPOLLHUP));
    evt[i].writeable = !!(ev[i].events & EPOLLOUT);
  }
  return ready;
}

static niopoll_t *nioepoll_create(void) {
  nioepoll_t *ep;
  int epfd = -1;

#ifdef EPOLL_CLOEXEC
  epfd = epoll_create1(EPOLL_CLOEXEC);
#endif

  if (epfd < 0) {
    epfd = epoll_create(100000);
    if (epfd < 0)
      return NULL;
  }

#ifdef FD_CLOEXEC
  fcntl(epfd, F_SETFD, FD_CLOEXEC);
#endif

  ep = (nioepoll_t *)nio_malloc(sizeof(nioepoll_t));
  ep->fd = epfd;
  ep->np.method_backend = nioepoll_backend;
  ep->np.method_destroy = nioepoll_destroy;
  ep->np.method_register = nioepoll_register;
  ep->np.method_deregister = nioepoll_deregister;
  ep->np.method_ioevent = nioepoll_ioevent;
  ep->np.method_wait = nioepoll_wait;

  return &ep->np;
}
#elif defined(__APPLE__) || defined(__BSD__)
#include <errno.h>
#include <string.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct niokqueue_s {
  niopoll_t np;
  int fd;
} niokqueue_t;

static const char *niokqueue_backend(niopoll_t *p) { return "kqueue"; }

static void niokqueue_destroy(niopoll_t *p) {
  niokqueue_t *kq = nio_entry(p, niokqueue_t, np);
  close(kq->fd);
  nio_free(kq);
}

static int niokqueue_register(niopoll_t *p, int fd, void *userdata) {
  niokqueue_t *kq = nio_entry(p, niokqueue_t, np);
  struct kevent ke;

  EV_SET(&ke, fd, EVFILT_READ, EV_ADD | EV_DISABLE, 0, 0, userdata);
  if ((kevent(kq->fd, &ke, 1, NULL, 0, NULL) < 0) || (ke.flags & EV_ERROR))
    goto reterr;

  EV_SET(&ke, fd, EVFILT_WRITE, EV_ADD | EV_DISABLE, 0, 0, userdata);
  if ((kevent(kq->fd, &ke, 1, NULL, 0, NULL) < 0) || (ke.flags & EV_ERROR))
    goto clean;

  return 0;

clean:
  EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
  kevent(kq->fd, &ke, 1, NULL, 0, NULL);

reterr:
  return -1;
}

static int niokqueue_deregister(niopoll_t *p, int fd) {
  niokqueue_t *kq = nio_entry(p, niokqueue_t, np);
  struct kevent ke;

  EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
  kevent(kq->fd, &ke, 1, NULL, 0, NULL);

  EV_SET(&ke, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
  kevent(kq->fd, &ke, 1, NULL, 0, NULL);

  return 0;
}

static int niokqueue_ioevent(niopoll_t *p, int fd, int readable, int writeable,
                             void *userdata) {
  niokqueue_t *kq = nio_entry(p, niokqueue_t, np);
  struct kevent ke;

  EV_SET(&ke, fd, EVFILT_READ, readable ? EV_ENABLE : EV_DISABLE, 0, 0,
         userdata);
  kevent(kq->fd, &ke, 1, NULL, 0, NULL);

  EV_SET(&ke, fd, EVFILT_WRITE, writeable ? EV_ENABLE : EV_DISABLE, 0, 0,
         userdata);
  kevent(kq->fd, &ke, 1, NULL, 0, NULL);

  return 0;
}

static int niokqueue_wait(niopoll_t *p, nioevent_t *evt, int count,
                          int timeout) {
  niokqueue_t *kq = nio_entry(p, niokqueue_t, np);
  struct kevent ev[count];
  struct timespec ts;
  struct timespec *ts_timeout;
  int ready, i;

  if (timeout < 0)
    ts_timeout = NULL;
  else if (0 == timeout) {
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    ts_timeout = &ts;
  } else {
    ts.tv_sec = timeout / 1000;
    ts.tv_nsec = (timeout % 1000) * 1000000;
    ts_timeout = &ts;
  }

  ready = kevent(kq->fd, NULL, 0, ev, count, ts_timeout);

  for (i = 0; i < ready; ++i) {
    evt[i].fd = ev[i].ident;
    evt[i].userdata = ev[i].udata;

    evt[i].error = !!(ev[i].flags & EV_ERROR);
    evt[i].readable = (EVFILT_READ == ev[i].filter);
    evt[i].writeable = (EVFILT_WRITE == ev[i].filter);
  }
  return ready;
}

static niopoll_t *niokqueue_create(void) {
  niokqueue_t *kq;
  int kqfd;

  kqfd = kqueue();
  if (kqfd < 0)
    return NULL;

  kq = (niokqueue_t *)nio_malloc(sizeof(niokqueue_t));
  kq->fd = kqfd;
  kq->np.method_backend = niokqueue_backend;
  kq->np.method_destroy = niokqueue_destroy;
  kq->np.method_register = niokqueue_register;
  kq->np.method_deregister = niokqueue_deregister;
  kq->np.method_ioevent = niokqueue_ioevent;
  kq->np.method_wait = niokqueue_wait;

  return &kq->np;
}
#else
#if defined(_WIN32)
#include <WinSock2.h>
#else
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif /* _WIN32 */

#undef FD_SETSIZE
#define FD_SETSIZE 2048

#define FD_POLLIN 0x01
#define FD_POLLOUT 0x02
#define FD_POLLERR 0x04

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

typedef struct sockfd {
  int fd;
  short events;
  short revents;
  void *ud;
} sockfd;

typedef struct nioselect_s {
  niopoll_t np;
  int nfds;
  struct sockfd sfds[FD_SETSIZE];
} nioselect_t;

static const char *nioselect_backend(niopoll_t *p) { return "select"; }

static void nioselect_destroy(niopoll_t *p) {
  nioselect_t *sp = nio_entry(p, nioselect_t, np);
  nio_free(sp);
}

static int nioselect_register(niopoll_t *p, int fd, void *userdata) {
  nioselect_t *sp = nio_entry(p, nioselect_t, np);
  int i;

  if (sp->nfds >= FD_SETSIZE)
    return -1;

  for (i = 0; i < sp->nfds; ++i)
    if (sp->sfds[i].fd == fd)
      return -1;

  i = sp->nfds;

  sp->sfds[i].fd = fd;
  sp->sfds[i].events = NIO_NIL;
  sp->sfds[i].revents = NIO_NIL;
  sp->sfds[i].ud = userdata;

  sp->nfds += 1;
  return 0;
}

static int nioselect_deregister(niopoll_t *p, int fd) {
  nioselect_t *sp = nio_entry(p, nioselect_t, np);
  int i, last;

  if (sp->nfds <= 0)
    return -1;

  last = sp->nfds - 1;

  if (sp->sfds[last].fd == fd) {
    sp->sfds[last].fd = INVALID_SOCKET;
    sp->sfds[last].events = NIO_NIL;
    sp->sfds[last].revents = NIO_NIL;
    sp->sfds[last].ud = NULL;

    sp->nfds -= 1;
    return 0;
  }

  for (i = 0; i < last; ++i) {
    if (sp->sfds[i].fd == fd) {
      sp->sfds[i].fd = sp->sfds[last].fd;
      sp->sfds[i].events = sp->sfds[last].events;
      sp->sfds[i].revents = sp->sfds[last].revents;
      sp->sfds[i].ud = sp->sfds[last].ud;

      sp->sfds[last].fd = INVALID_SOCKET;
      sp->sfds[last].events = NIO_NIL;
      sp->sfds[last].revents = NIO_NIL;
      sp->sfds[last].ud = NULL;

      sp->nfds -= 1;
      return 0;
    }
  }
  return -1;
}

static int nioselect_ioevent(niopoll_t *p, int fd, int readable, int writeable,
                             void *userdata) {
  nioselect_t *sp = nio_entry(p, nioselect_t, np);
  int i;

  for (i = 0; i < sp->nfds; ++i) {
    if (sp->sfds[i].fd == fd) {
      sp->sfds[i].events =
          (readable ? FD_POLLIN : 0) | (writeable ? FD_POLLOUT : 0);
      sp->sfds[i].ud = userdata;
      return 0;
    }
  }
  return -1;
}

static struct timeval *map_timeout(int millisec, struct timeval *timeout) {
  if (millisec < 0)
    return NULL;

  if (0 == millisec) {
    timeout->tv_sec = 0;
    timeout->tv_usec = 0;
  } else {
    timeout->tv_sec = millisec / 1000;
    timeout->tv_usec = (millisec % 1000) * 1000;
  }
  return timeout;
}

static int nioselect_wait(niopoll_t *p, nioevent_t *evt, int count,
                          int timeout) {
  nioselect_t *sp = nio_entry(p, nioselect_t, np);
  fd_set read_fds;
  fd_set write_fds;
  fd_set except_fds;
  struct timeval tv_time;
  int max_n, max_fd = -1;
  int ready, i, j = 0;

  FD_ZERO(&read_fds);
  FD_ZERO(&write_fds);
  FD_ZERO(&except_fds);

  for (i = 0; i < sp->nfds; ++i) {
    if (sp->sfds[i].fd == INVALID_SOCKET)
      continue;

    if (sp->sfds[i].events == NIO_NIL)
      continue;

    sp->sfds[i].revents = 0;

    if (sp->sfds[i].events & FD_POLLIN)
      FD_SET(sp->sfds[i].fd, &read_fds);

    if (sp->sfds[i].events & FD_POLLOUT)
      FD_SET(sp->sfds[i].fd, &write_fds);

    if (sp->sfds[i].events & FD_POLLERR)
      FD_SET(sp->sfds[i].fd, &except_fds);

    max_fd = max(max_fd, sp->sfds[i].fd);
  }

  ready = select(max_fd + 1, &read_fds, &write_fds, &except_fds,
                 map_timeout(timeout, &tv_time));

  if (ready >= 0) {
    max_n = min(ready, count);

    for (i = 0; i < sp->nfds && j < max_n; ++i) {
      if (sp->sfds[i].fd == INVALID_SOCKET)
        continue;

      if (FD_ISSET(sp->sfds[i].fd, &except_fds))
        sp->sfds[i].revents |= FD_POLLERR;

      if (FD_ISSET(sp->sfds[i].fd, &read_fds))
        sp->sfds[i].revents |= FD_POLLIN;

      if (FD_ISSET(sp->sfds[i].fd, &write_fds))
        sp->sfds[i].revents |= FD_POLLOUT;

      if (NIO_NIL != sp->sfds[i].revents) {
        evt[j].fd = sp->sfds[i].fd;
        evt[j].userdata = sp->sfds[i].ud;
        evt[j].error = !!(sp->sfds[i].revents & FD_POLLERR);
        evt[j].readable = !!(sp->sfds[i].revents & FD_POLLIN);
        evt[j].writeable = !!(sp->sfds[i].revents & FD_POLLOUT);

        j += 1;
      }
    }
  }

  return j;
}

static niopoll_t *nioselect_create(void) {
  nioselect_t *sp = (nioselect_t *)nio_malloc(sizeof(nioselect_t));
  int i;

  for (i = 0; i < FD_SETSIZE; ++i) {
    sp->sfds[i].fd = INVALID_SOCKET;
    sp->sfds[i].events = NIO_NIL;
    sp->sfds[i].revents = NIO_NIL;
    sp->sfds[i].ud = NULL;
  }
  sp->nfds = 0;

  sp->np.method_backend = nioselect_backend;
  sp->np.method_destroy = nioselect_destroy;
  sp->np.method_register = nioselect_register;
  sp->np.method_deregister = nioselect_deregister;
  sp->np.method_ioevent = nioselect_ioevent;
  sp->np.method_wait = nioselect_wait;

  return &sp->np;
}
#endif

nio_pollcreator nio_pollcreate = NULL;

int nio_pollinit(nio_pollcreator creator) {
  if (creator)
    nio_pollcreate = creator;

  if (!nio_pollcreate) {
#if defined(__linux__)
    nio_pollcreate = nioepoll_create;
#elif defined(__APPLE__) || defined(__BSD__)
    nio_pollcreate = niokqueue_create;
#else
    nio_pollcreate = nioselect_create;
#endif
  }

  return nio_pollcreate ? 0 : -1;
}
