/*
 *  nio4c_poll.c
 *
 *  copyright (c) 2019, 2020 Xiongfei Shi
 *
 *  author: Xiongfei Shi <jenson.shixf(a)gmail.com>
 *  license: Apache-2.0
 *
 *  https://github.com/shixiongfei/nio4c
 */

#include "nio4c_internal.h"

#if defined(__linux__)
#include <sys/epoll.h>
#include <unistd.h>

struct niopoll_s {
  int backend;
  int epfd;
};

niopoll_t *niopoll_create(void) {
  niopoll_t *p;
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

  p = (niopoll_t *)nio_malloc(sizeof(niopoll_t));
  p->backend = NIO_EPOLL;
  p->epfd = epfd;

  return p;
}

void niopoll_destroy(niopoll_t *p) {
  close(p->epfd);
  nio_free(p);
}

int niopoll_backend(niopoll_t *p) { return p->backend; }

int niopoll_register(niopoll_t *p, int fd, void *userdata) {
  struct epoll_event ev;

  ev.events = 0;
  ev.data.ptr = userdata;

  return (epoll_ctl(p->epfd, EPOLL_CTL_ADD, fd, &ev) < 0) ? -1 : 0;
}

int niopoll_deregister(niopoll_t *p, int fd) {
  return (epoll_ctl(p->epfd, EPOLL_CTL_DEL, fd, NULL) < 0) ? -1 : 0;
}

int niopoll_ioevent(niopoll_t *p, int fd, int readable, int writeable,
                    void *userdata) {
  struct epoll_event ev;

  ev.events = (readable ? EPOLLIN : 0) | (writeable ? EPOLLOUT : 0);
  ev.data.ptr = userdata;

  return (epoll_ctl(p->epfd, EPOLL_CTL_MOD, fd, &ev) < 0) ? -1 : 0;
}

int niopoll_wait(niopoll_t *p, nioevent_t *evt, int count, int timeout) {
  struct epoll_event ev[count];
  int ready, i;

  ready = epoll_wait(p->epfd, ev, count, timeout);

  for (i = 0; i < ready; ++i) {
    evt[i].fd = ev[i].data.fd;
    evt[i].userdata = ev[i].data.ptr;

    evt[i].error = !!(ev[i].events & EPOLLERR);
    evt[i].readable = !!(ev[i].events & (EPOLLIN | EPOLLHUP));
    evt[i].writeable = !!(ev[i].events & EPOLLOUT);
  }
  return ready;
}
#elif defined(__APPLE__)
#include <errno.h>
#include <string.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

struct niopoll_s {
  int backend;
  int kqfd;
};

niopoll_t *niopoll_create(void) {
  niopoll_t *p;
  int kqfd;

  kqfd = kqueue();
  if (kqfd < 0)
    return NULL;

  p = (niopoll_t *)nio_malloc(sizeof(niopoll_t));
  p->backend = NIO_KQUEUE;
  p->kqfd = kqfd;

  return p;
}

void niopoll_destroy(niopoll_t *p) {
  close(p->kqfd);
  nio_free(p);
}

int niopoll_backend(niopoll_t *p) { return p->backend; }

int niopoll_register(niopoll_t *p, int fd, void *userdata) {
  struct kevent ke;

  EV_SET(&ke, fd, EVFILT_READ, EV_ADD | EV_DISABLE, 0, 0, userdata);
  if ((kevent(p->kqfd, &ke, 1, NULL, 0, NULL) < 0) || (ke.flags & EV_ERROR))
    goto reterr;

  EV_SET(&ke, fd, EVFILT_WRITE, EV_ADD | EV_DISABLE, 0, 0, userdata);
  if ((kevent(p->kqfd, &ke, 1, NULL, 0, NULL) < 0) || (ke.flags & EV_ERROR))
    goto clean;

  return 0;

clean:
  EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
  kevent(p->kqfd, &ke, 1, NULL, 0, NULL);

reterr:
  return -1;
}

