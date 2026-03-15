#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

int vfprintf(int (*put)(void* put_arg0, char c), void* put_arg0, const char* format, va_list args);