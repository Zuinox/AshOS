#pragma once
#include "hardware.h"
#include "video.h"
#include "apic.h"

typedef struct {
    unsigned short isr_low;
    unsigned short kernel_cs;
    unsigned char  ist;
    unsigned char  attributes;
    unsigned short isr_mid;
    unsigned int   isr_high;
    unsigned int   reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    unsigned short limit;
    unsigned long long base;
} __attribute__((packed)) idtr_t;

// Frame pushed by the CPU for __attribute__((interrupt)) handlers.
struct interrupt_frame {
    unsigned long long ip, cs, flags, sp, ss;
};

// Full register snapshot built by the exception stubs in idt.c.
// Field order mirrors the push order (last pushed = lowest address).
typedef struct {
    unsigned long long r15, r14, r13, r12, r11, r10, r9, r8;
    unsigned long long rbp, rdi, rsi, rdx, rcx, rbx, rax;
    unsigned long long vector, error_code;
    unsigned long long rip, cs, rflags, rsp, ss;
} exception_frame_t;

void idt_init(void);
void idt_set_gate(unsigned char vector, void *isr, unsigned char ist);

extern volatile int usb_interrupt_fired;
