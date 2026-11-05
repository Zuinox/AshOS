#include "pci.h"

static pci_device_info_t pci_devices[MAX_PCI_DEVICES];
static int pci_device_count = 0;

int pci_get_device_count(void) { return pci_device_count; }

const pci_device_info_t* pci_get_device(int idx) {
    if (idx < 0 || idx >= pci_device_count) return 0;
    return &pci_devices[idx];
}

const char* pci_class_name(unsigned char base_class, unsigned char sub_class, unsigned char prog_if) {
    if (base_class == 0x06 && sub_class == 0x00) return "Host Bridge";
    if (base_class == 0x06 && sub_class == 0x01) return "ISA Bridge";
    if (base_class == 0x03) return "Display GPU";
    if (base_class == 0x0C && sub_class == 0x03 && prog_if == 0x30) return "xHCI USB 3.0";
    if (base_class == 0x0C && sub_class == 0x05) return "SMBus Ctrl";
    if (base_class == 0x01 && sub_class == 0x06) return "AHCI SATA";
    if (base_class == 0x02) return "Network NIC";
    return "PCI Device";
}

unsigned int pci_read(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset) {
    unsigned int address = (unsigned int)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(0xCF8, address);
    return inl(0xCFC);
}

void pci_write(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset, unsigned int value) {
    unsigned int address = (unsigned int)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(0xCF8, address);
    outl(0xCFC, value);
}

void pci_enable_msi(unsigned char bus, unsigned char slot, unsigned char func, unsigned char vector) {
    unsigned int status_cmd = pci_read(bus, slot, func, 0x04);
    if (!(status_cmd & (1 << 20))) return;

    unsigned int cap_ptr = pci_read(bus, slot, func, 0x34) & 0xFF;

    while (cap_ptr != 0) {
        unsigned int cap_hdr = pci_read(bus, slot, func, cap_ptr);
        unsigned char cap_id = cap_hdr & 0xFF;

        if (cap_id == 0x05) {
            serial_print("    |-- Wiring MSI Interrupt to Vector ");
            serial_print_hex(vector);
            serial_println("...");

            unsigned int msg_ctrl = (cap_hdr >> 16) & 0xFFFF;
            int is_64bit = (msg_ctrl & (1 << 7)) != 0;

            pci_write(bus, slot, func, cap_ptr + 4, 0xFEE00000);

            if (is_64bit) {
                pci_write(bus, slot, func, cap_ptr + 8, 0);
                pci_write(bus, slot, func, cap_ptr + 12, vector);
            } else {
                pci_write(bus, slot, func, cap_ptr + 8, vector);
            }

            unsigned int cap_reg = pci_read(bus, slot, func, cap_ptr);
            cap_reg |= (1 << 16);
            pci_write(bus, slot, func, cap_ptr, cap_reg);

            serial_println("    |-- USB Controller MSI Hardware Link: SUCCESS!");
            return;
        }
        cap_ptr = (cap_hdr >> 8) & 0xFF;
    }
}

void pci_scan_bus(void) {
    pci_device_count = 0;
    serial_println("--- SCANNING PCI BUS FOR HARDWARE ---");
    for (unsigned int bus = 0; bus < 256; bus++) {
        for (unsigned int slot = 0; slot < 32; slot++) {
            unsigned int id_reg = pci_read(bus, slot, 0, 0);
            unsigned short vendor = id_reg & 0xFFFF;
            if (vendor == 0xFFFF) continue;
            unsigned short device = (id_reg >> 16) & 0xFFFF;

            unsigned int class_info = pci_read(bus, slot, 0, 0x08);
            unsigned char base_class = (class_info >> 24) & 0xFF;
            unsigned char sub_class = (class_info >> 16) & 0xFF;
            unsigned char prog_if = (class_info >> 8) & 0xFF;

            if (pci_device_count < MAX_PCI_DEVICES) {
                pci_device_info_t* d = &pci_devices[pci_device_count++];
                d->bus = (unsigned char)bus;
                d->slot = (unsigned char)slot;
                d->func = 0;
                d->vendor_id = vendor;
                d->device_id = device;
                d->base_class = base_class;
                d->sub_class = sub_class;
                d->prog_if = prog_if;
            }

            if (base_class == 0x03) {
                serial_println("--> Found Graphics Controller!");
                unsigned int bar0 = pci_read(bus, slot, 0, 0x10);
                unsigned long long candidate_fb = bar0 & 0xFFFFFFF0ULL;

                serial_print("    |-- Framebuffer Physical Base: ");
                serial_print_hex(candidate_fb);
                serial_println("");
                if (candidate_fb != 0 && gpu_fb_base == 0) {
                    gpu_fb_base = candidate_fb;
                    vmm_map_mmio(gpu_fb_base, 256);
                }
            }

            if (base_class == 0x0C && sub_class == 0x03 && prog_if == 0x30) {
                serial_println("--> Found XHCI Controller!");

                unsigned int pci_cmd = pci_read(bus, slot, 0, 0x04);
                pci_cmd |= (1 << 1) | (1 << 2);
                pci_write(bus, slot, 0, 0x04, pci_cmd);
                serial_println("    |-- PCI Bus Mastering & Memory Space: ENABLED.");

                unsigned int bar0 = pci_read(bus, slot, 0, 0x10);
                unsigned int bar1 = pci_read(bus, slot, 0, 0x14);
                xhci_mmio_global = (bar0 & 0xFFFFFFF0) | ((unsigned long long)bar1 << 32);

                serial_print("    |-- XHCI MMIO Physical: ");
                serial_print_hex(xhci_mmio_global);
                serial_println("");

                vmm_map_mmio(xhci_mmio_global, 16);

                pci_enable_msi(bus, slot, 0, 33);
            }
        }
    }
}
