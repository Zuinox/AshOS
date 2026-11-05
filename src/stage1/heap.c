#include "heap.h"

static heap_block_t* heap_head = 0;
static unsigned int heap_total = 0;

void heap_init(unsigned long long start_addr, unsigned int total_size) {
    heap_head = (heap_block_t*)start_addr;
    heap_total = total_size;

    heap_head->size = total_size - sizeof(heap_block_t);
    heap_head->is_free = 1;
    heap_head->next = 0;

    serial_println("--- Kernel Heap Initialized ---");
}

unsigned int heap_total_bytes(void) {
    return heap_total;
}

unsigned int heap_used_bytes(void) {
    unsigned int used = 0;
    heap_block_t* cur = heap_head;
    while (cur != 0) {
        if (!cur->is_free) {
            used += cur->size + (unsigned int)sizeof(heap_block_t);
        }
        cur = cur->next;
    }
    return used;
}

void* kmalloc(unsigned int size) {
    if (size == 0) return 0;

    unsigned int aligned_size = (size + 7) & ~7;
    heap_block_t* current = heap_head;

    while (current != 0) {
        if (current->is_free && current->size >= aligned_size) {
            if (current->size > aligned_size + sizeof(heap_block_t) + 8) {
                heap_block_t* new_block = (heap_block_t*)((unsigned char*)current + sizeof(heap_block_t) + aligned_size);

                new_block->size = current->size - aligned_size - sizeof(heap_block_t);
                new_block->is_free = 1;
                new_block->next = current->next;

                current->size = aligned_size;
                current->next = new_block;
            }

            current->is_free = 0;
            return (void*)((unsigned char*)current + sizeof(heap_block_t));
        }
        current = current->next;
    }

    serial_println("[!] Kernel Panic: Out of Heap Memory!");
    return 0;
}

void kfree(void* ptr) {
    if (!ptr) return;

    heap_block_t* block = (heap_block_t*)((unsigned char*)ptr - sizeof(heap_block_t));
    block->is_free = 1;

    heap_block_t* current = heap_head;
    while (current != 0) {
        if (current->is_free && current->next != 0 && current->next->is_free) {
            current->size += sizeof(heap_block_t) + current->next->size;
            current->next = current->next->next;
        } else {
            current = current->next;
        }
    }
}
