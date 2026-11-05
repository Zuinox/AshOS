// src/stage1/video.c - High-speed 64-bit double-buffered framebuffer renderer
//
// Uses 64-bit QWORD (`rep stosq` / `rep movsq`) fills and copies for minimal
// frame latency, and renders a crisp 12x19 arrow pointer with outline & shadow.

#include "video.h"
#include "vmm.h"
#include "lib.h"
#include "font.h"

static unsigned int *front_buffer = 0;
static unsigned int *back_buffer = 0;
static int video_ready = 0;

static inline void fast_fill32(unsigned int *dst, unsigned int color, size_t count) {
  if (count == 0) return;
  unsigned long long pair = ((unsigned long long)color << 32) | color;
  size_t qwords = count >> 1;
  void *d = dst;
  if (qwords) {
    __asm__ volatile("rep stosq"
                     : "+D"(d), "+c"(qwords)
                     : "a"(pair)
                     : "memory");
  }
  if (count & 1) {
    *((unsigned int *)d) = color;
  }
}

static inline void fast_copy32(unsigned int *dst, const unsigned int *src, size_t dwords) {
  size_t qwords = dwords >> 1;
  void *d = dst;
  const void *s = src;
  if (qwords) {
    __asm__ volatile("rep movsq"
                     : "+D"(d), "+S"(s), "+c"(qwords)
                     :
                     : "memory");
  }
  if (dwords & 1) {
    *((unsigned int *)d) = *((const unsigned int *)s);
  }
}

int video_is_ready(void) {
  return video_ready && front_buffer != 0 && back_buffer != 0;
}

void video_init(unsigned long long physical_fb_base) {
  serial_println("\n--- Initializing Video Subsystem ---");
  serial_print("    |-- Mapping FB at: ");
  serial_print_hex(physical_fb_base);
  serial_println("");
  serial_print("    |-- Resolution: ");
  serial_print_hex(fb_width);
  serial_print(" x ");
  serial_print_hex(fb_height);
  serial_print(" (pitch=");
  serial_print_hex(fb_pitch);
  serial_println(")");

  if (fb_width == 0 || fb_height == 0) {
    serial_println("[FAIL] Video resolution is 0!");
    return;
  }
  if (fb_pitch < fb_width * 4) {
    fb_pitch = fb_width * 4;
  }

  // 1. Map physical FB across its full byte size
  unsigned long long fb_bytes = (unsigned long long)fb_pitch * fb_height;
  unsigned long long fb_pages = (fb_bytes + 4095) / 4096;
  vmm_map_mmio(physical_fb_base, fb_pages);
  front_buffer = (unsigned int *)physical_fb_base;

  // 2. Dynamically size and allocate the back-buffer
  unsigned long long back_bytes = (unsigned long long)fb_width * fb_height * 4;
  unsigned int back_pages = (unsigned int)((back_bytes + 4095) / 4096);
  void *back_phys = dma_alloc_blocks(back_pages);
  if (!back_phys) {
    kernel_panic("Video: failed to allocate contiguous back-buffer");
  }
  back_buffer = (unsigned int *)0xFFFFB00000000000ULL;

  // 3. Map Back-Buffer as Write-Back RAM
  page_table_t *pml4 = vmm_get_active_pml4();
  for (unsigned int i = 0; i < back_pages; i++) {
    unsigned long long phys = (unsigned long long)back_phys + ((unsigned long long)i * 4096);
    unsigned long long virt = (unsigned long long)back_buffer + ((unsigned long long)i * 4096);
    if (vmm_map_page(pml4, virt, phys, PAGE_PRESENT | PAGE_RW) != 0) {
      kernel_panic("Video: failed to map back-buffer");
    }
  }
  vmm_load_pml4(pml4);
  video_ready = 1;

  serial_println("    |-- Double Buffer Allocated & Mapped.");
  serial_println("    |-- SubhashishOS has CRYSTAL CLEAR EYES.");
}

