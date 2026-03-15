#include "ohci.h"
#include "usb.h"
#include <log.h>
#include <printf.h>
#include "../../fs/vfs.h"
#include "../../fs/devfs.h"
#include "../../mem/pmm.h"
#include "../../mem/kheap.h"
#include "../../cstdlib.h"
#include <mem/kslab.h>
#include "../lapic.h"
#include "../idt.h"

LOG_MODULE("ohci")

extern void* phys_to_virt(uint64_t phys_addr);

typedef struct {
    volatile uint32_t config;
    volatile uint32_t tail_p;
    volatile uint32_t head_p;
    volatile uint32_t next_ed;
} __attribute__((packed)) ohci_ed_t;

typedef struct {
    volatile uint32_t config;
    volatile uint32_t cbp;
    volatile uint32_t next_td;
    volatile uint32_t be;
} __attribute__((packed)) ohci_td_t;

typedef struct {
    volatile uint32_t intr_table[32];
    volatile uint16_t frame_number;
    volatile uint16_t pad1;
    volatile uint32_t done_head;
    volatile uint8_t reserved[116];
} __attribute__((packed, aligned(256))) ohci_hcca_t;

typedef struct {
    ohci_hcca_t hcca;
    ohci_ed_t control_ed[16];
    ohci_td_t setup_td[16];
    ohci_td_t status_td[16];
    ohci_td_t dummy_td[16];
    usb_setup_packet_t setup_packet[16];
    
    
    ohci_td_t desc_setup_td[16];
    ohci_td_t desc_data_td[16];
    ohci_td_t desc_status_td[16];
    ohci_ed_t desc_ed[16];
    ohci_td_t desc_dummy_td[16];
    usb_setup_packet_t desc_packet[16];
    usb_device_descriptor_t dev_desc[16];

    
    ohci_td_t conf_setup_td[16];
    ohci_td_t conf_data_td[16];
    ohci_td_t conf_status_td[16];
    ohci_ed_t conf_ed[16];
    ohci_td_t conf_dummy_td[16];
    usb_setup_packet_t conf_packet[16];
    uint8_t conf_desc[16][256];

    
    ohci_td_t set_conf_setup_td[16];
    ohci_td_t set_conf_status_td[16];
    ohci_td_t set_conf_dummy_td[16];
    ohci_ed_t set_conf_ed[16];
    usb_setup_packet_t set_conf_packet[16];

    
    ohci_ed_t intr_ed[16];
    ohci_td_t intr_td[16];
    ohci_td_t intr_dummy_td[16];
    uint8_t kbd_report[16][8];
    uint8_t is_kbd[16];

    char dummy_pad[256];
} __attribute__((packed)) ohci_mem_t;

#define MAX_OHCI_CONTROLLERS 4
static ohci_mem_t* ohci_controllers[MAX_OHCI_CONTROLLERS];
static volatile uint32_t* ohci_regs[MAX_OHCI_CONTROLLERS];
static int num_ohci = 0;

__attribute__((interrupt))
static void isr_ohci(struct interrupt_frame* frame) {
    lapic_eoi();
    LOG_INFO("OHCI MSI INTERRUPT FIRED!");
}

void dummy_mdelay(int ms) {
    for (volatile uint32_t i = 0; i < ms * 10000; i++) {
        asm volatile("pause");
    }
}

static uint32_t pcie_read_bar(pcie_device_info_t* dev, int bar_index) {
    uint32_t* bars = (uint32_t*)(dev->ecam_base + 0x10);
    return bars[bar_index];
}


static uint32_t usb_dev_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0; 
}

static uint32_t usb_dev_write(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    (void)node; (void)offset; (void)buffer;
    return size;
}

static vfs_operations_t usb_dev_ops = {
    .read = usb_dev_read,
    .write = usb_dev_write
};

