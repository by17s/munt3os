#include "tarfs.h"
#include "vfs.h"
#include "log.h"
#include "cstdlib.h"
#include "mem/kheap.h"

LOG_MODULE("tarfs")

struct tar_header {
    char filename[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} __attribute__((packed));

typedef struct tar_dir_entry {
    struct tar_dir_entry* next;
    vfs_node_t* node;
} tar_dir_entry_t;

typedef struct tar_dir_data {
    tar_dir_entry_t* children;
} tar_dir_data_t;

typedef struct tar_file_data {
    vfs_node_t* source_node;
    uint64_t data_offset;
} tar_file_data_t;

static uint32_t octal_to_int(const char *str, int size) {
    uint32_t n = 0;
    const char *c = str;
    while (size-- > 0) {
        if (*c >= '0' && *c <= '7') {
            n = (n * 8) + (*c - '0');
        }
        c++;
    }
    return n;
}

static uint32_t tarfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (node->type != VFS_FILE || !node->device) return 0;
    tar_file_data_t* fd = (tar_file_data_t*)node->device;
    if (offset >= node->size) return 0;
    if (offset + size > node->size) size = node->size - offset;

    if (fd->source_node && fd->source_node->ops && fd->source_node->ops->read) {
        return fd->source_node->ops->read(fd->source_node, fd->data_offset + offset, size, buffer);
    }
    return 0;
}

static struct dirent* tarfs_readdir(vfs_node_t* node, uint32_t index) {
    if (node->type != VFS_DIRECTORY || !node->device) return NULL;
    tar_dir_data_t* dd = (tar_dir_data_t*)node->device;

    tar_dir_entry_t* entry = dd->children;
    uint32_t i = 0;
    while (entry && i < index) {
        entry = entry->next;
        i++;
    }

    if (!entry) return NULL;

    
    
    
    static struct dirent dir_out;
    strncpy(dir_out.name, entry->node->name, sizeof(dir_out.name) - 1);
    dir_out.name[sizeof(dir_out.name) - 1] = '\0';
    dir_out.ino = entry->node->inode;
    return &dir_out;
}

static vfs_node_t* tarfs_finddir(vfs_node_t* node, char* name) {
    if (node->type != VFS_DIRECTORY || !node->device) return NULL;
    tar_dir_data_t* dd = (tar_dir_data_t*)node->device;

    tar_dir_entry_t* entry = dd->children;
    while (entry) {
        if (strcmp(entry->node->name, name) == 0) {
            return entry->node;
        }
        entry = entry->next;
    }
    return NULL;
}

static vfs_operations_t tarfs_ops = {
    .read = tarfs_read,
    .write = NULL, 
    .open = NULL,
    .close = NULL,
    .readdir = tarfs_readdir,
    .finddir = tarfs_finddir,
    .create = NULL,
    .mkdir = NULL,
    .rmdir = NULL,
    .unlink = NULL,
    .symlink = NULL,
    .readlink = NULL,
    .rename = NULL,
    .chmod = NULL,
    .chown = NULL,
    .stat = NULL,
    .ioctl = NULL
};

static uint32_t inode_counter = 1;

static vfs_node_t* create_dir_node(const char* name) {
    vfs_node_t* n = vfs_alloc_node();
    if (!n) return NULL;
    strncpy(n->name, name, sizeof(n->name)-1);
    n->inode = inode_counter++;
    n->type = VFS_DIRECTORY;
    n->ops = &tarfs_ops;
    
    tar_dir_data_t* dd = (tar_dir_data_t*)khmalloc(sizeof(tar_dir_data_t));
    if (dd) {
        dd->children = NULL;
        n->device = dd;
    }
    return n;
}

