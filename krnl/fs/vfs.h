#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <api/sysdef.h>

typedef struct vfs_node vfs_node_t;
typedef struct vfs_operations vfs_operations_t;
typedef struct dirent dirent_t;

enum vfs_node_type {
    VFS_UNKNOWN = 0,
    VFS_FILE = 1,
    VFS_DIRECTORY = 2,
    VFS_CHAR_DEVICE = 3,
    VFS_BLOCK_DEVICE = 4,
    VFS_PIPE = 5,
    VFS_SYMLINK = 6,
    VFS_MOUNTPOINT = 8
};

struct dirent {
    char name[256];
    uint32_t ino;
};


struct vfs_operations {
    uint32_t (*read)(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
    uint32_t (*write)(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
    void (*open)(vfs_node_t* node);
    void (*close)(vfs_node_t* node);
    struct dirent* (*readdir)(vfs_node_t* node, uint32_t index);
    vfs_node_t* (*finddir)(vfs_node_t* node, char* name);
    int (*create)(vfs_node_t* node, char* name, uint16_t permission);
    int (*mkdir)(vfs_node_t* node, char* name, uint16_t permission);
    int (*rmdir)(vfs_node_t* node, char* name);
    int (*unlink)(vfs_node_t* node, char* name);
    int (*symlink)(vfs_node_t* node, char* name, char* target);
    int (*readlink)(vfs_node_t* node, char* buffer, size_t size);
    int (*rename)(vfs_node_t* node, char* old_name, char* new_name);
    int (*chmod)(vfs_node_t* node, uint16_t permission);
    int (*chown)(vfs_node_t* node, uint32_t uid, uint32_t gid);
    int (*stat)(vfs_node_t* node, struct stat* stat_buf);
    int (*ioctl)(vfs_node_t* node, int request, void* arg);
};

struct vfs_node {
    char name[256];
    uint32_t inode;
    uint32_t mask;
    uint32_t uid;
    uint32_t gid;
    uint32_t flags;
    uint32_t size;
    enum vfs_node_type type;

    vfs_operations_t* ops;

    
    void* device;

    blksize_t blksize;
	blkcnt_t blocks;

    time_t atime;
	time_t mtime;
	time_t ctime;
};


typedef struct vfs_filesystem {
    char name[32];
    uint32_t flags;
    vfs_node_t* (*mount)(const char* source, const char* target, void* data);
    struct vfs_filesystem* next;
} vfs_filesystem_t;

void vfs_register_fs(const char* fsname, uint32_t flags, vfs_node_t* (*mount_func)(const char* source, const char* target, void* data));
int vfs_mount(const char* source, const char* target, const char* fsname, void* data);

void vfs_init(void);
vfs_node_t* vfs_alloc_node(void);
void vfs_free_node(vfs_node_t* node);

vfs_node_t* vfs_get_root(void);
void vfs_set_root(vfs_node_t* root);

char* vfs_resolve_path(const char* pwd, const char* path);


uint32_t vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
uint32_t vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
void vfs_open(vfs_node_t* node);
void vfs_close(vfs_node_t* node);
struct dirent* vfs_readdir(vfs_node_t* node, uint32_t index);
vfs_node_t* vfs_finddir(vfs_node_t* node, char* name);

vfs_node_t* kopen(const char* path);
void kstat(vfs_node_t* node, struct stat* stat_buf);
void kclose(vfs_node_t* node);