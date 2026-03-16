#include "syscall.h"
#include "../task/sched.h"
#include "../task/exec.h"
#include "../fs/vfs.h"
#include "../hw/idt.h"
#include "../log.h"
#include "../mem/vmm.h"
#include "../net/socket.h"
#include "../mem/pmm.h"
#include <string.h>

#include <hw/rtc.h>

LOG_MODULE("syscall");


uint64_t syscall_handler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);





static int sys_open(const char* path, int flags) {
    if (!path) return -1;
    
    
    thread_t* thread = sched_get_current_thread();
    if (!thread) return -1;

    
    int fd = -1;
    for (int i = 0; i < MAX_FDS; i++) {
        if (thread->fds[i].node == NULL) {
            fd = i;
            break;
        }
    }

    if (fd == -1) {
        LOG_WARN("FD limit reached for process TID: %llu", thread->id);
        return -1; 
    }

    
    vfs_node_t* node = kopen(path);
    if (!node) {
        return -1; 
    }

    
    thread->fds[fd].node = node;
    thread->fds[fd].offset = 0;
    thread->fds[fd].flags = flags;

    return fd;
}

static int sys_read(int fd, void* buffer, size_t size) {
    if (!buffer || fd < 0 || fd >= MAX_FDS) return -1;

    thread_t* thread = sched_get_current_thread();
    if (!thread || !thread->fds[fd].node) return -1;

    file_descriptor_t* fdesc = &thread->fds[fd];

    uint32_t bytes_read = vfs_read(fdesc->node, fdesc->offset, size, buffer);
    fdesc->offset += bytes_read;

    return bytes_read;
}

static int sys_write(int fd, const void* buffer, size_t size) {
    if (!buffer || fd < 0 || fd >= MAX_FDS) return -1;

    thread_t* thread = sched_get_current_thread();
    if (!thread || !thread->fds[fd].node) return -1;

    file_descriptor_t* fdesc = &thread->fds[fd];

    uint32_t bytes_written = vfs_write(fdesc->node, fdesc->offset, size, (uint8_t*)buffer);
    fdesc->offset += bytes_written;

    return bytes_written;
}

int sys_stat(const char *restrict path, struct stat *restrict statbuf) {

}

int sys_fstat(int fd, struct stat *statbuf) {

}

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

static int sys_lseek(int fd, int offset, int whence) {
    if (fd < 0 || fd >= MAX_FDS) return -1;

    thread_t* thread = sched_get_current_thread();
    if (!thread || !thread->fds[fd].node) return -1;

    file_descriptor_t* fdesc = &thread->fds[fd];
    switch (whence)
    {
    case SEEK_SET:
        fdesc->offset = offset;
        break;
    case SEEK_CUR:
        fdesc->offset += offset;
        break;
    case SEEK_END:
        fdesc->offset = fdesc->node->size + offset;
        break;
    default:
        break;
    }
    return (int)fdesc->offset;
}

static int sys_close(int fd) {
    if (fd < 0 || fd >= MAX_FDS) return -1;

    thread_t* thread = sched_get_current_thread();
    if (!thread || !thread->fds[fd].node) return -1;

    file_descriptor_t* fdesc = &thread->fds[fd];
    
    kclose(fdesc->node);
    
    
    fdesc->node = NULL;
    fdesc->offset = 0;
    fdesc->flags = 0;

    return 0;
}

static int sys_readdir(int fd, struct dirent* dirent, uint32_t index) {
    if (fd < 0 || fd >= MAX_FDS || !dirent) return -1;

    thread_t* thread = sched_get_current_thread();
    if (!thread || !thread->fds[fd].node) return -1;

    file_descriptor_t* fdesc = &thread->fds[fd];
    struct dirent* res = vfs_readdir(fdesc->node, index);
    
    if (res) {
        memcpy(dirent, res, sizeof(struct dirent));
        return 1; 
    }
    
    return 0; 
}

static int sys_chdir(const char* path) {
    if (!path) return -1;
    vfs_node_t* node = kopen(path);
    if (!node) return -1;

    if (node->type != VFS_DIRECTORY && node->type != VFS_MOUNTPOINT) {
        kclose(node);
        return -1;
    }

    thread_t* thread = sched_get_current_thread();
    if (thread->cwd) {
        kclose(thread->cwd);
    }
    thread->cwd = node;
    return 0;
}

