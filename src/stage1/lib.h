#pragma once
// src/stage1/lib.h - tiny freestanding C library for the kernel.

typedef unsigned long long size_t;

void *memset(void *dst, int value, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);

size_t kstrlen(const char *s);
int kstrcmp(const char *a, const char *b);
int kstrncmp(const char *a, const char *b, size_t n);
// Copies at most dst_size-1 chars and always NUL-terminates. Returns strlen(src).
size_t kstrlcpy(char *dst, const char *src, size_t dst_size);
// Appends src to dst (capacity dst_size), always NUL-terminates.
size_t kstrlcat(char *dst, const char *src, size_t dst_size);
// Unsigned integer -> decimal string. buf must hold at least 21 bytes.
void kutoa(unsigned long long value, char *buf);

