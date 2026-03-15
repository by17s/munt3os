#include "fbdev.h"
#include "dev.h"
#include "mm.h"
#include "cstdlib.h"
#include "fs/devfs.h"

#include "hw/video.h"

#include "log.h"
LOG_MODULE("fbdev");

typedef struct {
    void* base;
    uint64_t size;
    framebuffer_t fb_info;
} fbdev_t;

static uint32_t fbdev_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    fbdev_t* fb = (fbdev_t*)node->device;
    if (offset >= fb->size) return 0;
    if (offset + size > fb->size) size = fb->size - offset;
    memcpy_fast(buffer, (uint8_t*)fb->base + offset, size);
    return size;
}

static uint32_t fbdev_write(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    fbdev_t* fb = (fbdev_t*)node->device;
    if (offset >= fb->size) return 0;
    if (offset + size > fb->size) size = fb->size - offset;
    memcpy_fast((uint8_t*)fb->base + offset, buffer, size);
    return size;
}

static int fbdev_ioctl(vfs_node_t* node, int request, void* arg) {
    fbdev_t* fb = (fbdev_t*)node->device;
    switch (request)
    {
    case FBDEV_IOCTL_MAP:
        if (!arg) break;
        struct tty_fb_info* info = (struct tty_fb_info*)arg;

        uint64_t fb_virt = (uint64_t)fb->fb_info.buffer;
        uint64_t size = (uint64_t)fb->fb_info.pitch * fb->fb_info.height;
        uint64_t num_pages = (size + 4095) / 4096;
        
        
        uint64_t user_virt = 0xA0000000;
        
        LOG_INFO("Mapping %llu pages of FB (virt: %llx) to user_virt %llx", num_pages, fb_virt, user_virt);
        LOG_INFO("FB Info: %dx%dx%d, pitch=%d, total_size=%llu", fb->fb_info.width, fb->fb_info.height, fb->fb_info.bpp, fb->fb_info.pitch, size);

        vmm_context_t active_ctx = vmm_get_active_context();
        for (uint64_t i = 0; i < num_pages; i++) {
            uint64_t p_addr = vmm_virt_to_phys(vmm_get_kernel_context(), fb_virt + i * 4096);
            if (i == 0) LOG_INFO("First page phys: %llx", p_addr);
            vmm_map(&active_ctx, user_virt + i * 4096, p_addr, VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER | VMM_FLAG_NO_CACHE | VMM_FLAG_NO_FREE);
        }

        info->buffer = (void*)user_virt;
        info->width = fb->fb_info.width;
        info->height = fb->fb_info.height;
        info->pitch = fb->fb_info.pitch;
        info->bpp = fb->fb_info.bpp;    
        LOG_INFO("FBDEV_IOCTL_MAP done: buf=%llx w=%d h=%d", (uint64_t)info->buffer, info->width, info->height);
        return 0;
        break;
    
    default:
        break;
    }
    return -1; 
}

static vfs_operations_t fbdev_ops = {
    .read = fbdev_read,
    .write = fbdev_write,
    .ioctl = fbdev_ioctl,
};

static uint32_t __fbdev_counter = 0;

vfs_node_t* dev_fb_init(framebuffer_t* fb_info) {
    fbdev_t* fb = (fbdev_t*)kmalloc(sizeof(fbdev_t));
    if (!fb) 
        return NULL;
    fb->base = fb_info->buffer;
    fb->size = fb_info->width * fb_info->height * (fb_info->bpp / 8);
    fb->fb_info = *fb_info;

    vfs_node_t* node = vfs_alloc_node();
    if (!node) 
    return NULL;
    snprintf(node->name, sizeof(node->name), "fb%u", __fbdev_counter++);
    node->type = VFS_BLOCK_DEVICE;
    node->ops = &fbdev_ops;
    node->size = fb->size;
    node->device = (void*)fb;
    
    devfs_register_device(node->name, node);
    
    return node;
}