static int sys_ioctl(int fd, int request, void* arg) {
    if (fd < 0 || fd >= MAX_FDS) return -1;

    thread_t* thread = sched_get_current_thread();
    if (!thread || !thread->fds[fd].node) return -1;

    vfs_node_t* node = thread->fds[fd].node;
    if (node->ops && node->ops->ioctl) {
        return node->ops->ioctl(node, request, arg);
    }
    return -1;
}

extern time_t start_time;

static time_t sys_time(time_t* tloc) {
    time_t current_time = rtc_get_unix_time();
    if (tloc) {
        *tloc = current_time;
    }
    return current_time;
}

static uint64_t sys_brk(uint64_t addr) {
    thread_t* thread = sched_get_current_thread();
    if (!thread) return 0;

    
    if (addr == 0 || addr < thread->heap_start) {
        return thread->heap_end;
    }

    uint64_t current_aligned = (thread->heap_end + 0xFFF) & ~0xFFF;
    uint64_t new_aligned = (addr + 0xFFF) & ~0xFFF;

    if (new_aligned > current_aligned) {
        
        vmm_context_t ctx;
        ctx.pml4_phys = (pagemap_t*)(thread->cr3);
        ctx.pml4_virt = (pagemap_t*)(thread->cr3 + 0xFFFF800000000000); 

        for (uint64_t p = current_aligned; p < new_aligned; p += 0x1000) {
            uint64_t phys = (uint64_t)pmm_alloc(1);
            if (!phys) return thread->heap_end; 
            
            
            memset((void*)(phys + 0xFFFF800000000000), 0, 0x1000);

            
            vmm_map(&ctx, p, phys, VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
        }
    } 
    

    thread->heap_end = addr;
    return thread->heap_end;
}

static void* sys_mmap(void* addr, size_t length, int prot, int flags, int fd, int offset) {
    if (length == 0) return (void*)-1;
    length = (length + 0xFFF) & ~0xFFF;

    thread_t* thread = sched_get_current_thread();
    if (!thread) return (void*)-1;

    uint64_t vaddr = (uint64_t)addr;
    if (vaddr == 0) {
        if (thread->mmap_next == 0) thread->mmap_next = 0x8000000000;
        vaddr = thread->mmap_next;
        thread->mmap_next += length;
    } else {
        vaddr = (vaddr + 0xFFF) & ~0xFFF;
        
    }

    vmm_context_t ctx;
    ctx.pml4_phys = (pagemap_t*)(thread->cr3);
    ctx.pml4_virt = (pagemap_t*)(thread->cr3 + 0xFFFF800000000000); 

    for (uint64_t p = vaddr; p < vaddr + length; p += 0x1000) {
        
        if (!vmm_virt_to_phys(&ctx, p)) {
            uint64_t phys = (uint64_t)pmm_alloc(1);
            if (!phys) return (void*)-1; 
            memset((void*)(phys + 0xFFFF800000000000), 0, 0x1000);
            vmm_map(&ctx, p, phys, VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
        }
    }
    
    return (void*)vaddr;
}

static int sys_munmap(void* addr, size_t length) {
    if (!addr || length == 0) return -1;
    length = (length + 0xFFF) & ~0xFFF;
    
    thread_t* thread = sched_get_current_thread();
    if (!thread) return -1;

    vmm_context_t ctx;
    ctx.pml4_phys = (pagemap_t*)(thread->cr3);
    ctx.pml4_virt = (pagemap_t*)(thread->cr3 + 0xFFFF800000000000); 

    for (uint64_t p = (uint64_t)addr; p < (uint64_t)addr + length; p += 0x1000) {
        uint64_t phys = vmm_virt_to_phys(&ctx, p);
        if (phys) {
            pmm_free((void*)phys, 1);
            vmm_unmap(&ctx, p);
        }
    }
    
    
    asm volatile("mov %0, %%cr3" : : "r"(thread->cr3) : "memory");

    return 0;
}

static pid_t sys_getpid(void) {
    thread_t* thread = sched_get_current_thread();
    return thread ? thread->id : -1;
}

static uid_t sys_getuid(void) {
    thread_t* thread = sched_get_current_thread();
    return thread ? thread->cred.euid : -1;
}

static gid_t sys_getgid(void) {
    thread_t* thread = sched_get_current_thread();
    return thread ? thread->cred.egid : -1;
}

static void sys_setuid(uid_t uid) {
    thread_t* thread = sched_get_current_thread();
    if (thread) thread->cred.euid = uid;
}

static void sys_setgid(gid_t gid) {
    thread_t* thread = sched_get_current_thread();
    if (thread) thread->cred.egid = gid;
}


uint64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    
    switch (num) {
        case SYS_READ:
            return sys_read((int)arg1, (void*)arg2, (size_t)arg3);
        case SYS_WRITE:
            return sys_write((int)arg1, (const void*)arg2, (size_t)arg3);
        case SYS_OPEN:
            return sys_open((const char*)arg1, (int)arg2);
        case SYS_CLOSE:
            return sys_close((int)arg1);
        case SYS_FORK:
            return sched_fork();
        case SYS_EXECVE:
            return sys_execve((const char*)arg1);
        case SYS_EXIT:
            sched_exit((int)arg1);
            return 0; 
        case SYS_WAITPID:
            return sched_waitpid((uint64_t)arg1, (int*)arg2);
        case SYS_READDIR:
            return sys_readdir((int)arg1, (struct dirent*)arg2, (uint32_t)arg3);
        case SYS_CHDIR:
            return sys_chdir((const char*)arg1);
        case SYS_IOCTL:
            return sys_ioctl((int)arg1, (int)arg2, (void*)arg3);
        case SYS_BRK: 
            return sys_brk((uint64_t)arg1);
        case SYS_MMAP:
            return (uint64_t)sys_mmap((void*)arg1, (size_t)arg2, (int)arg3, (int)arg4, (int)arg5, 0);
        case SYS_MUNMAP:
            return sys_munmap((void*)arg1, (size_t)arg2);
        case SYS_KILL:
            return sched_kill((uint64_t)arg1, (int)arg2);
        case SYS_LSEEK:
            return sys_lseek((int)arg1, (int)arg2, (int)arg3);
        case SYS_GETPID:
            return sys_getpid();
        case SYS_GETUID:
            return sys_getuid();
        case SYS_GETGID:
            return sys_getgid();
        case SYS_SETUID:
            sys_setuid((uid_t)arg1);
            return 0;
        case SYS_SETGID:
            sys_setgid((gid_t)arg1);
            return 0;

        case SYS_SOCKET:
            return sys_socket((int)arg1, (int)arg2, (int)arg3);
        case SYS_BIND:
            return sys_bind((int)arg1, (const struct sockaddr_un*)arg2, (size_t)arg3);
        case SYS_LISTEN:
            return sys_listen((int)arg1, (int)arg2);
        case SYS_ACCEPT:
            return sys_accept((int)arg1, (struct sockaddr_un*)arg2, (size_t*)arg3);
        case SYS_CONNECT:
            return sys_connect((int)arg1, (const struct sockaddr_un*)arg2, (size_t)arg3);
        default:
            LOG_WARN("Unknown syscall %llu", num);
            return -1;
    }
}


__attribute__((naked)) void isr_syscall(void) {
    asm volatile(
        "push %rcx\n"
        "push %r11\n"
        "push %r8\n"
        "push %r9\n"
        "push %r10\n"

        
        
        
        

        "mov %r8, %r9\n"      
        "mov %r10, %r8\n"     
        "mov %rdx, %rcx\n"    
        "mov %rsi, %rdx\n"    
        "mov %rdi, %rsi\n"    
        "mov %rax, %rdi\n"    

        "call syscall_handler\n"
        
        "pop %r10\n"
        "pop %r9\n"
        "pop %r8\n"
        "pop %r11\n"
        "pop %rcx\n"
        "iretq\n"
    );
}

void syscall_init(void) {
    
    
    idt_set_descriptor(0x80, isr_syscall, 0xEE); 
    
    LOG_INFO("Syscalls initialized (Wired to INT 0x80, User Access Enabled)");
}
