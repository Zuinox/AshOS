ENTRY(payload_main)
SECTIONS
{
    . = 0x0;
    .text : {
        *(.text.entry)
        *(.text .text.*)
    }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
}