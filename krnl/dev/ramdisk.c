#include "ramdisk.h"
#include "dev.h"
#include "mm.h"
#include "cstdlib.h"
#include "fs/devfs.h"

typedef struct {
    void* base;
    uint64_t size;
} ramdisk_t;

static uint32_t ramdisk_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    ramdisk_t* ram0 = (ramdisk_t*)node->device;
    if (offset >= ram0->size) return 0;
    if (offset + size > ram0->size) size = ram0->size - offset;
    memcpy_fast(buffer, (uint8_t*)ram0->base + offset, size);
    return size;
}

static vfs_operations_t ramdisk_ops = {
    .read = ramdisk_read,
};

static uint32_t __ramdisk_counter = 0;

vfs_node_t* dev_ramdisk_init(void* base, uint64_t size) {
    ramdisk_t* rd = (ramdisk_t*)kmalloc(sizeof(ramdisk_t));
    if (!rd) return NULL;
    rd->base = base;
    rd->size = size;

    vfs_node_t* node = vfs_alloc_node();
    if (!node) return NULL;
    snprintf(node->name, sizeof(node->name), "ram%u", __ramdisk_counter++);
    node->type = VFS_BLOCK_DEVICE;
    node->ops = &ramdisk_ops;
    node->size = size;
    node->device = (void*)rd;
    
    devfs_register_device(node->name, node);
    
    return node;
}