void ohci_init(pcie_device_info_t* dev) {
    uint32_t bar0 = pcie_read_bar(dev, 0);
    if (!bar0) {
        LOG_WARN("OHCI: Invalid BAR0");
        return;
    }

    uint64_t mmio_phys = bar0 & 0xFFFFFFF0;
    volatile uint32_t* regs = (volatile uint32_t*)phys_to_virt(mmio_phys);
    
    
    uint32_t rev = regs[0];
    LOG_INFO("OHCI Controller Revision: %x", rev & 0xFF);

    idt_set_descriptor(0x40, isr_ohci, 0x8E);
    if (pcie_enable_msi(dev, 0x40)) {
        LOG_INFO("OHCI: MSI enabled successfully on vector 0x40");
        
        regs[0x10 / 4] = 0x80000002; 
    } else {
        LOG_WARN("OHCI: MSI not supported or could not be enabled");
    }

    void* p_mem = pmm_alloc(2);
    if (!p_mem) {
        LOG_ERROR("OHCI: Failed to allocate physical memory");
        return;
    }
    ohci_mem_t* mem_phys = (ohci_mem_t*)p_mem;
    ohci_mem_t* mem_virt = (ohci_mem_t*)phys_to_virt((uint64_t)mem_phys);
    memset(mem_virt, 0, sizeof(ohci_mem_t));

    if (num_ohci < MAX_OHCI_CONTROLLERS) {
        ohci_controllers[num_ohci] = mem_virt;
        ohci_regs[num_ohci] = regs;
        num_ohci++;
    }

    
    regs[0x08 / 4] = 1; 
    while (regs[0x08 / 4] & 1) dummy_mdelay(1);
    dummy_mdelay(2);

    
    regs[0x18 / 4] = (uint32_t)(uint64_t)mem_phys; 

    
    if ((regs[0x34 / 4] & 0x3FFF) == 0) {
        regs[0x34 / 4] = (regs[0x34 / 4] & 0x80000000) | 0x2778; 
    }

    
    
    regs[0x04 / 4] = 0x93;

    
    uint32_t rh_desc_a = regs[0x48 / 4];
    int num_ports = rh_desc_a & 0xFF;
    LOG_INFO("OHCI: Number of ports: %d", num_ports);

    int controller_index = usb_controller_count;
    if (usb_controller_count < MAX_USB_CONTROLLERS) {
        usb_controller_info_t* ctrl = &usb_controllers[usb_controller_count++];
        ctrl->type = USB_CTRL_OHCI;
        ctrl->pcie_bus = dev->bus;
        ctrl->pcie_dev = dev->dev;
        ctrl->pcie_func = dev->func;
        ctrl->version_major = rev & 0xFF;
        ctrl->version_minor = 0;
        ctrl->num_ports = (num_ports < MAX_USB_PORTS) ? num_ports : MAX_USB_PORTS;
        
        int addr_counter = 1;

        for (int i = 0; i < ctrl->num_ports; i++) {
            
            regs[0x54 / 4 + i] = (1 << 8); 
            dummy_mdelay(20);

            uint32_t port_status = regs[0x54 / 4 + i];
            int connected = (port_status & 1);
            ctrl->ports[i].port_number = i + 1;
            ctrl->ports[i].connected = connected;
            
            if (connected) {
                int is_low_speed = (port_status & (1 << 9)) ? 1 : 0;
                LOG_INFO("OHCI Port %d: Device connected. Speed: %s, Resetting...", i + 1, is_low_speed ? "Low" : "Full");
                
                
                regs[0x54 / 4 + i] = (1 << 4); 
                while (regs[0x54 / 4 + i] & (1 << 4)) dummy_mdelay(1);
                
                regs[0x54 / 4 + i] = (1 << 20); 
                dummy_mdelay(10); 
                
                
                mem_virt->setup_packet[i].bmRequestType = 0x00; 
                mem_virt->setup_packet[i].bRequest = 0x05;      
                mem_virt->setup_packet[i].wValue = addr_counter;
                mem_virt->setup_packet[i].wIndex = 0;
                mem_virt->setup_packet[i].wLength = 0;

                uint32_t td_setup_phys = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, setup_td[i]);
                uint32_t td_status_phys = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, status_td[i]);
                uint32_t td_dummy_phys = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, dummy_td[i]);
                
                
                mem_virt->dummy_td[i].config = 0;
                mem_virt->dummy_td[i].cbp = 0;
                mem_virt->dummy_td[i].next_td = 0;
                mem_virt->dummy_td[i].be = 0;

                
                mem_virt->status_td[i].config = (1 << 18) | (2 << 19) | (7 << 21) | (3 << 24) | (14 << 28);
                mem_virt->status_td[i].cbp = 0;
                mem_virt->status_td[i].next_td = td_dummy_phys;
                mem_virt->status_td[i].be = 0;

                
                mem_virt->setup_td[i].config = (0 << 19) | (7 << 21) | (2 << 24) | (14 << 28);
                mem_virt->setup_td[i].cbp = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, setup_packet[i]);
                mem_virt->setup_td[i].next_td = td_status_phys;
                mem_virt->setup_td[i].be = mem_virt->setup_td[i].cbp + 7;

                
                mem_virt->control_ed[i].config = (0 & 0x7F) | ((0 & 0xF) << 7) 
                                               | (is_low_speed ? (1 << 13) : 0)
                                               | (8 << 16);
                mem_virt->control_ed[i].tail_p = td_dummy_phys;
                mem_virt->control_ed[i].head_p = td_setup_phys;
                mem_virt->control_ed[i].next_ed = 0;

                
                
                regs[0x04 / 4] &= ~0x10; 
                regs[0x20 / 4] = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, control_ed[i]); 
                regs[0x04 / 4] |= 0x10; 
                regs[0x08 / 4] = 2; 

                
                int timeout = 500;
                while (((mem_virt->status_td[i].config >> 28) == 14) && timeout > 0) {
                    dummy_mdelay(1);
                    timeout--;
                }

                uint32_t cc = mem_virt->status_td[i].config >> 28;
                struct usb_dev* new_dev = usb_alloc_dev();
                if (new_dev) {
                    new_dev->address = addr_counter;
                    new_dev->port_number = i + 1;
                    new_dev->speed = is_low_speed ? 1 : 0;
                    new_dev->connected = 1;
                    new_dev->controller = &usb_controllers[controller_index];
                }
                
                if (timeout == 0 || (cc != 0 && cc != 14)) {
                    LOG_WARN("OHCI Port %d: SET_ADDRESS failed! CC=%d", i + 1, cc);
                } else {
                    LOG_INFO("OHCI Port %d: Enumerated successfully as ADDR %d.", i + 1, addr_counter);
                    
                    dummy_mdelay(20);

                    
                    regs[0x04 / 4] &= ~0x10; 

                    
                    mem_virt->desc_packet[i].bmRequestType = 0x80; 
                    mem_virt->desc_packet[i].bRequest = 0x06;      
                    mem_virt->desc_packet[i].wValue = 0x0100;      
                    mem_virt->desc_packet[i].wIndex = 0;           
                    mem_virt->desc_packet[i].wLength = sizeof(usb_device_descriptor_t); 

                    uint32_t desc_setup_phys = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, desc_setup_td[i]);
                    uint32_t desc_data_phys = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, desc_data_td[i]);
                    uint32_t desc_status_phys = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, desc_status_td[i]);
                    uint32_t desc_dummy_phys = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, desc_dummy_td[i]);

                    
                    mem_virt->desc_dummy_td[i].config = 0;
                    mem_virt->desc_dummy_td[i].cbp = 0;
                    mem_virt->desc_dummy_td[i].next_td = 0;
                    mem_virt->desc_dummy_td[i].be = 0;

                    
                    mem_virt->desc_status_td[i].config = (1 << 19) | (7 << 21) | (3 << 24) | (14 << 28);
                    mem_virt->desc_status_td[i].cbp = 0;
                    mem_virt->desc_status_td[i].next_td = desc_dummy_phys;
                    mem_virt->desc_status_td[i].be = 0;

                    
                    mem_virt->desc_data_td[i].config = (1 << 18) | (2 << 19) | (7 << 21) | (3 << 24) | (14 << 28);
                    mem_virt->desc_data_td[i].cbp = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, dev_desc[i]);
                    mem_virt->desc_data_td[i].next_td = desc_status_phys;
                    mem_virt->desc_data_td[i].be = mem_virt->desc_data_td[i].cbp + sizeof(usb_device_descriptor_t) - 1;

                    
                    mem_virt->desc_setup_td[i].config = (0 << 19) | (7 << 21) | (2 << 24) | (14 << 28);
                    mem_virt->desc_setup_td[i].cbp = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, desc_packet[i]);
                    mem_virt->desc_setup_td[i].next_td = desc_data_phys;
                    mem_virt->desc_setup_td[i].be = mem_virt->desc_setup_td[i].cbp + 7;

                    
                    mem_virt->desc_ed[i].config = (addr_counter & 0x7F) | ((0 & 0xF) << 7) 
                                                   | (is_low_speed ? (1 << 13) : 0)
                                                   | (8 << 16);
                    mem_virt->desc_ed[i].tail_p = desc_dummy_phys;
                    mem_virt->desc_ed[i].head_p = desc_setup_phys;
                    mem_virt->desc_ed[i].next_ed = 0;

                    regs[0x20 / 4] = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, desc_ed[i]); 
                    regs[0x04 / 4] |= 0x10; 
                    regs[0x08 / 4] = 2; 

                    timeout = 500;
                    while (((mem_virt->desc_status_td[i].config >> 28) == 14) && timeout > 0) {
                        dummy_mdelay(1);
                        timeout--;
                    }

                    cc = mem_virt->desc_status_td[i].config >> 28;
                    if (timeout == 0 || (cc != 0 && cc != 14)) {
                        LOG_WARN("OHCI Port %d: GET_DESCRIPTOR failed! CC=%d", i + 1, cc);
                    } else {
                        usb_device_descriptor_t* desc = &mem_virt->dev_desc[i];
                        new_dev->desc = *desc;
                        new_dev->desc_fetched = true;

                        LOG_INFO("OHCI Port %d: VID:0x%04x PID:0x%04x | Class:%d Sub:%d Protocol:%d | MaxPacket0:%d USB:%x.%02x", 
                            i + 1, desc->idVendor, desc->idProduct, desc->bDeviceClass, desc->bDeviceSubClass, 
                            desc->bDeviceProtocol, desc->bMaxPacketSize0, desc->bcdUSB >> 8, desc->bcdUSB & 0xFF);

                        
                        mem_virt->conf_packet[i].bmRequestType = 0x80;
                        mem_virt->conf_packet[i].bRequest = 0x06; 
                        mem_virt->conf_packet[i].wValue = 0x0200; 
                        mem_virt->conf_packet[i].wIndex = 0;
                        mem_virt->conf_packet[i].wLength = 256; 

                        uint32_t conf_setup_phys = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, conf_setup_td[i]);
                        uint32_t conf_data_phys = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, conf_data_td[i]);
                        uint32_t conf_status_phys = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, conf_status_td[i]);
                        uint32_t conf_dummy_phys = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, conf_dummy_td[i]);

                        mem_virt->conf_dummy_td[i].config = 0;
                        mem_virt->conf_dummy_td[i].cbp = 0;
                        mem_virt->conf_dummy_td[i].next_td = 0;
                        mem_virt->conf_dummy_td[i].be = 0;

                        mem_virt->conf_status_td[i].config = (1 << 19) | (7 << 21) | (3 << 24) | (14u << 28);
                        mem_virt->conf_status_td[i].cbp = 0;
                        mem_virt->conf_status_td[i].next_td = conf_dummy_phys;
                        mem_virt->conf_status_td[i].be = 0;

                        mem_virt->conf_data_td[i].config = (1 << 18) | (2 << 19) | (7 << 21) | (3 << 24) | (14u << 28);
                        mem_virt->conf_data_td[i].cbp = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, conf_desc[i]);
                        mem_virt->conf_data_td[i].next_td = conf_status_phys;
                        mem_virt->conf_data_td[i].be = mem_virt->conf_data_td[i].cbp + 256 - 1;

                        mem_virt->conf_setup_td[i].config = (0 << 19) | (7 << 21) | (2 << 24) | (14u << 28);
                        mem_virt->conf_setup_td[i].cbp = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, conf_packet[i]);
                        mem_virt->conf_setup_td[i].next_td = conf_data_phys;
                        mem_virt->conf_setup_td[i].be = mem_virt->conf_setup_td[i].cbp + 7;

                        mem_virt->conf_ed[i].config = (addr_counter & 0x7F) | (desc->bMaxPacketSize0 << 16) | (is_low_speed ? (1<<13) : 0);
                        mem_virt->conf_ed[i].tail_p = conf_dummy_phys;
                        mem_virt->conf_ed[i].head_p = conf_setup_phys;
                        mem_virt->conf_ed[i].next_ed = 0;

                        regs[0x04 / 4] &= ~0x10; 
                        regs[0x20 / 4] = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, conf_ed[i]); 
                        regs[0x04 / 4] |= 0x10; 
                        regs[0x08 / 4] = (1 << 1); 

                        LOG_INFO("OHCI Port %d: GET_CONFIGURATION requested. Waiting...", i+1);

                        timeout = 1000;
                        while ((mem_virt->conf_status_td[i].config >> 28) == 14 && timeout-- > 0) {
                            dummy_mdelay(1);
                        }

                        LOG_INFO("OHCI Port %d: TDs CC => setup=%d, data=%d, status=%d", i+1, mem_virt->conf_setup_td[i].config >> 28, mem_virt->conf_data_td[i].config >> 28, mem_virt->conf_status_td[i].config >> 28);
                        uint32_t cc = mem_virt->conf_status_td[i].config >> 28;
                        if (cc == 0 || cc == 14) {
                            int ep_addr = 0;
                            int ep_max_packet = 8;
                            bool is_kbd = false;
                            uint8_t* ptr = mem_virt->conf_desc[i];
                            uint16_t total_len = ptr[2] | (ptr[3] << 8);
                            LOG_INFO("OHCI Port %d: Config fetched. CC=%d, length=%d", i+1, cc, total_len);

                            if (total_len > 256) total_len = 256;
                            uint8_t* end_ptr = ptr + total_len;

                            while (ptr < end_ptr && ptr[0] > 0) {
                                if (ptr[1] == 4) { 
                                    LOG_INFO("  -> Interface: class=%d sub=%d proto=%d", ptr[5], ptr[6], ptr[7]);
                                    if (ptr[5] == 3 && ptr[6] == 1 && ptr[7] == 1) { 
                                        is_kbd = true;
                                        usb_dev_present_by_type(USB_DEV_TYPE_KEYBOARD);
                                    } else if (ptr[5] == 3 && ptr[6] == 1 && ptr[7] == 2) { 
                                        usb_dev_present_by_type(USB_DEV_TYPE_MOUSE);
                                    }
                                } else if (ptr[1] == 5 && is_kbd && ep_addr == 0) { 
                                    LOG_INFO("  -> Endpoint: addr=0x%x attr=%d maxpacket=%d", ptr[2], ptr[3], ptr[4] | (ptr[5] << 8));
                                    if ((ptr[2] & 0x80) && (ptr[3] & 3) == 3) { 
                                        ep_addr = ptr[2] & 0x0F;
                                        ep_max_packet = ptr[4] | (ptr[5] << 8);
                                    }
                                }
                                ptr += ptr[0];
                            }

                            if (is_kbd && ep_addr != 0) {
                                LOG_INFO("OHCI Port %d: HID Keyboard detected on EP %d!", i+1, ep_addr);

                                
                                mem_virt->set_conf_packet[i].bmRequestType = 0x00;
                                mem_virt->set_conf_packet[i].bRequest = 0x09; 
                                mem_virt->set_conf_packet[i].wValue = 1;      
                                mem_virt->set_conf_packet[i].wIndex = 0;
                                mem_virt->set_conf_packet[i].wLength = 0;

                                uint32_t set_conf_setup_phys = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, set_conf_setup_td[i]);
                                uint32_t set_conf_status_phys = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, set_conf_status_td[i]);
                                uint32_t set_conf_dummy_phys = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, set_conf_dummy_td[i]);

                                mem_virt->set_conf_dummy_td[i].config = 0;
                                mem_virt->set_conf_dummy_td[i].cbp = 0;
                                mem_virt->set_conf_dummy_td[i].next_td = 0;
                                mem_virt->set_conf_dummy_td[i].be = 0;

                                mem_virt->set_conf_status_td[i].config = (1 << 18) | (2 << 19) | (7 << 21) | (3 << 24) | (14u << 28);
                                mem_virt->set_conf_status_td[i].cbp = 0;
                                mem_virt->set_conf_status_td[i].next_td = set_conf_dummy_phys;
                                mem_virt->set_conf_status_td[i].be = 0;

                                mem_virt->set_conf_setup_td[i].config = (0 << 19) | (7 << 21) | (2 << 24) | (14u << 28);
                                mem_virt->set_conf_setup_td[i].cbp = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, set_conf_packet[i]);
                                mem_virt->set_conf_setup_td[i].next_td = set_conf_status_phys;
                                mem_virt->set_conf_setup_td[i].be = mem_virt->set_conf_setup_td[i].cbp + 7;

                                mem_virt->set_conf_ed[i].config = (addr_counter & 0x7F) | (desc->bMaxPacketSize0 << 16) | (is_low_speed ? (1<<13) : 0);
                                mem_virt->set_conf_ed[i].tail_p = set_conf_dummy_phys;
                                mem_virt->set_conf_ed[i].head_p = set_conf_setup_phys;
                                mem_virt->set_conf_ed[i].next_ed = 0;

                                regs[0x04 / 4] &= ~0x10; 
                                regs[0x20 / 4] = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, set_conf_ed[i]); 
                                regs[0x04 / 4] |= 0x10; 
                                regs[0x08 / 4] = (1 << 1); 

                                timeout = 1000;
                                while ((mem_virt->set_conf_status_td[i].config >> 28) == 14 && timeout-- > 0) {
                                    dummy_mdelay(1);
                                }

                                
                                uint32_t intr_phys = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, intr_td[i]);
                                uint32_t intr_dummy_phys = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, intr_dummy_td[i]);

                                mem_virt->intr_dummy_td[i].config = 0;
                                mem_virt->intr_dummy_td[i].cbp = 0;
                                mem_virt->intr_dummy_td[i].next_td = 0;
                                mem_virt->intr_dummy_td[i].be = 0;

                                mem_virt->intr_td[i].config = (0 << 24)  | (1 << 18) | (14u << 28) | (1 << 18) | (2 << 19) ;
                                mem_virt->intr_td[i].cbp = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, kbd_report[i]);
                                mem_virt->intr_td[i].next_td = intr_dummy_phys;
                                mem_virt->intr_td[i].be = mem_virt->intr_td[i].cbp + 7;

                                mem_virt->intr_ed[i].config = (addr_counter & 0x7F) | (ep_addr << 7) | (ep_max_packet << 16) | (is_low_speed ? (1<<13) : 0);
                                mem_virt->intr_ed[i].tail_p = intr_dummy_phys;
                                mem_virt->intr_ed[i].head_p = intr_phys;
                                mem_virt->intr_ed[i].next_ed = 0; 

                                mem_virt->hcca.intr_table[0] = (uint32_t)(uint64_t)mem_phys + offsetof(ohci_mem_t, intr_ed[i]);
                                regs[0x04 / 4] |= (1 << 2); 

                                mem_virt->is_kbd[i] = 1;
                                LOG_INFO("OHCI Port %d: Setup periodic polling on EP %d.", i+1, ep_addr);
                            }
                        }
                    }
                }

                vfs_node_t* dev_node = vfs_alloc_node();
                if (dev_node) {
                    dev_node->type = VFS_CHAR_DEVICE;
                    dev_node->ops = &usb_dev_ops;
                    dev_node->size = 0;
                    dev_node->device = new_dev;

                    char path_buffer[64];
                    strcpy(path_buffer, "usb/ohci/");
                    
                    char num_str[16];
                    __tool_int_to_str(addr_counter, 10, num_str);
                    strcat(path_buffer, num_str);

                    devfs_register_device(path_buffer, dev_node);
                    LOG_INFO("Registered USB device at /dev/%s", path_buffer);
                    addr_counter++;
                }

            } else {
                LOG_INFO("OHCI Port %d: No device", i + 1);
            }
        }
    }
}

