#include "ext2.h"
#include "vfs.h"
#include "log.h"
#include "cstdlib.h"
#include "mem/kheap.h"

LOG_MODULE("ext2fs")



typedef struct ext2_fs {
    vfs_node_t*       block_node;       
    ext2_superblock_t sb;               
    uint32_t          block_size;       
    uint32_t          inodes_per_group;
    uint32_t          inode_size;       
    uint32_t          groups_count;     
    uint32_t          bgdt_block;       
    uint32_t          pointers_per_block; 
} ext2_fs_t;


typedef struct ext2_node_data {
    ext2_fs_t* fs;
    uint32_t   ino;        
    ext2_inode_t inode;   
} ext2_node_data_t;



static uint32_t read_bytes(ext2_fs_t* fs, uint64_t offset, uint32_t size, void* buf) {
    if (!fs->block_node->ops || !fs->block_node->ops->read) return 0;
    return fs->block_node->ops->read(fs->block_node, (uint32_t)offset, size, (uint8_t*)buf);
}

static uint32_t read_block(ext2_fs_t* fs, uint32_t block_no, void* buf) {
    if (block_no == 0) {
        
        memset(buf, 0, fs->block_size);
        return fs->block_size;
    }
    uint64_t offset = (uint64_t)block_no * fs->block_size;
    return read_bytes(fs, offset, fs->block_size, buf);
}



static uint32_t write_bytes(ext2_fs_t* fs, uint64_t offset, uint32_t size, const void* buf) {
    if (!fs->block_node->ops || !fs->block_node->ops->write) return 0;
    return fs->block_node->ops->write(fs->block_node, (uint32_t)offset, size, (uint8_t*)buf);
}

static uint32_t write_block(ext2_fs_t* fs, uint32_t block_no, const void* buf) {
    if (block_no == 0) return 0;
    uint64_t offset = (uint64_t)block_no * fs->block_size;
    return write_bytes(fs, offset, fs->block_size, buf);
}



static bool read_group_desc(ext2_fs_t* fs, uint32_t group, ext2_group_desc_t* out) {
    uint64_t offset = (uint64_t)fs->bgdt_block * fs->block_size
                    + (uint64_t)group * sizeof(ext2_group_desc_t);
    return read_bytes(fs, offset, sizeof(ext2_group_desc_t), out)
           == sizeof(ext2_group_desc_t);
}

static bool write_group_desc(ext2_fs_t* fs, uint32_t group, ext2_group_desc_t* gd) {
    uint64_t offset = (uint64_t)fs->bgdt_block * fs->block_size
                    + (uint64_t)group * sizeof(ext2_group_desc_t);
    return write_bytes(fs, offset, sizeof(ext2_group_desc_t), gd)
           == sizeof(ext2_group_desc_t);
}



static bool read_inode(ext2_fs_t* fs, uint32_t ino, ext2_inode_t* out) {
    if (ino == 0) return false;

    uint32_t group    = (ino - 1) / fs->inodes_per_group;
    uint32_t index    = (ino - 1) % fs->inodes_per_group;

    ext2_group_desc_t gd;
    if (!read_group_desc(fs, group, &gd)) return false;

    uint64_t offset = (uint64_t)gd.bg_inode_table * fs->block_size
                    + (uint64_t)index * fs->inode_size;
    return read_bytes(fs, offset, sizeof(ext2_inode_t), out)
           == sizeof(ext2_inode_t);
}

static bool write_inode(ext2_fs_t* fs, uint32_t ino, ext2_inode_t* in) {
    if (ino == 0) return false;
    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t index = (ino - 1) % fs->inodes_per_group;
    ext2_group_desc_t gd;
    if (!read_group_desc(fs, group, &gd)) return false;
    uint64_t offset = (uint64_t)gd.bg_inode_table * fs->block_size
                    + (uint64_t)index * fs->inode_size;
    return write_bytes(fs, offset, sizeof(ext2_inode_t), in) == sizeof(ext2_inode_t);
}


static uint32_t ext2_now(void) {
    return 0; 
}



static void flush_superblock(ext2_fs_t* fs) {
    write_bytes(fs, EXT2_SUPERBLOCK_OFFSET, sizeof(ext2_superblock_t), &fs->sb);
}








static uint32_t alloc_block(ext2_fs_t* fs, uint32_t preferred_group) {
    uint8_t* bitmap = (uint8_t*)khmalloc(fs->block_size);
    if (!bitmap) return 0;

    for (uint32_t g = 0; g < fs->groups_count; g++) {
        uint32_t group = (preferred_group + g) % fs->groups_count;
        ext2_group_desc_t gd;
        if (!read_group_desc(fs, group, &gd)) continue;
        if (gd.bg_free_blocks_count == 0) continue;

        read_block(fs, gd.bg_block_bitmap, bitmap);

        uint32_t blocks_in_group = fs->sb.s_blocks_per_group;
        
        uint32_t last_group_blocks = fs->sb.s_blocks_count
                                   - group * fs->sb.s_blocks_per_group;
        if (last_group_blocks < blocks_in_group)
            blocks_in_group = last_group_blocks;

        for (uint32_t i = 0; i < blocks_in_group; i++) {
            if (!(bitmap[i / 8] & (1u << (i % 8)))) {
                
                bitmap[i / 8] |= (1u << (i % 8));
                write_block(fs, gd.bg_block_bitmap, bitmap);

                uint32_t block_no = fs->sb.s_first_data_block
                                  + group * fs->sb.s_blocks_per_group + i;

                gd.bg_free_blocks_count--;
                write_group_desc(fs, group, &gd);

                fs->sb.s_free_blocks_count--;
                flush_superblock(fs);

                
                memset(bitmap, 0, fs->block_size);
                write_block(fs, block_no, bitmap);

                khfree(bitmap);
                return block_no;
            }
        }
    }
    khfree(bitmap);
    return 0;
}

