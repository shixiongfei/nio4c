/*
 *  nio4c_monitor.c
 *
 *  copyright (c) 2019, 2020 Xiongfei Shi
 *
 *  author: Xiongfei Shi <jenson.shixf(a)gmail.com>
 *  license: Apache-2.0
 *
 *  https://github.com/shixiongfei/nio4c
 */

#include "nio4c_internal.h"

niomonitor_t *monitor_new(nioselector_t *selector, niosocket_t *io,
                          int interest, void *ud) {
  niomonitor_t *monitor = (niomonitor_t *)nio_malloc(sizeof(niomonitor_t));
  if (!monitor)
    return NULL;

  monitor->selector = selector;
  monitor->io = io;
  monitor->ud = ud;
  monitor->interests = interest;
  monitor->readiness = 0;
  monitor->closed = 0;

  return monitor;
}

void monitor_destroy(niomonitor_t *monitor) {
  if (!monitor_closed(monitor))
    monitor_close(monitor, 1);
  nio_free(monitor);
}

void *monitor_userdata(niomonitor_t *monitor) { return monitor->ud; }

niosocket_t *monitor_io(niomonitor_t *monitor) { return monitor->io; }

int monitor_close(niomonitor_t *monitor, int deregister) {
  if (monitor_closed(monitor))
    return -1;

  if (deregister)
    selector_deregister(monitor->selector, monitor->io);
  monitor->closed = 1;

  return 0;
}

int monitor_getinterests(niomonitor_t *monitor) { return monitor->interests; }

int monitor_resetinterests(niomonitor_t *monitor) {
  return niopoll_ioevent(monitor->selector->selector, nio_sockfd(monitor->io),
                         NIO_READ == (monitor->interests & NIO_READ),
                         NIO_WRITE == (monitor->interests & NIO_WRITE),
                         monitor);
}

int monitor_setinterests(niomonitor_t *monitor, int interests) {
  if (monitor_closed(monitor))
    return -1;
  if (interests == monitor->interests)
    return 0;

  monitor->interests = interests;
  return monitor_resetinterests(monitor);
}

int monitor_addinterest(niomonitor_t *monitor, int interest) {
  if (monitor_closed(monitor))
    return -1;
  if (interest == (monitor->interests & interest))
    return 0;

  monitor->interests |= interest;
  return monitor_resetinterests(monitor);
}

int monitor_removeinterest(niomonitor_t *monitor, int interest) {
  if (monitor_closed(monitor))
    return -1;
  if (NIO_NIL == interest)
    return 0;
  if (interest != (monitor->interests & interest))
    return 0;

  monitor->interests &= ~interest;
  return monitor_resetinterests(monitor);
}

int monitor_readable(niomonitor_t *monitor) {
  return NIO_READ == (monitor->readiness & NIO_READ);
}

int monitor_writable(niomonitor_t *monitor) {
  return NIO_WRITE == (monitor->readiness & NIO_WRITE);
}

int monitor_exception(niomonitor_t *monitor) {
  return NIO_IOERROR == (monitor->readiness & NIO_IOERROR);
}

int monitor_closed(niomonitor_t *monitor) { return monitor->closed; }
