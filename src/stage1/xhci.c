#include "xhci.h"
#include "vmm.h"
#include "timer.h"

// Bounded poll helper: returns 0 if (*reg & mask) == expected before timeout_ms, -1 on timeout
static int xhci_wait_bits(volatile xhci_op_regs_t *ops, int is_sts,
                          unsigned int mask, unsigned int expected,
                          unsigned long long timeout_ms) {
  unsigned long long start = timer_ms();
  while (((is_sts ? ops->usb_sts : ops->usb_cmd) & mask) != expected) {
    memory_barrier();
    cpu_pause();
    if (timer_ms() - start > timeout_ms) {
      return -1;
    }
  }
  return 0;
}

typedef struct {
  int state;
  int type; // 1 = Keyboard, 2 = Mouse
  int port_id; // Which root hub port this device is on
  unsigned char *in_ctx;
  unsigned char *out_ctx;
  xhci_trb_t *xfer_ring;
  xhci_trb_t *ep1_ring;
  unsigned char *buffer;
  int xfer_idx;
  int ep1_idx;
  unsigned char xfer_cyc;
  unsigned char ep1_cyc;
} usb_device_t;

static usb_device_t devices[16]; // Support up to 16 slots

static volatile xhci_trb_t *cmd_ring = 0;
static volatile xhci_trb_t *evt_ring = 0;
static volatile erst_entry_t *erst = 0;
static volatile unsigned long long *dcbaap = 0;

static int cmd_idx = 0, evt_idx = 0;
static unsigned char cmd_cyc = 1, evt_cyc = 1;

// Active port being enumerated (set when we issue Enable Slot)
static int pending_port_id = 0;

// Global XHCI base for use in helpers
static unsigned long long xhci_base = 0;

static inline int xhci_has_event() {
  memory_barrier();
  return ((evt_ring[evt_idx].control & 1) == evt_cyc);
}

static inline void xhci_commit_cmd(unsigned int dlo, unsigned int dhi,
                                   unsigned int sta, unsigned int ctrl) {
  cmd_ring[cmd_idx].data_low = dlo;
  cmd_ring[cmd_idx].data_high = dhi;
  cmd_ring[cmd_idx].status = sta;
  memory_barrier();
  cmd_ring[cmd_idx].control = ctrl | cmd_cyc;
  memory_barrier();
  cmd_idx++;
  if (cmd_idx >= 255) {
    cmd_ring[255].control = (6 << 10) | (1 << 1) | cmd_cyc; // Link TRB with TC
    cmd_idx = 0;
    cmd_cyc ^= 1;
  }
}

static inline void xhci_commit_xfer(int slot, unsigned int dlo,
                                    unsigned int dhi, unsigned int sta,
                                    unsigned int ctrl) {
  usb_device_t *d = &devices[slot];
  d->xfer_ring[d->xfer_idx].data_low = dlo;
  d->xfer_ring[d->xfer_idx].data_high = dhi;
  d->xfer_ring[d->xfer_idx].status = sta;
  memory_barrier();
  d->xfer_ring[d->xfer_idx].control = ctrl | d->xfer_cyc;
  memory_barrier();
  d->xfer_idx++;
  if (d->xfer_idx >= 255) {
    d->xfer_ring[255].data_low = (unsigned int)(unsigned long long)d->xfer_ring;
    d->xfer_ring[255].data_high = 0;
    d->xfer_ring[255].status = 0;
    memory_barrier();
    d->xfer_ring[255].control = (6 << 10) | (1 << 1) | d->xfer_cyc;
    memory_barrier();
    d->xfer_idx = 0;
    d->xfer_cyc ^= 1;
  }
}

static inline void xhci_ring_cmd_db() {
  volatile unsigned int *dbs = (volatile unsigned int *)(xhci_base +
      ((volatile xhci_cap_regs_t *)xhci_base)->db_off);
  memory_barrier();
  dbs[0] = 0;
}

static inline void xhci_ring_ep_db(int slot, int target) {
  volatile unsigned int *dbs = (volatile unsigned int *)(xhci_base +
      ((volatile xhci_cap_regs_t *)xhci_base)->db_off);
  memory_barrier();
  dbs[slot] = target;
}