static void free_block(ext2_fs_t* fs, uint32_t block_no) {
    if (block_no == 0) return;
    uint32_t group = (block_no - fs->sb.s_first_data_block) / fs->sb.s_blocks_per_group;
    uint32_t index = (block_no - fs->sb.s_first_data_block) % fs->sb.s_blocks_per_group;

    ext2_group_desc_t gd;
    if (!read_group_desc(fs, group, &gd)) return;

    uint8_t* bitmap = (uint8_t*)khmalloc(fs->block_size);
    if (!bitmap) return;
    read_block(fs, gd.bg_block_bitmap, bitmap);
    bitmap[index / 8] &= ~(1u << (index % 8));
    write_block(fs, gd.bg_block_bitmap, bitmap);
    khfree(bitmap);

    gd.bg_free_blocks_count++;
    write_group_desc(fs, group, &gd);
    fs->sb.s_free_blocks_count++;
    flush_superblock(fs);
}







static uint32_t alloc_inode(ext2_fs_t* fs, bool is_dir) {
    uint8_t* bitmap = (uint8_t*)khmalloc(fs->block_size);
    if (!bitmap) return 0;

    for (uint32_t group = 0; group < fs->groups_count; group++) {
        ext2_group_desc_t gd;
        if (!read_group_desc(fs, group, &gd)) continue;
        if (gd.bg_free_inodes_count == 0) continue;

        read_block(fs, gd.bg_inode_bitmap, bitmap);

        for (uint32_t i = 0; i < fs->inodes_per_group; i++) {
            if (!(bitmap[i / 8] & (1u << (i % 8)))) {
                bitmap[i / 8] |= (1u << (i % 8));
                write_block(fs, gd.bg_inode_bitmap, bitmap);

                uint32_t ino = group * fs->inodes_per_group + i + 1;

                gd.bg_free_inodes_count--;
                if (is_dir) gd.bg_used_dirs_count++;
                write_group_desc(fs, group, &gd);

                fs->sb.s_free_inodes_count--;
                flush_superblock(fs);

                
                ext2_inode_t blank;
                memset(&blank, 0, sizeof(blank));
                write_inode(fs, ino, &blank);

                khfree(bitmap);
                return ino;
            }
        }
    }
    khfree(bitmap);
    return 0;
}

static void free_inode(ext2_fs_t* fs, uint32_t ino, bool is_dir) {
    if (ino == 0) return;
    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t index = (ino - 1) % fs->inodes_per_group;

    ext2_group_desc_t gd;
    if (!read_group_desc(fs, group, &gd)) return;

    uint8_t* bitmap = (uint8_t*)khmalloc(fs->block_size);
    if (!bitmap) return;
    read_block(fs, gd.bg_inode_bitmap, bitmap);
    bitmap[index / 8] &= ~(1u << (index % 8));
    write_block(fs, gd.bg_inode_bitmap, bitmap);
    khfree(bitmap);

    gd.bg_free_inodes_count++;
    if (is_dir && gd.bg_used_dirs_count > 0) gd.bg_used_dirs_count--;
    write_group_desc(fs, group, &gd);
    fs->sb.s_free_inodes_count++;
    flush_superblock(fs);
}







