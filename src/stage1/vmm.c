// src/stage1/vmm.c - Virtual Memory Manager (x86_64 4-level paging)
//
// The first 4GB of physical memory is identity mapped using 2MB pages. That
// needs only 1 PDPT + 4 page directories (~20KB) instead of ~8MB of 4KB page
// tables, and boots instantly. When a 4KB mapping with different attributes is
// required inside that range (e.g. uncacheable MMIO), the covering 2MB page is
// split into 512 4KB pages on demand.

#include "vmm.h"

page_table_t *pml4_root = 0;

static page_table_t *alloc_table(void) {
  page_table_t *t = (page_table_t *)pmm_alloc_block();
  if (!t)
    return 0;
  for (int i = 0; i < ENTRIES_PER_TABLE; i++)
    t->entries[i] = 0;
  return t;
}

void vmm_load_pml4(page_table_t *pml4) {
  unsigned long long address = (unsigned long long)pml4;
  __asm__ volatile("mov %0, %%cr3" : : "r"(address) : "memory");
}

page_table_t *vmm_get_active_pml4(void) {
  unsigned long long cr3_val;
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3_val));
  return (page_table_t *)(cr3_val & PAGE_MASK);
}

static inline void invlpg(unsigned long long addr) {
  __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

static void flush_tlb(void) {
  unsigned long long cr3_val;
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3_val));
  __asm__ volatile("mov %0, %%cr3" : : "r"(cr3_val) : "memory");
}

// Returns the next-level table for `entry`, allocating it if absent.
static page_table_t *next_level(unsigned long long *entry) {
  if (!(*entry & PAGE_PRESENT)) {
    page_table_t *t = alloc_table();
    if (!t)
      return 0;
    *entry = (unsigned long long)t | PAGE_PRESENT | PAGE_RW;
  }
  return (page_table_t *)(*entry & PAGE_MASK);
}

int vmm_map_page(page_table_t *pml4, unsigned long long virtual_addr,
                 unsigned long long physical_addr, unsigned long long flags) {
  unsigned int pml4_idx = (virtual_addr >> 39) & 0x1FF;
  unsigned int pdpt_idx = (virtual_addr >> 30) & 0x1FF;
  unsigned int pd_idx = (virtual_addr >> 21) & 0x1FF;
  unsigned int pt_idx = (virtual_addr >> 12) & 0x1FF;

  page_table_t *pdpt = next_level(&pml4->entries[pml4_idx]);
  if (!pdpt)
    return -1;
  page_table_t *pd = next_level(&pdpt->entries[pdpt_idx]);
  if (!pd)
    return -1;

  unsigned long long *pde = &pd->entries[pd_idx];
  if ((*pde & PAGE_PRESENT) && (*pde & PAGE_HUGE)) {
    // Split the 2MB page into 512 4KB pages with the same attributes.
    page_table_t *pt = alloc_table();
    if (!pt)
      return -1;
    unsigned long long base = *pde & ~(HUGE_PAGE_SIZE - 1) & PAGE_MASK;
    unsigned long long attrs = *pde & (PAGE_PRESENT | PAGE_RW | PAGE_USER |
                                       PAGE_PWT | PAGE_PCD);
    for (int i = 0; i < ENTRIES_PER_TABLE; i++)
      pt->entries[i] = (base + (unsigned long long)i * 4096) | attrs;
    *pde = (unsigned long long)pt | PAGE_PRESENT | PAGE_RW;
    flush_tlb();
  }

  page_table_t *pt = next_level(pde);
  if (!pt)
    return -1;
  pt->entries[pt_idx] = (physical_addr & PAGE_MASK) | flags;
  invlpg(virtual_addr);
  return 0;
}

void vmm_init(void) {
  serial_println("--- VMM Initializing ---");

  pml4_root = alloc_table();
  page_table_t *pdpt = alloc_table();
  if (!pml4_root || !pdpt)
    kernel_panic("VMM: out of memory allocating root page tables");
  pml4_root->entries[0] = (unsigned long long)pdpt | PAGE_PRESENT | PAGE_RW;

  // Identity map 0..4GB with 2MB pages (4 page directories x 512 entries).
  for (int g = 0; g < 4; g++) {
    page_table_t *pd = alloc_table();
    if (!pd)
      kernel_panic("VMM: out of memory allocating page directories");
    pdpt->entries[g] = (unsigned long long)pd | PAGE_PRESENT | PAGE_RW;
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
      unsigned long long phys =
          ((unsigned long long)g << 30) + (unsigned long long)i * HUGE_PAGE_SIZE;
      pd->entries[i] = phys | PAGE_PRESENT | PAGE_RW | PAGE_HUGE;
    }
  }

  vmm_load_pml4(pml4_root);
  serial_println("    |-- Identity mapped 0-4GB with 2MB pages. CR3 loaded.");
}

void vmm_map_mmio(unsigned long long physical_base,
                  unsigned long long num_pages) {
  unsigned long long base = physical_base & PAGE_MASK;
  for (unsigned long long i = 0; i < num_pages; i++) {
    unsigned long long target = base + i * 4096;
    if (vmm_map_page(pml4_root, target, target,
                     PAGE_PRESENT | PAGE_RW | PAGE_PCD | PAGE_PWT) != 0)
      kernel_panic("VMM: out of memory while mapping MMIO");
  }
}
