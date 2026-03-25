#pragma once
#include <stdint.h>
#include <stddef.h>

#include "api/syscall.h"

#include "cred.h"

#define CAP_KERNEL 0x8000000000000000
#define CAP_MASK_USER 0x7FFFFFFFFFFFFFFF

#define CAP_SETUID 0x1
#define CAP_SETGID 0x2
#define CAP_SETEUID 0x4
#define CAP_SETEGID 0x8
#define CAP_SETPCAP 0x10

#define CAP_NET_BIND_PORT 0x1000
#define CAP_NET_ADMIN 0x2000
#define CAP_SYS_ADMIN 0x4000
#define CAP_KILL 0x8000

#define CAP_CHECK(cap, flag) (((cap) & (flag)) == (flag))
#define CAP_KTHREAD (CAP_KERNEL | CAP_SETUID | CAP_SETGID | CAP_SETEUID | CAP_SETEGID | CAP_SETPCAP | CAP_SYS_ADMIN | CAP_NET_ADMIN | CAP_KILL)

#define UID_ROOT 0
#define GID_ROOT 0

typedef enum thread_state {
    THREAD_RUNNING = 0,
    THREAD_WAITING,
    THREAD_ZOMBIE,
    THREAD_DEAD
} thread_state_t;

typedef struct thread {
    uint64_t id;
    uint64_t parent_id;
    char name[256];
    uint64_t rsp;
    uint64_t cr3;
    uint64_t start_time;
    
    void* stack_bottom;
    size_t stack_size;
    
    uint64_t heap_start;
    uint64_t heap_end;

    uint64_t mmap_next;

    thread_state_t state;
    int exit_code;
    
    struct vfs_node* cwd;
    file_descriptor_t fds[MAX_FDS];
    cred_t cred;

    int64_t cap;

    struct thread* next;
} thread_t;

void sched_init(void);
thread_t* sched_add_thread(const char* name, void (*entry_point)(void), void* stack_bottom, size_t stack_size, uint64_t cr3);
int64_t sched_kthread(const char* name, void (*entry_point)(void), int64_t cap, uid_t uid, gid_t gid);
uint64_t sched_fork(void);
void sched_exit(int status);
int sched_kill(uint64_t pid, int sig);
int sched_waitpid(uint64_t pid, int* status);
thread_t* sched_get_current_thread(void);

void isr_lapic_timer(void); 

void sched_yield(void);
void sched_print_threads(void);

static inline void set_capabilities(int64_t cap, int set) {
    thread_t* t = sched_get_current_thread();
    if (t) {
        if (set) {
            t->cap |= cap;
        } else {
            t->cap &= ~cap;
        }
    }
}

static inline void setuid(uid_t uid) {
    thread_t* t = sched_get_current_thread();
    if (t && CAP_CHECK(t->cap, CAP_SETUID)) t->cred.uid = uid;
}

static inline void setgid(gid_t gid) {
    thread_t* t = sched_get_current_thread();
    if (t && CAP_CHECK(t->cap, CAP_SETGID)) t->cred.gid = gid;
}

static inline void seteuid(uid_t euid) {
    thread_t* t = sched_get_current_thread();
    if (t && CAP_CHECK(t->cap, CAP_SETEUID)) t->cred.euid = euid;
}

static inline void setegid(gid_t egid) {
    thread_t* t = sched_get_current_thread();
    if (t && CAP_CHECK(t->cap, CAP_SETEGID)) t->cred.egid = egid;
}

static inline void kill(uint64_t pid) {
    thread_t* t = sched_get_current_thread();
    if (t && (t->cred.uid == UID_ROOT || t->cred.euid == UID_ROOT || CAP_CHECK(t->cap, CAP_KILL) || CAP_CHECK(t->cap, CAP_KERNEL))) {
        sched_kill(pid, 9); 
    }
}
