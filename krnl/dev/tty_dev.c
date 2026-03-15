#include "dev.h"
#include "../fs/vfs.h"
#include "../tty.h"
#include <stddef.h>
#include <mem/vmm.h>
#include "../log.h"

#include "api/sysdef.h"

LOG_MODULE("tty");

static uint32_t dev_tty_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    (void)node; (void)offset;
    tty_t* tty = NULL;
    tty_get(-1, &tty); 
    if (!tty) return 0;
    
    uint32_t bytes_read = 0;
    while (bytes_read < size) {
        int c = tty->getchar(tty);
        if (c >= 0) {
            buffer[bytes_read++] = (uint8_t)c;
        } else {
            break; 
        }
    }
    return bytes_read;
}

static uint32_t dev_tty_write(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    (void)node; (void)offset;
    tty_t* tty = NULL;
    tty_get(-1, &tty); 
    if (!tty) return 0;

    for (uint32_t i = 0; i < size; i++) {
        tty->putchar(tty, buffer[i]);
    }
    return size;
}

static int dev_tty_ioctl(vfs_node_t* node, int request, void* arg) {
    tty_t* tty = NULL;
    tty_get(-1, &tty); 
    if (!tty) return -1;

    switch (request) {
        case TTY_IOCTL_CLEAR_SCREEN: 
            tty->clear(tty);
            break;
        case TTY_IOCTL_MAP_FRAMEBUFFER: {
            if (!arg) break;
            struct tty_fb_info* info = (struct tty_fb_info*)arg;

            uint64_t fb_virt = (uint64_t)tty->fb->buffer;
            uint64_t size = (uint64_t)tty->fb->pitch * tty->fb->height;
            uint64_t num_pages = (size + 4095) / 4096;
            
            
            uint64_t user_virt = 0xA0000000;
            
            LOG_INFO("Mapping %llu pages of FB (virt: %llx) to user_virt %llx", num_pages, fb_virt, user_virt);
            LOG_INFO("FB Info: %dx%dx%d, pitch=%d, total_size=%llu", tty->fb->width, tty->fb->height, tty->fb->bpp, tty->fb->pitch, size);

                vmm_context_t active_ctx = vmm_get_active_context();
                for (uint64_t i = 0; i < num_pages; i++) {
                    uint64_t p_addr = vmm_virt_to_phys(vmm_get_kernel_context(), fb_virt + i * 4096);
                    if (i == 0) LOG_INFO("First page phys: %llx", p_addr);
                    vmm_map(&active_ctx, user_virt + i * 4096, p_addr, VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER | VMM_FLAG_NO_CACHE | VMM_FLAG_NO_FREE);
                }            info->buffer = (void*)user_virt;
            info->width = tty->fb->width;
            info->height = tty->fb->height;
            info->pitch = tty->fb->pitch;
            info->bpp = tty->fb->bpp;
            LOG_INFO("TTY_IOCTL_MAP_FRAMEBUFFER done: buf=%llx w=%d h=%d", (uint64_t)info->buffer, info->width, info->height);
            break;
        }
        default:
            return -1;
    }
    return 0;
}

static vfs_operations_t tty_ops = {
    .read = dev_tty_read,
    .write = dev_tty_write,
    .ioctl = dev_tty_ioctl,
};

void dev_tty_init(void) {
    vfs_node_t* node = vfs_alloc_node();
    if (!node) return;
    
    node->type = VFS_CHAR_DEVICE;
    node->ops = &tty_ops;
    
    devfs_register_device("tty", node);
}
