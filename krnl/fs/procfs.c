










#include "fs/procfs.h"
#include "fs/vfs.h"
#include "mem/kheap.h"
#include "mem/pmm.h"
#include "cstdlib.h"
#include "log.h"

#include "hw/rtc.h"

#include "os_build.h"

LOG_MODULE("procfs")




typedef struct procfs_entry {
    struct procfs_entry* next;
    vfs_node_t* node;
} procfs_entry_t;


typedef struct {
    vfs_node_t* node;
    procfs_entry_t* children;
    uint32_t child_count;
} procfs_dir_data_t;


typedef struct {
    uint8_t* data;
    size_t   size;
    size_t   capacity;
} procfs_file_data_t;



static vfs_node_t* procfs_root_node = NULL;
static uint32_t procfs_inode_counter = 5000;


static vfs_operations_t procfs_dir_ops;
static vfs_operations_t procfs_file_ops;

extern time_t start_time;



static vfs_node_t* procfs_dir_finddir(vfs_node_t* node, char* name) {
    if (!node || node->type != VFS_DIRECTORY || !node->device) return NULL;
    procfs_dir_data_t* dir = (procfs_dir_data_t*)node->device;
    procfs_entry_t* entry = dir->children;
    while (entry) {
        if (strcmp(entry->node->name, name) == 0)
            return entry->node;
        entry = entry->next;
    }
    return NULL;
}

static struct dirent* procfs_dir_readdir(vfs_node_t* node, uint32_t index) {
    if (!node || node->type != VFS_DIRECTORY || !node->device) return NULL;
    procfs_dir_data_t* dir = (procfs_dir_data_t*)node->device;
    procfs_entry_t* entry = dir->children;
    uint32_t i = 0;
    while (entry && i < index) {
        entry = entry->next;
        i++;
    }
    if (!entry) return NULL;

    static struct dirent d;
    memset(&d, 0, sizeof(d));
    strncpy(d.name, entry->node->name, sizeof(d.name) - 1);
    d.ino = entry->node->inode;
    return &d;
}

static int procfs_dir_stat(vfs_node_t* node, struct stat* st) {
    if (!node || !st) return -1;
    memset(st, 0, sizeof(struct stat));
    st->st_ino = node->inode;
    st->st_mode = 0555 | 0040000; 
    st->st_size = 0;
    return 0;
}



static uint32_t procfs_file_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !node->device || !buffer) return 0;
    procfs_file_data_t* fdata = (procfs_file_data_t*)node->device;

    if (offset >= fdata->size) return 0;
    uint32_t remaining = fdata->size - offset;
    uint32_t to_read = (size < remaining) ? size : remaining;
    memcpy(buffer, fdata->data + offset, to_read);
    return to_read;
}

static uint32_t procfs_file_read_uptime(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !node->device || !buffer) return 0;
    procfs_file_data_t* fdata = (procfs_file_data_t*)node->device;

    snprintf((char*)fdata->data, fdata->size, "%llu", rtc_get_unix_time() - start_time);
    return procfs_file_read(node, offset, size, buffer);
}

static uint32_t procfs_file_read_mem(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !node->device || !buffer) return 0;
    procfs_file_data_t* fdata = (procfs_file_data_t*)node->device;

    struct pmm_stat stat;
    pmm_stat(&stat);
    snprintf((char*)fdata->data, 
        fdata->size, "%llu MB / %llu MB", 
        (stat.usable_pages - stat.used_pages) * PAGE_SIZE / (1024 * 1024), 
        stat.usable_pages * PAGE_SIZE / (1024 * 1024)
    );
    return procfs_file_read(node, offset, size, buffer);
}

static uint32_t procfs_file_write(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !node->device || !buffer) return 0;
    procfs_file_data_t* fdata = (procfs_file_data_t*)node->device;

    
    size_t needed = offset + size;
    if (needed > fdata->capacity) {
        size_t new_cap = needed * 2;
        uint8_t* new_buf = (uint8_t*)khmalloc(new_cap);
        if (!new_buf) return 0;
        if (fdata->data) {
            memcpy(new_buf, fdata->data, fdata->size);
            khfree(fdata->data);
        }
        fdata->data = new_buf;
        fdata->capacity = new_cap;
    }

    memcpy(fdata->data + offset, buffer, size);
    if (offset + size > fdata->size)
        fdata->size = offset + size;
    node->size = fdata->size;
    return size;
}

