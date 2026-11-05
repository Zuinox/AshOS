#pragma once
#include "hardware.h"
#include "pmm.h"

static inline void memory_barrier() { __asm__ volatile ("mfence" ::: "memory"); }

#define PORTSC_CCS (1 << 0)
#define PORTSC_PP  (1 << 9)
#define PORTSC_RW1C_MASK 0x00FE0002  // Bits 17-23 (change bits) + Bit 1 (PED)

#define STATE_IDLE 0
#define STATE_PORT_READY 1
#define STATE_SLOT_ENABLED 2
#define STATE_ADDRESSED 3
#define STATE_DESC_FETCHED 4
#define STATE_CONFIGURING 5
#define STATE_CONFIGURED 6

typedef struct __attribute__((packed)) {
    unsigned char  cap_length;
    unsigned char  reserved;
    unsigned short hci_version;
    unsigned int   hcs_params1, hcs_params2, hcs_params3, hcc_params1;
    unsigned int   db_off, rts_off, hcc_params2;
} xhci_cap_regs_t;

typedef struct __attribute__((packed)) {
    unsigned int usb_cmd, usb_sts, page_size;
    unsigned char res1[8];
    unsigned int dnctrl;
    unsigned long long crcr;
    unsigned char res2[16];
    unsigned long long dcbaap;
    unsigned int config;
} xhci_op_regs_t;

typedef struct __attribute__((packed)) {
    unsigned int iman, imod, erstsz, res;
    unsigned long long erstba, erdp;
} xhci_intr_regs_t;

typedef struct __attribute__((packed)) {
    unsigned long long ptr;
    unsigned int size, res;
} erst_entry_t;

typedef struct __attribute__((packed)) {
    unsigned int data_low, data_high, status, control;
} xhci_trb_t;

void xhci_init(unsigned long long base);
void xhci_check_event(unsigned long long base);