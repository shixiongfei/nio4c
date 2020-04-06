/*
 *  nio4c.c
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
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <stdio.h>
static WSADATA wsa_data;
#else /* _WIN32 */
#include <signal.h>
#endif /* _WIN32 */

static void *alloc_emul(void *ptr, size_t size) {
  if (size)
    return realloc(ptr, size);
  free(ptr);
  return NULL;
}

static void *(*nio_alloc)(void *, size_t) = alloc_emul;

void nio_setalloc(void *(*allocator)(void *, size_t)) {
  nio_alloc = allocator ? allocator : alloc_emul;
}

void *nio_realloc(void *ptr, size_t size) { return nio_alloc(ptr, size); }

void *nio_calloc(size_t count, size_t size) {
  void *p = nio_malloc(count * size);
  if (p)
    memset(p, 0, count * size);
  return p;
}

unsigned long nio_nextpower(unsigned long size) {
  if (0 == size)
    return 2;

  /* fast check if power of two */
  if (0 == (size & (size - 1)))
    return size;

  size -= 1;
  size |= size >> 1;
  size |= size >> 2;
  size |= size >> 4;
  size |= size >> 8;
  size |= size >> 16;
#if ULONG_MAX == ULLONG_MAX
  size |= size >> 32;
#endif
  size += 1;

  return size;
}

void nio_initialize(void) {
#ifdef _WIN32
  _setmaxstdio(2048);
  WSAStartup(MAKEWORD(2, 2), &wsa_data);
#else
  signal(SIGPIPE, SIG_IGN);
#endif
}

void nio_finalize(void) {
#ifdef _WIN32
  WSACleanup();
#endif
}

unsigned short nio_checksum(const void *buffer, int len) {
  unsigned int acc;
  unsigned short src;
  unsigned char *octetptr;

  acc = 0;
  octetptr = (unsigned char *)buffer;

  while (len > 1) {
    src = (*octetptr) << 8;
    octetptr += 1;

    src |= (*octetptr);
    octetptr += 1;

    acc += src;
    len -= 2;
  }

  if (len > 0) {
    src = (*octetptr) << 8;
    acc += src;
  }

  while (0 != (acc & 0xFFFF0000UL))
    acc = (acc >> 16) + (acc & 0x0000FFFFUL);

  return htons((unsigned short)acc);
}