static vfs_node_t* tarfs_get_or_create_dir(vfs_node_t* root, const char* path) {
    if (!path || *path == '\0') return root;

    char temp_path[256];
    strncpy(temp_path, path, sizeof(temp_path)-1);
    temp_path[sizeof(temp_path)-1] = '\0';
    
    
    int len = strlen(temp_path);
    while (len > 0 && temp_path[len-1] == '/') {
        temp_path[len-1] = '\0';
        len--;
    }

    if (len == 0) return root;

    vfs_node_t* curr = root;
    char* token = temp_path;
    char* slash;

    while (token && *token) {
        slash = NULL;
        for (int i = 0; token[i]; i++) {
            if (token[i] == '/') {
                slash = &token[i];
                break;
            }
        }
        
        if (slash) *slash = '\0';

        vfs_node_t* next = tarfs_finddir(curr, token);
        if (!next) {
            next = create_dir_node(token);
            if (!next) return NULL;
            
            tar_dir_data_t* dd = (tar_dir_data_t*)curr->device;
            tar_dir_entry_t* entry = (tar_dir_entry_t*)khmalloc(sizeof(tar_dir_entry_t));
            entry->node = next;
            entry->next = dd->children;
            dd->children = entry;
        }

        curr = next;
        
        if (slash) token = slash + 1;
        else break;
        
        while (token && *token == '/') token++;
    }

    return curr;
}

static vfs_node_t* tarfs_mount(const char* source, const char* target, void* data) {
    (void)target;
    
    vfs_node_t* source_node = NULL;
    if(source == NULL) {
        LOG_ERROR("tarfs: Mount requires a source block device");
        return NULL;
    } else {
        source_node = kopen(source);
        if(source_node == NULL) {
            tarfs_mount_data_t* md = (tarfs_mount_data_t*)data;
            if (!md || !md->block_node) {
                LOG_ERROR("tarfs: Mount requires tarfs_mount_data_t with block_node");
                return NULL;
            }
            source_node = md->block_node;
        }
        
    }
    
    if (!source_node || !source_node->ops || !source_node->ops->read) {
        LOG_ERROR("tarfs: Source block node cannot be read");
        return NULL;
    }

    vfs_node_t* root = create_dir_node("/");
    if (!root) return NULL;

    uint64_t offset = 0;

    while (true) {
        struct tar_header header;
        uint32_t bytes_read = source_node->ops->read(source_node, offset, 512, (uint8_t*)&header);
        if (bytes_read < 512 || header.filename[0] == '\0') {
            break; 
        }

        uint32_t size = octal_to_int(header.size, 11);

        char fn[256];
        memset(fn, 0, sizeof(fn));
        strncpy(fn, header.filename, 100);

        if (header.prefix[0]) {
            char temp[256];
            strncpy(temp, header.prefix, 155);
            strcat(temp, "/");
            strncat(temp, header.filename, 100);
            strncpy(fn, temp, 255);
        }

        bool is_dir = (header.typeflag == '5');
        
        if (fn[strlen(fn)-1] == '/') is_dir = true;

        if (is_dir) {
            tarfs_get_or_create_dir(root, fn);
        } else {
            
            char* last_slash = NULL;
            for (int i = 0; fn[i]; i++) {
                if (fn[i] == '/') last_slash = &fn[i];
            }

            char file_name[128];
            vfs_node_t* parent = root;
            
            if (last_slash) {
                *last_slash = '\0';
                parent = tarfs_get_or_create_dir(root, fn);
                strncpy(file_name, last_slash + 1, sizeof(file_name)-1);
            } else {
                strncpy(file_name, fn, sizeof(file_name)-1);
            }

            file_name[sizeof(file_name)-1] = '\0';

            vfs_node_t* file_n = vfs_alloc_node();
            strncpy(file_n->name, file_name, sizeof(file_n->name)-1);
            file_n->inode = inode_counter++;
            file_n->type = VFS_FILE;
            file_n->size = size;
            file_n->ops = &tarfs_ops;
            
            tar_file_data_t* fd = (tar_file_data_t*)khmalloc(sizeof(tar_file_data_t));
            if (fd) {
                fd->source_node = source_node;
                fd->data_offset = offset + 512;
            }
            file_n->device = fd;

            if (parent && parent->device) {
                tar_dir_data_t* pdd = (tar_dir_data_t*)parent->device;
                tar_dir_entry_t* entry = (tar_dir_entry_t*)khmalloc(sizeof(tar_dir_entry_t));
                entry->node = file_n;
                entry->next = pdd->children;
                pdd->children = entry;
            }
        }

        uint32_t blocks = (size + 511) / 512;
        offset += 512 + (blocks * 512);
    }

    LOG_INFO("tarfs: Mounted successfully from block device (size: %llu)", (uint64_t)source_node->size);
    return root;
}

void tarfs_init(void) {
    vfs_register_fs("tarfs", 0, tarfs_mount);
    LOG_INFO("tarfs registered");
}
