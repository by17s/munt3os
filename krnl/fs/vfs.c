#include "vfs.h"
#include <stddef.h>
#include <stdint.h>
#include "mem/kslab.h"
#include "mem/kheap.h"
#include "log.h"
#include "cstdlib.h"

LOG_MODULE("vfs")

static kmem_cache_t* vfs_node_cache;
static vfs_node_t* fs_root = NULL;
static vfs_filesystem_t* filesystems = NULL;

void vfs_register_fs(const char* fsname, uint32_t flags, vfs_node_t* (*mount_func)(const char*, const char*, void*)) {
    vfs_filesystem_t* fs = (vfs_filesystem_t*)khmalloc(sizeof(vfs_filesystem_t));
    if (!fs) return;

    memset(fs, 0, sizeof(vfs_filesystem_t));
    strncpy(fs->name, fsname, sizeof(fs->name) - 1);
    fs->flags = flags;
    fs->mount = mount_func;

    fs->next = filesystems;
    filesystems = fs;

    LOG_INFO("VFS: Registered filesystem '%s'", fsname);
}

int vfs_mount(const char* source, const char* target, const char* fsname, void* data) {
    vfs_filesystem_t* fs = filesystems;
    while (fs != NULL) {
        if (strcmp(fs->name, fsname) == 0) {
            vfs_node_t* node = fs->mount(source, target, data);
            if (!node) {
                LOG_ERROR("VFS: Failed to mount '%s' at '%s' (type: %s)", source ? source : "none", target, fsname);
                return -1;
            }

            if (strcmp(target, "/") == 0) {
                fs_root = node;
                LOG_INFO("VFS: Mounted '%s' as root '/'", fsname);
                return 0;
            } else {
                if (!fs_root) {
                    LOG_ERROR("VFS: Cannot mount at '%s' without root filesystem!", target);
                    return -1;
                }
                char* target_path = vfs_resolve_path("/", target);
                vfs_node_t* curr = fs_root;
                char* p = target_path;
                if (*p == '/') p++;
                while (*p) {
                    char* slash = NULL;
                    for (int i = 0; p[i]; i++) {
                        if (p[i] == '/') {
                            slash = &p[i];
                            break;
                        }
                    }
                    if (slash) *slash = '\0';
                    curr = vfs_finddir(curr, p);
                    if (!curr) break;
                    if (slash) p = slash + 1;
                    else break;
                }
                
                if (curr) {
                    curr->ops = node->ops;
                    curr->device = node->device;
                    curr->type = node->type;
                    curr->size = node->size;
                    curr->inode = node->inode;
                    LOG_INFO("VFS: Mounted '%s' at '%s'", fsname, target);
                } else {
                    LOG_ERROR("VFS: Mount target '%s' not found", target);
                }
                khfree(target_path);
                return curr ? 0 : -1;
            }
            return 0;
        }
        fs = fs->next;
    }
    LOG_ERROR("VFS: Unknown filesystem type '%s'", fsname);
    return -1;
}

void vfs_init(void) {
    LOG_INFO("VFS: Initializing virtual file system");
    vfs_node_cache = kmem_cache_create("vfs_node_cache", sizeof(vfs_node_t));
    if (!vfs_node_cache) {
        LOG_ERROR("VFS: Failed to create vfs_node cache");
    }
}

vfs_node_t* vfs_alloc_node(void) {
    if (!vfs_node_cache) return NULL;
    vfs_node_t* node = (vfs_node_t*)kmem_cache_alloc(vfs_node_cache);
    if (node) {
        memset(node, 0, sizeof(vfs_node_t));
    } else {
        LOG_ERROR("VFS: Failed to allocate vfs_node from cache");
    }
    return node;
}

void vfs_free_node(vfs_node_t* node) {
    if (vfs_node_cache && node) {
        kmem_cache_free(vfs_node_cache, node);
    }
}

vfs_node_t* vfs_get_root(void) {
    return fs_root;
}

void vfs_set_root(vfs_node_t* root) {
    fs_root = root;
}

