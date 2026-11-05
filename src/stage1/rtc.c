// src/stage1/rtc.c - Cached CMOS RTC reader (queries I/O ports at most every 500ms)

#include "rtc.h"
#include "hardware.h"
#include "timer.h"

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

static int cached_h = 0, cached_m = 0, cached_s = 0;
static unsigned long long last_rtc_poll_ms = 0;
static int rtc_has_sample = 0;

static int get_update_in_progress_flag(void) {
    outb(CMOS_ADDRESS, 0x0A);
    return (inb(CMOS_DATA) & 0x80);
}

static unsigned char get_RTC_register(int reg) {
    outb(CMOS_ADDRESS, (unsigned char)reg);
    return inb(CMOS_DATA);
}

static void wait_uip_clear(void) {
    int guard = 0;
    while (get_update_in_progress_flag() && ++guard < 10000) {
        cpu_pause();
    }
}

void rtc_read_time(int* hours, int* minutes, int* seconds) {
    unsigned long long now = timer_ms();
    if (rtc_has_sample && (now - last_rtc_poll_ms < 500)) {
        *hours = cached_h;
        *minutes = cached_m;
        *seconds = cached_s;
        return;
    }
    last_rtc_poll_ms = now;

    unsigned char last_second, last_minute, last_hour;
    unsigned char registerB;

    wait_uip_clear();
    int s = get_RTC_register(0x00);
    int m = get_RTC_register(0x02);
    int h = get_RTC_register(0x04);

    int tries = 0;
    do {
        last_second = (unsigned char)s;
        last_minute = (unsigned char)m;
        last_hour   = (unsigned char)h;

        wait_uip_clear();
        s = get_RTC_register(0x00);
        m = get_RTC_register(0x02);
        h = get_RTC_register(0x04);
    } while (((last_second != s) || (last_minute != m) || (last_hour != h)) && ++tries < 4);

    registerB = get_RTC_register(0x0B);

    if (!(registerB & 0x04)) {
        s = (s & 0x0F) + ((s / 16) * 10);
        m = (m & 0x0F) + ((m / 16) * 10);
        h = ((h & 0x0F) + (((h & 0x70) / 16) * 10)) | (h & 0x80);
    }

    if (!(registerB & 0x02) && (h & 0x80)) {
        h = ((h & 0x7F) + 12) % 24;
    }

    // Apply IST (+05:30) offset from CMOS UTC time
    int total_minutes = h * 60 + m + (5 * 60 + 30);
    total_minutes %= (24 * 60);
    cached_h = total_minutes / 60;
    cached_m = total_minutes % 60;
    cached_s = s;
    rtc_has_sample = 1;

    *hours = cached_h;
    *minutes = cached_m;
    *seconds = cached_s;
}
