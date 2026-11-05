#include "acpi.h"

int compare_guid(efi_guid_t* g1, const efi_guid_t* g2) {
    if (g1->Data1 != g2->Data1) return 0; 
    if (g1->Data2 != g2->Data2) return 0;
    if (g1->Data3 != g2->Data3) return 0;
    for (int i = 0; i < 8; i++) { if (g1->Data4[i] != g2->Data4[i]) return 0; }
    return 1;
}

void acpi_parse_xsdt(unsigned long long xsdt_phys) {
    serial_println("\n--- SCANNING XSDT FOR MADT ---");
    
    acpi_header_t* xsdt = (acpi_header_t*)xsdt_phys;
    int entries = (xsdt->length - sizeof(acpi_header_t)) / 8;
    unsigned long long* table_ptrs = (unsigned long long*)(xsdt_phys + sizeof(acpi_header_t));

    for (int i = 0; i < entries; i++) {
        acpi_header_t* table = (acpi_header_t*)table_ptrs[i];
        
        if (table->signature[0] == 'A' && table->signature[1] == 'P' && 
            table->signature[2] == 'I' && table->signature[3] == 'C') {
            
            serial_println("--> MADT (APIC) TABLE LOCATED!");
            madt_t* madt = (madt_t*)table;
            lapic_phys_addr = madt->local_apic_address;

            unsigned char* entry_ptr = (unsigned char*)table + 44;
            unsigned char* end_ptr = (unsigned char*)table + table->length;
            
            while (entry_ptr < end_ptr) {
                madt_entry_header_t* entry = (madt_entry_header_t*)entry_ptr;
                if (entry->type == 1) { 
                    madt_io_apic_t* ioapic = (madt_io_apic_t*)entry;
                    io_apic_phys_addr = ioapic->io_apic_address;
                    serial_print("    |-- Found I/O APIC at: ");
                    serial_print_hex(io_apic_phys_addr);
                    serial_println("");
                } 
                entry_ptr += entry->length;
            }
            return;
        }
    }
}

unsigned long long acpi_find_rsdp(void* system_table) {
    serial_println("\n--- HUNTING FOR ACPI TABLES ---");
    efi_system_table_t* efi_st = (efi_system_table_t*)system_table;
    unsigned long long entries = efi_st->num_table_entries;
    efi_config_table_t* config_table = (efi_config_table_t*)efi_st->config_table_ptr;

    for (unsigned long long i = 0; i < entries; i++) {
        if (compare_guid(&config_table[i].guid, &ACPI_20_TABLE_GUID)) {
            serial_println("--> ACPI 2.0 Table Found!");
            return config_table[i].table;
        }
    }
    return 0;
}
