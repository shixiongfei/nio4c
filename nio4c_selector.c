/*
 *  nio4c_selector.c
 *
 *  copyright (c) 2019 Xiongfei Shi
 *
 *  author: Xiongfei Shi <jenson.shixf(a)gmail.com>
 *  license: Apache-2.0
 *
 *  https://github.com/shixiongfei/nio4c
 */

#include <assert.h>
#include "nio4c.h"
#include "internal.h"

nioselector_t *nio_selector(void) {
  nioselector_t *selector;
  niosocket_t sock_pipe[2];

  selector = (nioselector_t *)nio_malloc(sizeof(nioselector_t));
  if (!selector) return NULL;

  if (0 != nio_pipe(sock_pipe)) {
    nio_free(selector);
    return NULL;
  }

  selector->selector = niopoll_create();
  selector->wakeup = sock_pipe[0];
  selector->waker = sock_pipe[1];
  selector->closed = 0;
  niohtable_create(&selector->selectables);

  niopoll_register(selector->selector, nio_sockfd(&selector->wakeup), NULL);
  niopoll_register(selector->selector, nio_sockfd(&selector->waker), NULL);

  niopoll_ioevent(selector->selector, nio_sockfd(&selector->wakeup), 1, 0, NULL);
  niopoll_ioevent(selector->selector, nio_sockfd(&selector->waker), 1, 0, NULL);

  return selector;
}

void selector_destroy(nioselector_t *selector) {
  niopoll_deregister(selector->selector, nio_sockfd(&selector->wakeup));
  niopoll_deregister(selector->selector, nio_sockfd(&selector->waker));
  niopoll_destroy(selector->selector);
  nio_destroysocket(&selector->waker);
  nio_destroysocket(&selector->wakeup);
  niohtable_destroy(&selector->selectables);
  nio_free(selector);
}

const char *selector_backend(nioselector_t *selector) {
  switch (niopoll_backend(selector->selector)) {
    case NIO_EPOLL:
      return "epoll";
    case NIO_KQUEUE:
      return "kqueue";
    case NIO_SELECT:
      return "select";
    case NIO_NONE:
    default:
      return "unknown";
  }
  return NULL;  /* never return! */
}

niomonitor_t *selector_register(nioselector_t *selector, niosocket_t *io,
  int interest, void *ud) {
  niomonitor_t *monitor;

  if (selector_closed(selector)) return NULL;

  if (0 == niohtable_get(&selector->selectables, io, NULL))
    return NULL;

  monitor = monitor_new(selector, io, interest, ud);
  if (!monitor) return NULL;

  if (0 != niopoll_register(selector->selector, nio_sockfd(io), monitor)) {
    monitor_close(monitor, 0);
    monitor_destroy(monitor);
    return NULL;
  }

  monitor_resetinterests(monitor);
  niohtable_set(&selector->selectables, io, monitor, NULL);

  return monitor;
}

niomonitor_t *selector_deregister(nioselector_t *selector, niosocket_t *io) {
  niomonitor_t *monitor = NULL;

  niohtable_erase(&selector->selectables, io, &monitor);
  if (monitor && !monitor_closed(monitor)) {
    niopoll_deregister(selector->selector, nio_sockfd(io));
    monitor_close(monitor, 0);
  }

  return monitor;
}

int selector_select(nioselector_t *selector, niomonitor_t **monitors, int count,
  unsigned int millisec) {
  dynarray(nioevent_t, pevt, count);
  int i, ready, buffer, offset = 0;
  niomonitor_t *monitor;
  niohtableiter_t iter;

  niohtable_iter(&selector->selectables, &iter);
  while (0 == niohtable_next(&iter, NULL, &monitor))
    monitor->readiness = NIO_NIL;

  ready = niopoll_wait(selector->selector, pevt, count, millisec);

  for (i = 0; i < ready; ++i) {
    monitor = (niomonitor_t *)pevt[i].userdata;

    if (pevt[i].error) {
      assert(monitor);
      monitor->readiness |= NIO_IOERROR;
    }

    if (pevt[i].readable) {
      if (pevt[i].fd == nio_sockfd(&selector->wakeup))
        nio_recv(&selector->wakeup, &buffer, sizeof(buffer));
      else
        monitor->readiness |= NIO_READ;
    }

    if (pevt[i].writeable)
      monitor->readiness |= NIO_WRITE;

    if (monitor) monitors[offset++] = monitor;
  }

  return offset;
}

int selector_wakeup(nioselector_t *selector) {
  char sig = '\0';
  nio_send(&selector->waker, &sig, sizeof(sig));
  return 0;
}

int selector_registered(nioselector_t *selector, niosocket_t *io) {
  return 0 == niohtable_get(&selector->selectables, io, NULL);
}

int selector_close(nioselector_t *selector) {
  if (selector_closed(selector)) return -1;

  nio_shutdown(&selector->wakeup, SHUT_RDWR);
  nio_shutdown(&selector->waker, SHUT_RDWR);

  selector->closed = 1;
  return 0;
}

int selector_closed(nioselector_t *selector) {
  return selector->closed;
}

int selector_empty(nioselector_t *selector) {
  return selector->selectables.used == 0;
}
