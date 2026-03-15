#include "socket.h"
#include "../fs/vfs.h"
#include "../util/ring_buffer.h"
#include "../mm.h"
#include "../task/sched.h"
#include "../log.h"
#include "../cstdlib.h"

#define MAX_SOCKETS 64

typedef enum {
    SOCKET_STATE_UNBOUND,
    SOCKET_STATE_BOUND,
    SOCKET_STATE_LISTENING,
    SOCKET_STATE_CONNECTED,
    SOCKET_STATE_CLOSED
} socket_state_t;

typedef struct unix_socket {
    char path[108];
    socket_state_t state;
    ring_buffer_t rx_buffer;
    
    struct unix_socket* peer;
    
    struct unix_socket* accept_queue[16];
    int accept_head;
    int accept_tail;
    
    vfs_node_t* node;
} unix_socket_t;

static unix_socket_t* bound_sockets[MAX_SOCKETS];

static uint32_t socket_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    unix_socket_t* sock = (unix_socket_t*)node->device;
    if (!sock || sock->state != SOCKET_STATE_CONNECTED) return 0;
    
    size_t avail;
    while ((avail = ring_buffer_available(&sock->rx_buffer)) == 0) {
        if (!sock->peer || sock->peer->state == SOCKET_STATE_CLOSED) return 0;
        sched_yield(); 
    }
    
    size_t to_read = size < avail ? size : avail;
    return ring_buffer_read(&sock->rx_buffer, buffer, to_read);
}

static uint32_t socket_write(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    unix_socket_t* sock = (unix_socket_t*)node->device;
    if (!sock || sock->state != SOCKET_STATE_CONNECTED || !sock->peer) return 0;
    
    size_t written = 0;
    while (written < size) {
        if (sock->peer->state == SOCKET_STATE_CLOSED) break;
        
        size_t free_sp = ring_buffer_free_space(&sock->peer->rx_buffer);
        if (free_sp > 0) {
            size_t w = (size - written) < free_sp ? (size - written) : free_sp;
            w = ring_buffer_write(&sock->peer->rx_buffer, buffer + written, w);
            written += w;
        } else {
            sched_yield();
        }
    }
    return written;
}

static void socket_close(vfs_node_t* node) {
    unix_socket_t* sock = (unix_socket_t*)node->device;
    if (!sock) return;
    
    sock->state = SOCKET_STATE_CLOSED;
    if (sock->peer) {
        sock->peer->peer = NULL;
    }
    kfree(sock->rx_buffer.buffer);
    for (int i=0; i<MAX_SOCKETS; i++) {
        if (bound_sockets[i] == sock) bound_sockets[i] = NULL;
    }
    kfree(sock);
}

static vfs_operations_t socket_ops = {
    .read = socket_read,
    .write = socket_write,
    .open = NULL,
    .close = socket_close,
};

static unix_socket_t* create_socket_obj(void) {
    unix_socket_t* sock = kmalloc(sizeof(unix_socket_t));
    if (!sock) return NULL;
    memset(sock, 0, sizeof(unix_socket_t));
    ring_buffer_init(&sock->rx_buffer, 4096);
    sock->state = SOCKET_STATE_UNBOUND;
    
    sock->node = vfs_alloc_node();
    if (!sock->node) {
        kfree(sock->rx_buffer.buffer);
        kfree(sock);
        return NULL;
    }
    sock->node->type = VFS_PIPE;
    sock->node->ops = &socket_ops;
    sock->node->device = sock;
    return sock;
}

int sys_socket(int domain, int type, int protocol) {
    if (domain != AF_UNIX || type != SOCK_STREAM) return -1;
    
    thread_t* thread = sched_get_current_thread();
    if (!thread) return -1;
    
    int fd = -1;
    for (int i = 0; i < MAX_FDS; i++) {
        if (thread->fds[i].node == NULL) {
            fd = i;
            break;
        }
    }
    if (fd == -1) return -1;
    
    unix_socket_t* sock = create_socket_obj();
    if (!sock) return -1;
    
    thread->fds[fd].node = sock->node;
    thread->fds[fd].offset = 0;
    thread->fds[fd].flags = 0;
    
    return fd;
}

