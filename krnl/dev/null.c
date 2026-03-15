#include "dev.h"
#include "fs/vfs.h"
#include <stddef.h>

static uint32_t null_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0; 
}

static uint32_t null_write(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    (void)node; (void)offset; (void)buffer;
    return size;
}

static vfs_operations_t null_ops = {
    .read = null_read,
    .write = null_write
};

void dev_null_init(void) {
    vfs_node_t* node = vfs_alloc_node();
    if (!node) return;
    
    node->type = VFS_CHAR_DEVICE;
    node->ops = &null_ops;
    node->size = 2048; 
    
    devfs_register_device("null", node);
}
