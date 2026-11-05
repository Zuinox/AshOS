// src/stage1/kernel.c

#include "acpi.h"
#include "apic.h"
#include "bootinfo.h"
#include "gdt.h"
#include "gui.h"
#include "hardware.h"
#include "heap.h"
#include "idt.h"
#include "pci.h"
#include "pmm.h"
#include "timer.h"
#include "video.h"
#include "vmm.h"
#include "xhci.h"

unsigned int cursor_x = 50;
unsigned int cursor_y = 50;
int font_scale = 3;

unsigned long long xhci_mmio_global = 0;
unsigned long long gpu_fb_base = 0;
unsigned int fb_pitch = 0, fb_width = 0, fb_height = 0;

volatile int mouse_x = 640, mouse_y = 380;
volatile unsigned char mouse_buttons = 0;

unsigned int io_apic_phys_addr = 0;
unsigned int lapic_phys_addr = 0;

// Ring buffer for keyboard events (capacity 32)
#define KEY_QUEUE_CAP 32
static unsigned char key_q_code[KEY_QUEUE_CAP];
static unsigned char key_q_mods[KEY_QUEUE_CAP];
static unsigned int key_q_head = 0;
static unsigned int key_q_tail = 0;

void input_push_key(unsigned char code, unsigned char mods) {
  unsigned int next = (key_q_head + 1) % KEY_QUEUE_CAP;
  if (next == key_q_tail) {
    return; // Queue full, drop
  }
  key_q_code[key_q_head] = code;
  key_q_mods[key_q_head] = mods;
  key_q_head = next;
}

int input_pop_key(unsigned char *code, unsigned char *mods) {
  if (key_q_tail == key_q_head) {
    return 0;
  }
  *code = key_q_code[key_q_tail];
  *mods = key_q_mods[key_q_tail];
  key_q_tail = (key_q_tail + 1) % KEY_QUEUE_CAP;
  return 1;
}

void serial_write(char c) {
  while ((inb(0x3F8 + 5) & 0x20) == 0)
    ;
  outb(0x3F8, c);
}

