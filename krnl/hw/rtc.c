#include "hw/rtc.h"
#include "memio.h" 

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

static int get_update_in_progress_flag(void) {
    outb(CMOS_ADDRESS, 0x0A);
    return (inb(CMOS_DATA) & 0x80);
}

static uint8_t get_rtc_register(int reg) {
    outb(CMOS_ADDRESS, reg);
    return inb(CMOS_DATA);
}


static uint8_t bcd_to_binary(uint8_t value) {
    return ((value / 16) * 10) + (value & 0x0F);
}

uint64_t rtc_get_unix_time(void) {
    uint8_t second, minute, hour, day, month;
    uint16_t year;
    uint8_t registerB;
    
    
    while (get_update_in_progress_flag());
    
    second = get_rtc_register(0x00);
    minute = get_rtc_register(0x02);
    hour   = get_rtc_register(0x04);
    day    = get_rtc_register(0x07);
    month  = get_rtc_register(0x08);
    year   = get_rtc_register(0x09);
    
    registerB = get_rtc_register(0x0B);
    
    if (!(registerB & 0x04)) {
        second = bcd_to_binary(second);
        minute = bcd_to_binary(minute);
        hour   = bcd_to_binary(hour & 0x7F) | (hour & 0x80);
        day    = bcd_to_binary(day);
        month  = bcd_to_binary(month);
        year   = bcd_to_binary(year);
    }
    
    if (!(registerB & 0x02) && (hour & 0x80)) {
        hour = ((hour & 0x7F) + 12) % 24;
    }
    
    year += (year < 70) ? 2000 : 1900;
    int days_in_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
        days_in_month[1] = 29;
    }
    
    uint64_t days = 0;
    
    for (int y = 1970; y < year; y++) {
        days += ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ? 366 : 365;
    }
    
    for (int m = 0; m < month - 1; m++) {
        days += days_in_month[m];
    }
    
    days += (day - 1);
    
    uint64_t unix_time = (days * 86400) + (hour * 3600) + (minute * 60) + second;
    
    return unix_time;
}

void rtc_init(void) {
}