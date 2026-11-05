// src/stage1/idt.c
//
// Interrupt Descriptor Table.
//
//  * Vectors 0-31 (CPU exceptions) go through small assembly stubs that build
//    a full register snapshot (exception_frame_t) and call exception_handler(),
//    which prints a detailed crash report to serial AND to the screen.
//  * The double fault (#8) runs on its own IST stack (see gdt.c), so even a
//    kernel stack overflow is reported instead of silently triple-faulting.
//  * Vectors 32-255 are hardware/spurious interrupts. The kernel currently runs
//    with interrupts disabled (polling architecture), but every vector still
//    has a safe handler in case interrupts are enabled in the future.

#include "idt.h"
#include "gdt.h"
#include "lib.h"

static idt_entry_t idt[256];
static idtr_t idtr;

void idt_set_gate(unsigned char vector, void *isr, unsigned char ist) {
  unsigned long long isr_addr = (unsigned long long)isr;
  idt[vector].isr_low = isr_addr & 0xFFFF;
  idt[vector].kernel_cs = GDT_KERNEL_CODE;
  idt[vector].ist = ist & 0x7;
  idt[vector].attributes = 0x8E; // present, ring 0, 64-bit interrupt gate
  idt[vector].isr_mid = (isr_addr >> 16) & 0xFFFF;
  idt[vector].isr_high = (isr_addr >> 32) & 0xFFFFFFFF;
  idt[vector].reserved = 0;
}

// ---------------------------------------------------------------------------
// Exception stubs. CPUs push an error code only for some vectors; for the
// others we push a dummy 0 so every frame has the same layout.
// ---------------------------------------------------------------------------
#define ISR_NOERR(n)                                                           \
  ".global isr_stub_" #n "\n"                                                  \
  "isr_stub_" #n ":\n"                                                         \
  "    pushq $0\n"                                                             \
  "    pushq $" #n "\n"                                                        \
  "    jmp isr_common\n"
#define ISR_ERR(n)                                                             \
  ".global isr_stub_" #n "\n"                                                  \
  "isr_stub_" #n ":\n"                                                         \
  "    pushq $" #n "\n"                                                        \
  "    jmp isr_common\n"

__asm__(".text\n"
        ISR_NOERR(0) ISR_NOERR(1) ISR_NOERR(2) ISR_NOERR(3) ISR_NOERR(4)
        ISR_NOERR(5) ISR_NOERR(6) ISR_NOERR(7) ISR_ERR(8) ISR_NOERR(9)
        ISR_ERR(10) ISR_ERR(11) ISR_ERR(12) ISR_ERR(13) ISR_ERR(14)
        ISR_NOERR(15) ISR_NOERR(16) ISR_ERR(17) ISR_NOERR(18) ISR_NOERR(19)
        ISR_NOERR(20) ISR_ERR(21) ISR_NOERR(22) ISR_NOERR(23) ISR_NOERR(24)
        ISR_NOERR(25) ISR_NOERR(26) ISR_NOERR(27) ISR_NOERR(28) ISR_ERR(29)
        ISR_ERR(30) ISR_NOERR(31)
        "isr_common:\n"
        "    pushq %rax\n pushq %rbx\n pushq %rcx\n pushq %rdx\n"
        "    pushq %rsi\n pushq %rdi\n pushq %rbp\n"
        "    pushq %r8\n  pushq %r9\n  pushq %r10\n pushq %r11\n"
        "    pushq %r12\n pushq %r13\n pushq %r14\n pushq %r15\n"
        "    cld\n"
        "    movq %rsp, %rdi\n"      // exception_frame_t *
        "    movq %rsp, %rbx\n"      // remember unaligned rsp
        "    andq $-16, %rsp\n"      // SysV ABI: 16-byte aligned call
        "    call exception_handler\n"
        "    movq %rbx, %rsp\n"
        "    popq %r15\n popq %r14\n popq %r13\n popq %r12\n"
        "    popq %r11\n popq %r10\n popq %r9\n  popq %r8\n"
        "    popq %rbp\n popq %rdi\n popq %rsi\n"
        "    popq %rdx\n popq %rcx\n popq %rbx\n popq %rax\n"
        "    addq $16, %rsp\n"       // drop vector + error code
        "    iretq\n");