static bool inode_set_block(ext2_fs_t* fs, ext2_inode_t* inode, uint32_t n,
                             uint32_t phys_block, uint32_t preferred_group) {
    const uint32_t ppb = fs->pointers_per_block;

    if (n < EXT2_NDIR_BLOCKS) {
        inode->i_block[n] = phys_block;
        return true;
    }
    n -= EXT2_NDIR_BLOCKS;

    uint8_t* buf = (uint8_t*)khmalloc(fs->block_size);
    if (!buf) return false;

    if (n < ppb) {
        
        if (!inode->i_block[EXT2_IND_BLOCK]) {
            uint32_t ind = alloc_block(fs, preferred_group);
            if (!ind) { khfree(buf); return false; }
            memset(buf, 0, fs->block_size);
            write_block(fs, ind, buf);
            inode->i_block[EXT2_IND_BLOCK] = ind;
            
            inode->i_blocks += fs->block_size / 512;
        }
        read_block(fs, inode->i_block[EXT2_IND_BLOCK], buf);
        ((uint32_t*)buf)[n] = phys_block;
        write_block(fs, inode->i_block[EXT2_IND_BLOCK], buf);
        khfree(buf);
        return true;
    }
    n -= ppb;

    if (n < ppb * ppb) {
        
        if (!inode->i_block[EXT2_DIND_BLOCK]) {
            uint32_t dind = alloc_block(fs, preferred_group);
            if (!dind) { khfree(buf); return false; }
            memset(buf, 0, fs->block_size);
            write_block(fs, dind, buf);
            inode->i_block[EXT2_DIND_BLOCK] = dind;
            inode->i_blocks += fs->block_size / 512;
        }
        read_block(fs, inode->i_block[EXT2_DIND_BLOCK], buf);
        uint32_t* dind_table = (uint32_t*)buf;
        uint32_t  dind_idx   = n / ppb;
        if (!dind_table[dind_idx]) {
            uint32_t ind = alloc_block(fs, preferred_group);
            if (!ind) { khfree(buf); return false; }
            uint8_t* zero = (uint8_t*)khmalloc(fs->block_size);
            if (!zero) { khfree(buf); return false; }
            memset(zero, 0, fs->block_size);
            write_block(fs, ind, zero);
            khfree(zero);
            dind_table[dind_idx] = ind;
            write_block(fs, inode->i_block[EXT2_DIND_BLOCK], buf);
            inode->i_blocks += fs->block_size / 512;
        }
        uint32_t ind_block = dind_table[dind_idx];
        read_block(fs, ind_block, buf);
        ((uint32_t*)buf)[n % ppb] = phys_block;
        write_block(fs, ind_block, buf);
        khfree(buf);
        return true;
    }

    
    n -= ppb * ppb;
    if (!inode->i_block[EXT2_TIND_BLOCK]) {
        uint32_t tind = alloc_block(fs, preferred_group);
        if (!tind) { khfree(buf); return false; }
        memset(buf, 0, fs->block_size);
        write_block(fs, tind, buf);
        inode->i_block[EXT2_TIND_BLOCK] = tind;
        inode->i_blocks += fs->block_size / 512;
    }
    read_block(fs, inode->i_block[EXT2_TIND_BLOCK], buf);
    uint32_t* tind_table = (uint32_t*)buf;
    uint32_t  ti = n / (ppb * ppb);
    if (!tind_table[ti]) {
        uint32_t dind = alloc_block(fs, preferred_group);
        if (!dind) { khfree(buf); return false; }
        uint8_t* zero = (uint8_t*)khmalloc(fs->block_size);
        if (zero) { memset(zero, 0, fs->block_size); write_block(fs, dind, zero); khfree(zero); }
        tind_table[ti] = dind;
        write_block(fs, inode->i_block[EXT2_TIND_BLOCK], buf);
        inode->i_blocks += fs->block_size / 512;
    }
    uint32_t dind_block = tind_table[ti];
    read_block(fs, dind_block, buf);
    uint32_t* dind_table2 = (uint32_t*)buf;
    uint32_t  di = (n / ppb) % ppb;
    if (!dind_table2[di]) {
        uint32_t ind = alloc_block(fs, preferred_group);
        if (!ind) { khfree(buf); return false; }
        uint8_t* zero = (uint8_t*)khmalloc(fs->block_size);
        if (zero) { memset(zero, 0, fs->block_size); write_block(fs, ind, zero); khfree(zero); }
        dind_table2[di] = ind;
        write_block(fs, dind_block, buf);
        inode->i_blocks += fs->block_size / 512;
    }
    uint32_t ind_block2 = dind_table2[di];
    read_block(fs, ind_block2, buf);
    ((uint32_t*)buf)[n % ppb] = phys_block;
    write_block(fs, ind_block2, buf);
    khfree(buf);
    return true;
}



static void inode_free_blocks(ext2_fs_t* fs, ext2_inode_t* inode) {
    const uint32_t ppb = fs->pointers_per_block;
    uint8_t* buf  = (uint8_t*)khmalloc(fs->block_size);
    uint8_t* buf2 = (uint8_t*)khmalloc(fs->block_size);
    uint8_t* buf3 = (uint8_t*)khmalloc(fs->block_size);
    if (!buf || !buf2 || !buf3) { khfree(buf); khfree(buf2); khfree(buf3); return; }

    
    for (uint32_t i = 0; i < EXT2_NDIR_BLOCKS; i++) {
        if (inode->i_block[i]) { free_block(fs, inode->i_block[i]); inode->i_block[i] = 0; }
    }
    
    if (inode->i_block[EXT2_IND_BLOCK]) {
        read_block(fs, inode->i_block[EXT2_IND_BLOCK], buf);
        for (uint32_t i = 0; i < ppb; i++)
            if (((uint32_t*)buf)[i]) free_block(fs, ((uint32_t*)buf)[i]);
        free_block(fs, inode->i_block[EXT2_IND_BLOCK]);
        inode->i_block[EXT2_IND_BLOCK] = 0;
    }
    
    if (inode->i_block[EXT2_DIND_BLOCK]) {
        read_block(fs, inode->i_block[EXT2_DIND_BLOCK], buf);
        for (uint32_t i = 0; i < ppb; i++) {
            if (((uint32_t*)buf)[i]) {
                read_block(fs, ((uint32_t*)buf)[i], buf2);
                for (uint32_t j = 0; j < ppb; j++)
                    if (((uint32_t*)buf2)[j]) free_block(fs, ((uint32_t*)buf2)[j]);
                free_block(fs, ((uint32_t*)buf)[i]);
            }
        }
        free_block(fs, inode->i_block[EXT2_DIND_BLOCK]);
        inode->i_block[EXT2_DIND_BLOCK] = 0;
    }
    
    if (inode->i_block[EXT2_TIND_BLOCK]) {
        read_block(fs, inode->i_block[EXT2_TIND_BLOCK], buf);
        for (uint32_t i = 0; i < ppb; i++) {
            if (((uint32_t*)buf)[i]) {
                read_block(fs, ((uint32_t*)buf)[i], buf2);
                for (uint32_t j = 0; j < ppb; j++) {
                    if (((uint32_t*)buf2)[j]) {
                        read_block(fs, ((uint32_t*)buf2)[j], buf3);
                        for (uint32_t k = 0; k < ppb; k++)
                            if (((uint32_t*)buf3)[k]) free_block(fs, ((uint32_t*)buf3)[k]);
                        free_block(fs, ((uint32_t*)buf2)[j]);
                    }
                }
                free_block(fs, ((uint32_t*)buf)[i]);
            }
        }
        free_block(fs, inode->i_block[EXT2_TIND_BLOCK]);
        inode->i_block[EXT2_TIND_BLOCK] = 0;
    }
    inode->i_blocks = 0;
    khfree(buf); khfree(buf2); khfree(buf3);
}







