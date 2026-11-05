#pragma once

// =====================================================
// PORT I/O
// =====================================================
#if defined(__x86_64__) || defined(__i386__)

static inline void outb(unsigned short port, unsigned char val) {
  __asm__ volatile("outb %b0, %w1" : : "a"(val), "Nd"(port));
}
static inline unsigned char inb(unsigned short port) {
  unsigned char ret;
  __asm__ volatile("inb %w1, %b0" : "=a"(ret) : "Nd"(port));
  return ret;
}
static inline void outl(unsigned short port, unsigned int val) {
  __asm__ volatile("outl %0, %w1" : : "a"(val), "Nd"(port));
}
static inline unsigned int inl(unsigned short port) {
  unsigned int ret;
  __asm__ volatile("inl %w1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}
static inline void outw(unsigned short port, unsigned short val) {
  __asm__ volatile("outw %w0, %w1" : : "a"(val), "Nd"(port));
}
static inline unsigned short inw(unsigned short port) {
  unsigned short ret;
  __asm__ volatile("inw %w1, %w0" : "=a"(ret) : "Nd"(port));
  return ret;
}

#else

// Stubs to satisfy IDE linters running on non-x86 architectures (e.g. ARM Macs)
static inline void outb(unsigned short port, unsigned char val) { (void)port; (void)val; }
static inline unsigned char inb(unsigned short port) { (void)port; return 0; }
static inline void outl(unsigned short port, unsigned int val) { (void)port; (void)val; }
static inline unsigned int inl(unsigned short port) { (void)port; return 0; }
static inline void outw(unsigned short port, unsigned short val) { (void)port; (void)val; }
static inline unsigned short inw(unsigned short port) { (void)port; return 0; }
static inline void pic_remap() {}
static inline void mouse_init() {}

#endif

static inline void cpu_pause(void) {
#if defined(__x86_64__) || defined(__i386__)
  __asm__ volatile("pause");
#endif
}

// =====================================================
// GLOBAL HARDWARE STATE
// =====================================================
extern unsigned long long xhci_mmio_global;
extern unsigned long long gpu_fb_base;
extern unsigned int fb_pitch;
extern unsigned int fb_width;
extern unsigned int fb_height;
extern volatile int mouse_x;
extern volatile int mouse_y;
extern volatile unsigned char mouse_buttons;

// Keyboard event queue (filled by the USB driver, drained by the main loop).
// `code` is a USB HID usage ID, `mods` is the HID modifier byte.
#define KEY_MOD_LSHIFT 0x02
#define KEY_MOD_RSHIFT 0x20
void input_push_key(unsigned char code, unsigned char mods);
int input_pop_key(unsigned char *code, unsigned char *mods);

// Fatal error: logs to serial, paints a panic screen and halts forever.
__attribute__((noreturn)) void kernel_panic(const char *msg);

// =====================================================
// SERIAL DEBUGGING
// =====================================================
void serial_write(char c);

static inline void serial_print(const char *str) {
  while (*str)
    serial_write(*str++);
}

static inline void serial_println(const char *str) {
  serial_print(str);
  serial_write('\r');
  serial_write('\n');
}

static inline void serial_print_hex(unsigned long long num) {
  char hex_str[19] = "0x0000000000000000";
  char hex_chars[] = "0123456789ABCDEF";
  for (int i = 15; i >= 0; i--) {
    hex_str[i + 2] = hex_chars[num & 0xF];
    num >>= 4;
  }
  serial_print(hex_str);
}