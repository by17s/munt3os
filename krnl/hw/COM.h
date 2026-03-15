#pragma once

#define COM1_PORT 0x3F8

void com_init(int com_port);
void com_write_char(int com_port, char c);
void com_print(int com_port, const char* str);