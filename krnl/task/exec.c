#include "exec.h"
#include "sched.h"
#include "elf.h"
#include "../fs/vfs.h"
#include "../mem/vmm.h"
#include "../mem/pmm.h"
#include "../mm.h"
#include "../log.h"
#include "../dev/dev.h"

__attribute__((naked)) static void sched_execve_noreturn(uint64_t entry, uint64_t stack_top) {
    asm volatile(
        "mov %rsi, %rsp\n"
        "push $0x30\n" 
        "push %rsi\n"  
        "push $0x202\n" 
        "push $0x28\n"  
        "push %rdi\n"   
        "iretq\n"
    );
}

int sys_execve(const char* path) {
    vfs_node_t* node = kopen(path);
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
    
    
    
    if (!thread->fds[0].node) thread->fds[0].node = kopen("/dev/tty");
    if (!thread->fds[1].node) thread->fds[1].node = kopen("/dev/tty");
    if (!thread->fds[2].node) thread->fds[2].node = kopen("/dev/tty");
    if (thread->fds[0].node) thread->fds[0].flags = 0;
    if (thread->fds[1].node) thread->fds[1].flags = 0;
    if (thread->fds[2].node) thread->fds[2].flags = 0;

    vmm_switch_context(new_ctx);
    
    kfree(new_ctx); 

    
    sched_execve_noreturn(entry, stack_top);

    return 0; 
}
