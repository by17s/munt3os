#include "devfs.h"
#include "vfs.h"
#include "log.h"
#include "mem/kheap.h"
#include "cstdlib.h"

LOG_MODULE("devfs")

typedef struct devfs_entry {
    struct devfs_entry* next;
    vfs_node_t* node;
} devfs_entry_t;

typedef struct {
    vfs_node_t* node;
    devfs_entry_t* children;
} devfs_dir_data_t;

static vfs_node_t* devfs_root_node = NULL;
static uint32_t devfs_inode_counter = 1000;

static vfs_operations_t devfs_dir_ops; 

static vfs_node_t* devfs_finddir(vfs_node_t* node, char* name) {
    if (node->type != VFS_DIRECTORY || !node->device) return NULL;
    devfs_dir_data_t* dir_data = (devfs_dir_data_t*)node->device;
    devfs_entry_t* entry = dir_data->children;
    while(entry) {
        if(strcmp(entry->node->name, name) == 0) return entry->node;
        entry = entry->next;
    }
    return NULL;
}

static struct dirent* devfs_readdir(vfs_node_t* node, uint32_t index) {
    if (node->type != VFS_DIRECTORY || !node->device) return NULL;
    devfs_dir_data_t* dir_data = (devfs_dir_data_t*)node->device;
    devfs_entry_t* entry = dir_data->children;
    uint32_t i = 0;
    while(entry && i < index) {
        entry = entry->next;
        i++;
    }
    if(!entry) return NULL;
    
    static struct dirent d_out;
    strncpy(d_out.name, entry->node->name, sizeof(d_out.name)-1);
    d_out.name[sizeof(d_out.name)-1] = '\0';
    d_out.ino = entry->node->inode;
    return &d_out;
}

static int devfs_mkdir(vfs_node_t* node, char* name, uint16_t permission) {
    (void)permission; 
    if (node->type != VFS_DIRECTORY || !node->device) return -1;
    
    
    if (devfs_finddir(node, name) != NULL) return -1;

    devfs_dir_data_t* dir_data = (devfs_dir_data_t*)node->device;

    vfs_node_t* new_dir = vfs_alloc_node();
    if (!new_dir) return -1;

    strncpy(new_dir->name, name, sizeof(new_dir->name)-1);
    new_dir->inode = devfs_inode_counter++;
    new_dir->type = VFS_DIRECTORY;
    new_dir->ops = &devfs_dir_ops;

    devfs_dir_data_t* new_dir_data = (devfs_dir_data_t*)khmalloc(sizeof(devfs_dir_data_t));
    if (!new_dir_data) {
        vfs_free_node(new_dir);
        return -1;
    }
    new_dir_data->node = new_dir;
    new_dir_data->children = NULL;
    new_dir->device = new_dir_data;

    devfs_entry_t* entry = (devfs_entry_t*)khmalloc(sizeof(devfs_entry_t));
    if(!entry) {
        
        return -1;
    }
    entry->node = new_dir;
    entry->next = dir_data->children;
    dir_data->children = entry;

    return 0;
}

static vfs_operations_t devfs_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = devfs_readdir,
    .finddir = devfs_finddir,
    .mkdir = devfs_mkdir
};

static vfs_node_t* devfs_mount(const char* source, const char* target, void* data) {
    (void)source;
    (void)target;
    (void)data;
    
    
    if (devfs_root_node) {
        return devfs_root_node;
    }

    vfs_node_t* root = vfs_alloc_node();
    if(!root) return NULL;
    strcpy(root->name, "dev");
    root->inode = devfs_inode_counter++;
    root->type = VFS_DIRECTORY;
    root->ops = &devfs_dir_ops;
    
    devfs_dir_data_t* root_data = (devfs_dir_data_t*)khmalloc(sizeof(devfs_dir_data_t));
    root_data->node = root;
    root_data->children = NULL;
    root->device = root_data;

    devfs_root_node = root;
    return root;
}

void devfs_register_device(const char* path, vfs_node_t* device_node) {
    if (!devfs_root_node) {
        
        devfs_mount(NULL, NULL, NULL);
    }
    if (!devfs_root_node || !device_node) return;

    
    char path_buf[256];
    strncpy(path_buf, path, 255);
    path_buf[255] = '\0';

    vfs_node_t* current = devfs_root_node;
    char* token = strtok(path_buf, "/");
    char* prev_token = token;
    
    while(token != NULL) {
        token = strtok(NULL, "/");
        if (token == NULL) {
            
            strncpy(device_node->name, prev_token, sizeof(device_node->name)-1);
            device_node->inode = devfs_inode_counter++;
            
            devfs_dir_data_t* dir_data = (devfs_dir_data_t*)current->device;
            devfs_entry_t* entry = (devfs_entry_t*)khmalloc(sizeof(devfs_entry_t));
            entry->node = device_node;
            entry->next = dir_data->children;
            dir_data->children = entry;
            break;
        } else {
            
            vfs_node_t* next_dir = devfs_finddir(current, prev_token);
            if (!next_dir) {
                devfs_mkdir(current, prev_token, 0);
                next_dir = devfs_finddir(current, prev_token);
            }
            if(next_dir && next_dir->type == VFS_DIRECTORY) {
                current = next_dir;
            } else {
                return; 
            }
            prev_token = token;
        }
    }
}

void devfs_init(void) {
    
    vfs_register_fs("devfs", 0, devfs_mount);
}

