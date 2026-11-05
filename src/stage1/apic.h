#pragma once
#include "hardware.h"
#include "acpi.h"

#define LAPIC_ID          0x0020
#define LAPIC_VERSION     0x0030
#define LAPIC_EOI         0x00B0
#define LAPIC_SPURIOUS    0x00F0
#define LAPIC_TIMER       0x0320
#define LAPIC_TIMER_INIT  0x0380
#define LAPIC_TIMER_CURR  0x0390
#define LAPIC_TIMER_DIV   0x03E0

unsigned int apic_read(unsigned int offset);
void apic_write(unsigned int offset, unsigned int value);
void apic_init(void);
void apic_send_eoi(void);