// Issue Enable Slot command for a port
static void xhci_enable_slot_for_port(int port_id) {
  pending_port_id = port_id;
  xhci_commit_cmd(0, 0, 0, (9 << 10));
  xhci_ring_cmd_db();
}

static void xhci_scan_for_devices() {
  if (pending_port_id != 0) return; // Only process one enumeration at a time

  volatile xhci_cap_regs_t *caps = (volatile xhci_cap_regs_t *)xhci_base;
  volatile xhci_op_regs_t *ops =
      (volatile xhci_op_regs_t *)(xhci_base + caps->cap_length);
  unsigned int max_ports = (caps->hcs_params1 >> 24) & 0xFF;

  for (unsigned int i = 0; i < max_ports; i++) {
    volatile unsigned int *portsc =
        (volatile unsigned int *)((unsigned char *)ops + 0x400 + (i * 0x10));
    unsigned int psc = *portsc;

    if (psc & PORTSC_CCS) {
      int has_slot = 0;
      for (int s = 1; s < 16; s++) {
        if (devices[s].state != STATE_IDLE && devices[s].port_id == (i + 1)) {
          has_slot = 1;
          break;
        }
      }
      if (!has_slot) {
        if (!(psc & (1 << 1))) {
          // Reset port
          unsigned int np = psc & ~PORTSC_RW1C_MASK;
          *portsc = np | (1 << 4) | PORTSC_PP;
          memory_barrier();
          mdelay(20);
          np = *portsc;
          *portsc = (np & ~PORTSC_RW1C_MASK) | PORTSC_PP | (np & PORTSC_RW1C_MASK);
          memory_barrier();
        }
        serial_print("[XHCI] Automagic Device found, enabling slot for port: ");
        serial_print_hex(i + 1);
        serial_println("");
        xhci_enable_slot_for_port(i + 1);
        break; // Process one at a time
      }
    }
  }
}


static void xhci_address_device(int slot) {
  usb_device_t *d = &devices[slot];
  d->state = STATE_SLOT_ENABLED;

  volatile xhci_cap_regs_t *caps = (volatile xhci_cap_regs_t *)xhci_base;
  volatile xhci_op_regs_t *ops =
      (volatile xhci_op_regs_t *)(xhci_base + caps->cap_length);
  int csz = (caps->hcc_params1 >> 2) & 1;
  int shift = csz ? 6 : 5; // 64 or 32 byte contexts

  volatile unsigned int *portsc =
      (volatile unsigned int *)((unsigned char *)ops + 0x400 +
                                ((d->port_id - 1) * 0x10));
  unsigned int speed = (*portsc >> 10) & 0xF;

  // Max packet size for EP0 based on speed
  unsigned int max_packet = 8;
  if (speed == 4)      max_packet = 512; // SuperSpeed
  else if (speed == 3) max_packet = 64;  // High Speed
  else if (speed == 1) max_packet = 64;  // Full Speed (commonly 64)

  serial_print("[XHCI] Slot ");
  serial_print_hex(slot);
  serial_print(" Port ");
  serial_print_hex(d->port_id);
  serial_print(" Speed ");
  serial_print_hex(speed);
  serial_println(" Addressing...");

  // Point DCBAAP entry to the output context
  dcbaap[slot] = (unsigned long long)d->out_ctx;

  // Zero the input context
  for (int i = 0; i < 2048; i++)
    d->in_ctx[i] = 0;

  // --- Input Control Context (first context-sized block) ---
  // Bit 0 = A0 = Add Slot Context
  // Bit 1 = A1 = Add EP0 Context (DCI 1)
  ((unsigned int *)d->in_ctx)[0] = 0x00000000; // D (Drop)
  ((unsigned int *)d->in_ctx)[1] = 0x00000003; // A (Add): Slot + EP0

  // --- Slot Context (second block) ---
  unsigned int *sc = (unsigned int *)(d->in_ctx + (1 << shift));
  sc[0] = (1 << 27)          // Context Entries = 1 (highest enabled DCI)
        | (speed << 20)       // Port Speed
        | 0;                  // Route String = 0 (root port device)
  sc[1] = (d->port_id << 16); // Root Hub Port Number
  sc[2] = 0;
  sc[3] = 0;

  // --- EP0 (Control) Context (third block, DCI 1) ---
  unsigned int *ep0 = (unsigned int *)(d->in_ctx + (2 << shift));
  ep0[0] = 0;
  ep0[1] = (3 << 1)              // Error Count = 3
          | (4 << 3)              // Endpoint Type = 4 (Control Bidirectional)
          | (max_packet << 16);   // Max Packet Size
  ep0[2] = (unsigned int)((unsigned long long)d->xfer_ring | 1); // DCS=1
  ep0[3] = (unsigned int)(((unsigned long long)d->xfer_ring) >> 32);
  ep0[4] = 8; // Average TRB Length

  xhci_commit_cmd(
      (unsigned int)(unsigned long long)d->in_ctx,
      (unsigned int)(((unsigned long long)d->in_ctx) >> 32),
      0,
      (11 << 10) | (slot << 24) // Type 11 = Address Device
  );
  xhci_ring_cmd_db();
}