#define DECLARE_STUB(n) extern void isr_stub_##n(void);
DECLARE_STUB(0) DECLARE_STUB(1) DECLARE_STUB(2) DECLARE_STUB(3)
DECLARE_STUB(4) DECLARE_STUB(5) DECLARE_STUB(6) DECLARE_STUB(7)
DECLARE_STUB(8) DECLARE_STUB(9) DECLARE_STUB(10) DECLARE_STUB(11)
DECLARE_STUB(12) DECLARE_STUB(13) DECLARE_STUB(14) DECLARE_STUB(15)
DECLARE_STUB(16) DECLARE_STUB(17) DECLARE_STUB(18) DECLARE_STUB(19)
DECLARE_STUB(20) DECLARE_STUB(21) DECLARE_STUB(22) DECLARE_STUB(23)
DECLARE_STUB(24) DECLARE_STUB(25) DECLARE_STUB(26) DECLARE_STUB(27)
DECLARE_STUB(28) DECLARE_STUB(29) DECLARE_STUB(30) DECLARE_STUB(31)

static void (*const exception_stubs[32])(void) = {
    isr_stub_0,  isr_stub_1,  isr_stub_2,  isr_stub_3,  isr_stub_4,
    isr_stub_5,  isr_stub_6,  isr_stub_7,  isr_stub_8,  isr_stub_9,
    isr_stub_10, isr_stub_11, isr_stub_12, isr_stub_13, isr_stub_14,
    isr_stub_15, isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19,
    isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23, isr_stub_24,
    isr_stub_25, isr_stub_26, isr_stub_27, isr_stub_28, isr_stub_29,
    isr_stub_30, isr_stub_31};

static const char *const exception_names[32] = {
    "DIVIDE ERROR",         "DEBUG",
    "NMI",                  "BREAKPOINT",
    "OVERFLOW",             "BOUND RANGE EXCEEDED",
    "INVALID OPCODE",       "DEVICE NOT AVAILABLE",
    "DOUBLE FAULT",         "COPROCESSOR SEGMENT OVERRUN",
    "INVALID TSS",          "SEGMENT NOT PRESENT",
    "STACK-SEGMENT FAULT",  "GENERAL PROTECTION FAULT",
    "PAGE FAULT",           "RESERVED",
    "x87 FLOATING-POINT",   "ALIGNMENT CHECK",
    "MACHINE CHECK",        "SIMD FLOATING-POINT",
    "VIRTUALIZATION",       "CONTROL PROTECTION",
    "RESERVED",             "RESERVED",
    "RESERVED",             "RESERVED",
    "RESERVED",             "RESERVED",
    "HYPERVISOR INJECTION", "VMM COMMUNICATION",
    "SECURITY EXCEPTION",   "RESERVED"};

// ---------------------------------------------------------------------------
// Crash reporting
// ---------------------------------------------------------------------------
static volatile int in_panic = 0;

static void hex_to_str(unsigned long long v, char *out) {
  const char *digits = "0123456789ABCDEF";
  out[0] = '0';
  out[1] = 'x';
  for (int i = 15; i >= 0; i--) {
    out[2 + i] = digits[v & 0xF];
    v >>= 4;
  }
  out[18] = '\0';
}

static void serial_reg(const char *name, unsigned long long v) {
  serial_print(name);
  serial_print_hex(v);
  serial_println("");
}

// Paints a red panic box with up to `count` lines of text, then presents it.
static void draw_panic_screen(const char *title, const char *const *lines,
                              int count) {
  if (!video_is_ready())
    return;
  int w = (int)fb_width, h = (int)fb_height;
  draw_rect(0, 0, w, h, 0x400000);
  draw_rect(40, 40, w - 80, 60, 0xAA0000);
  draw_string(60, 58, "KERNEL PANIC - SubhashishOS halted", 0xFFFFFF, 3);
  draw_string(60, 130, title, 0xFFDD55, 2);
  for (int i = 0; i < count; i++)
    draw_string(60, 170 + i * 26, lines[i], 0xFFFFFF, 2);
  draw_string(60, h - 60, "Full register dump was written to the serial log.",
              0xBBBBBB, 2);
  video_swap();
}

__attribute__((noreturn)) static void halt_forever(void) {
  for (;;)
    __asm__ volatile("cli; hlt");
}

__attribute__((noreturn)) void kernel_panic(const char *msg) {
  __asm__ volatile("cli");
  serial_println("\r\n!!! KERNEL PANIC !!!");
  serial_print("    |-- ");
  serial_println(msg);
  if (!in_panic) {
    in_panic = 1;
    const char *lines[1] = {msg};
    draw_panic_screen("Fatal error:", lines, 1);
  }
  halt_forever();
}

