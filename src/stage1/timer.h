#pragma once
// src/stage1/timer.h - wall-clock style delays and timeouts without interrupts.

void timer_init(void);
unsigned long long timer_ms(void);     // milliseconds since timer_init()
unsigned long long timer_us(void);     // microseconds since timer_init()
void udelay(unsigned long long us);
void mdelay(unsigned long long ms);
unsigned long long timer_tsc_per_ms(void);