static int procfs_file_stat(vfs_node_t* node, struct stat* st) {
    if (!node || !st) return -1;
    memset(st, 0, sizeof(struct stat));
    st->st_ino = node->inode;
    st->st_mode = 0444 | 0100000; 
    st->st_size = node->size;
    return 0;
}



static vfs_operations_t procfs_dir_ops = {
    .read     = NULL,
    .write    = NULL,
    .open     = NULL,
    .close    = NULL,
    .readdir  = procfs_dir_readdir,
    .finddir  = procfs_dir_finddir,
    .create   = NULL,
    .mkdir    = NULL,
    .rmdir    = NULL,
    .unlink   = NULL,
    .stat     = procfs_dir_stat,
    .ioctl    = NULL,
};

static vfs_operations_t procfs_file_ops = {
    .read     = procfs_file_read,
    .write    = NULL,
    .open     = NULL,
    .close    = NULL,
    .readdir  = NULL,
    .finddir  = NULL,
    .create   = NULL,
    .mkdir    = NULL,
    .rmdir    = NULL,
    .unlink   = NULL,
    .stat     = procfs_file_stat,
    .ioctl    = NULL,
};

static vfs_operations_t procfs_file_ops_uptime = {
    .read     = procfs_file_read_uptime,
    .write    = NULL,
    .open     = NULL,
    .close    = NULL,
    .readdir  = NULL,
    .finddir  = NULL,
    .create   = NULL,
    .mkdir    = NULL,
    .rmdir    = NULL,
    .unlink   = NULL,
    .stat     = procfs_file_stat,
    .ioctl    = NULL,
};

static vfs_operations_t procfs_file_ops_mem = {
    .read     = procfs_file_read_mem,
    .write    = NULL,
    .open     = NULL,
    .close    = NULL,
    .readdir  = NULL,
    .finddir  = NULL,
    .create   = NULL,
    .mkdir    = NULL,
    .rmdir    = NULL,
    .unlink   = NULL,
    .stat     = procfs_file_stat,
    .ioctl    = NULL,
};




static vfs_node_t* procfs_alloc_dir(const char* name) {
    vfs_node_t* node = vfs_alloc_node();
    if (!node) return NULL;

    strncpy(node->name, name, sizeof(node->name) - 1);
    node->inode = procfs_inode_counter++;
    node->type = VFS_DIRECTORY;
    node->flags = VFS_DIRECTORY;
    node->ops = &procfs_dir_ops;

    procfs_dir_data_t* ddata = (procfs_dir_data_t*)khmalloc(sizeof(procfs_dir_data_t));
    if (!ddata) {
        vfs_free_node(node);
        return NULL;
    }
    memset(ddata, 0, sizeof(procfs_dir_data_t));
    ddata->node = node;
    ddata->children = NULL;
    ddata->child_count = 0;
    node->device = ddata;

    return node;
}


static vfs_node_t* procfs_alloc_file(const char* name, const char* buf, size_t buf_size) {
    vfs_node_t* node = vfs_alloc_node();
    if (!node) return NULL;

    strncpy(node->name, name, sizeof(node->name) - 1);
    node->inode = procfs_inode_counter++;
    node->type = VFS_FILE;
    node->flags = VFS_FILE;
    node->size = buf_size;
    node->ops = &procfs_file_ops;

    procfs_file_data_t* fdata = (procfs_file_data_t*)khmalloc(sizeof(procfs_file_data_t));
    if (!fdata) {
        vfs_free_node(node);
        return NULL;
    }
    memset(fdata, 0, sizeof(procfs_file_data_t));

    if (buf && buf_size > 0) {
        size_t cap = buf_size < 64 ? 64 : buf_size;
        fdata->data = (uint8_t*)khmalloc(cap);
        if (!fdata->data) {
            khfree(fdata);
            vfs_free_node(node);
            return NULL;
        }
        memcpy(fdata->data, buf, buf_size);
        fdata->size = buf_size;
        fdata->capacity = cap;
    }

    node->device = fdata;
    return node;
}


