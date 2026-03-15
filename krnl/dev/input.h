#pragma once

#include <stdint.h>
#include <fs/vfs.h>
#include <api/sysdef.h>

void input_init(void);

void input_report_key_mouse(uint16_t code, int32_t value);
void input_report_rel_mouse(uint16_t code, int32_t value);
void input_sync_mouse(void);

void input_report_key_kbd(uint16_t code, int32_t value);
void input_sync_kbd(void);
uint8_t input_get_kbd_leds(void);

