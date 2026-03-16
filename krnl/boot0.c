#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "limine.h"

#include "hw/idt.h"
#include "hw/acpi.h"
#include "hw/COM.h"
#include "hw/video.h"
#include "hw/ps2.h"
#include "hw/cpuid.h"
#include "hw/pcie.h"
#include "hw/usb/usb.h"

#include "mem/pmm.h"
#include "mem/vmm.h"
#include "mem/kheap.h"
#include "mm.h"

#include "fs/vfs.h"
#include "fs/tarfs.h"
#include "fs/devfs.h"
#include "fs/procfs.h"
#include "fs/ext2.h"

#include "log.h"

#include <kernel.h>


LOG_MODULE("boot0");


LIMINE_BASE_REVISION(1)


struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0
};


static void hcf(void) {
    asm("cli");
    for (;;) {
        asm("hlt");
    }
}

void _start(void) {
    log_init(LOG_DEV_COM1);
    if (LIMINE_BASE_REVISION_SUPPORTED == false) hcf();
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) hcf();

    struct limine_framebuffer *fbl = framebuffer_request.response->framebuffers[0];

    CLI();
    
    
    idt_init();
    mm_init();
    vmm_init();

    
    vfs_init();
    tarfs_init();
    devfs_init();
    procfs_init();
    ext2fs_init();

    
    acpi_init();
    pcie_init();
    pic_remap();

    if(acpi_ps2_keyboard_present() || acpi_ps2_mouse_present()) {
        ps2_init();
    }

    STI();
    
    
    void* test_page = pmm_alloc(1);
    if (test_page == NULL) {
        LOG_ERROR("Failed to allocate test page!");
        hcf();
    } 
    pmm_free(test_page, 1); 
    void* heapa = kmalloc(1024);
    if (heapa == NULL) {
        LOG_ERROR("Failed to allocate heap memory!");
        hcf();
    }
    kfree(heapa);
    LOG_INFO("Memory test passed successfully!");

    framebuffer_t *fb = (framebuffer_t*)kmalloc(sizeof(framebuffer_t));
    if (fb == NULL) {
        LOG_ERROR("Failed to allocate framebuffer!");
        hcf();
    }
    if (vid_fb_init(fb, framebuffer_request.response->framebuffers[0]) != 0) {
        LOG_ERROR("Failed to initialize framebuffer!");
        hcf();
    }
    vid_fb_clear(fb, 0x000000);

    tty_t *tty;
    if (tty_get(0, &tty) != 0) {
        LOG_ERROR("Failed to get TTY!");
        hcf();
    }
    if (tty_init(tty, fb) != 0) {
        LOG_ERROR("Failed to initialize TTY!");
        hcf();
    }
    tty_set_active(0);
    

    struct limine_module_response *mod_resp = module_request.response;
    void* initrd_base = NULL;
    size_t initrd_size = 0;

    if (mod_resp != NULL) {
        for (uint64_t i = 0; i < mod_resp->module_count; i++) {
            struct limine_file *module = mod_resp->modules[i];
            LOG_INFO("Module found: %s", module->path);
            
            
            if (i == 0) {
                initrd_base = module->address;
                initrd_size = module->size;
            }
        }
    }

    LOG_INFO("boot0 loaded successfully!");
    int kerr = kmain(tty, 0, initrd_base, initrd_size);
    if(kerr != 0) {
        LOG_ERROR("kmain returned with error %d!", kerr);
    }
    
    hcf();
}