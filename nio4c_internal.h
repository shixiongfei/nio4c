/*
 *  nio4c_internal.h
 *
 *  copyright (c) 2019, 2020 Xiongfei Shi
 *
 *  author: Xiongfei Shi <xiongfei.shi(a)icloud.com>
 *  license: Apache-2.0
 *
 *  https://github.com/shixiongfei/nio4c
 */

#ifndef __NIO4C_INTERNAL_H__
#define __NIO4C_INTERNAL_H__

#include "nio4c.h"

#ifdef __cplusplus
extern "C" {
#endif

void *nio_realloc(void *ptr, size_t size);
void *nio_calloc(size_t count, size_t size);

#define nio_malloc(size) nio_realloc(NULL, size)
#define nio_free(ptr) nio_realloc(ptr, 0)

#if defined(_MSC_VER)
#include <malloc.h>
#define nio_dynarray(type, name, size)                                         \
  type *name = (type *)_alloca((size) * sizeof(type))
#else
#define nio_dynarray(type, name, size) type name[size]
#endif

unsigned long nio_nextpower(unsigned long size);

extern nio_pollcreator nio_pollcreate;
int nio_pollinit(nio_pollcreator creator);

#define niopoll_backend(p) (p)->method_backend(p)
#define niopoll_destroy(p) (p)->method_destroy(p)
#define niopoll_register(p, fd, ud) (p)->method_register(p, fd, ud)
#define niopoll_deregister(p, fd) (p)->method_deregister(p, fd)
#define niopoll_ioevent(p, fd, rd, wd, ud)                                     \
  (p)->method_ioevent(p, fd, rd, wd, ud)
#define niopoll_wait(p, e, c, t) (p)->method_wait(p, e, c, t)

typedef struct niohnode_s niohnode_t;

struct niohnode_s {
  niosocket_t *io;
  niomonitor_t *monitor;
  niohnode_t *next;
};

typedef struct niohtable_s {
  niohnode_t **htable;
  int size;
  int used;
  int mask;
} niohtable_t;

typedef struct niohtableiter_s {
  int index;
  niohtable_t *htable;
  niohnode_t *entry;
  niohnode_t *next;
} niohtableiter_t;

int niohtable_create(niohtable_t *ht);
void niohtable_destroy(niohtable_t *ht);

/* returns: -1 = failed, 0 = new one, 1 = replace */
int niohtable_set(niohtable_t *ht, niosocket_t *io, niomonitor_t *monitor,
                  niomonitor_t **old);
int niohtable_get(niohtable_t *ht, niosocket_t *io, niomonitor_t **monitor);
int niohtable_erase(niohtable_t *ht, niosocket_t *io, niomonitor_t **monitor);

int niohtable_iter(niohtable_t *ht, niohtableiter_t *iter);
int niohtable_next(niohtableiter_t *iter, niosocket_t **io,
                   niomonitor_t **monitor);

#define NIO_IOERROR 4

struct nioselector_s {
  niopoll_t *selector;
  niohtable_t selectables;
  niosocket_t wakeup;
  niosocket_t waker;
  int closed;
};

struct niomonitor_s {
  nioselector_t *selector;
  niosocket_t *io;
  void *ud;
  int interests;
  int readiness;
  int closed;
};

niomonitor_t *monitor_new(nioselector_t *selector, niosocket_t *io,
                          int interest, void *ud);
int monitor_resetinterests(niomonitor_t *monitor);

#ifdef __cplusplus
};
#endif

#endif /* __NIO4C_INTERNAL_H__ */