static void xhci_configure_endpoint(int slot) {
  usb_device_t *d = &devices[slot];
  volatile xhci_cap_regs_t *caps = (volatile xhci_cap_regs_t *)xhci_base;
  int csz = (caps->hcc_params1 >> 2) & 1;
  int shift = csz ? 6 : 5;

  for (int i = 0; i < 2048; i++)
    d->in_ctx[i] = 0;

  // Add Slot (A0) + EP1 IN (A3, DCI=3)
  ((unsigned int *)d->in_ctx)[0] = 0x00000000; // D
  ((unsigned int *)d->in_ctx)[1] = 0x00000009; // A: Slot (bit0) + EP1-IN (bit3)

  // Slot Context: update Context Entries to 3
  unsigned int *sc = (unsigned int *)(d->in_ctx + (1 << shift));
  sc[0] = (3 << 27); // Context Entries = 3

  // EP1 IN (Interrupt) at DCI 3 = block index 4
  // Interval = 1 (2^0 * 125us = 125us / 1ms high-frequency polling for smooth mouse/kbd)
  unsigned int *ep1 = (unsigned int *)(d->in_ctx + (4 << shift));
  ep1[0] = (1 << 16);
  ep1[1] = (3 << 1)    // Error Count = 3
          | (7 << 3)    // Endpoint Type = 7 (Interrupt IN)
          | (8 << 16);  // Max Packet Size = 8
  ep1[2] = (unsigned int)((unsigned long long)d->ep1_ring | 1); // DCS=1
  ep1[3] = (unsigned int)(((unsigned long long)d->ep1_ring) >> 32);
  ep1[4] = 8; // Average TRB Length

  xhci_commit_cmd(
      (unsigned int)(unsigned long long)d->in_ctx,
      (unsigned int)(((unsigned long long)d->in_ctx) >> 32),
      0,
      (12 << 10) | (slot << 24) // Type 12 = Configure Endpoint
  );
  xhci_ring_cmd_db();
}

static void xhci_set_configuration(int slot) {
  // SET_CONFIGURATION 1 — no data stage, OUT status
  // SETUP stage: bmRequestType=0x00 bRequest=0x09 wValue=0x0001 wIndex=0 wLength=0
  xhci_commit_xfer(slot, 0x00010900, 0x00000000, 8,
                   (2 << 10) | (1 << 6)); // SETUP TRB, Immediate Data
  // Status IN stage
  xhci_commit_xfer(slot, 0, 0, 0,
                   (4 << 10) | (1 << 16) | (1 << 5)); // STATUS TRB, DIR=IN
  xhci_ring_ep_db(slot, 1);
}

static void xhci_queue_read(int slot) {
  usb_device_t *d = &devices[slot];
  d->ep1_ring[d->ep1_idx].data_low =
      (unsigned int)(unsigned long long)d->buffer;
  d->ep1_ring[d->ep1_idx].data_high = 0;
  d->ep1_ring[d->ep1_idx].status = 8; // 8 bytes
  memory_barrier();
  d->ep1_ring[d->ep1_idx].control = (1 << 10) | (1 << 5) | d->ep1_cyc; // Normal TRB, IOC
  memory_barrier();

  d->ep1_idx++;
  if (d->ep1_idx >= 255) {
    // CRITICAL: Refresh the Link TRB at index 255 with the current cycle bit
    // before flipping ep1_cyc, otherwise the controller halts after 255 events!
    d->ep1_ring[255].data_low = (unsigned int)(unsigned long long)d->ep1_ring;
    d->ep1_ring[255].data_high = 0;
    d->ep1_ring[255].status = 0;
    memory_barrier();
    d->ep1_ring[255].control = (6 << 10) | (1 << 1) | d->ep1_cyc;
    memory_barrier();
    d->ep1_idx = 0;
    d->ep1_cyc ^= 1;
  }
  xhci_ring_ep_db(slot, 3); // Doorbell target 3 = EP1 IN (DCI 3)
}

