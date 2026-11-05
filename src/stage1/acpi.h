#pragma once
#include "hardware.h"



typedef struct {
  unsigned int Data1;
  unsigned short Data2;
  unsigned short Data3;
  unsigned char Data4[8];
} __attribute__((packed)) efi_guid_t;

typedef struct {
  efi_guid_t guid;
  unsigned long long table;
} __attribute__((packed)) efi_config_table_t;

typedef struct {
  unsigned char header[24];
  unsigned long long fw_vendor;
  unsigned int fw_revision;
  unsigned int padding;
  unsigned long long con_in_handle, con_in;
  unsigned long long con_out_handle, con_out;
  unsigned long long std_err_handle, std_err;
  unsigned long long runtime_services;
  unsigned long long boot_services;
  unsigned long long num_table_entries;
  unsigned long long config_table_ptr;
} __attribute__((packed)) efi_system_table_t;

typedef struct {
  char signature[4];
  unsigned int length;
  unsigned char revision;
  unsigned char checksum;
  char oem_id[6];
  char oem_table_id[8];
  unsigned int oem_revision;
  unsigned int creator_id;
  unsigned int creator_revision;
} __attribute__((packed)) acpi_header_t;

typedef struct {
  char signature[8];
  unsigned char checksum;
  char oem_id[6];
  unsigned char revision;
  unsigned int rsdt_address;
  unsigned int length;
  unsigned long long xsdt_address;
  unsigned char extended_checksum;
  unsigned char reserved[3];
} __attribute__((packed)) rsdp_t;

typedef struct {
  acpi_header_t header;
  unsigned int local_apic_address;
  unsigned int flags;
} __attribute__((packed)) madt_t;

typedef struct {
  unsigned char type;
  unsigned char length;
} __attribute__((packed)) madt_entry_header_t;

typedef struct {
  madt_entry_header_t header;
  unsigned char io_apic_id;
  unsigned char reserved;
  unsigned int io_apic_address;
  unsigned int global_system_interrupt_base;
} __attribute__((packed)) madt_io_apic_t;

extern unsigned int io_apic_phys_addr;
extern unsigned int lapic_phys_addr;

static const efi_guid_t ACPI_20_TABLE_GUID = {
    0x8868e871,
    0xe4f1,
    0x11d3,
    {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}};

int compare_guid(efi_guid_t *g1, const efi_guid_t *g2);
void acpi_parse_xsdt(unsigned long long xsdt_phys);
unsigned long long acpi_find_rsdp(void *system_table);