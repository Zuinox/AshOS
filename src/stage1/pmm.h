#pragma once
#include "hardware.h"
#include "bootinfo.h"

#define BLOCK_SIZE 4096
#define BLOCKS_PER_BYTE 8

// The PMM tracks physical memory below 4GB (the region the kernel identity
// maps). 4GB / 4KB = 1M blocks -> 128KB bitmap stored in the kernel's .bss.
#define PMM_MAX_PHYS   0x100000000ULL
#define PMM_MAX_BLOCKS (PMM_MAX_PHYS / BLOCK_SIZE)

extern unsigned int max_blocks;   // blocks the PMM may hand out (usable RAM)
extern unsigned int used_blocks;  // blocks currently allocated

// Phase 1: build the bitmap from the UEFI memory map. Only
// EfiConventionalMemory is treated as free.
void pmm_init(const BootInfo *boot_info);
// Phase 2: once the kernel runs on its own stack, GDT and page tables, the
// firmware's boot-services memory is no longer needed and can be reused.
void pmm_reclaim_boot_services(const BootInfo *boot_info);

void* pmm_alloc_block(void);
void pmm_free_block(void* address);
void* dma_alloc_blocks(unsigned int num_blocks); // physically contiguous
unsigned long long pmm_total_ram_bytes(void);     // all RAM seen in the map
