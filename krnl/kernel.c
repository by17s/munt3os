#include <kernel.h>

#include "log.h"
LOG_MODULE("kernel");


#include "mem/pmm.h"
#include "mem/vmm.h"
#include <mm.h>


#include <tty.h>
#include <hw/cpuid.h>
#include "dev/dev.h"

#include <util/cmdline.h>

#include "task/sched.h"
#include "api/syscall.h"
#include "font/psf.h"

#include "fs/vfs.h"
#include "fs/tarfs.h"
#include "fs/devfs.h"
#include "fs/procfs.h"

#include "dev/ramdisk.h"
#include "dev/fbdev.h"
#include "dev/input.h"


#include "hw/acpi.h"
#include "hw/lapic.h"
#include "hw/COM.h"
#include "hw/video.h"
#include "hw/ps2.h"
#include "hw/pcie.h"
#include "hw/usb/usb.h"

#include "hw/rtc.h"

#define PMM_BLOCK_SIZE 4096


static inline uint64_t syscall(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    uint64_t ret;
    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(sys_num), "D"(arg1), "S"(arg2), "d"(arg3)
        : "rcx", "r11", "memory" 
    );
    return ret;
}

void thread_vidswap(void) {
    tty_t* tty;
    while (1) {
        tty_get(-1, &tty);
        vid_fb_swap(tty->fb);
        asm volatile("hlt");
    }
}

static inline void enable_sse(void) {
    uint64_t cr0, cr4;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2); 
    cr0 |= (1 << 1);  
    asm volatile("mov %0, %%cr0" :: "r"(cr0));

    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);  
    cr4 |= (1 << 10); 
    asm volatile("mov %0, %%cr4" :: "r"(cr4));
}

time_t start_time = 0;

int kmain(tty_t *tty, int flags, void* initrd_base, size_t initrd_size) {
    if(!tty) {
        LOG_ERROR("Failed to get main TTY!");
        return -1;
    }

    enable_sse();

    LOG_INFO("Hello from kmain! Flags: 0x%x", flags);
    start_time = rtc_get_unix_time();
                
    cpu_info_t cpu;
    get_cpu_info(&cpu);
    tty->printf(tty, "CPU: %s @ ", cpu.brand_string);
    
    if (cpu.base_freq_mhz > 0) {
        tty->printf(tty, "%u MHz (Max: %u MHz)\n", cpu.base_freq_mhz, cpu.max_freq_mhz);
    } else {
        tty->puts(tty, "(Frequency info not available via CPUID (Leaf 0x16 unsupported))\n");
    }

    struct pmm_stat mem_stat;
    pmm_stat(&mem_stat);
    tty->printf(tty, "RAM: %llu MB\n", (mem_stat.usable_pages * 0x1000) / 1024 / 1024 );
    tty->putchar(tty, '\n');

    if (initrd_base && initrd_size > 0) {
        vfs_node_t* ramdisk_node = dev_ramdisk_init(initrd_base, initrd_size);

        tarfs_mount_data_t tar_data = {
            .block_node = ramdisk_node
        };

        vfs_mount("/dev/ram0", "/", "tarfs", &tar_data);
        LOG_INFO("VFS mounted initramfs at /");
        
        vfs_node_t* font_node = kopen("/usr/share/alt-8x16.psf");
        if (font_node) {
            uint8_t* font_buf = kmalloc(font_node->size);
            if (font_buf) {
                vfs_read(font_node, 0, font_node->size, font_buf);
                psf_font_t* font = kmalloc(sizeof(psf_font_t));
                if (psf_init_font(font, font_buf) == 0) {
                    tty->font = font;
                    LOG_INFO("Loaded PSF font from initramfs!");
                } else {
                    LOG_ERROR("Failed to parse PSF font!");
                    kfree(font);
                    kfree(font_buf);
                }
            }
            kclose(font_node);
        } else {
            LOG_ERROR("Could not open font file /usr/share/alt-8x16.psF");
        }

    } else {
        LOG_WARN("No initramfs was provided to the kernel!");
    }

    
    vfs_mount("devfs", "/dev", "devfs", NULL);
    vfs_mount("procfs", "/proc", "procfs", NULL);
    procfs_setup();

    
    dev_null_init();
    dev_zero_init();
    dev_random_init();
    dev_fb_init(tty->fb);
    dev_tty_init();

    input_init();

    usb_init();

    sched_init();
    syscall_init();
    
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    
    void* stack_usbpolling = (void*)kmalloc(4096);
    void* stack_cmdline = (void*)kmalloc(8192); 
    void* stack_vidswap = (void*)kmalloc(4096);

    sched_add_thread("[USB Polling]", (void*)usb_polling_thread, stack_usbpolling, 4096, cr3);

    if(vid_fb_enable_swap(tty->fb) == 0) {
        LOG_INFO("Framebuffer swap enabled successfully.");
        sched_add_thread("[Video]", (void*)thread_vidswap, stack_vidswap, 4096, cr3);
    } else {
        LOG_ERROR("Failed to enable framebuffer swap.");
    }

    vfs_mount("/dev/sda", "/mnt", "tarfs", NULL);
    
    if(acpi_ps2_keyboard_present() || usb_dev_present(USB_DEV_TYPE_KEYBOARD)) {
        sched_add_thread("UxSh (Fake)", cmdline_run, stack_cmdline, 8192, cr3);
    } else {
        tty_printf("No PS/2 keyboard or USB keyboard detected. CMDLINE support will be disabled.\n");
    }
    
    lapic_timer_init(0xEF, 10000000, 0x0B);

    while (1) {
        asm volatile("hlt");
    }
}