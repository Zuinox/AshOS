#pragma once
#include "hardware.h"

typedef struct heap_block {
    unsigned int size;
    unsigned int is_free;
    struct heap_block* next;
} heap_block_t;

void heap_init(unsigned long long start_addr, unsigned int total_size);
void* kmalloc(unsigned int size);
void kfree(void* ptr);
unsigned int heap_total_bytes(void);
unsigned int heap_used_bytes(void);
