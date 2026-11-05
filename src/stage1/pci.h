#pragma once
#include "hardware.h"
#include "vmm.h"
#include "xhci.h"

#define MAX_PCI_DEVICES 16

typedef struct {
    unsigned char bus, slot, func;
    unsigned short vendor_id, device_id;
    unsigned char base_class, sub_class, prog_if;
} pci_device_info_t;

extern unsigned long long xhci_mmio_global;
extern unsigned long long gpu_fb_base;

unsigned int pci_read(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset);
void pci_write(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset, unsigned int value);
void pci_enable_msi(unsigned char bus, unsigned char slot, unsigned char func, unsigned char vector);
void pci_scan_bus(void);
int pci_get_device_count(void);
const pci_device_info_t* pci_get_device(int idx);
const char* pci_class_name(unsigned char base_class, unsigned char sub_class, unsigned char prog_if);