void usb_kbd_handle_report(uint8_t report[8]);

void ohci_poll() {
    for (int c = 0; c < num_ohci; c++) {
        ohci_mem_t* mem_virt = ohci_controllers[c];
        
        for (int i = 0; i < 16; i++) {
            if (mem_virt->is_kbd[i]) {
                ohci_ed_t* ped = &mem_virt->intr_ed[i];
                ohci_td_t* ptd = &mem_virt->intr_td[i];

                uint32_t head_td = ped->head_p & ~0xF;
                if (head_td == ped->tail_p) {
                    
                    uint32_t cc = (ptd->config >> 28) & 0xF;
                    if (cc == 0) { 
                        
                        usb_kbd_handle_report(mem_virt->kbd_report[i]);
                    }

                    
                    uint32_t intr_dummy_phys = ped->tail_p; 

                    ptd->config = (0 ) | (1 << 18) | (14u << 28) | (1 << 18) | (2 << 19) ;
                    
                    
                    
                    
                    
                    
                    
                    uint64_t phys_base = (uint64_t)ohci_regs[c][0x18 / 4]; 
                    uint64_t cbpphys = phys_base + offsetof(ohci_mem_t, kbd_report[i]);

                    ptd->cbp = (uint32_t)cbpphys;
                    ptd->next_td = intr_dummy_phys;
                    ptd->be = ptd->cbp + 7;

                    uint32_t intr_td_phys = (uint32_t)(phys_base + offsetof(ohci_mem_t, intr_td[i]));

                    
                    ped->head_p = intr_td_phys | (ped->head_p & 3);
                }
            }
        }
    }
}
