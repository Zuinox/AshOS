// src/stage1/start.c
//
// The very first code that runs in the kernel.
//
// The bootloader calls this as `void entry(BootInfo *)` using the System V
// ABI, so the BootInfo pointer arrives in RDI. At that moment we are still
// running on the UEFI firmware's stack, which lives in memory the kernel does
// not own. _start therefore:
//   1. disables interrupts,
//   2. zeroes .bss (it is not stored in kernel.bin, so it contains garbage),
//   3. switches to the kernel's own 64KB stack (inside .bss),
//   4. calls kmain(boot_info) and halts forever if it ever returns.

#define KERNEL_STACK_SIZE (64 * 1024)

__attribute__((aligned(16))) unsigned char kernel_stack[KERNEL_STACK_SIZE];

__asm__(
    ".section .text.entry, \"ax\", @progbits\n"
    ".global _start\n"
    "_start:\n"
    "    cli\n"
    "    cld\n"
    "    mov  %rdi, %r12\n"               // preserve BootInfo* (callee-saved)
    // --- zero .bss ---
    "    lea  __bss_start(%rip), %rdi\n"
    "    lea  __bss_end(%rip), %rcx\n"
    "    sub  %rdi, %rcx\n"
    "    xor  %eax, %eax\n"
    "    rep  stosb\n"
    // --- switch to the kernel stack ---
    "    lea  kernel_stack(%rip), %rsp\n"
    "    add  $" "65536" ", %rsp\n"       // KERNEL_STACK_SIZE
    "    and  $-16, %rsp\n"
    "    xor  %ebp, %ebp\n"               // terminate stack traces
    "    mov  %r12, %rdi\n"
    "    call kmain\n"
    "1:  cli\n"
    "    hlt\n"
    "    jmp  1b\n"
    ".text\n");

_Static_assert(KERNEL_STACK_SIZE == 65536,
               "update the immediate in _start if you change the stack size");

