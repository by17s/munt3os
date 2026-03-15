#pragma once

#include <hw/video.h>
#include <fs/vfs.h>

#include <api/sysdef.h>

vfs_node_t* dev_fb_init(framebuffer_t* fb_info);