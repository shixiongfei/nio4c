/*
 *  nio4c_htable.c
 *
 *  copyright (c) 2019, 2020 Xiongfei Shi
 *
 *  author: Xiongfei Shi <jenson.shixf(a)gmail.com>
 *  license: Apache-2.0
 *
 *  https://github.com/shixiongfei/nio4c
 */

#include "nio4c_internal.h"
#include <limits.h>

#define HTABLE_INITSIZE 8
#define HTABLE_MAXSIZE (INT_MAX / 2)

int niohtable_create(niohtable_t *ht) {
  ht->htable = NULL;
  ht->size = 0;
  ht->used = 0;
  ht->mask = 0;
  return 0;
}

void niohtable_destroy(niohtable_t *ht) {
  if (ht->htable) {
    niohnode_t *e, *p;
    int i;

    for (i = 0; i < ht->size && ht->used > 0; ++i) {
      e = ht->htable[i];

      while (e) {
        p = e->next;
        nio_free(e);
        ht->used -= 1;
        e = p;
      }
    }
  }
  ht->size = 0;
  ht->used = 0;
  ht->mask = 0;
}

static int htable_expand(niohtable_t *ht, int size) {
  niohnode_t **t;
  niohnode_t *e, *n;
  int newsize, newused, sockfd, i;
  unsigned int newmask;

  if (ht->used > size)
    return -1;

  if (ht->size >= HTABLE_MAXSIZE)
    return -1;

  if (0 == size)
    size = HTABLE_INITSIZE;

  if (size >= HTABLE_MAXSIZE)
    size = HTABLE_MAXSIZE;

  newsize = (int)nio_nextpower(size);
  newmask = newsize - 1;
  newused = ht->used;

  t = (niohnode_t **)nio_calloc(newsize, sizeof(niohnode_t *));
  if (!t)
    return -1;

  if (ht->htable) {
    for (i = 0; i < ht->size && ht->used > 0; ++i) {
      e = ht->htable[i];

      while (e) {
        n = e->next;
        sockfd = nio_sockfd(e->io);

        e->next = t[sockfd & newmask];
        t[sockfd & newmask] = e;

        ht->used -= 1;
        e = n;
      }
    }
    nio_free(ht->htable);
  }

  ht->htable = t;
  ht->used = newused;
  ht->size = newsize;
  ht->mask = newmask;

  return 0;
}

static int htable_expandifneeded(niohtable_t *ht) {
  if (ht->used >= ht->size)
    return htable_expand(ht, ht->size * 2);
  return 0;
}

int niohtable_set(niohtable_t *ht, niosocket_t *io, niomonitor_t *monitor,
                  niomonitor_t **old) {
  niohnode_t *e, *p = NULL;
  int sockfd;

  if (!io)
    return -1;

  if (!monitor)
    return (0 == niohtable_erase(ht, io, old)) ? 1 : -1;

  if (0 != htable_expandifneeded(ht))
    return -1;

  sockfd = nio_sockfd(io);
  e = ht->htable[sockfd & ht->mask];

  while (e) {
    if (sockfd == nio_sockfd(e->io)) {
      if (old)
        *old = e->monitor;

      /* replace element */
      if (e->monitor != monitor)
        e->monitor = monitor;

      return 1;
    }
    e = e->next;
  }

  /* new element */
  e = (niohnode_t *)nio_malloc(sizeof(niohnode_t));
  if (!e)
    return -1;

  e->io = io;
  e->monitor = monitor;

  e->next = ht->htable[sockfd & ht->mask];
  ht->htable[sockfd & ht->mask] = e;
  ht->used += 1;

  return 0;
}

int niohtable_get(niohtable_t *ht, niosocket_t *io, niomonitor_t **monitor) {
  niohnode_t *e;
  int sockfd;

  if (!io || 0 == ht->size || 0 == ht->used)
    return -1;

  sockfd = nio_sockfd(io);
  e = ht->htable[sockfd & ht->mask];

  while (e) {
    if (sockfd == nio_sockfd(e->io)) {
      if (monitor)
        *monitor = e->monitor;
      return 0;
    }
    e = e->next;
  }
  return -1;
}

int niohtable_erase(niohtable_t *ht, niosocket_t *io, niomonitor_t **monitor) {
  niohnode_t *e, *p = NULL;
  int sockfd;

  if (!io || 0 == ht->size || 0 == ht->used)
    return -1;

  sockfd = nio_sockfd(io);
  e = ht->htable[sockfd & ht->mask];

  while (e) {
    if (sockfd == nio_sockfd(e->io)) {
      /* erase element */
      if (p)
        p->next = e->next;
      else
        ht->htable[sockfd & ht->mask] = e->next;

      if (monitor)
        *monitor = e->monitor;
      nio_free(e);
      ht->used -= 1;

      return 0;
    }
    p = e;
    e = e->next;
  }

  return -1;
}

int niohtable_iter(niohtable_t *ht, niohtableiter_t *iter) {
  iter->htable = ht;
  iter->index = -1;
  iter->entry = NULL;
  iter->next = NULL;
  return 0;
}

int niohtable_next(niohtableiter_t *iter, niosocket_t **io,
                   niomonitor_t **monitor) {
  while (1) {
    if (!iter->entry) {
      iter->index += 1;

      if (iter->index >= iter->htable->size)
        break;

      iter->entry = iter->htable->htable[iter->index];
    } else
      iter->entry = iter->next;

    if (iter->entry) {
      iter->next = iter->entry->next;

      if (io)
        *io = iter->entry->io;

      if (monitor)
        *monitor = iter->entry->monitor;

      return 0;
    }
  }
  return -1;
}
