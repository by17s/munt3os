#pragma once

#include "fs/vfs.h"


void dev_init(void);


#include "fs/devfs.h"


void dev_null_init(void);
void dev_zero_init(void);
void dev_random_init(void);
void dev_tty_init(void);