void exception_handler(exception_frame_t *f) {
  unsigned long long cr2, cr3;
  __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
  const char *name =
      (f->vector < 32) ? exception_names[f->vector] : "UNKNOWN EXCEPTION";

  serial_println("\r\n!!! CPU EXCEPTION !!!");
  serial_print("    |-- ");
  serial_println(name);
  serial_reg("    |-- Vector:     ", f->vector);
  serial_reg("    |-- Error code: ", f->error_code);
  serial_reg("    |-- RIP:        ", f->rip);
  serial_reg("    |-- CS:         ", f->cs);
  serial_reg("    |-- RFLAGS:     ", f->rflags);
  serial_reg("    |-- RSP:        ", f->rsp);
  serial_reg("    |-- SS:         ", f->ss);
  serial_reg("    |-- CR2:        ", cr2);
  serial_reg("    |-- CR3:        ", cr3);
  serial_reg("    |-- RAX: ", f->rax);
  serial_reg("    |-- RBX: ", f->rbx);
  serial_reg("    |-- RCX: ", f->rcx);
  serial_reg("    |-- RDX: ", f->rdx);
  serial_reg("    |-- RSI: ", f->rsi);
  serial_reg("    |-- RDI: ", f->rdi);
  serial_reg("    |-- RBP: ", f->rbp);
  serial_reg("    |-- R8:  ", f->r8);
  serial_reg("    |-- R9:  ", f->r9);
  serial_reg("    |-- R10: ", f->r10);
  serial_reg("    |-- R11: ", f->r11);
  serial_reg("    |-- R12: ", f->r12);
  serial_reg("    |-- R13: ", f->r13);
  serial_reg("    |-- R14: ", f->r14);
  serial_reg("    |-- R15: ", f->r15);

  if (f->vector == 14) {
    serial_print("    |-- Page fault cause: ");
    serial_print((f->error_code & 1) ? "protection violation" : "page not present");
    serial_print((f->error_code & 2) ? ", write" : ", read");
    serial_println((f->error_code & 16) ? ", instruction fetch" : "");
  }

  // Nested fault while reporting: don't touch video again.
  if (!in_panic) {
    in_panic = 1;
    static char l_vec[48], l_err[48], l_rip[48], l_cr2[48], l_rsp[48];
    char hex[19];
    kstrlcpy(l_vec, "Vector:     ", sizeof(l_vec));
    kutoa(f->vector, hex);
    kstrlcat(l_vec, hex, sizeof(l_vec));
    hex_to_str(f->error_code, hex);
    kstrlcpy(l_err, "Error code: ", sizeof(l_err));
    kstrlcat(l_err, hex, sizeof(l_err));
    hex_to_str(f->rip, hex);
    kstrlcpy(l_rip, "RIP:        ", sizeof(l_rip));
    kstrlcat(l_rip, hex, sizeof(l_rip));
    hex_to_str(cr2, hex);
    kstrlcpy(l_cr2, "CR2:        ", sizeof(l_cr2));
    kstrlcat(l_cr2, hex, sizeof(l_cr2));
    hex_to_str(f->rsp, hex);
    kstrlcpy(l_rsp, "RSP:        ", sizeof(l_rsp));
    kstrlcat(l_rsp, hex, sizeof(l_rsp));
    const char *lines[5] = {l_vec, l_err, l_rip, l_cr2, l_rsp};
    draw_panic_screen(name, lines, 5);
  }

  // All CPU exceptions are treated as fatal in this kernel.
  halt_forever();
}

// ---------------------------------------------------------------------------
// Hardware / spurious interrupt handlers (vectors 32-255)
// ---------------------------------------------------------------------------
__attribute__((interrupt)) static void irq_unhandled_isr(
    struct interrupt_frame *frame) {
  (void)frame;
  apic_send_eoi();
}

// LAPIC spurious vector: must NOT send an EOI.
__attribute__((interrupt)) static void irq_spurious_isr(
    struct interrupt_frame *frame) {
  (void)frame;
}

volatile int usb_interrupt_fired = 0;

// USB controller MSI (vector 33). The compiler-generated prologue saves all
// registers and keeps the stack aligned (the old hand-written stub did not).
__attribute__((interrupt)) static void usb_msi_isr(
    struct interrupt_frame *frame) {
  (void)frame;
  usb_interrupt_fired = 1;
  apic_send_eoi();
}

void idt_init(void) {
  for (int i = 32; i < 256; i++)
    idt_set_gate((unsigned char)i, (void *)irq_unhandled_isr, 0);
  for (int i = 0; i < 32; i++)
    idt_set_gate((unsigned char)i, (void *)exception_stubs[i],
                 i == 8 ? IST_DOUBLE_FAULT : 0);
  idt_set_gate(33, (void *)usb_msi_isr, 0);
  idt_set_gate(0xFF, (void *)irq_spurious_isr, 0);

  idtr.base = (unsigned long long)&idt[0];
  idtr.limit = (unsigned short)(sizeof(idt_entry_t) * 256 - 1);
  __asm__ volatile("lidt %0" : : "m"(idtr));
  serial_println("[OK] IDT loaded (32 exception handlers with crash reports).");
}