void xhci_init(unsigned long long base) {
  xhci_base = base;
  volatile xhci_cap_regs_t *caps = (volatile xhci_cap_regs_t *)base;
  volatile xhci_op_regs_t *ops =
      (volatile xhci_op_regs_t *)(base + caps->cap_length);
  volatile xhci_intr_regs_t *intr =
      (volatile xhci_intr_regs_t *)(base + caps->rts_off + 0x20);

  serial_print("[XHCI] cap_length: ");
  serial_print_hex(caps->cap_length);
  serial_println("");
  serial_print("[XHCI] HCS_PARAMS1: ");
  serial_print_hex(caps->hcs_params1);
  serial_println("");
  serial_print("[XHCI] HCC_PARAMS1: ");
  serial_print_hex(caps->hcc_params1);
  serial_println("");

  // Reset device state
  cmd_idx = 0; evt_idx = 0;
  cmd_cyc = 1; evt_cyc = 1;
  pending_port_id = 0;
  for (int i = 0; i < 16; i++) {
    devices[i].state = STATE_IDLE;
    devices[i].port_id = 0;
  }

  // Halt the controller
  ops->usb_cmd &= ~(1 << 0);
  mdelay(5);

  // Issue Host Controller Reset with bounded timeouts (500ms each)
  ops->usb_cmd |= (1 << 1);
  if (xhci_wait_bits(ops, 0, (1 << 1), 0, 500) != 0) {
    serial_println("[XHCI] WARN: Timeout waiting for HCRST to clear.");
  }
  if (xhci_wait_bits(ops, 1, (1 << 11), 0, 500) != 0) {
    serial_println("[XHCI] WARN: Timeout waiting for CNR=0.");
  }
  serial_println("[XHCI] Controller Reset Complete.");

  // Allocate DMA-capable memory pool (128 pages = 512KB)
  unsigned char *dma_pool = (unsigned char *)dma_alloc_blocks(128);
  if (!dma_pool) {
    serial_println("[XHCI] FAIL: Could not allocate 512KB DMA pool!");
    return;
  }

  // Mark every page of DMA pool as uncacheable
  for (int i = 0; i < 128; i++) {
    unsigned long long pa = (unsigned long long)dma_pool + (i * 4096);
    vmm_map_page(vmm_get_active_pml4(), pa, pa,
                 PAGE_PRESENT | PAGE_RW | PAGE_PWT | PAGE_PCD);
  }
  // Flush TLB
  unsigned long long cr3_val;
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3_val));
  __asm__ volatile("mov %0, %%cr3" : : "r"(cr3_val) : "memory");

  // Zero the entire DMA pool
  for (int i = 0; i < 128 * 4096; i++)
    dma_pool[i] = 0;

  // Carve up DMA pool
  cmd_ring = (volatile xhci_trb_t *)(dma_pool + 0x0000);
  evt_ring = (volatile xhci_trb_t *)(dma_pool + 0x1000);
  erst      = (volatile erst_entry_t *)(dma_pool + 0x2000);
  dcbaap    = (volatile unsigned long long *)(dma_pool + 0x3000);

  // Command Ring: Link TRB at index 255 to wrap back to start
  cmd_ring[255].data_low  = (unsigned int)(unsigned long long)cmd_ring;
  cmd_ring[255].data_high = (unsigned int)((unsigned long long)cmd_ring >> 32);
  cmd_ring[255].status    = 0;
  cmd_ring[255].control   = (6 << 10) | (1 << 1) | 1; // Type=Link, TC=1, Cycle=1

  // Event Ring Segment Table
  erst[0].ptr  = (unsigned long long)evt_ring;
  erst[0].size = 256;

  // Configure interrupter 0
  intr->erstsz = 1;
  intr->erstba = (unsigned long long)erst;
  intr->erdp   = (unsigned long long)evt_ring | (1 << 3); // EHB=1 clears pending

  intr->iman = 3; // Enable interrupt + clear pending

  // Operational registers
  ops->page_size = 1;
  ops->crcr = (unsigned long long)cmd_ring | 1; // RCS=1
  ops->dcbaap = (unsigned long long)dcbaap;
  ops->config = caps->hcs_params1 & 0xFF; // MaxSlotsEn

  // Per-slot DMA layout (32KB per slot, starting at dma_pool + 0x4000)
  for (int s = 0; s < 16; s++) {
    unsigned char *sb = dma_pool + 0x4000 + (s * 0x8000);
    devices[s].out_ctx   = sb + 0x0000;
    devices[s].in_ctx    = sb + 0x1000;
    devices[s].xfer_ring = (xhci_trb_t *)(sb + 0x2000);
    devices[s].ep1_ring  = (xhci_trb_t *)(sb + 0x3000);
    devices[s].buffer    = sb + 0x4000;
    devices[s].xfer_idx  = 0; devices[s].xfer_cyc = 1;
    devices[s].ep1_idx   = 0; devices[s].ep1_cyc  = 1;

    // Link TRBs for both rings
    devices[s].xfer_ring[255].data_low  = (unsigned int)(unsigned long long)devices[s].xfer_ring;
    devices[s].xfer_ring[255].data_high = 0;
    devices[s].xfer_ring[255].control   = (6 << 10) | (1 << 1) | 1;
    devices[s].ep1_ring[255].data_low   = (unsigned int)(unsigned long long)devices[s].ep1_ring;
    devices[s].ep1_ring[255].data_high  = 0;
    devices[s].ep1_ring[255].control    = (6 << 10) | (1 << 1) | 1;
  }

  serial_print("[XHCI] CRCR:   "); serial_print_hex(ops->crcr);   serial_println("");
  serial_print("[XHCI] DCBAAP: "); serial_print_hex(ops->dcbaap); serial_println("");

  // Start the controller (RS=1) and enable port change events (EWE=1)
  ops->usb_cmd |= (1 << 0) | (1 << 2) | (1 << 3); // RS + INTE + HSEE
  if (xhci_wait_bits(ops, 1, (1 << 0), 0, 500) != 0) {
    serial_println("[XHCI] WARN: Timeout waiting for HCH=0.");
  }

  // Power on all ports and let Port Status Change Events drive enumeration
  unsigned int max_ports = (caps->hcs_params1 >> 24) & 0xFF;
  serial_print("[XHCI] Max Ports: ");
  serial_print_hex(max_ports);
  serial_println("");

  for (unsigned int i = 0; i < max_ports; i++) {
    volatile unsigned int *portsc =
        (volatile unsigned int *)((unsigned char *)ops + 0x400 + (i * 0x10));
    // Power on the port, preserve non-RW1C bits, clear RW1C bits by writing 0
    unsigned int psc = *portsc & ~PORTSC_RW1C_MASK;
    *portsc = psc | PORTSC_PP;
    memory_barrier();
  }

  // Short delay for ports to stabilize
  mdelay(15);

  // Scan ports for initial devices
  xhci_scan_for_devices();
}

