#include "apic.h"

unsigned int apic_read(unsigned int offset) {
    return *((volatile unsigned int*)((unsigned long long)lapic_phys_addr + offset));
}

void apic_write(unsigned int offset, unsigned int value) {
    *((volatile unsigned int*)((unsigned long long)lapic_phys_addr + offset)) = value;
}

void apic_init() {
    serial_println("\n--- ENABLING LOCAL APIC ---");
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
    serial_println("    |-- Legacy 8259 PIC successfully disabled.");

    unsigned int version = apic_read(LAPIC_VERSION);
    serial_print("    |-- APIC Hardware Version: ");
    serial_print_hex(version);
    serial_println("");

    apic_write(0x0080, 0); 
    apic_write(LAPIC_SPURIOUS, 0x1FF);
    serial_println("    |-- Local APIC Software Enable: SUCCESS!");
}

void apic_send_eoi() {
    apic_write(LAPIC_EOI, 0); 
}
