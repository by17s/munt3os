#ifndef __SYSDEF_H
#define __SYSDEF_H

#include <stdint.h>


#define TTY_IOCTL_CLEAR_SCREEN 0
#define TTY_IOCTL_SET_FG_COLOR 1
#define TTY_IOCTL_SET_BG_COLOR 2
#define TTY_IOCTL_SET_CURSOR_POS 3
#define TTY_IOCTL_GET_CURSOR_POS 4

#define FBDEV_IOCTL_GET_INFO 0x1001
#define FBDEV_IOCTL_MAP 0x1002
#define FBDEV_IOCTL_UNMAP 0x1003

#define TTY_IOCTL_MAP_FRAMEBUFFER 0x10000
#define TTY_IOCTL_UNMAP_FRAMEBUFFER 0x10001


#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040

#include <sys/types.h>

typedef uint64_t off64_t;
typedef int64_t blkcnt64_t;
typedef uint64_t ino64_t;

struct stat {
	dev_t st_dev;
	ino_t st_ino;
	mode_t st_mode;
	nlink_t st_nlink;
	uid_t st_uid;
	gid_t st_gid;
	dev_t st_rdev;
	off_t st_size;
	time_t st_atime;
	time_t st_mtime;
	time_t st_ctime;
	blksize_t st_blksize;
	blkcnt_t st_blocks;
	mode_t st_attr;
}; 

struct stat64 {
	dev_t st_dev;
	ino64_t st_ino;
	mode_t st_mode;
	nlink_t st_nlink;
	uid_t st_uid;
	gid_t st_gid;
	dev_t st_rdev;
	off64_t st_size;
	time_t st_atime;
	time_t st_mtime;
	time_t st_ctime;
	blksize_t st_blksize;
	blkcnt64_t st_blocks;
	mode_t st_attr;
}; 

struct tty_fb_info {
    void* buffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
};

#define EV_SYN      0x00
#define EV_KEY      0x01
#define EV_REL      0x02
#define EV_ABS      0x03

#define REL_X       0x00
#define REL_Y       0x01
#define REL_WHEEL   0x08

#define BTN_MOUSE   0x110
#define BTN_LEFT    0x110
#define BTN_RIGHT   0x111
#define BTN_MIDDLE  0x112

struct input_event {
    struct timeval time;
    uint16_t type;
    uint16_t code;
    int32_t value;
};

#endif 