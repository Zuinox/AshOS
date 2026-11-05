// src/stage1/timer.c
//
// Time keeping for a kernel that runs with interrupts disabled.
//
// The CPU timestamp counter (TSC) increments at a fixed rate but that rate is
// unknown. At boot we measure it once against the legacy PIT (8254) channel 2,
// whose input clock is a known 1.193182 MHz. After that, delays and timeouts
// are simple TSC comparisons, independent of CPU speed or emulator speed.

#include "timer.h"
#include "hardware.h"

#define PIT_HZ 1193182ULL
#define CALIBRATION_MS 10ULL

static unsigned long long tsc_per_ms = 0;
static unsigned long long tsc_boot = 0;

static inline unsigned long long rdtsc(void) {
  unsigned int lo, hi;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((unsigned long long)hi << 32) | lo;
}

void timer_init(void) {
  // Port 0x61 bit0 = PIT ch2 gate, bit1 = speaker enable, bit5 = ch2 output.
  unsigned char p61 = inb(0x61);
  outb(0x61, (p61 & ~0x02) | 0x01); // gate high, speaker off

  // Channel 2, lobyte/hibyte, mode 0 (interrupt on terminal count), binary.
  outb(0x43, 0xB0);
  unsigned int count = (unsigned int)(PIT_HZ * CALIBRATION_MS / 1000ULL);
  outb(0x42, count & 0xFF);
  unsigned long long start = rdtsc();
  outb(0x42, (count >> 8) & 0xFF); // counting starts after the high byte

  // Wait for OUT2 to go high (terminal count), with a hard iteration cap so
  // broken/absent PIT hardware cannot hang the boot.
  unsigned long long guard = 0;
  while (!(inb(0x61) & 0x20)) {
    if (++guard > 50000000ULL)
      break;
  }
  unsigned long long end = rdtsc();

  if (guard <= 50000000ULL && end > start)
    tsc_per_ms = (end - start) / CALIBRATION_MS;

  if (tsc_per_ms < 1000) {
    // Calibration failed: assume a ~2 GHz TSC so timeouts still terminate.
    tsc_per_ms = 2000000ULL;
    serial_println("[WARN] PIT calibration failed, assuming 2 GHz TSC.");
  }

  outb(0x61, p61 & ~0x03); // restore: gate low, speaker off
  tsc_boot = rdtsc();

  serial_print("[OK] TSC calibrated: ");
  serial_print_hex(tsc_per_ms);
  serial_println(" ticks/ms");
}

unsigned long long timer_tsc_per_ms(void) { return tsc_per_ms; }

unsigned long long timer_ms(void) {
  if (!tsc_per_ms)
    return 0;
  return (rdtsc() - tsc_boot) / tsc_per_ms;
}

unsigned long long timer_us(void) {
  if (!tsc_per_ms)
    return 0;
  return ((rdtsc() - tsc_boot) * 1000ULL) / tsc_per_ms;
}

void udelay(unsigned long long us) {
  unsigned long long per_us = tsc_per_ms / 1000ULL;
  if (per_us == 0)
    per_us = 1;
  unsigned long long target = rdtsc() + us * per_us;
  while (rdtsc() < target)
    cpu_pause();
}

void mdelay(unsigned long long ms) {
  unsigned long long target = rdtsc() + ms * tsc_per_ms;
  while (rdtsc() < target)
    cpu_pause();
}

