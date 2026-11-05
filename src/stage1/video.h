#pragma once
#include "hardware.h"

void video_init(unsigned long long physical_fb_base);
int video_is_ready(void);
void video_swap(void);
void draw_pixel(unsigned int x, unsigned int y, unsigned int hex_color);
void draw_rect(unsigned int start_x, unsigned int start_y, unsigned int w,
               unsigned int h, unsigned int color);
void clear_screen(unsigned int color);
void draw_char(unsigned int x, unsigned int y, char c, unsigned int color,
               int scale);
void draw_cursor(unsigned int x, unsigned int y);
void draw_string(unsigned int x, unsigned int y, const char* str, unsigned int color, int scale);
void draw_hex32(unsigned int x, unsigned int y, unsigned int val, unsigned int color, int scale);
void draw_int(unsigned int x, unsigned int y, unsigned int val, unsigned int color, int scale);
