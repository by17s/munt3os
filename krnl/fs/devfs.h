#pragma once

#include "vfs.h"

void devfs_init(void);
void devfs_register_device(const char* path, vfs_node_t* device_node);