// ---------------------------------------------------------
// THE KERNEL MAIN: Invoked by _start (src/stage1/start.c)
// after .bss is zeroed and the 64KB kernel stack is active.
// ---------------------------------------------------------
void kmain(BootInfo *boot_info) {
  // 1. Initialize Serial Logging
  outb(0x3F8 + 3, 0x80);
  outb(0x3F8 + 0, 0x03);
  outb(0x3F8 + 1, 0x00);
  outb(0x3F8 + 3, 0x03);

  serial_println("\n========================================");
  serial_println(" SubhashishOS: Kernel Initializing");
  serial_println("========================================\n");

  if (!boot_info || boot_info->Magic != BOOTINFO_MAGIC) {
    kernel_panic("BootInfo magic mismatch or NULL pointer!");
  }

  // Copy BootInfo onto the kernel stack so we can safely reclaim
  // UEFI boot-services memory later.
  BootInfo bi = *boot_info;

  // 2. Load Kernel GDT + TSS (with dedicated Double-Fault IST stack) and IDT
  gdt_init();
  idt_init();
  timer_init();

  // 3. RAW PROOF OF LIFE TEST (stride-aware)
  if (bi.FramebufferBase != 0 && bi.Width != 0 && bi.Height != 0) {
    serial_println(
        "[OK] Framebuffer address received! Painting screen green...");
    volatile unsigned int *fb = (volatile unsigned int *)bi.FramebufferBase;
    unsigned int stride =
        bi.PixelsPerScanLine ? bi.PixelsPerScanLine : bi.Width;

    for (unsigned int y = 0; y < bi.Height; y++) {
      volatile unsigned int *row = fb + (unsigned long long)y * stride;
      for (unsigned int x = 0; x < bi.Width; x++) {
        row[x] = 0x00FF00;
      }
    }
    __asm__ volatile("mfence" ::: "memory");
    serial_println("[OK] Screen painted green.");
  } else {
    serial_println("[FAIL] Briefcase Framebuffer is NULL!");
  }

  // 4. Parse ACPI from the Briefcase
  if (bi.AcpiRsdp != 0) {
    serial_println("[INFO] Parsing ACPI tables...");
    rsdp_t *rsdp = (rsdp_t *)bi.AcpiRsdp;
    if (rsdp->revision >= 2 && rsdp->xsdt_address != 0) {
      acpi_parse_xsdt(rsdp->xsdt_address);
    }
  }

  // 5. Initialize Physical & Virtual Memory from the real UEFI Memory Map
  serial_println("[INFO] Initializing Memory Manager...");
  pmm_init(&bi);
  vmm_init();
  // Now that our own GDT, stack, and page tables are active, reclaim UEFI
  // Boot Services memory.
  pmm_reclaim_boot_services(&bi);

  if (lapic_phys_addr != 0 && io_apic_phys_addr != 0) {
    serial_println("\n--- BRIDGING APIC REGISTERS ---");
    vmm_map_mmio(lapic_phys_addr, 1);
    vmm_map_mmio(io_apic_phys_addr, 1);
    serial_println("    |-- APIC and I/O APIC Mapped to Virtual Memory.");
  }

  // 6. Initialize Heap
  serial_println("[INFO] Initializing Kernel Heap...");
  unsigned long long heap_virtual_base = 0xFFFFC00000000000ULL;
  unsigned int heap_size = 1024 * 1024;
  for (unsigned int offset = 0; offset < heap_size; offset += 4096) {
    void *phys_block = pmm_alloc_block();
    if (!phys_block ||
        vmm_map_page(pml4_root, heap_virtual_base + offset,
                     (unsigned long long)phys_block,
                     PAGE_PRESENT | PAGE_RW) != 0) {
      kernel_panic("Heap: failed to allocate or map initial heap pages");
    }
  }
  vmm_load_pml4(pml4_root);
  heap_init(heap_virtual_base, heap_size);

  // 7. Preserve UEFI GOP Framebuffer info before PCI scan (so multi-GPU setups
  //    with unconfigured BAR0s cannot clobber the active GOP framebuffer)
  unsigned long long gop_fb_base = (unsigned long long)bi.FramebufferBase;
  fb_width = bi.Width;
  fb_height = bi.Height;
  fb_pitch = (bi.PixelsPerScanLine ? bi.PixelsPerScanLine : bi.Width) * 4;

  serial_println("[INFO] Scanning PCI Bus...");
  pci_scan_bus();

  if (gop_fb_base != 0) {
    gpu_fb_base = gop_fb_base;
  }

  // 8. Initialize OS-Level Graphics
  if (gpu_fb_base != 0) {
    serial_println("[INFO] Initializing Advanced Video Output...");
    video_init(gpu_fb_base);
    clear_screen(0x1E1E1E);
  }

  // 9. Initialize Local APIC (legacy PIC masked)
  if (lapic_phys_addr != 0) {
    serial_println("[INFO] Initializing Local APIC...");
    apic_init();
  }

  serial_println("\n--- OS BOOT COMPLETE ---");

  // 10. Initialize the Graphical Shell
  gui_init();
  serial_println(">>> START TYPING ON THE SCREEN! <<<");

  // 11. Power on the USB 3.0 (xHCI) Controller
  if (xhci_mmio_global != 0) {
    serial_println("[INFO] Powering up USB 3.0 (XHCI) Controller...");
    xhci_init(xhci_mmio_global);
    serial_println("[OK] USB Controller Online.");
  } else {
    serial_println(
        "[FAIL] XHCI MMIO is ZERO. The PCI scanner didn't find the USB card!");
  }

  // 12. Event-Driven + 125 FPS Polling Loop (immediate redraw on mouse/key input)
  serial_println("[DEBUG] Calling gui_render_frame...");
  gui_render_frame(mouse_x, mouse_y);
  serial_println("[DEBUG] gui_render_frame finished!");

  unsigned long long last_frame_ms = timer_ms();
  int prev_mx = mouse_x, prev_my = mouse_y;
  unsigned char prev_mb = mouse_buttons;

  while (1) {
    if (xhci_mmio_global != 0) {
      xhci_check_event(xhci_mmio_global);
    }

    int cur_mx = mouse_x, cur_my = mouse_y;
    unsigned char cur_mb = mouse_buttons;
    int input_dirty = 0;

    if (cur_mx != prev_mx || cur_my != prev_my || cur_mb != prev_mb) {
      gui_handle_mouse(cur_mx, cur_my, cur_mb);
      prev_mx = cur_mx;
      prev_my = cur_my;
      prev_mb = cur_mb;
      input_dirty = 1;
    }

    unsigned char key_code, key_mods;
    while (input_pop_key(&key_code, &key_mods)) {
      gui_handle_keyboard(key_code, key_mods);
      input_dirty = 1;
    }

    unsigned long long now = timer_ms();
    // Redraw immediately if mouse/keyboard moved (capped at 4ms / 250Hz),
    // or every 16ms (60Hz) when idle so the clock & blinking cursor stay smooth.
    if ((input_dirty && (now - last_frame_ms >= 4)) ||
        (now - last_frame_ms >= 16)) {
      gui_render_frame(cur_mx, cur_my);
      last_frame_ms = now;
    } else {
      cpu_pause();
    }
  }
}