char* vfs_resolve_path(const char* pwd, const char* path) {
    if (!path) return NULL;

    char* resolved = (char*)khmalloc(1024);
    char* normalized = (char*)khmalloc(1024);
    char* temp = (char*)khmalloc(1024);
    char** tokens = (char**)khmalloc(128 * sizeof(char*));

    if (!resolved || !normalized || !temp || !tokens) {
        if (resolved) khfree(resolved);
        if (normalized) khfree(normalized);
        if (temp) khfree(temp);
        if (tokens) khfree(tokens);
        return NULL;
    }

    memset(resolved, 0, 1024);
    memset(normalized, 0, 1024);
    memset(temp, 0, 1024);

    if (path[0] == '/') {
        strncpy(resolved, path, 1023);
    } else {
        if (pwd) {
            strncpy(resolved, pwd, 1023);
        } else {
            resolved[0] = '/';
            resolved[1] = '\0';
        }

        
        size_t len = strlen(resolved);
        if (len > 0 && resolved[len - 1] != '/') {
            if (len < 1023) {
                resolved[len] = '/';
                resolved[len + 1] = '\0';
            }
        }
        strncat(resolved, path, 1023 - strlen(resolved));
    }

    int token_count = 0;

    
    strncpy(temp, resolved, 1023);

    char* p = temp;
    char* token = NULL;
    
    
    while (*p) {
        while (*p == '/') p++; 
        if (!*p) break;
        
        token = p;
        while (*p && *p != '/') p++;
        if (*p) {
            *p = '\0';
            p++;
        }
        
        if (strcmp(token, ".") == 0) {
            continue; 
        } else if (strcmp(token, "..") == 0) {
            if (token_count > 0) token_count--; 
        } else {
            if (token_count < 128) {
                tokens[token_count++] = token;
            }
        }
    }

    
    normalized[0] = '/';
    normalized[1] = '\0';
    for (int i = 0; i < token_count; i++) {
        strncat(normalized, tokens[i], 1023 - strlen(normalized));
        if (i < token_count - 1) {
            strncat(normalized, "/", 1023 - strlen(normalized));
        }
    }

    
    if (strlen(normalized) == 0) {
        normalized[0] = '/';
        normalized[1] = '\0';
    }

    char* final_path = (char*)khmalloc(strlen(normalized) + 1);
    if (final_path) {
        strcpy(final_path, normalized);
    }
    
    khfree(resolved);
    khfree(normalized);
    khfree(temp);
    khfree(tokens);
    
    return final_path;
}

uint32_t vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (node && node->ops && node->ops->read) {
        return node->ops->read(node, offset, size, buffer);
    }
    return 0;
}

uint32_t vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (node && node->ops && node->ops->write) {
        return node->ops->write(node, offset, size, buffer);
    }
    return 0;
}

void vfs_open(vfs_node_t* node) {
    if (node && node->ops && node->ops->open) {
        node->ops->open(node);
    }
}

void vfs_close(vfs_node_t* node) {
    if (node && node->ops && node->ops->close) {
        node->ops->close(node);
    }
}

struct dirent* vfs_readdir(vfs_node_t* node, uint32_t index) {
    if (node && node->ops && node->ops->readdir) {
        return node->ops->readdir(node, index);
    }
    return NULL;
}

vfs_node_t* vfs_finddir(vfs_node_t* node, char* name) {
    if (node && node->ops && node->ops->finddir) {
        return node->ops->finddir(node, name);
    }
    return NULL;
}

vfs_node_t* kopen(const char* path) {
    if (!path) return NULL;
    
    char* resolved = vfs_resolve_path("/", path);
    if (!resolved) return NULL;

    vfs_node_t* curr = fs_root;
    char* p = resolved;
    if (*p == '/') p++;
    
    while (*p && curr) {
        char* slash = NULL;
        for (int i = 0; p[i]; i++) {
            if (p[i] == '/') {
                slash = &p[i];
                break;
            }
        }
        if (slash) *slash = '\0';
        
        curr = vfs_finddir(curr, p);
        
        if (slash) p = slash + 1;
        else break;
    }
    
    khfree(resolved);
    return curr;
}

void kstat(vfs_node_t* node, struct stat* stat_buf) {
    if (node && node->ops) {
        if(node->ops->stat)
            node->ops->stat(node, stat_buf);
        else {
            stat_buf->st_ino = node->inode;
            stat_buf->st_mode = node->mask;
            stat_buf->st_uid = node->uid;
            stat_buf->st_gid = node->gid;
            stat_buf->st_size = node->size;
            stat_buf->st_blocks = node->blocks;
            stat_buf->st_blksize = node->blksize;

            stat_buf->st_atime = node->atime;
            stat_buf->st_mtime = node->mtime;
            stat_buf->st_ctime = node->ctime;
        }
    }
}

void kclose(vfs_node_t* node) {
    vfs_close(node);
}
