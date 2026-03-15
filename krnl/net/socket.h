#pragma once

#include <stdint.h>
#include <stddef.h>

#define AF_UNIX 1
#define SOCK_STREAM 1

struct sockaddr_un {
    uint16_t sun_family;
    char sun_path[108];
};

struct sockaddr {
    uint16_t sa_family;
    char sa_data[14];
};

int sys_socket(int domain, int type, int protocol);
int sys_bind(int fd, const struct sockaddr_un* addr, size_t addrlen);
int sys_listen(int fd, int backlog);
int sys_accept(int fd, struct sockaddr_un* addr, size_t* addrlen);
int sys_connect(int fd, const struct sockaddr_un* addr, size_t addrlen);

void sockets_init(void);