static uint32_t inode_get_block(ext2_fs_t* fs, ext2_inode_t* inode, uint32_t n) {
    const uint32_t ppb = fs->pointers_per_block; 

    
    if (n < EXT2_NDIR_BLOCKS) {
        return inode->i_block[n];
    }
    n -= EXT2_NDIR_BLOCKS;

    
    if (n < ppb) {
        if (!inode->i_block[EXT2_IND_BLOCK]) return 0;
        uint8_t* buf = (uint8_t*)khmalloc(fs->block_size);
        if (!buf) return 0;
        read_block(fs, inode->i_block[EXT2_IND_BLOCK], buf);
        uint32_t res = ((uint32_t*)buf)[n];
        khfree(buf);
        return res;
    }
    n -= ppb;

    
    if (n < ppb * ppb) {
        if (!inode->i_block[EXT2_DIND_BLOCK]) return 0;
        uint8_t* buf = (uint8_t*)khmalloc(fs->block_size);
        if (!buf) return 0;
        read_block(fs, inode->i_block[EXT2_DIND_BLOCK], buf);
        uint32_t ind = ((uint32_t*)buf)[n / ppb];
        if (!ind) { khfree(buf); return 0; }
        read_block(fs, ind, buf);
        uint32_t res = ((uint32_t*)buf)[n % ppb];
        khfree(buf);
        return res;
    }
    n -= ppb * ppb;

    
    {
        if (!inode->i_block[EXT2_TIND_BLOCK]) return 0;
        uint8_t* buf = (uint8_t*)khmalloc(fs->block_size);
        if (!buf) return 0;
        read_block(fs, inode->i_block[EXT2_TIND_BLOCK], buf);
        uint32_t dind = ((uint32_t*)buf)[n / (ppb * ppb)];
        if (!dind) { khfree(buf); return 0; }
        read_block(fs, dind, buf);
        uint32_t ind = ((uint32_t*)buf)[(n / ppb) % ppb];
        if (!ind) { khfree(buf); return 0; }
        read_block(fs, ind, buf);
        uint32_t res = ((uint32_t*)buf)[n % ppb];
        khfree(buf);
        return res;
    }
}



static uint32_t ext2_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node->device) return 0;
    ext2_node_data_t* nd = (ext2_node_data_t*)node->device;
    ext2_fs_t*        fs = nd->fs;
    ext2_inode_t*     in = &nd->inode;

    uint32_t file_size = in->i_size;
    if (offset >= file_size) return 0;
    if (offset + size > file_size) size = file_size - offset;

    uint32_t bytes_read = 0;
    uint32_t bs         = fs->block_size;

    while (bytes_read < size) {
        uint32_t logical_block = (offset + bytes_read) / bs;
        uint32_t block_offset  = (offset + bytes_read) % bs;
        uint32_t to_copy       = bs - block_offset;
        if (to_copy > size - bytes_read) to_copy = size - bytes_read;

        uint32_t disk_block = inode_get_block(fs, in, logical_block);

        if (disk_block == 0) {
            
            memset(buffer + bytes_read, 0, to_copy);
        } else {
            uint64_t phys_offset = (uint64_t)disk_block * bs + block_offset;
            uint32_t got = read_bytes(fs, phys_offset, to_copy, buffer + bytes_read);
            if (got == 0) break;
            to_copy = got;
        }
        bytes_read += to_copy;
    }
    return bytes_read;
}





static uint32_t ext2_write(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node->device || node->type != VFS_FILE) return 0;
    ext2_node_data_t* nd = (ext2_node_data_t*)node->device;
    ext2_fs_t*        fs = nd->fs;
    ext2_inode_t*     in = &nd->inode;
    uint32_t          bs = fs->block_size;

    if (size == 0) return 0;

    uint32_t preferred_group = (nd->ino - 1) / fs->inodes_per_group;
    uint32_t bytes_written = 0;

    while (bytes_written < size) {
        uint32_t logical_block = (offset + bytes_written) / bs;
        uint32_t block_offset  = (offset + bytes_written) % bs;
        uint32_t to_write      = bs - block_offset;
        if (to_write > size - bytes_written) to_write = size - bytes_written;

        uint32_t disk_block = inode_get_block(fs, in, logical_block);

        if (!disk_block) {
            
            disk_block = alloc_block(fs, preferred_group);
            if (!disk_block) break;
            if (!inode_set_block(fs, in, logical_block, disk_block, preferred_group)) break;
            
            in->i_blocks += bs / 512;
        }

        
        if (block_offset != 0 || to_write != bs) {
            uint8_t* tmp = (uint8_t*)khmalloc(bs);
            if (!tmp) break;
            read_block(fs, disk_block, tmp);
            memcpy(tmp + block_offset, buffer + bytes_written, to_write);
            write_block(fs, disk_block, tmp);
            khfree(tmp);
        } else {
            write_block(fs, disk_block, buffer + bytes_written);
        }
        bytes_written += to_write;
    }

    uint32_t end = offset + bytes_written;
    if (end > in->i_size) {
        in->i_size = end;
        node->size = end;
    }
    uint32_t now = ext2_now();
    in->i_mtime = now;
    in->i_ctime = now;
    write_inode(fs, nd->ino, in);

    return bytes_written;
}





typedef bool (*dirent_cb_t)(ext2_dirent_t* de, void* ctx);

