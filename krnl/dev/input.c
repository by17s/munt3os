#include "input.h"
#include <util/ring_buffer.h>
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <log.h>
#include <cstdlib.h>
#include <task/sched.h>
#include <mm.h> 

LOG_MODULE("input");

static ring_buffer_t mouse_rb;
static ring_buffer_t kbd_rb;
static uint8_t kbd_led_state = 0;  

static uint32_t input_read_mouse(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    (void)node; (void)offset;
    while (ring_buffer_available(&mouse_rb) == 0) {
        sched_yield();
    }
    size_t av = ring_buffer_available(&mouse_rb);
    if (av < size) size = av;
    return ring_buffer_read(&mouse_rb, buffer, size);
}

static uint32_t input_read_kbd(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    (void)node; (void)offset;
    while (ring_buffer_available(&kbd_rb) == 0) {
        sched_yield();
    }
    size_t av = ring_buffer_available(&kbd_rb);
    if (av < size) size = av;
    return ring_buffer_read(&kbd_rb, buffer, size);
}

static vfs_operations_t mouse_ops = {
    .read = input_read_mouse,
};

static vfs_operations_t kbd_ops = {
    .read = input_read_kbd,
};

void input_init(void) {
    
    ring_buffer_init(&mouse_rb, sizeof(struct input_event) * 256);
    ring_buffer_init(&kbd_rb, sizeof(struct input_event) * 256);

    vfs_node_t* m_node = vfs_alloc_node();
    if (m_node) {
        m_node->type = VFS_CHAR_DEVICE;
        m_node->ops = &mouse_ops;
        devfs_register_device("input/mouse", m_node);
    }

    vfs_node_t* k_node = vfs_alloc_node();
    if (k_node) {
        k_node->type = VFS_CHAR_DEVICE;
        k_node->ops = &kbd_ops;
        devfs_register_device("input/kbd", k_node);
    }
}

void input_report_key_mouse(uint16_t code, int32_t value) {
    struct input_event ev = {0};
    ev.type = EV_KEY; ev.code = code; ev.value = value;
    ring_buffer_write(&mouse_rb, (uint8_t*)&ev, sizeof(ev));
}

void input_report_rel_mouse(uint16_t code, int32_t value) {
    struct input_event ev = {0};
    ev.type = EV_REL; ev.code = code; ev.value = value;
    ring_buffer_write(&mouse_rb, (uint8_t*)&ev, sizeof(ev));
}

void input_sync_mouse(void) {
    struct input_event ev = {0};
    ev.type = EV_SYN; ev.code = 0; ev.value = 0;
    ring_buffer_write(&mouse_rb, (uint8_t*)&ev, sizeof(ev));
}

void input_report_key_kbd(uint16_t code, int32_t value) {
    
    
    if (value == 1) {
        if (code == 0x53) kbd_led_state ^= 0x01; 
        if (code == 0x39) kbd_led_state ^= 0x02; 
        if (code == 0x47) kbd_led_state ^= 0x04; 
    }
    struct input_event ev = {0};
    ev.type = EV_KEY; ev.code = code; ev.value = value;
    ring_buffer_write(&kbd_rb, (uint8_t*)&ev, sizeof(ev));
}

uint8_t input_get_kbd_leds(void) {
    return kbd_led_state;
}

void input_sync_kbd(void) {
    struct input_event ev = {0};
    ev.type = EV_SYN; ev.code = 0; ev.value = 0;
    ring_buffer_write(&kbd_rb, (uint8_t*)&ev, sizeof(ev));
}
