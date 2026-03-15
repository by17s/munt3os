#include "usb.h"
#include "ohci.h"
#include "ehci.h"
#include "xhci.h"
#include <log.h>
#include <printf.h>
#include <mem/kslab.h>

LOG_MODULE("usb");

usb_controller_info_t usb_controllers[MAX_USB_CONTROLLERS];
int usb_controller_count = 0;

static int64_t _usb_present_devices = 0;

static kmem_cache_t* usb_cache; 

struct usb_dev *usb_alloc_dev() {
    return (struct usb_dev *)kmem_cache_alloc(usb_cache);
}

void usb_free_dev(struct usb_dev *dev) {
    kmem_cache_free(usb_cache, (void*)dev);
}

int usb_dev_present_by_type(int64_t dev_type) {
    _usb_present_devices |= (1 << dev_type);
    return _usb_present_devices;
}

int usb_dev_present(int64_t dev_type) {
    return _usb_present_devices & (1 << dev_type);
}

void usb_poll(void) {
    ohci_poll();
    ehci_poll();
    xhci_poll();
}

void usb_polling_thread(void* arg) {
    (void)arg;
    LOG_INFO("USB Polling thread started");
    while (1) {
        usb_poll();
        
        for (volatile int i = 0; i < 1000000; i++);
    }
}

void usb_init(void) {
    LOG_INFO("Initializing USB Controllers from PCIe...");

    usb_cache = kmem_cache_create("usb_dev_cache", sizeof(struct usb_dev));
    if (!usb_cache) {
        LOG_ERROR("USB: Failed to create USB device cache");
        return;
    }

    for (int i = 0; i < pcie_device_count; i++) {
        pcie_device_info_t* dev = &pcie_devices[i];
        


        
        
        if (dev->header.class_code == 0x0C && dev->header.subclass == 0x03) {
            
            
            if (dev->header.prog_if == 0x10) {
                LOG_INFO("Found OHCI Controller at %x:%x.%d", dev->bus, dev->dev, dev->func);
                ohci_init(dev);
            }
            
            else if (dev->header.prog_if == 0x20) {
                LOG_INFO("Found EHCI Controller at %x:%x.%d", dev->bus, dev->dev, dev->func);
                ehci_init(dev);
            }

            
            else if (dev->header.prog_if == 0x30) {
                LOG_INFO("Found xHCI Controller at %x:%x.%d", dev->bus, dev->dev, dev->func);
                xhci_init(dev);
            }
        }
    }
}