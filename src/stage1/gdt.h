#pragma once
// src/stage1/gdt.h - the kernel's own Global Descriptor Table and TSS.

#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_TSS         0x18

// Interrupt Stack Table slot used for the double-fault handler, so a kernel
// stack overflow produces a readable crash report instead of a triple fault.
#define IST_DOUBLE_FAULT 1

void gdt_init(void);