int niopoll_deregister(niopoll_t *p, int fd) {
  struct kevent ke;

  EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
  kevent(p->kqfd, &ke, 1, NULL, 0, NULL);

  EV_SET(&ke, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
  kevent(p->kqfd, &ke, 1, NULL, 0, NULL);

  return 0;
}

int niopoll_ioevent(niopoll_t *p, int fd, int readable, int writeable,
                    void *userdata) {
  struct kevent ke;

  EV_SET(&ke, fd, EVFILT_READ, readable ? EV_ENABLE : EV_DISABLE, 0, 0,
         userdata);
  kevent(p->kqfd, &ke, 1, NULL, 0, NULL);

  EV_SET(&ke, fd, EVFILT_WRITE, writeable ? EV_ENABLE : EV_DISABLE, 0, 0,
         userdata);
  kevent(p->kqfd, &ke, 1, NULL, 0, NULL);

  return 0;
}

int niopoll_wait(niopoll_t *p, nioevent_t *evt, int count, int timeout) {
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

  ready = kevent(p->kqfd, NULL, 0, ev, count, ts_timeout);

  for (i = 0; i < ready; ++i) {
    evt[i].fd = ev[i].ident;
    evt[i].userdata = ev[i].udata;

    evt[i].error = !!(ev[i].flags & EV_ERROR);
    evt[i].readable = (EVFILT_READ == ev[i].filter);
    evt[i].writeable = (EVFILT_WRITE == ev[i].filter);
  }
  return ready;
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

struct niopoll_s {
  int backend;
  int nfds;
  struct sockfd sfds[FD_SETSIZE];
};

niopoll_t *niopoll_create(void) {
  niopoll_t *p = (niopoll_t *)nio_malloc(sizeof(niopoll_t));
  int i;

  for (i = 0; i < FD_SETSIZE; ++i) {
    p->sfds[i].fd = INVALID_SOCKET;
    p->sfds[i].events = NIO_NIL;
    p->sfds[i].revents = NIO_NIL;
    p->sfds[i].ud = NULL;
  }
  p->nfds = 0;
  p->backend = NIO_SELECT;

  return p;
}

void niopoll_destroy(niopoll_t *p) { nio_free(p); }

int niopoll_backend(niopoll_t *p) { return p->backend; }

int niopoll_register(niopoll_t *p, int fd, void *userdata) {
  int i;

  if (p->nfds >= FD_SETSIZE)
    return -1;

  for (i = 0; i < p->nfds; ++i)
    if (p->sfds[i].fd == fd)
      return -1;

  i = p->nfds;

  p->sfds[i].fd = fd;
  p->sfds[i].events = NIO_NIL;
  p->sfds[i].revents = NIO_NIL;
  p->sfds[i].ud = userdata;

  p->nfds += 1;
  return 0;
}

int niopoll_deregister(niopoll_t *p, int fd) {
  int i, last;

  if (p->nfds <= 0)
    return -1;

  last = p->nfds - 1;

  if (p->sfds[last].fd == fd) {
    p->sfds[last].fd = INVALID_SOCKET;
    p->sfds[last].events = NIO_NIL;
    p->sfds[last].revents = NIO_NIL;
    p->sfds[last].ud = NULL;

    p->nfds -= 1;
    return 0;
  }

  for (i = 0; i < last; ++i) {
    if (p->sfds[i].fd == fd) {
      p->sfds[i].fd = p->sfds[last].fd;
      p->sfds[i].events = p->sfds[last].events;
      p->sfds[i].revents = p->sfds[last].revents;
      p->sfds[i].ud = p->sfds[last].ud;

      p->sfds[last].fd = INVALID_SOCKET;
      p->sfds[last].events = NIO_NIL;
      p->sfds[last].revents = NIO_NIL;
      p->sfds[last].ud = NULL;

      p->nfds -= 1;
      return 0;
    }
  }
  return -1;
}

int niopoll_ioevent(niopoll_t *p, int fd, int readable, int writeable,
                    void *userdata) {
  int i;

  for (i = 0; i < p->nfds; ++i) {
    if (p->sfds[i].fd == fd) {
      p->sfds[i].events =
          (readable ? FD_POLLIN : 0) | (writeable ? FD_POLLOUT : 0);
      p->sfds[i].ud = userdata;
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

int niopoll_wait(niopoll_t *p, nioevent_t *evt, int count, int timeout) {
  fd_set read_fds;
  fd_set write_fds;
  fd_set except_fds;
  struct timeval tv_time;
  int max_n, max_fd = -1;
  int ready, i, j = 0;

  FD_ZERO(&read_fds);
  FD_ZERO(&write_fds);
  FD_ZERO(&except_fds);

  for (i = 0; i < p->nfds; ++i) {
    if (p->sfds[i].fd == INVALID_SOCKET)
      continue;

    if (p->sfds[i].events == NIO_NIL)
      continue;

    p->sfds[i].revents = 0;

    if (p->sfds[i].events & FD_POLLIN)
      FD_SET(p->sfds[i].fd, &read_fds);

    if (p->sfds[i].events & FD_POLLOUT)
      FD_SET(p->sfds[i].fd, &write_fds);

    if (p->sfds[i].events & FD_POLLERR)
      FD_SET(p->sfds[i].fd, &except_fds);

    max_fd = max(max_fd, p->sfds[i].fd);
  }

  ready = select(max_fd + 1, &read_fds, &write_fds, &except_fds,
                 map_timeout(timeout, &tv_time));

  if (ready >= 0) {
    max_n = min(ready, count);

    for (i = 0; i < p->nfds && j < max_n; ++i) {
      if (p->sfds[i].fd == INVALID_SOCKET)
        continue;

      if (FD_ISSET(p->sfds[i].fd, &except_fds))
        p->sfds[i].revents |= FD_POLLERR;

      if (FD_ISSET(p->sfds[i].fd, &read_fds))
        p->sfds[i].revents |= FD_POLLIN;

      if (FD_ISSET(p->sfds[i].fd, &write_fds))
        p->sfds[i].revents |= FD_POLLOUT;

      if (NIO_NIL != p->sfds[i].revents) {
        evt[j].fd = p->sfds[i].fd;
        evt[j].userdata = p->sfds[i].ud;
        evt[j].error = !!(p->sfds[i].revents & FD_POLLERR);
        evt[j].readable = !!(p->sfds[i].revents & FD_POLLIN);
        evt[j].writeable = !!(p->sfds[i].revents & FD_POLLOUT);

        j += 1;
      }
    }
  }

  return j;
}
#endif