static int procfs_add_child(vfs_node_t* parent, vfs_node_t* child) {
    if (!parent || !child || parent->type != VFS_DIRECTORY || !parent->device) return -1;
    procfs_dir_data_t* dir = (procfs_dir_data_t*)parent->device;

    
    procfs_entry_t* check = dir->children;
    while (check) {
        if (strcmp(check->node->name, child->name) == 0) {
            LOG_WARN("Entry '%s' already exists in '%s'", child->name, parent->name);
            return -1;
        }
        check = check->next;
    }

    procfs_entry_t* entry = (procfs_entry_t*)khmalloc(sizeof(procfs_entry_t));
    if (!entry) return -1;
    entry->node = child;
    entry->next = dir->children;
    dir->children = entry;
    dir->child_count++;
    return 0;
}


static vfs_node_t* procfs_resolve_path(const char* path) {
    if (!procfs_root_node) return NULL;
    if (!path || path[0] == '\0' || (path[0] == '/' && path[1] == '\0'))
        return procfs_root_node;

    char buf[256];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    vfs_node_t* current = procfs_root_node;
    char* token = strtok(buf, "/");
    while (token) {
        if (current->type != VFS_DIRECTORY || !current->ops || !current->ops->finddir) return NULL;
        vfs_node_t* next = current->ops->finddir(current, token);
        if (!next) return NULL;
        current = next;
        token = strtok(NULL, "/");
    }
    return current;
}



vfs_node_t* procfs_create_dir(const char* path, const char* name) {
    if (!name) return NULL;

    vfs_node_t* parent = procfs_resolve_path(path);
    if (!parent || parent->type != VFS_DIRECTORY) {
        LOG_ERROR("Cannot create dir '%s': parent '%s' not found", name, path ? path : "/");
        return NULL;
    }

    vfs_node_t* dir_node = procfs_alloc_dir(name);
    if (!dir_node) return NULL;

    if (procfs_add_child(parent, dir_node) != 0) {
        
        if (dir_node->device) khfree(dir_node->device);
        vfs_free_node(dir_node);
        return NULL;
    }

    LOG_INFO("Created dir '/proc/%s%s%s'", path ? path : "", (path && path[0]) ? "/" : "", name);
    return dir_node;
}

vfs_node_t* procfs_create_file(const char* path, const char* name, const char* buf, size_t buf_size) {
    if (!name) return NULL;

    vfs_node_t* parent = procfs_resolve_path(path);
    if (!parent || parent->type != VFS_DIRECTORY) {
        LOG_ERROR("Cannot create file '%s': parent '%s' not found", name, path ? path : "/");
        return NULL;
    }

    vfs_node_t* file_node = procfs_alloc_file(name, buf, buf_size);
    if (!file_node) return NULL;

    if (procfs_add_child(parent, file_node) != 0) {
        if (file_node->device) {
            procfs_file_data_t* fdata = (procfs_file_data_t*)file_node->device;
            if (fdata->data) khfree(fdata->data);
            khfree(fdata);
        }
        vfs_free_node(file_node);
        return NULL;
    }

    
    return file_node;
}

int procfs_update_file(vfs_node_t* node, const char* buf, size_t buf_size) {
    if (!node || node->type != VFS_FILE || !node->device) return -1;

    procfs_file_data_t* fdata = (procfs_file_data_t*)node->device;

    
    if (buf_size > fdata->capacity) {
        uint8_t* new_buf = (uint8_t*)khmalloc(buf_size);
        if (!new_buf) return -1;
        if (fdata->data) khfree(fdata->data);
        fdata->data = new_buf;
        fdata->capacity = buf_size;
    }

    if (buf && buf_size > 0) {
        memcpy(fdata->data, buf, buf_size);
    }
    fdata->size = buf_size;
    node->size = buf_size;
    return 0;
}

