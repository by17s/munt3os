#include "sched.h"
#include "../hw/lapic.h"
#include "../hw/idt.h"
#include "../mem/vmm.h"
#include "../mem/kheap.h"
#include "../log.h"
#include "../cstdlib.h"
#include <mm.h>

LOG_MODULE("sched");

thread_t* current_thread = NULL;
static thread_t* thread_queue = NULL;

static uint64_t next_tid = 1;

thread_t* sched_get_current_thread(void) {
    return current_thread;
}

void sched_init(void) {
    idt_set_descriptor(0xEF, isr_lapic_timer, 0x8E);
    LOG_INFO("Scheduler initialized, ISR wired to 0xEF");
}

void sched_add_thread(const char* name, void (*entry_point)(void), void* stack_bottom, size_t stack_size, uint64_t cr3) {
    thread_t* t = (thread_t*)kmalloc(4096); 
    memset(t, 0, 4096);
    
    t->id = next_tid++;
    t->cr3 = cr3;
    t->stack_bottom = stack_bottom;
    t->stack_size = stack_size;
    t->state = THREAD_RUNNING;
    t->exit_code = 0;
    t->parent_id = current_thread ? current_thread->id : 0;
    t->cwd = current_thread ? current_thread->cwd : NULL;

    if (name) {
        strncpy(t->name, name, 31);
        t->name[31] = '\0';
    } else {
        strcpy(t->name, "unnamed");
    }
    
    

    
    uint64_t* stack = (uint64_t*)((uint64_t)stack_bottom + stack_size);
    
    
    
    *(--stack) = 0x30; 
    *(--stack) = (uint64_t)stack_bottom + stack_size; 
    *(--stack) = 0x202; 
    *(--stack) = 0x28; 
    *(--stack) = (uint64_t)entry_point; 
    
    
    *(--stack) = 0; 
    *(--stack) = 0; 
    *(--stack) = 0; 
    *(--stack) = 0; 
    *(--stack) = 0; 
    *(--stack) = 0; 
    *(--stack) = 0; 
    *(--stack) = 0; 
    *(--stack) = 0; 
    *(--stack) = 0; 
    *(--stack) = 0; 
    *(--stack) = 0; 
    *(--stack) = 0; 
    *(--stack) = 0; 
    *(--stack) = 0; 
    
    t->rsp = (uint64_t)stack;
    
    
    if (!thread_queue) {
        t->next = t; 
        thread_queue = t;
    } else {
        thread_t* tail = thread_queue;
        while (tail->next != thread_queue) tail = tail->next;
        tail->next = t;
        t->next = thread_queue;
    }
    LOG_INFO("Thread added '%s' TID: %llu", t->name, t->id);
}

uint64_t sched_tick(uint64_t rsp) {
    lapic_eoi();
    
    if (!current_thread && !thread_queue) return rsp;
    if (!current_thread) {
        current_thread = thread_queue;
        if (current_thread->state == THREAD_RUNNING) {
            return current_thread->rsp;
        }
    }
    
    
    current_thread->rsp = rsp;
    
    
    thread_t* next_t = current_thread->next;
    thread_t* start_t = next_t;
    
    while(next_t->state != THREAD_RUNNING) {
        next_t = next_t->next;
        if (next_t == start_t) {
            
            
            break;
        }
    }
    
    current_thread = next_t;
    
    
    asm volatile("mov %0, %%cr3" :: "r"(current_thread->cr3) : "memory");
    
    return current_thread->rsp;
}

void sched_exit(int status) {
    if (!current_thread) return;
    current_thread->state = THREAD_ZOMBIE;
    current_thread->exit_code = status;
    
    
    for (int i = 0; i < MAX_FDS; i++) {
        if (current_thread->fds[i].node) {
            
            current_thread->fds[i].node = NULL;
        }
    }

    LOG_INFO("Thread '%s' (TID: %llu) exited with code %d", current_thread->name, current_thread->id, status);

    
    while(1) {
        asm volatile("int $0xEF"); 
    }
}

