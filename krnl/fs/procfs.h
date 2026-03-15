#ifndef _FS_PROCFS_H
#define _FS_PROCFS_H

#include "fs/vfs.h"
#include <stddef.h>


void procfs_init(void);
void procfs_setup(void);





vfs_node_t* procfs_create_dir(const char* path, const char* name);







vfs_node_t* procfs_create_file(const char* path, const char* name, const char* buf, size_t buf_size);






int procfs_update_file(vfs_node_t* node, const char* buf, size_t buf_size);





int procfs_remove(const char* path, const char* name);

#endif
