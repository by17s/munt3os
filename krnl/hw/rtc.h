#ifndef _HW_RTC_H
#define _HW_RTC_H

#include <stdint.h>
#include <api/sysdef.h>

void rtc_init(void);
uint64_t rtc_get_unix_time(void);

static inline time_t now(time_t* t) {
    uint64_t unix_time = rtc_get_unix_time();
    if (t) *t = (time_t)unix_time;
    return (time_t)unix_time;
}

#endif 