// src/stage1/pmm.c - Physical Memory Manager (bitmap allocator)
//
// One bit per 4KB block: 1 = used/reserved, 0 = free. The bitmap starts with
// every block marked used, and only regions that the UEFI memory map reports
// as usable RAM are released. Everything else (firmware, ACPI tables, MMIO,
// the kernel image, the bootloader's data) stays reserved.

#include "pmm.h"

static unsigned char memory_bitmap[PMM_MAX_BLOCKS / BLOCKS_PER_BYTE];
unsigned int max_blocks = 0;
unsigned int used_blocks = 0;

static unsigned int search_hint = 0;         // next-fit starting point
static unsigned long long total_ram_bytes = 0;

extern char __kernel_start[], __kernel_end[]; // from kernel.ld

static inline void bit_set(unsigned int b) {
  memory_bitmap[b / 8] |= (unsigned char)(1u << (b % 8));
}
static inline void bit_clear(unsigned int b) {
  memory_bitmap[b / 8] &= (unsigned char)~(1u << (b % 8));
}
static inline int bit_test(unsigned int b) {
  return memory_bitmap[b / 8] & (1u << (b % 8));
}

// Releases whole blocks fully inside [base, base+len), clipped to 4GB.
static void release_range(unsigned long long base, unsigned long long len) {
  unsigned long long start = (base + BLOCK_SIZE - 1) / BLOCK_SIZE;
  unsigned long long end = (base + len) / BLOCK_SIZE;
  if (end > PMM_MAX_BLOCKS)
    end = PMM_MAX_BLOCKS;
  for (unsigned long long b = start; b < end; b++) {
    if (bit_test((unsigned int)b)) {
      bit_clear((unsigned int)b);
      max_blocks++;
    }
  }
}

// Marks every block touching [base, base+len) as reserved.
static void reserve_range(unsigned long long base, unsigned long long len) {
  unsigned long long start = base / BLOCK_SIZE;
  unsigned long long end = (base + len + BLOCK_SIZE - 1) / BLOCK_SIZE;
  if (end > PMM_MAX_BLOCKS)
    end = PMM_MAX_BLOCKS;
  for (unsigned long long b = start; b < end; b++) {
    if (!bit_test((unsigned int)b)) {
      bit_set((unsigned int)b);
      max_blocks--;
    }
  }
}

static void release_type(const BootInfo *bi, unsigned int type_a,
                         unsigned int type_b) {
  unsigned char *map = (unsigned char *)bi->MemoryMap;
  unsigned long long count = bi->MemoryMapSize / bi->DescriptorSize;
  for (unsigned long long i = 0; i < count; i++) {
    efi_memory_descriptor_t *d =
        (efi_memory_descriptor_t *)(map + i * bi->DescriptorSize);
    if (d->Type == type_a || d->Type == type_b)
      release_range(d->PhysicalStart, d->NumberOfPages * BLOCK_SIZE);
  }
}

// Regions that must never be handed out, even if the map says "free".
static void reserve_critical(void) {
  reserve_range(0, 0x100000); // first 1MB: real-mode IVT/BDA/EBDA, legacy ROMs
  reserve_range((unsigned long long)__kernel_start,
                KERNEL_MAX_SIZE); // kernel image + .bss + bootloader slack
}

void pmm_init(const BootInfo *bi) {
  for (unsigned int i = 0; i < sizeof(memory_bitmap); i++)
    memory_bitmap[i] = 0xFF;
  max_blocks = 0;
  used_blocks = 0;
  total_ram_bytes = 0;

  if (!bi->MemoryMap || bi->DescriptorSize < sizeof(efi_memory_descriptor_t) ||
      bi->MemoryMapSize == 0)
    kernel_panic("PMM: bootloader passed an invalid UEFI memory map");

  unsigned char *map = (unsigned char *)bi->MemoryMap;
  unsigned long long count = bi->MemoryMapSize / bi->DescriptorSize;
  serial_println("--- PMM: UEFI memory map ---");
  for (unsigned long long i = 0; i < count; i++) {
    efi_memory_descriptor_t *d =
        (efi_memory_descriptor_t *)(map + i * bi->DescriptorSize);
    // Types 1-7 are RAM in some form; 9/10 are ACPI RAM.
    if ((d->Type >= 1 && d->Type <= 7) || d->Type == 9 || d->Type == 10)
      total_ram_bytes += d->NumberOfPages * BLOCK_SIZE;
  }

  release_type(bi, EFI_CONVENTIONAL_MEMORY, EFI_CONVENTIONAL_MEMORY);
  reserve_critical();

  serial_print("    |-- Descriptors:        ");
  serial_print_hex(count);
  serial_println("");
  serial_print("    |-- Total RAM (bytes):  ");
  serial_print_hex(total_ram_bytes);
  serial_println("");
  serial_print("    |-- Free blocks (<4GB): ");
  serial_print_hex(max_blocks);
  serial_println("");
}

void pmm_reclaim_boot_services(const BootInfo *bi) {
  unsigned int before = max_blocks;
  release_type(bi, EFI_BOOT_SERVICES_CODE, EFI_BOOT_SERVICES_DATA);
  reserve_critical();
  serial_print("[OK] PMM reclaimed boot-services blocks: ");
  serial_print_hex(max_blocks - before);
  serial_println("");
}

unsigned long long pmm_total_ram_bytes(void) { return total_ram_bytes; }

void *pmm_alloc_block(void) {
  if (used_blocks >= max_blocks)
    return 0;
  const unsigned int bytes = sizeof(memory_bitmap);
  for (unsigned int n = 0; n < bytes; n++) {
    unsigned int i = (search_hint / 8 + n) % bytes;
    if (memory_bitmap[i] == 0xFF)
      continue;
    for (unsigned int j = 0; j < 8; j++) {
      unsigned int b = i * 8 + j;
      if (!bit_test(b)) {
        bit_set(b);
        used_blocks++;
        search_hint = b + 1;
        return (void *)((unsigned long long)b * BLOCK_SIZE);
      }
    }
  }
  return 0;
}

void pmm_free_block(void *address) {
  unsigned long long addr = (unsigned long long)address;
  if (addr % BLOCK_SIZE || addr >= PMM_MAX_PHYS) {
    serial_println("[PMM] WARNING: free of invalid address ignored.");
    return;
  }
  unsigned int b = (unsigned int)(addr / BLOCK_SIZE);
  if (!bit_test(b)) {
    serial_println("[PMM] WARNING: double free ignored.");
    return;
  }
  bit_clear(b);
  used_blocks--;
  if (b < search_hint)
    search_hint = b;
}

void *dma_alloc_blocks(unsigned int num_blocks) {
  if (num_blocks == 0 || max_blocks - used_blocks < num_blocks)
    return 0;
  unsigned int run = 0, start = 0;
  for (unsigned int b = 0; b < PMM_MAX_BLOCKS; b++) {
    if (!bit_test(b)) {
      if (run == 0)
        start = b;
      if (++run == num_blocks) {
        for (unsigned int k = 0; k < num_blocks; k++)
          bit_set(start + k);
        used_blocks += num_blocks;
        return (void *)((unsigned long long)start * BLOCK_SIZE);
      }
    } else {
      run = 0;
    }
  }
  return 0;
}
