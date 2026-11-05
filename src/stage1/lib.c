// src/stage1/lib.c - tiny freestanding C library for the kernel.
//
// memset/memcpy are implemented with `rep stosb` / `rep movsb` so the compiler
// can never "optimise" them back into calls to themselves.

#include "lib.h"

void *memset(void *dst, int value, size_t n) {
  void *d = dst;
  __asm__ volatile("rep stosb"
                   : "+D"(d), "+c"(n)
                   : "a"(value)
                   : "memory");
  return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
  void *d = dst;
  __asm__ volatile("rep movsb"
                   : "+D"(d), "+S"(src), "+c"(n)
                   :
                   : "memory");
  return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  if (d == s || n == 0)
    return dst;
  if (d < s || d >= s + n)
    return memcpy(dst, src, n);
  // Overlapping, copy backwards.
  while (n--)
    d[n] = s[n];
  return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *x = (const unsigned char *)a;
  const unsigned char *y = (const unsigned char *)b;
  for (size_t i = 0; i < n; i++) {
    if (x[i] != y[i])
      return x[i] - y[i];
  }
  return 0;
}

size_t kstrlen(const char *s) {
  size_t n = 0;
  while (s[n])
    n++;
  return n;
}

int kstrcmp(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *(const unsigned char *)a - *(const unsigned char *)b;
}

int kstrncmp(const char *a, const char *b, size_t n) {
  while (n && *a && *a == *b) {
    a++;
    b++;
    n--;
  }
  if (n == 0)
    return 0;
  return *(const unsigned char *)a - *(const unsigned char *)b;
}

size_t kstrlcpy(char *dst, const char *src, size_t dst_size) {
  size_t len = kstrlen(src);
  if (dst_size == 0)
    return len;
  size_t n = (len < dst_size - 1) ? len : dst_size - 1;
  for (size_t i = 0; i < n; i++)
    dst[i] = src[i];
  dst[n] = '\0';
  return len;
}

size_t kstrlcat(char *dst, const char *src, size_t dst_size) {
  size_t used = 0;
  while (used < dst_size && dst[used])
    used++;
  if (used == dst_size)
    return used + kstrlen(src);
  return used + kstrlcpy(dst + used, src, dst_size - used);
}

void kutoa(unsigned long long value, char *buf) {
  char tmp[21];
  int t = 0;
  if (value == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }
  while (value > 0) {
    tmp[t++] = (char)('0' + (value % 10));
    value /= 10;
  }
  int i = 0;
  while (t > 0)
    buf[i++] = tmp[--t];
  buf[i] = '\0';
}