static void foreach_dirent(vfs_node_t* node, dirent_cb_t cb, void* ctx) {
    ext2_node_data_t* nd = (ext2_node_data_t*)node->device;
    ext2_fs_t*        fs = nd->fs;
    ext2_inode_t*     in = &nd->inode;
    uint32_t          bs = fs->block_size;

    uint8_t* block_buf = (uint8_t*)khmalloc(bs);
    if (!block_buf) return;

    uint32_t total   = in->i_size;
    uint32_t visited = 0;
    uint32_t logical = 0;

    while (visited < total) {
        uint32_t disk_block = inode_get_block(fs, in, logical);
        if (disk_block == 0) { visited += bs; logical++; continue; }
        read_block(fs, disk_block, block_buf);

        uint32_t pos = 0;
        while (pos < bs && visited + pos < total) {
            ext2_dirent_t* de = (ext2_dirent_t*)(block_buf + pos);
            if (de->rec_len == 0) break; 
            if (de->inode != 0) {        
                if (cb(de, ctx)) { khfree(block_buf); return; }
            }
            pos += de->rec_len;
        }
        visited += bs;
        logical++;
    }
    khfree(block_buf);
}








static int dir_append_entry(vfs_node_t* dir, uint32_t ino,
                             const char* name, uint8_t file_type) {
    ext2_node_data_t* nd  = (ext2_node_data_t*)dir->device;
    ext2_fs_t*        fs  = nd->fs;
    ext2_inode_t*     din = &nd->inode;
    uint32_t          bs  = fs->block_size;

    uint8_t name_len = (uint8_t)strlen(name);
    
    uint16_t needed = (uint16_t)(8 + ((name_len + 3) & ~3u));

    uint8_t* block_buf = (uint8_t*)khmalloc(bs);
    if (!block_buf) return -1;

    uint32_t total   = din->i_size;
    uint32_t logical = 0;
    uint32_t preferred_group = (nd->ino - 1) / fs->inodes_per_group;

    
    for (uint32_t visited = 0; visited < total; visited += bs, logical++) {
        uint32_t disk_block = inode_get_block(fs, din, logical);
        if (!disk_block) continue;
        read_block(fs, disk_block, block_buf);

        uint32_t pos = 0;
        while (pos < bs) {
            ext2_dirent_t* de = (ext2_dirent_t*)(block_buf + pos);
            if (de->rec_len == 0) break;

            
            uint16_t real = (uint16_t)(8 + ((de->name_len + 3) & ~3u));
            uint16_t slack = de->rec_len - real;

            if (de->inode == 0) {
                
                if (de->rec_len >= needed) {
                    de->inode     = ino;
                    de->name_len  = name_len;
                    de->file_type = file_type;
                    memcpy(de->name, name, name_len);
                    write_block(fs, disk_block, block_buf);
                    khfree(block_buf);
                    goto done_mtime;
                }
            } else if (slack >= needed) {
                
                uint16_t old_rec = de->rec_len;
                de->rec_len = real;

                ext2_dirent_t* ne = (ext2_dirent_t*)(block_buf + pos + real);
                ne->inode     = ino;
                ne->rec_len   = old_rec - real;
                ne->name_len  = name_len;
                ne->file_type = file_type;
                memcpy(ne->name, name, name_len);
                write_block(fs, disk_block, block_buf);
                khfree(block_buf);
                goto done_mtime;
            }
            pos += de->rec_len;
        }
    }

    
    {
        uint32_t new_block = alloc_block(fs, preferred_group);
        if (!new_block) { khfree(block_buf); return -1; }
        if (!inode_set_block(fs, din, logical, new_block, preferred_group)) {
            free_block(fs, new_block);
            khfree(block_buf);
            return -1;
        }
        din->i_blocks += bs / 512;
        din->i_size   += bs;
        dir->size      = din->i_size;

        memset(block_buf, 0, bs);
        ext2_dirent_t* ne = (ext2_dirent_t*)block_buf;
        ne->inode     = ino;
        ne->rec_len   = (uint16_t)bs;   
        ne->name_len  = name_len;
        ne->file_type = file_type;
        memcpy(ne->name, name, name_len);
        write_block(fs, new_block, block_buf);
        khfree(block_buf);
    }

done_mtime:;
    uint32_t now = ext2_now();
    din->i_mtime = now;
    din->i_ctime = now;
    write_inode(fs, nd->ino, din);
    return 0;
}






static int dir_remove_entry(vfs_node_t* dir, const char* name, uint32_t* out_ino) {
    ext2_node_data_t* nd  = (ext2_node_data_t*)dir->device;
    ext2_fs_t*        fs  = nd->fs;
    ext2_inode_t*     din = &nd->inode;
    uint32_t          bs  = fs->block_size;
    uint8_t           nlen = (uint8_t)strlen(name);

    uint8_t* block_buf = (uint8_t*)khmalloc(bs);
    if (!block_buf) return -1;

    uint32_t total   = din->i_size;
    uint32_t logical = 0;

    for (uint32_t visited = 0; visited < total; visited += bs, logical++) {
        uint32_t disk_block = inode_get_block(fs, din, logical);
        if (!disk_block) continue;
        read_block(fs, disk_block, block_buf);

        ext2_dirent_t* prev = NULL;
        uint32_t pos = 0;
        while (pos < bs) {
            ext2_dirent_t* de = (ext2_dirent_t*)(block_buf + pos);
            if (de->rec_len == 0) break;

            if (de->inode != 0 && de->name_len == nlen
                && memcmp(de->name, name, nlen) == 0) {
                if (out_ino) *out_ino = de->inode;
                if (prev) {
                    
                    prev->rec_len += de->rec_len;
                } else {
                    
                    de->inode = 0;
                }
                write_block(fs, disk_block, block_buf);
                khfree(block_buf);

                uint32_t now = ext2_now();
                din->i_mtime = now;
                din->i_ctime = now;
                write_inode(fs, nd->ino, din);
                return 0;
            }
            prev = de;
            pos += de->rec_len;
        }
    }
    khfree(block_buf);
    return -1;  
}