int sched_waitpid(uint64_t pid, int* status) {
    if (!current_thread) return -1;
    
    
    
    while (1) {
        thread_t* target = NULL;
        thread_t* t = thread_queue;
        if (t) {
            do {
                if (t->id == pid && t->parent_id == current_thread->id) {
                    target = t;
                    break;
                }
                t = t->next;
            } while (t != thread_queue);
        }

        if (!target) {
            return -1; 
        }

        if (target->state == THREAD_ZOMBIE) {
            if (status) *status = target->exit_code;
            
            
            target->state = THREAD_DEAD;
            
            thread_t* prev = thread_queue;
            while(prev->next != target) prev = prev->next;
            
            if (prev == target) {
                thread_queue = NULL; 
            } else {
                prev->next = target->next;
                if (thread_queue == target) thread_queue = target->next;
            }

            if (target->cr3 != (uint64_t)vmm_get_kernel_context()->pml4_phys) {
                
                vmm_free_cr3(target->cr3);
            }

            
            kfree(target->stack_bottom);
            
            kfree(target);
            
            return pid;
        }

        
        asm volatile("int $0xEF");
    }
    return -1;
}

uint64_t sched_fork_internal(uint64_t rsp_val) {
    if (!current_thread) return -1;
    if (!current_thread->stack_bottom || current_thread->stack_size == 0) {
        LOG_ERROR("Cannot fork thread without proper stack boundaries.");
        return -1;
    }
    
    thread_t* child = (thread_t*)kmalloc(4096);
    if (!child) return -1;
    memset(child, 0, 4096);
    
    child->id = next_tid++;
    child->parent_id = current_thread->id;
    
    
    vmm_context_t current_ctx;
    current_ctx.pml4_phys = (pagemap_t*)current_thread->cr3;
    current_ctx.pml4_virt = (pagemap_t*)((uint64_t)current_thread->cr3 + 0xFFFF800000000000);
    
    vmm_context_t* cloned_ctx = vmm_clone_context(&current_ctx);
    if (!cloned_ctx) {
        kfree(child);
        return -1;
    }
    child->cr3 = (uint64_t)cloned_ctx->pml4_phys;
    kfree(cloned_ctx);

    child->state = THREAD_RUNNING;
    child->exit_code = 0;
    child->cwd = current_thread->cwd;
    child->heap_start = current_thread->heap_start;
    child->heap_end = current_thread->heap_end;
    child->mmap_next = current_thread->mmap_next;
    child->stack_size = current_thread->stack_size;
    child->stack_bottom = kmalloc(child->stack_size);
    if (!child->stack_bottom) {
        kfree(child);
        return -1;
    }
    
    strcpy(child->name, current_thread->name);
    
    strncat(child->name, "_f", 31 - strlen(child->name));
    
    
    for (int i = 0; i < MAX_FDS; i++) {
        if (current_thread->fds[i].node) {
            child->fds[i] = current_thread->fds[i];
            
        }
    }
    
    
    memcpy(child->stack_bottom, current_thread->stack_bottom, child->stack_size);
    
    
    uint64_t rsp_offset = (uint64_t)rsp_val - (uint64_t)current_thread->stack_bottom;
    child->rsp = (uint64_t)child->stack_bottom + rsp_offset;
    
    
    
    uint64_t* child_top = (uint64_t*)child->rsp;
    *child_top = 0; 

    
    uint64_t parent_stack_start = (uint64_t)current_thread->stack_bottom;
    uint64_t parent_stack_end = parent_stack_start + current_thread->stack_size;
    uint64_t child_stack_start = (uint64_t)child->stack_bottom;
    
    
    for (uint64_t i = 0; i < child->stack_size - 7; i += 8) {
        uint64_t* ptr = (uint64_t*)(child_stack_start + i);
        if (*ptr >= parent_stack_start && *ptr < parent_stack_end) {
            *ptr = *ptr - parent_stack_start + child_stack_start;
        }
    }
    
    
    if (!thread_queue) {
        child->next = child;
        thread_queue = child;
    } else {
        thread_t* tail = thread_queue;
        while (tail->next != thread_queue) tail = tail->next;
        tail->next = child;
        child->next = thread_queue;
    }
    
    LOG_INFO("Forked thread '%s' TID: %llu -> Child TID: %llu", current_thread->name, current_thread->id, child->id);
    
    return child->id; 
}