void video_swap(void) {
  if (!front_buffer || !back_buffer)
    return;

  unsigned int pitch_dwords = fb_pitch / 4;
  if (pitch_dwords == fb_width) {
    fast_copy32(front_buffer, back_buffer, (size_t)fb_width * fb_height);
  } else {
    for (unsigned int y = 0; y < fb_height; y++) {
      fast_copy32(front_buffer + (size_t)y * pitch_dwords,
                  back_buffer + (size_t)y * fb_width,
                  fb_width);
    }
  }

  __asm__ volatile("mfence" ::: "memory");
}

void draw_pixel(unsigned int x, unsigned int y, unsigned int hex_color) {
  if (x >= fb_width || y >= fb_height || !back_buffer)
    return;
  back_buffer[y * fb_width + x] = hex_color;
}

void draw_rect(unsigned int start_x, unsigned int start_y, unsigned int w,
               unsigned int h, unsigned int color) {
  if (!back_buffer || start_x >= fb_width || start_y >= fb_height || w == 0 || h == 0)
    return;
  if (start_x + w > fb_width)
    w = fb_width - start_x;
  if (start_y + h > fb_height)
    h = fb_height - start_y;

  for (unsigned int y = 0; y < h; y++) {
    unsigned int *row = &back_buffer[(start_y + y) * fb_width + start_x];
    fast_fill32(row, color, w);
  }
}

void clear_screen(unsigned int color) {
  if (!back_buffer) return;
  fast_fill32(back_buffer, color, (size_t)fb_width * fb_height);
}

void draw_char(unsigned int x, unsigned int y, char c, unsigned int color,
               int scale) {
  unsigned char font_idx = (unsigned char)c;
  if (font_idx >= 128) font_idx = '?';
  for (int row = 0; row < 8; row++) {
    unsigned char row_data = font8x8_basic[font_idx][row];
    for (int col = 0; col < 8; col++) {
      if (row_data & (1 << col)) {
        draw_rect(x + (col * scale), y + (row * scale), scale, scale, color);
      }
    }
  }
}

// Crisp 12x19 arrow cursor sprite:
// 'X' = dark border (0x111111), '.' = white fill (0xFFFFFF), 's' = shadow (0x222222)
static const char *const cursor_bitmap[19] = {
    "X           ",
    "XX          ",
    "X.X         ",
    "X..X        ",
    "X...X       ",
    "X....X      ",
    "X.....X     ",
    "X......X    ",
    "X.......X   ",
    "X........X  ",
    "X.........X ",
    "X......XXXXX",
    "X...X..X    ",
    "X..XX..X    ",
    "X.X  X..X   ",
    "XX   X..X   ",
    "X     X..X  ",
    "      X..X  ",
    "       XX   ",
};

void draw_cursor(unsigned int x, unsigned int y) {
  for (unsigned int row = 0; row < 19; row++) {
    const char *line = cursor_bitmap[row];
    for (unsigned int col = 0; col < 12; col++) {
      char ch = line[col];
      if (ch == 'X') {
        draw_pixel(x + col + 1, y + row + 1, 0x050505); // drop shadow
        draw_pixel(x + col, y + row, 0x111111);         // border
      } else if (ch == '.') {
        draw_pixel(x + col, y + row, 0xFFFFFF);         // interior fill
      }
    }
  }
}

void draw_string(unsigned int x, unsigned int y, const char* str, unsigned int color, int scale) {
  int cur_x = x;
  while (*str) {
    draw_char(cur_x, y, *str, color, scale);
    cur_x += 8 * scale;
    str++;
  }
}

void draw_hex32(unsigned int x, unsigned int y, unsigned int val, unsigned int color, int scale) {
  const char* hex_chars = "0123456789ABCDEF";
  char buf[11];
  buf[0] = '0';
  buf[1] = 'x';
  buf[10] = '\0';
  for (int i = 7; i >= 0; i--) {
    buf[2 + i] = hex_chars[val & 0xF];
    val >>= 4;
  }
  draw_string(x, y, buf, color, scale);
}

void draw_int(unsigned int x, unsigned int y, unsigned int val, unsigned int color, int scale) {
  char buf[21];
  kutoa(val, buf);
  draw_string(x, y, buf, color, scale);
}
