/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     CMOS Time implementation.
 */

#ifndef X86_TIME_H
#define X86_TIME_H
#include <stdint.h>
#include <stdbool.h>
#include "intrin/intrin.h"

// RTC CMOS ports
#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

// TIME_ENTRY struct with full date
typedef struct {
    uint8_t second;   // 0–59
    uint8_t minute;   // 0–59
    uint8_t hour;     // 0–23
    uint8_t day;      // 1–31
    uint8_t month;    // 1–12
    uint16_t year;    // full year, e.g., 2025
} TIME_ENTRY;

// Read from CMOS
static inline uint8_t cmos_read(uint8_t reg) {
    __outbyte(CMOS_ADDRESS, reg);
    return __inbyte(CMOS_DATA);
}

// Check if RTC is updating
static inline bool rtc_updating(void) {
    __outbyte(CMOS_ADDRESS, 0x0A);
    return (__inbyte(CMOS_DATA) & 0x80) != 0;
}

// Convert BCD → binary
static inline uint8_t bcd_to_bin(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

// Get current time/date (GIVES UTC TIME)
static TIME_ENTRY get_time(void) {
    TIME_ENTRY t;
    uint8_t century = 0;
    uint8_t regB;

    // Wait until RTC is not updating
    while (rtc_updating());

    // Read raw values
    t.second = cmos_read(0x00);
    t.minute = cmos_read(0x02);
    t.hour = cmos_read(0x04);
    t.day = cmos_read(0x07);
    t.month = cmos_read(0x08);
    uint8_t year = cmos_read(0x09);

    // Some BIOSes provide century register (0x32) if available
    century = cmos_read(0x32);

    // Status register B tells us data format
    regB = cmos_read(0x0B);

    // Convert from BCD if needed
    if (!(regB & 0x04)) {
        t.second = bcd_to_bin(t.second);
        t.minute = bcd_to_bin(t.minute);
        t.hour = bcd_to_bin(t.hour & 0x7F);
        t.day = bcd_to_bin(t.day);
        t.month = bcd_to_bin(t.month);
        year = bcd_to_bin(year);
        if (century) century = bcd_to_bin(century);
    }

    // Convert 12h → 24h if needed
    if (!(regB & 0x02) && (t.hour & 0x80)) {
        t.hour = ((t.hour & 0x7F) + 12) % 24;
    }

    // Build full year
    if (century != 0) {
        t.year = (century * 100) + year;
    }
    else {
        // Fallback: assume 20xx
        t.year = 2000 + year;
    }

    return t;
}
#endif