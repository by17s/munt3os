#pragma once
#include <stddef.h>
#include "vfs.h"

typedef struct {
    vfs_node_t* block_node;
} tarfs_mount_data_t;

void tarfs_init(void);
