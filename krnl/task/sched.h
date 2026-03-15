#pragma once
#include <stdint.h>
#include <stddef.h>

#include "../api/syscall.h"

typedef enum thread_state {
    THREAD_RUNNING = 0,
    THREAD_WAITING,
    THREAD_ZOMBIE,
    THREAD_DEAD
} thread_state_t;

typedef struct thread {
    uint64_t id;
    uint64_t parent_id;
    char name[32];
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

    struct thread* next;
} thread_t;

void sched_init(void);
void sched_add_thread(const char* name, void (*entry_point)(void), void* stack_bottom, size_t stack_size, uint64_t cr3);
uint64_t sched_fork(void);
void sched_exit(int status);
int sched_kill(uint64_t pid, int sig);
int sched_waitpid(uint64_t pid, int* status);
thread_t* sched_get_current_thread(void);

void isr_lapic_timer(void); 

void sched_yield(void);
void sched_print_threads(void);
