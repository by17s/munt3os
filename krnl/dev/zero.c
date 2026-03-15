#include "dev.h"
#include "fs/vfs.h"
#include <stddef.h>
#include <stdint.h>
#include "cstdlib.h"

static uint32_t zero_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    (void)node; (void)offset;
    memset(buffer, 0, size);
    return size;
}

static uint32_t zero_write(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    (void)node; (void)offset; (void)buffer;
    return size;
}

static vfs_operations_t zero_ops = {
    .read = zero_read,
    .write = zero_write
};

void dev_zero_init(void) {
    vfs_node_t* node = vfs_alloc_node();
    if (!node) return;
    
    node->type = VFS_CHAR_DEVICE;
    node->ops = &zero_ops;
    node->size = 2048;
    
    devfs_register_device("zero", node);
}
