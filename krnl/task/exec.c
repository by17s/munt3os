#include "exec.h"
#include "sched.h"
#include "elf.h"
#include "../fs/vfs.h"
#include "../mem/vmm.h"
#include "../mem/pmm.h"
#include "../mm.h"
#include "../log.h"
#include "../dev/dev.h"

#include "cstdlib.h"

__attribute__((naked)) static void sched_execve_noreturn(uint64_t entry, uint64_t stack_top, uint64_t argc, uint64_t argv_ptr) {
    asm volatile(
        "mov %rsi, %rsp\n"
        "push $0x30\n"   /* SS */
        "push %rsi\n"    /* RSP */
        "push $0x202\n"  /* RFLAGS */
        "push $0x28\n"   /* CS */
        "push %rdi\n"    /* RIP = entry */
        "mov %rdx, %rdi\n"  /* argc → rdi (arg1 for main) */
        "mov %rcx, %rsi\n"  /* argv → rsi (arg2 for main) */
        "iretq\n"
    );
}

int sys_execve(const char* path, const char** argv) {
    vfs_node_t* node = uopen(path, O_RDONLY);
    if (!node) {
        return -1; 
    }
    
    vmm_context_t* new_ctx = vmm_create_context();
    if (!new_ctx) {
        kclose(node);
        return -1;
    }

    
    uint64_t entry = elf_load(node, new_ctx);
    kclose(node);

    if (entry == 0) {
        
        return -1;
    }

    
    uint64_t stack_size = 4096 * 4; 
    uint64_t stack_base = 0x700000000000;
    
    for (int i = 0; i < 4; i++) {
        uint64_t paddr = (uint64_t)pmm_alloc(1);
        vmm_map(new_ctx, stack_base + i * 4096, paddr, VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
    }

    uint64_t stack_top = stack_base + stack_size;

    
    thread_t* thread = sched_get_current_thread();
    uint64_t old_cr3 = thread->cr3;
    thread->cr3 = (uint64_t)new_ctx->pml4_phys; 
    
    if (old_cr3 != (uint64_t)vmm_get_kernel_context()->pml4_phys) {
        vmm_free_cr3(old_cr3);
    }
    
    
    thread->heap_start = 0x40000000;
    thread->heap_end = thread->heap_start;
    thread->mmap_next = 0x8000000000; 
    
    strncpy(thread->name, path, 255);
    thread->name[255] = '\0';
    
    if (!thread->fds[0].node) thread->fds[0].node = kopen("/dev/tty");
    if (!thread->fds[1].node) thread->fds[1].node = kopen("/dev/tty");
    if (!thread->fds[2].node) thread->fds[2].node = kopen("/dev/tty");
    if (thread->fds[0].node) thread->fds[0].flags = 0;
    if (thread->fds[1].node) thread->fds[1].flags = 0;
    if (thread->fds[2].node) thread->fds[2].flags = 0;

    vmm_switch_context(new_ctx);
    
    kfree(new_ctx); 

    
    int argc = 0;
    if (argv) {
        while (argv[argc]) argc++;
    }

    
    uint64_t sp = stack_top;

    
    uint64_t str_addrs[256];
    for (int i = argc - 1; i >= 0; i--) {
        size_t len = strlen(argv[i]) + 1;
        sp -= len;
        memcpy((void*)sp, argv[i], len);
        str_addrs[i] = sp;
    }

    
    sp &= ~0xFULL;

    
    sp -= sizeof(uint64_t);
    *(uint64_t*)sp = 0;

    
    for (int i = argc - 1; i >= 0; i--) {
        sp -= sizeof(uint64_t);
        *(uint64_t*)sp = str_addrs[i];
    }

    uint64_t argv_user = sp;

    
    sp &= ~0xFULL;

    
    sched_execve_noreturn(entry, sp, (uint64_t)argc, argv_user);

    return 0; 
}
