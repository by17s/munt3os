#pragma once
#include <stdint.h>
#include "fs/vfs.h"

vfs_node_t* dev_ramdisk_init(void* base, uint64_t size);
