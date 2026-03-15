#include "dev.h"
#include "fs/vfs.h"
#include <stddef.h>
#include <stdint.h>
#include "cstdlib.h"

static uint32_t rand_state = 123456789;
static uint8_t rand_byte(void) {
    rand_state = rand_state * 1103515245 + 12345;
    return (uint8_t)((rand_state >> 16) & 0xFF);
}

static uint32_t random_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    (void)node; (void)offset;
    for(uint32_t i=0; i<size; i++) {
        buffer[i] = rand_byte();
    }
    return size;
}

static uint32_t null_write_proxy(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    (void)node; (void)offset; (void)buffer;
    return size;
}

static vfs_operations_t random_ops = {
    .read = random_read,
    .write = null_write_proxy
};

void dev_random_init(void) {
    vfs_node_t* rng_node = vfs_alloc_node();
    if(rng_node) {
        rng_node->type = VFS_CHAR_DEVICE;
        rng_node->ops = &random_ops;
        rng_node->size = 2048;
        devfs_register_device("random", rng_node);
    }

    vfs_node_t* urng_node = vfs_alloc_node();
    if(urng_node) {
        urng_node->type = VFS_CHAR_DEVICE;
        urng_node->ops = &random_ops;
        urng_node->size = 2048;
        devfs_register_device("urandom", urng_node);
    }
}
