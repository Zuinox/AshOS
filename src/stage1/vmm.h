#pragma once
#include "hardware.h"
#include "pmm.h"

#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_RW       (1ULL << 1)
#define PAGE_USER     (1ULL << 2)
#define PAGE_PWT      (1ULL << 3)
#define PAGE_PCD      (1ULL << 4)
#define PAGE_HUGE     (1ULL << 7)   // PS bit: 2MB page at the PD level

#define ENTRIES_PER_TABLE 512
#define PAGE_MASK      0x000FFFFFFFFFF000ULL
#define HUGE_PAGE_SIZE 0x200000ULL

typedef struct __attribute__((aligned(4096))) {
    unsigned long long entries[ENTRIES_PER_TABLE];
} page_table_t;

extern page_table_t* pml4_root;

void vmm_load_pml4(page_table_t* pml4);
page_table_t* vmm_get_active_pml4(void);
// Maps a single 4KB page. Splits a covering 2MB page if necessary.
// Returns 0 on success, -1 if a page table could not be allocated.
int vmm_map_page(page_table_t* pml4, unsigned long long virtual_addr, unsigned long long physical_addr, unsigned long long flags);
void vmm_init(void);
// Identity-maps device memory as uncacheable. Works above 4GB too.
void vmm_map_mmio(unsigned long long physical_base, unsigned long long num_pages);