__attribute__((naked)) uint64_t sched_fork(void) {
    asm volatile(
        "pushq $0x30\n"           
        "mov %rsp, %r11\n"
        "add $8, %r11\n"
        "pushq %r11\n"            
        "pushfq\n"                
        "pushq $0x28\n"           
        "leaq 1f(%rip), %r11\n"   
        "pushq %r11\n"            
        
        "push %r15\n"
        "push %r14\n"
        "push %r13\n"
        "push %r12\n"
        "push %r11\n"
        "push %r10\n"
        "push %r9\n"
        "push %r8\n"
        "push %rbp\n"
        "push %rdi\n"
        "push %rsi\n"
        "push %rdx\n"
        "push %rcx\n"
        "push %rbx\n"
        "push %rax\n"
        
        "mov %rsp, %rdi\n"
        "call sched_fork_internal\n"
        
        "mov %rax, 0(%rsp)\n" 
        
        "pop %rax\n"
        "pop %rbx\n"
        "pop %rcx\n"
        "pop %rdx\n"
        "pop %rsi\n"
        "pop %rdi\n"
        "pop %rbp\n"
        "pop %r8\n"
        "pop %r9\n"
        "pop %r10\n"
        "pop %r11\n"
        "pop %r12\n"
        "pop %r13\n"
        "pop %r14\n"
        "pop %r15\n"
        
        "iretq\n"
        
        "1:\n" 
        "ret\n"
    );
}

__attribute__((aligned(16)))
static uint8_t sched_temp_stack[4096];

__attribute__((naked)) void isr_lapic_timer(void) {
    asm volatile(
        "push %r15\n"
        "push %r14\n"
        "push %r13\n"
        "push %r12\n"
        "push %r11\n"
        "push %r10\n"
        "push %r9\n"
        "push %r8\n"
        "push %rbp\n"
        "push %rdi\n"
        "push %rsi\n"
        "push %rdx\n"
        "push %rcx\n"
        "push %rbx\n"
        "push %rax\n"
        
        "mov %rsp, %rdi\n"
        "lea sched_temp_stack(%rip), %rsp\n"
        "add $4096, %rsp\n"
        "call sched_tick\n"
        "mov %rax, %rsp\n"
        
        "pop %rax\n"
        "pop %rbx\n"
        "pop %rcx\n"
        "pop %rdx\n"
        "pop %rsi\n"
        "pop %rdi\n"
        "pop %rbp\n"
        "pop %r8\n"
        "pop %r9\n"
        "pop %r10\n"
        "pop %r11\n"
        "pop %r12\n"
        "pop %r13\n"
        "pop %r14\n"
        "pop %r15\n"
        "iretq\n"
    );
}

int sched_kill(uint64_t pid, int sig) {
    if (!thread_queue) return -1;
    
    thread_t* target = NULL;
    thread_t* t = thread_queue;
    do {
        if (t->id == pid) {
            target = t;
            break;
        }
        t = t->next;
    } while (t != thread_queue);

    if (!target) return -1; 
    
    
    
    if (target->state != THREAD_ZOMBIE && target->state != THREAD_DEAD) {
        target->state = THREAD_ZOMBIE;
        target->exit_code = 128 + sig;
    }

    return 0;
}

void sched_yield(void) {
    asm volatile("int $0xEF");
}
