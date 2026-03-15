#include "ehci.h"
#include "usb.h"
#include <log.h>
#include "../../fs/devfs.h"
#include "../../fs/vfs.h"

LOG_MODULE("ehci")

extern void* phys_to_virt(uint64_t phys_addr);

static uint32_t pcie_read_bar(pcie_device_info_t* dev, int bar_index) {
    uint32_t* bars = (uint32_t*)(dev->ecam_base + 0x10);
    return bars[bar_index];
}

void ehci_poll(void) {
    
}

void ehci_init(pcie_device_info_t* dev) {
    uint32_t bar0 = pcie_read_bar(dev, 0);
    if (!bar0) {
        LOG_WARN("EHCI: Invalid BAR0");
        return;
    }

    uint64_t mmio_phys = bar0 & 0xFFFFFFF0;
    uint8_t* cap_regs = (uint8_t*)phys_to_virt(mmio_phys);
    
    uint8_t cap_length = cap_regs[0];
    uint16_t hci_version = *(uint16_t*)(cap_regs + 2);
    uint32_t hcsparams = *(uint32_t*)(cap_regs + 4);

    int num_ports = hcsparams & 0x0F;
    LOG_INFO("EHCI Controller Version: %x.%02x", hci_version >> 8, hci_version & 0xFF);
    LOG_INFO("EHCI: Number of ports: %d", num_ports);

    uint8_t* op_regs = cap_regs + cap_length;
    
    uint32_t* portsc = (uint32_t*)(op_regs + 0x44);

    if (usb_controller_count < MAX_USB_CONTROLLERS) {
        usb_controller_info_t* ctrl = &usb_controllers[usb_controller_count++];
        ctrl->type = USB_CTRL_EHCI;
        ctrl->pcie_bus = dev->bus;
        ctrl->pcie_dev = dev->dev;
        ctrl->pcie_func = dev->func;
        ctrl->version_major = hci_version >> 8;
        ctrl->version_minor = hci_version & 0xFF;
        ctrl->num_ports = (num_ports < MAX_USB_PORTS) ? num_ports : MAX_USB_PORTS;

        for (int i = 0; i < ctrl->num_ports; i++) {
            uint32_t status = portsc[i];
            int connected = (status & 1);
            ctrl->ports[i].port_number = i + 1;
            ctrl->ports[i].connected = connected;
            LOG_INFO("EHCI Port %d: %s", i + 1, connected ? "Device connected" : "No device");
        }

        
        vfs_node_t* dev_dir = vfs_alloc_node();
        if (dev_dir) {
            dev_dir->type = VFS_DIRECTORY;
            dev_dir->ops = NULL; 
            dev_dir->size = 0;
            dev_dir->device = NULL;
            devfs_register_device("usb/ehci", dev_dir);
        }

    } else {
        for (int i = 0; i < num_ports; i++) {
            uint32_t status = portsc[i];
            int connected = (status & 1);
            LOG_INFO("EHCI Port %d: %s", i + 1, connected ? "Device connected" : "No device");
        }
    }
}