static bool dir_is_empty(vfs_node_t* dir) {
    ext2_node_data_t* nd  = (ext2_node_data_t*)dir->device;
    ext2_fs_t*        fs  = nd->fs;
    ext2_inode_t*     din = &nd->inode;
    uint32_t          bs  = fs->block_size;

    uint8_t* buf = (uint8_t*)khmalloc(bs);
    if (!buf) return false;

    bool empty = true;
    uint32_t total = din->i_size, logical = 0;
    for (uint32_t v = 0; v < total && empty; v += bs, logical++) {
        uint32_t disk_block = inode_get_block(fs, din, logical);
        if (!disk_block) continue;
        read_block(fs, disk_block, buf);
        uint32_t pos = 0;
        while (pos < bs) {
            ext2_dirent_t* de = (ext2_dirent_t*)(buf + pos);
            if (de->rec_len == 0) break;
            if (de->inode != 0) {
                if (!(de->name_len == 1 && de->name[0] == '.') &&
                    !(de->name_len == 2 && de->name[0] == '.' && de->name[1] == '.')) {
                    empty = false;
                    break;
                }
            }
            pos += de->rec_len;
        }
    }
    khfree(buf);
    return empty;
}



typedef struct {
    uint32_t target_index;
    uint32_t current;
    struct dirent result;
    bool found;
} readdir_ctx_t;

static bool readdir_cb(ext2_dirent_t* de, void* ctx) {
    readdir_ctx_t* rc = (readdir_ctx_t*)ctx;
    if (rc->current == rc->target_index) {
        uint8_t len = de->name_len;
        if (len >= sizeof(rc->result.name)) len = sizeof(rc->result.name) - 1;
        memcpy(rc->result.name, de->name, len);
        rc->result.name[len] = '\0';
        rc->result.ino = de->inode;
        rc->found = true;
        return true; 
    }
    rc->current++;
    return false;
}

static struct dirent* ext2_readdir(vfs_node_t* node, uint32_t index) {
    if (!node->device || node->type != VFS_DIRECTORY) return NULL;
    static struct dirent out;
    readdir_ctx_t rc = { .target_index = index, .current = 0, .found = false };
    foreach_dirent(node, readdir_cb, &rc);
    if (!rc.found) return NULL;
    out = rc.result;
    return &out;
}



typedef struct {
    const char* name;
    uint32_t    found_ino;
    uint8_t     found_type;
    bool        found;
} finddir_ctx_t;

static bool finddir_cb(ext2_dirent_t* de, void* ctx) {
    finddir_ctx_t* fc = (finddir_ctx_t*)ctx;
    uint8_t len = de->name_len;
    if (len == strlen(fc->name) && memcmp(de->name, fc->name, len) == 0) {
        fc->found_ino  = de->inode;
        fc->found_type = de->file_type;
        fc->found = true;
        return true;
    }
    return false;
}


static vfs_node_t* ext2_make_node(ext2_fs_t* fs, uint32_t ino);

static vfs_node_t* ext2_finddir(vfs_node_t* node, char* name) {
    if (!node->device || node->type != VFS_DIRECTORY) return NULL;
    ext2_node_data_t* nd = (ext2_node_data_t*)node->device;

    finddir_ctx_t fc = { .name = name, .found = false };
    foreach_dirent(node, finddir_cb, &fc);
    if (!fc.found) return NULL;

    return ext2_make_node(nd->fs, fc.found_ino);
}



static int ext2_create(vfs_node_t* dir, char* name, uint16_t permission) {
    if (!dir->device || dir->type != VFS_DIRECTORY) return -1;
    ext2_node_data_t* nd = (ext2_node_data_t*)dir->device;
    ext2_fs_t*        fs = nd->fs;

    
    finddir_ctx_t fc = { .name = name, .found = false };
    foreach_dirent(dir, finddir_cb, &fc);
    if (fc.found) return -1;  

    uint32_t ino = alloc_inode(fs, false);
    if (!ino) return -1;

    uint32_t now = ext2_now();
    ext2_inode_t in;
    memset(&in, 0, sizeof(in));
    in.i_mode        = EXT2_S_IFREG | (permission & 0x0FFF);
    in.i_links_count = 1;
    in.i_atime = in.i_ctime = in.i_mtime = now;
    write_inode(fs, ino, &in);

    if (dir_append_entry(dir, ino, name, EXT2_FT_REG_FILE) != 0) {
        free_inode(fs, ino, false);
        return -1;
    }
    return 0;
}



static int ext2_mkdir(vfs_node_t* dir, char* name, uint16_t permission) {
    if (!dir->device || dir->type != VFS_DIRECTORY) return -1;
    ext2_node_data_t* nd = (ext2_node_data_t*)dir->device;
    ext2_fs_t*        fs = nd->fs;

    finddir_ctx_t fc = { .name = name, .found = false };
    foreach_dirent(dir, finddir_cb, &fc);
    if (fc.found) return -1;

    uint32_t ino = alloc_inode(fs, true);
    if (!ino) return -1;

    uint32_t now = ext2_now();
    ext2_inode_t in;
    memset(&in, 0, sizeof(in));
    in.i_mode        = EXT2_S_IFDIR | (permission & 0x0FFF);
    in.i_links_count = 2;  
    in.i_atime = in.i_ctime = in.i_mtime = now;
    write_inode(fs, ino, &in);

    
    vfs_node_t* new_node = ext2_make_node(fs, ino);
    if (!new_node) { free_inode(fs, ino, true); return -1; }

    
    dir_append_entry(new_node, ino,      ".",  EXT2_FT_DIR);
    dir_append_entry(new_node, nd->ino,  "..", EXT2_FT_DIR);

    
    ext2_node_data_t* nnd = (ext2_node_data_t*)new_node->device;
    write_inode(fs, ino, &nnd->inode);
    vfs_free_node(new_node);

    
    if (dir_append_entry(dir, ino, name, EXT2_FT_DIR) != 0) {
        free_inode(fs, ino, true);
        return -1;
    }

    
    nd->inode.i_links_count++;
    write_inode(fs, nd->ino, &nd->inode);
    dir->mask = nd->inode.i_mode & 0x0FFF;

    return 0;
}



