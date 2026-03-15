#pragma once

#include <stdint.h>
#include <stddef.h>

#define MAX_FDS 32

struct vfs_node; 

typedef struct file_descriptor {
    struct vfs_node* node;
    uint32_t offset;
    int flags;
} file_descriptor_t;

void syscall_init(void);


#include "sysnums.h"

#include "sysdef.h"