void xhci_check_event(unsigned long long base) {
  volatile xhci_cap_regs_t *caps = (volatile xhci_cap_regs_t *)base;
  volatile xhci_op_regs_t *ops =
      (volatile xhci_op_regs_t *)(base + caps->cap_length);
  volatile xhci_intr_regs_t *intr =
      (volatile xhci_intr_regs_t *)(base + caps->rts_off + 0x20);

  while (xhci_has_event()) {
    unsigned int ctrl   = evt_ring[evt_idx].control;
    unsigned int status = evt_ring[evt_idx].status;
    unsigned int dlo    = evt_ring[evt_idx].data_low;

    unsigned int type = (ctrl >> 10) & 0x3F;
    unsigned int code = (status >> 24) & 0xFF;
    // Slot ID is ALWAYS in ctrl[31:24] per XHCI spec §6.4.2
    int slot = (ctrl >> 24) & 0xFF;

    if (code != 1 && code != 13) {
      serial_print("[XHCI] Type=");
      serial_print_hex(type);
      serial_print(" Slot=");
      serial_print_hex(slot);
      serial_print(" ERROR code=");
      serial_print_hex(code);
      serial_println("");
    }

    if (type == 34) {
      // Port Status Change Event
      unsigned int port_id = (dlo >> 24) & 0xFF;
      volatile unsigned int *portsc =
          (volatile unsigned int *)((unsigned char *)ops + 0x400 +
                                    ((port_id - 1) * 0x10));
      unsigned int psc = *portsc;

      serial_print("[XHCI] Port Change port=");
      serial_print_hex(port_id);
      serial_print(" psc=");
      serial_print_hex(psc);
      serial_println("");

      // Clear RW1C bits (write-back only the RW1C bits that are set)
      *portsc = (psc & ~PORTSC_RW1C_MASK) | (psc & PORTSC_RW1C_MASK);
      memory_barrier();

      // If this is a connection event
      if ((psc & PORTSC_CCS)) {
        xhci_scan_for_devices();
      }

    } else if (type == 33 && code == 1) {
      // Command Completion Event
      if (slot <= 0 || slot >= 16) goto next_event;
      usb_device_t *d = &devices[slot];

      if (d->state == STATE_IDLE) {
        // This is the Enable Slot completion — grab the assigned slot id
        // and associate it with the port we were enumerating
        d->port_id = pending_port_id;
        pending_port_id = 0; // Consumed
        serial_print("[XHCI] Slot ");
        serial_print_hex(slot);
        serial_print(" assigned to port ");
        serial_print_hex(d->port_id);
        serial_println(". Addressing...");
        xhci_address_device(slot);

      } else if (d->state == STATE_SLOT_ENABLED) {
        // Address Device completed successfully
        serial_print("[XHCI] Slot ");
        serial_print_hex(slot);
        serial_println(" addressed. Fetching descriptor...");

        // GET_DESCRIPTOR(Device Descriptor, length=18)
        // USB SETUP packet bytes:
        //   bmRequestType=0x80  bRequest=0x06  wValue=0x0100  wIndex=0x0000  wLength=0x0012
        // Packed as little-endian DWORDs in the SETUP TRB:
        //   data_low  = [byte3][byte2][byte1][byte0] = 0x01 00 06 80 = 0x01000680
        //   data_high = [byte7][byte6][byte5][byte4] = 0x00 12 00 00 = 0x00120000
        xhci_commit_xfer(slot, 0x01000680, 0x00120000, 8,
                         (2 << 10)          // TRB Type = Setup Stage
                         | (1 << 6)         // Immediate Data (IDT=1)
                         | (3 << 16));      // Transfer Type = 3 (IN Data)
        xhci_commit_xfer(slot,
                         (unsigned int)(unsigned long long)d->buffer, 0,
                         18,               // Bytes to receive
                         (3 << 10)         // TRB Type = Data Stage
                         | (1 << 16));     // DIR = IN
        xhci_commit_xfer(slot, 0, 0, 0,
                         (4 << 10)          // Status Stage, DIR=0 (OUT for IN data)
                         | (1 << 5));       // IOC — generate Transfer Event
        xhci_ring_ep_db(slot, 1);           // Ring EP0 doorbell

        d->state = STATE_ADDRESSED;

      } else if (d->state == STATE_DESC_FETCHED) {
        // Configure Endpoint completed
        serial_print("[XHCI] Slot ");
        serial_print_hex(slot);
        serial_println(" endpoint configured. SET_CONFIGURATION...");
        xhci_set_configuration(slot);
        d->state = STATE_CONFIGURING;
      }

    } else if (type == 32 && (code == 1 || code == 13)) {
      // Transfer Event (Normal or Short Packet)
      if (slot <= 0 || slot >= 16) goto next_event;
      usb_device_t *d = &devices[slot];
      int ep_id = (ctrl >> 16) & 0x1F;

      if (ep_id == 1) {
        // EP0 Control transfer completed
        if (d->state == STATE_ADDRESSED) {
          serial_print("[XHCI] Descriptor received, slot=");
          serial_print_hex(slot);
          serial_println(". Configuring endpoint...");
          xhci_configure_endpoint(slot);
          d->state = STATE_DESC_FETCHED;

        } else if (d->state == STATE_CONFIGURING) {
          serial_print("[XHCI] Slot ");
          serial_print_hex(slot);
          serial_println(" ONLINE! Queuing first read.");
          d->state = STATE_CONFIGURED;
          xhci_queue_read(slot);
          
          // One enumeration finished! Check if others are waiting.
          xhci_scan_for_devices();
        }

      } else if (ep_id == 3) {
        // Interrupt IN data arrived (mouse/keyboard)
        int remainder = status & 0xFFFFFF;
        int transferred = 8 - remainder;

        if (d->type == 0) {
          // ROBUST DYNAMIC CLASSIFICATION
          // Keyboard boot protocol uses 8-byte reports (with byte 1 reserved=0).
          // usb-tablet (absolute pointer) uses 6-byte reports (buttons, x_lo, x_hi, y_lo, y_hi, wheel).
          // usb-mouse (relative pointer) uses 3- or 4-byte reports.
          if (transferred == 8 && d->buffer[1] == 0) {
            d->type = 1; // Keyboard
          } else if (transferred == 6) {
            d->type = 3; // Absolute USB Tablet (0..32767)
          } else {
            d->type = 2; // Relative USB Mouse
          }
        }

        if (d->type == 1) {
          // Keyboard: byte 0 = modifiers, byte 2 = primary keycode
          unsigned char mods = d->buffer[0];
          unsigned char b2 = d->buffer[2];
          if (b2 != 0) {
            input_push_key(b2, mods);
          }
        } else if (d->type == 3) {
          // Absolute USB Tablet (QEMU usb-tablet): seamless 1:1 host cursor tracking!
          unsigned char b0 = d->buffer[0];
          unsigned int abs_x = (unsigned int)d->buffer[1] | ((unsigned int)d->buffer[2] << 8);
          unsigned int abs_y = (unsigned int)d->buffer[3] | ((unsigned int)d->buffer[4] << 8);
          mouse_buttons = b0;
          if (fb_width > 0 && fb_height > 0) {
            mouse_x = (int)((abs_x * (unsigned long long)fb_width) / 32768ULL);
            mouse_y = (int)((abs_y * (unsigned long long)fb_height) / 32768ULL);
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x >= (int)fb_width)  mouse_x = (int)fb_width - 1;
            if (mouse_y >= (int)fb_height) mouse_y = (int)fb_height - 1;
          }
        } else if (d->type == 2) {
          // Mouse with gentle ballistic acceleration for smooth tracking
          unsigned char b0 = d->buffer[0];
          int dx = (signed char)d->buffer[1];
          int dy = (signed char)d->buffer[2];
          int adx = (dx < 0) ? -dx : dx;
          int ady = (dy < 0) ? -dy : dy;
          if (adx > 6) dx = (dx * 3) / 2;
          if (ady > 6) dy = (dy * 3) / 2;
          
          mouse_buttons = b0; // Always update button state (so release works)
          if (dx || dy) {
            mouse_x += dx;
            mouse_y += dy;
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x >= (int)fb_width)  mouse_x = fb_width - 1;
            if (mouse_y >= (int)fb_height) mouse_y = fb_height - 1;
          }
        }
        // Always re-arm the interrupt endpoint even on unexpected completion codes
        for (int b = 0; b < 8; b++) d->buffer[b] = 0;
        xhci_queue_read(slot);
      }
    } else if (type == 32) {
      // Non-normal transfer event (e.g. Missed Service / Babble / Split Error):
      // still re-arm EP1 IN so the mouse/keyboard never goes dead.
      if (slot > 0 && slot < 16) {
        usb_device_t *d = &devices[slot];
        int ep_id = (ctrl >> 16) & 0x1F;
        if (ep_id == 3 && d->state == STATE_CONFIGURED) {
          xhci_queue_read(slot);
        }
      }
    }

next_event:
    evt_idx++;
    if (evt_idx >= 256) {
      evt_idx = 0;
      evt_cyc ^= 1;
    }

    // Acknowledge the event to the controller
    ops->usb_sts |= (1 << 3); // Clear EINT
    intr->erdp = (unsigned long long)(&evt_ring[evt_idx]) | (1 << 3);
    memory_barrier();
  }
}