int sys_bind(int fd, const struct sockaddr_un* addr, size_t addrlen) {
    if (fd < 0 || fd >= MAX_FDS) return -1;
    thread_t* thread = sched_get_current_thread();
    if (!thread || !thread->fds[fd].node) return -1;
    
    unix_socket_t* sock = (unix_socket_t*)thread->fds[fd].node->device;
    if (!sock || sock->state != SOCKET_STATE_UNBOUND) return -1;
    
    for (int i=0; i<MAX_SOCKETS; i++) {
        if (bound_sockets[i] && strcmp(bound_sockets[i]->path, addr->sun_path) == 0) {
            return -1;
        }
    }
    
    for (int i=0; i<MAX_SOCKETS; i++) {
        if (!bound_sockets[i]) {
            bound_sockets[i] = sock;
            strcpy(sock->path, addr->sun_path);
            sock->state = SOCKET_STATE_BOUND;
            return 0;
        }
    }
    
    return -1;
}

int sys_listen(int fd, int backlog) {
    if (fd < 0 || fd >= MAX_FDS) return -1;
    thread_t* thread = sched_get_current_thread();
    if (!thread || !thread->fds[fd].node) return -1;
    
    unix_socket_t* sock = (unix_socket_t*)thread->fds[fd].node->device;
    if (!sock || sock->state != SOCKET_STATE_BOUND) return -1;
    
    sock->state = SOCKET_STATE_LISTENING;
    return 0;
}

int sys_connect(int fd, const struct sockaddr_un* addr, size_t addrlen) {
    if (fd < 0 || fd >= MAX_FDS) return -1;
    thread_t* thread = sched_get_current_thread();
    if (!thread || !thread->fds[fd].node) return -1;
    
    unix_socket_t* sock = (unix_socket_t*)thread->fds[fd].node->device;
    if (!sock || sock->state != SOCKET_STATE_UNBOUND) return -1;
    
    unix_socket_t* target = NULL;
    for (int i=0; i<MAX_SOCKETS; i++) {
        if (bound_sockets[i] && strcmp(bound_sockets[i]->path, addr->sun_path) == 0) {
            target = bound_sockets[i];
            break;
        }
    }
    
    if (!target || target->state != SOCKET_STATE_LISTENING) return -1;
    
    int next_tail = (target->accept_tail + 1) % 16;
    if (next_tail == target->accept_head) return -1;
    
    target->accept_queue[target->accept_tail] = sock;
    target->accept_tail = next_tail;
    
    while (sock->state == SOCKET_STATE_UNBOUND) {
        sched_yield();
    }
    
    return 0;
}

int sys_accept(int fd, struct sockaddr_un* addr, size_t* addrlen) {
    if (fd < 0 || fd >= MAX_FDS) return -1;
    thread_t* thread = sched_get_current_thread();
    if (!thread || !thread->fds[fd].node) return -1;
    
    unix_socket_t* server_sock = (unix_socket_t*)thread->fds[fd].node->device;
    if (!server_sock || server_sock->state != SOCKET_STATE_LISTENING) return -1;
    
    while (server_sock->accept_head == server_sock->accept_tail) {
        sched_yield();
    }
    
    unix_socket_t* client_sock = server_sock->accept_queue[server_sock->accept_head];
    server_sock->accept_head = (server_sock->accept_head + 1) % 16;
    
    unix_socket_t* accepted_sock = create_socket_obj();
    if (!accepted_sock) return -1;
    
    int new_fd = -1;
    for (int i = 0; i < MAX_FDS; i++) {
        if (thread->fds[i].node == NULL) {
            new_fd = i;
            break;
        }
    }
    if (new_fd == -1) {
        kfree(accepted_sock->rx_buffer.buffer);
        kfree(accepted_sock->node);
        kfree(accepted_sock);
        return -1;
    }
    
    thread->fds[new_fd].node = accepted_sock->node;
    thread->fds[new_fd].offset = 0;
    thread->fds[new_fd].flags = 0;
    
    accepted_sock->state = SOCKET_STATE_CONNECTED;
    client_sock->state = SOCKET_STATE_CONNECTED;
    
    accepted_sock->peer = client_sock;
    client_sock->peer = accepted_sock;
    
    if (addr && addrlen) {
        strcpy(addr->sun_path, server_sock->path);
        addr->sun_family = AF_UNIX;
        *addrlen = sizeof(struct sockaddr_un);
    }
    
    return new_fd;
}