static int ext2_unlink(vfs_node_t* dir, char* name) {
    if (!dir->device || dir->type != VFS_DIRECTORY) return -1;
    ext2_node_data_t* nd = (ext2_node_data_t*)dir->device;
    ext2_fs_t*        fs = nd->fs;

    uint32_t target_ino = 0;
    if (dir_remove_entry(dir, name, &target_ino) != 0) return -1;

    
    ext2_inode_t in;
    if (read_inode(fs, target_ino, &in)) {
        if (in.i_links_count > 0) in.i_links_count--;
        if (in.i_links_count == 0) {
            inode_free_blocks(fs, &in);
            in.i_dtime = ext2_now();
            write_inode(fs, target_ino, &in);
            free_inode(fs, target_ino, false);
        } else {
            write_inode(fs, target_ino, &in);
        }
    }
    return 0;
}



static int ext2_rmdir(vfs_node_t* dir, char* name) {
    if (!dir->device || dir->type != VFS_DIRECTORY) return -1;
    ext2_node_data_t* nd = (ext2_node_data_t*)dir->device;
    ext2_fs_t*        fs = nd->fs;

    
    finddir_ctx_t fc = { .name = name, .found = false };
    foreach_dirent(dir, finddir_cb, &fc);
    if (!fc.found) return -1;
    if (fc.found_type != EXT2_FT_DIR) return -1;

    
    vfs_node_t* target = ext2_make_node(fs, fc.found_ino);
    if (!target) return -1;
    bool empty = dir_is_empty(target);
    vfs_free_node(target);
    if (!empty) return -1;  

    uint32_t target_ino = fc.found_ino;
    dir_remove_entry(dir, name, NULL);

    ext2_inode_t in;
    if (read_inode(fs, target_ino, &in)) {
        inode_free_blocks(fs, &in);
        in.i_dtime = ext2_now();
        in.i_links_count = 0;
        write_inode(fs, target_ino, &in);
        free_inode(fs, target_ino, true);
    }

    
    if (nd->inode.i_links_count > 0) nd->inode.i_links_count--;
    write_inode(fs, nd->ino, &nd->inode);

    return 0;
}







static int ext2_rename(vfs_node_t* dir, char* old_name, char* new_name) {
    if (!dir->device || dir->type != VFS_DIRECTORY) return -1;

    
    finddir_ctx_t fc = { .name = old_name, .found = false };
    foreach_dirent(dir, finddir_cb, &fc);
    if (!fc.found) return -1;

    uint32_t ino       = fc.found_ino;
    uint8_t  file_type = fc.found_type;

    
    if (dir_remove_entry(dir, old_name, NULL) != 0) return -1;

    
    finddir_ctx_t fc2 = { .name = new_name, .found = false };
    foreach_dirent(dir, finddir_cb, &fc2);
    if (fc2.found) {
        ext2_node_data_t* nd = (ext2_node_data_t*)dir->device;
        ext2_fs_t*        fs = nd->fs;
        uint32_t victim_ino = fc2.found_ino;
        dir_remove_entry(dir, new_name, NULL);
        ext2_inode_t victim_in;
        if (read_inode(fs, victim_ino, &victim_in)) {
            if (victim_in.i_links_count > 0) victim_in.i_links_count--;
            if (victim_in.i_links_count == 0) {
                inode_free_blocks(fs, &victim_in);
                victim_in.i_dtime = ext2_now();
                write_inode(fs, victim_ino, &victim_in);
                free_inode(fs, victim_ino, (victim_in.i_mode & 0xF000) == EXT2_S_IFDIR);
            } else {
                write_inode(fs, victim_ino, &victim_in);
            }
        }
    }

    return dir_append_entry(dir, ino, new_name, file_type);
}



static int ext2_readlink(vfs_node_t* node, char* buffer, size_t size) {
    if (!node->device) return -1;
    ext2_node_data_t* nd = (ext2_node_data_t*)node->device;
    ext2_inode_t*     in = &nd->inode;

    uint32_t link_len = in->i_size;
    if (link_len == 0 || size == 0) return 0;

    
    if (link_len <= 60) {
        uint32_t copy = (link_len < size) ? link_len : (uint32_t)(size - 1);
        memcpy(buffer, (char*)in->i_block, copy);
        buffer[copy] = '\0';
        return (int)copy;
    }

    
    uint32_t copy = (link_len < (uint32_t)(size - 1)) ? link_len : (uint32_t)(size - 1);
    uint32_t got  = ext2_read(node, 0, copy, (uint8_t*)buffer);
    buffer[got] = '\0';
    return (int)got;
}



