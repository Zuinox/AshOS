/* src/stage1/kernel.ld
 *
 * Kernel linker script. The bootloader (su_bootloader/payload.c) loads the
 * flat kernel.bin at exactly KERNEL_PHYS_BASE (16MB) using AllocateAddress,
 * so the kernel is linked (non-PIC) to run at that address.
 *
 * Layout:  [.text.entry (_start) | .text | .rodata | .data | .bss]
 * .bss is NOT stored in kernel.bin; _start zeroes __bss_start..__bss_end.
 */
OUTPUT_FORMAT("elf64-x86-64")
ENTRY(_start)

KERNEL_PHYS_BASE = 0x1000000;

SECTIONS
{
    . = KERNEL_PHYS_BASE;
    __kernel_start = .;

    .text : {
        KEEP(*(.text.entry))      /* _start MUST be the first byte */
        *(.text .text.*)
    }

    .rodata ALIGN(4096) : { *(.rodata .rodata.*) }

    .data ALIGN(4096) : { *(.data .data.*) }

    .bss ALIGN(4096) (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    . = ALIGN(4096);
    __kernel_end = .;

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
