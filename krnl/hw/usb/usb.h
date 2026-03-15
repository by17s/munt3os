#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "../pcie.h"

#define MAX_USB_CONTROLLERS 8
#define MAX_USB_PORTS 16

#define USB_DEV_TYPE_KEYBOARD (1 << 0)
#define USB_DEV_TYPE_MOUSE    (1 << 1)
#define USB_DEV_TYPE_STORAGE  (1 << 2)
#define USB_DEV_TYPE_HID      (1 << 3) 
#define USB_DEV_TYPE_AUDIO    (1 << 4)
#define USB_DEV_TYPE_NETWORK  (1 << 5)

void usb_init(void);
void usb_poll(void);
void usb_polling_thread(void* arg);

typedef enum {
    USB_CTRL_OHCI,
    USB_CTRL_UHCI,
    USB_CTRL_EHCI,
    USB_CTRL_xHCI
} usb_controller_type_t;

typedef struct {
    int port_number;
    bool connected;
} usb_port_info_t;

typedef struct {
    usb_controller_type_t type;
    int pcie_bus;
    int pcie_dev;
    int pcie_func;
    uint32_t version_major;
    uint32_t version_minor;
    int num_ports;
    usb_port_info_t ports[MAX_USB_PORTS];
} usb_controller_info_t;

typedef struct {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_packet_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType; 
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed)) usb_device_descriptor_t;

struct usb_dev {
    int64_t address;
    int port_number;
    int speed; 
    int connected;

    usb_device_descriptor_t desc;
    bool desc_fetched;
    
    usb_controller_info_t* controller;
    
};

extern usb_controller_info_t usb_controllers[MAX_USB_CONTROLLERS];
extern int usb_controller_count;

struct usb_dev *usb_alloc_dev();
void usb_free_dev(struct usb_dev *dev);

int usb_dev_present_by_type(int64_t dev_type);
int usb_dev_present(int64_t dev_type);