static int ext2_stat(vfs_node_t* node, struct stat* st) {
    if (!node->device || !st) return -1;
    ext2_node_data_t* nd = (ext2_node_data_t*)node->device;
    ext2_inode_t*     in = &nd->inode;

    memset(st, 0, sizeof(*st));
    st->st_ino    = nd->ino;
    st->st_mode   = in->i_mode;
    st->st_nlink  = in->i_links_count;
    st->st_uid    = in->i_uid;
    st->st_gid    = in->i_gid;
    st->st_size   = in->i_size;
    st->st_atime  = in->i_atime;
    st->st_mtime  = in->i_mtime;
    st->st_ctime  = in->i_ctime;
    st->st_blksize= nd->fs->block_size;
    st->st_blocks = in->i_blocks;
    return 0;
}



static vfs_operations_t ext2_ops = {
    .read     = ext2_read,
    .write    = ext2_write,
    .open     = NULL,
    .close    = NULL,
    .readdir  = ext2_readdir,
    .finddir  = ext2_finddir,
    .create   = ext2_create,
    .mkdir    = ext2_mkdir,
    .rmdir    = ext2_rmdir,
    .unlink   = ext2_unlink,
    .symlink  = NULL,
    .readlink = ext2_readlink,
    .rename   = ext2_rename,
    .chmod    = NULL,
    .chown    = NULL,
    .stat     = ext2_stat,
    .ioctl    = NULL,
};







static vfs_node_t* ext2_make_node(ext2_fs_t* fs, uint32_t ino) {
    ext2_inode_t raw_inode;
    if (!read_inode(fs, ino, &raw_inode)) return NULL;

    vfs_node_t* node = vfs_alloc_node();
    if (!node) return NULL;

    
    uint16_t fmt = raw_inode.i_mode & 0xF000;
    switch (fmt) {
        case EXT2_S_IFREG:  node->type = VFS_FILE;         break;
        case EXT2_S_IFDIR:  node->type = VFS_DIRECTORY;    break;
        case EXT2_S_IFLNK:  node->type = VFS_SYMLINK;      break;
        case EXT2_S_IFBLK:  node->type = VFS_BLOCK_DEVICE; break;
        case EXT2_S_IFCHR:  node->type = VFS_CHAR_DEVICE;  break;
        default:            node->type = VFS_UNKNOWN;       break;
    }

    node->inode   = ino;
    node->mask    = raw_inode.i_mode & 0x0FFF;
    node->uid     = raw_inode.i_uid;
    node->gid     = raw_inode.i_gid;
    node->size    = raw_inode.i_size;
    node->atime   = raw_inode.i_atime;
    node->mtime   = raw_inode.i_mtime;
    node->ctime   = raw_inode.i_ctime;
    node->blksize = fs->block_size;
    node->blocks  = raw_inode.i_blocks;
    node->ops     = &ext2_ops;

    ext2_node_data_t* nd = (ext2_node_data_t*)khmalloc(sizeof(ext2_node_data_t));
    if (!nd) { vfs_free_node(node); return NULL; }
    nd->fs    = fs;
    nd->ino   = ino;
    nd->inode = raw_inode;
    node->device = nd;

    return node;
}



static vfs_node_t* ext2_mount(const char* source, const char* target, void* data) {
    (void)target;

    
    vfs_node_t* block_node = NULL;
    if (source) {
        block_node = kopen(source);
    }
    if (!block_node) {
        ext2_mount_data_t* md = (ext2_mount_data_t*)data;
        if (md) block_node = md->block_node;
    }
    if (!block_node || !block_node->ops || !block_node->ops->read) {
        LOG_ERROR("ext2fs: no readable block device");
        return NULL;
    }

    
    ext2_fs_t* fs = (ext2_fs_t*)khmalloc(sizeof(ext2_fs_t));
    if (!fs) return NULL;
    memset(fs, 0, sizeof(*fs));
    fs->block_node = block_node;

    
    uint32_t got = block_node->ops->read(block_node, EXT2_SUPERBLOCK_OFFSET,
                                         sizeof(ext2_superblock_t),
                                         (uint8_t*)&fs->sb);
    if (got < sizeof(ext2_superblock_t)) {
        LOG_ERROR("ext2fs: short read on superblock");
        khfree(fs);
        return NULL;
    }
    if (fs->sb.s_magic != EXT2_SUPER_MAGIC) {
        LOG_ERROR("ext2fs: bad magic 0x%04x (expected 0x%04x)",
                  fs->sb.s_magic, EXT2_SUPER_MAGIC);
        khfree(fs);
        return NULL;
    }

    fs->block_size          = 1024u << fs->sb.s_log_block_size;
    fs->inodes_per_group    = fs->sb.s_inodes_per_group;
    fs->inode_size          = (fs->sb.s_rev_level >= 1) ? fs->sb.s_inode_size : 128;
    fs->pointers_per_block  = fs->block_size / sizeof(uint32_t);

    
    
    
    fs->bgdt_block = (fs->block_size == 1024) ? 2 : 1;

    fs->groups_count = (fs->sb.s_blocks_count + fs->sb.s_blocks_per_group - 1)
                       / fs->sb.s_blocks_per_group;

    LOG_INFO("ext2fs: block_size=%u inode_size=%u groups=%u",
             fs->block_size, fs->inode_size, fs->groups_count);

    
    vfs_node_t* root = ext2_make_node(fs, EXT2_ROOT_INO);
    if (!root) {
        LOG_ERROR("ext2fs: failed to read root inode");
        khfree(fs);
        return NULL;
    }
    strncpy(root->name, "/", sizeof(root->name) - 1);

    LOG_INFO("ext2fs: mounted successfully (%u inodes, %u blocks)",
             fs->sb.s_inodes_count, fs->sb.s_blocks_count);
    return root;
}



void ext2fs_init(void) {
    vfs_register_fs("ext2", 0, ext2_mount);
    LOG_INFO("ext2fs registered");
}