int procfs_remove(const char* path, const char* name) {
    if (!name) return -1;

    vfs_node_t* parent = procfs_resolve_path(path);
    if (!parent || parent->type != VFS_DIRECTORY || !parent->device) return -1;

    procfs_dir_data_t* dir = (procfs_dir_data_t*)parent->device;
    procfs_entry_t* prev = NULL;
    procfs_entry_t* entry = dir->children;

    while (entry) {
        if (strcmp(entry->node->name, name) == 0) {
            
            if (entry->node->type == VFS_DIRECTORY && entry->node->device) {
                procfs_dir_data_t* child_dir = (procfs_dir_data_t*)entry->node->device;
                if (child_dir->child_count > 0) {
                    LOG_WARN("Cannot remove non-empty dir '%s'", name);
                    return -1;
                }
                khfree(child_dir);
            }

            
            if (entry->node->type == VFS_FILE && entry->node->device) {
                procfs_file_data_t* fdata = (procfs_file_data_t*)entry->node->device;
                if (fdata->data) khfree(fdata->data);
                khfree(fdata);
            }

            
            if (prev)
                prev->next = entry->next;
            else
                dir->children = entry->next;

            vfs_free_node(entry->node);
            khfree(entry);
            dir->child_count--;

            LOG_INFO("Removed '/proc/%s%s%s'", path ? path : "", (path && path[0]) ? "/" : "", name);
            return 0;
        }
        prev = entry;
        entry = entry->next;
    }

    LOG_WARN("Entry '%s' not found in '%s'", name, parent->name);
    return -1;
}



static vfs_node_t* procfs_mount_cb(const char* source, const char* target, void* data) {
    (void)source;
    (void)target;
    (void)data;

    if (procfs_root_node)
        return procfs_root_node;

    procfs_root_node = procfs_alloc_dir("proc");
    if (!procfs_root_node) {
        LOG_ERROR("Failed to allocate procfs root node");
        return NULL;
    }

    LOG_INFO("procfs mounted");
    return procfs_root_node;
}

void procfs_init(void) {
    vfs_register_fs("procfs", 0, procfs_mount_cb);
    LOG_INFO("procfs registered");
}

#include "hw/cpuid.h"

void procfs_setup(void) {
    char* s;
    s = khmalloc(strlen(OS_NAME) + 1);
    if (s) {
        strcpy(s, OS_NAME);
        procfs_create_file("", "os-name", s, strlen(s));
    }

    s = khmalloc(strlen(OS_VERSION) + 1);
    if (s) {
        strcpy(s, OS_VERSION);
        procfs_create_file("", "os-version", s, strlen(s));
    }

    s = khmalloc(strlen(OS_KRNL_NAME) + 1);
    if (s) {
        strcpy(s, OS_KRNL_NAME);
        procfs_create_file("", "krnl-name", s, strlen(s));
    }

    s = khmalloc(strlen(OS_KRNL_VERSION) + 1);
    if (s) {
        strcpy(s, OS_KRNL_VERSION);
        procfs_create_file("", "krnl-version", s, strlen(s));
    }

    s = khmalloc(16);
    if (s) {
        snprintf(s, 16, "%u", BUILD_NUMBER);
        procfs_create_file("", "os-build-number", s, strlen(s));
    }

    s = khmalloc(strlen(BUILD_DATE) + 1);
    if (s) {
        strcpy(s, BUILD_DATE);
        procfs_create_file("", "os-build-date", s, strlen(s));
    }

    s = khmalloc(18);
    if (s) {
        snprintf(s, 18, "%llu", rtc_get_unix_time());
        vfs_node_t* node = procfs_create_file("", "uptime", s, strlen(s));
        if (node) {
            node->ops = &procfs_file_ops_uptime;
        }
    }
    
    cpu_info_t cpu_info;
    get_cpu_info(&cpu_info);
    s = khmalloc(strlen(cpu_info.brand_string) + 1);
    if (s) {
        strcpy(s, cpu_info.brand_string);
        procfs_create_file("", "cpuname", s, strlen(s));
    }

    s = khmalloc(64);
    if (s) {
        snprintf(s, 64, "0 / 0");
        vfs_node_t* node = procfs_create_file("", "mem", s, 64);
        if (node) {
            node->ops = &procfs_file_ops_mem;
        }
    }
}
