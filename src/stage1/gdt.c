// src/stage1/gdt.c
//
// After ExitBootServices the CPU is still using the firmware's GDT, which
// lives in memory UEFI considers "boot services data" -- memory the kernel
// later hands out. We therefore install our own GDT with:
//   0x00 null | 0x08 64-bit kernel code | 0x10 kernel data | 0x18 TSS (16 bytes)
// The TSS provides an Interrupt Stack Table entry for the double-fault handler.

#include "gdt.h"
#include "hardware.h"

typedef struct __attribute__((packed)) {
  unsigned int reserved0;
  unsigned long long rsp[3];
  unsigned long long reserved1;
  unsigned long long ist[7];
  unsigned long long reserved2;
  unsigned short reserved3;
  unsigned short iomap_base;
} tss_t;

typedef struct __attribute__((packed)) {
  unsigned short limit;
  unsigned long long base;
} gdtr_t;

static unsigned long long gdt[5] __attribute__((aligned(16)));
static tss_t tss __attribute__((aligned(16)));
static gdtr_t gdtr;

#define DF_STACK_SIZE (16 * 1024)
static unsigned char double_fault_stack[DF_STACK_SIZE] __attribute__((aligned(16)));

void gdt_init(void) {
  gdt[0] = 0;
  gdt[1] = 0x00AF9A000000FFFFULL; // present, ring0, code, long mode (L=1)
  gdt[2] = 0x00CF92000000FFFFULL; // present, ring0, data, writable

  for (unsigned int i = 0; i < sizeof(tss); i++)
    ((unsigned char *)&tss)[i] = 0;
  tss.ist[IST_DOUBLE_FAULT - 1] =
      ((unsigned long long)&double_fault_stack[DF_STACK_SIZE]) & ~0xFULL;
  tss.iomap_base = sizeof(tss_t); // no I/O permission bitmap

  unsigned long long base = (unsigned long long)&tss;
  unsigned long long limit = sizeof(tss_t) - 1;
  gdt[3] = (limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) |
           (0x89ULL << 40) |                // present, 64-bit TSS (available)
           (((limit >> 16) & 0xF) << 48) | (((base >> 24) & 0xFF) << 56);
  gdt[4] = base >> 32;

  gdtr.limit = sizeof(gdt) - 1;
  gdtr.base = (unsigned long long)&gdt[0];

  __asm__ volatile("lgdt %0\n"
                   // Reload CS with a far return.
                   "pushq %1\n"
                   "leaq 1f(%%rip), %%rax\n"
                   "pushq %%rax\n"
                   "lretq\n"
                   "1:\n"
                   "movw %w2, %%ax\n"
                   "movw %%ax, %%ds\n"
                   "movw %%ax, %%es\n"
                   "movw %%ax, %%ss\n"
                   "movw %%ax, %%fs\n"
                   "movw %%ax, %%gs\n"
                   "ltr %w3\n"
                   :
                   : "m"(gdtr), "i"(GDT_KERNEL_CODE), "r"(GDT_KERNEL_DATA),
                     "r"(GDT_TSS)
                   : "rax", "memory");

  serial_println("[OK] Kernel GDT + TSS loaded (double-fault IST armed).");
}

