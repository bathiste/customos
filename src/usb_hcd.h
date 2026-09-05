#ifndef USB_HCD_H
#define USB_HCD_H

#include <stdint.h>
#include <stddef.h>

/*
 * USB Host Controller Driver (UHCI) + USB core layer for CustomOS.
 *
 * Initialises the UHCI host controller found on the PCI bus, enumerates
 * attached devices, and provides control / bulk transfer primitives.
 * Enough to discover a USB WiFi dongle (e.g. Realtek RTL8188EU) and
 * send it commands.  Full OHCI/EHCI/xHCI support is out of scope; UHCI
 * covers the bulk of low-cost USB WiFi dongles that QEMU emulates.
 */

/* USB standard request types and descriptors. */
#define USB_REQ_GET_STATUS        0
#define USB_REQ_CLEAR_FEATURE     1
#define USB_REQ_SET_FEATURE       3
#define USB_REQ_SET_ADDRESS       5
#define USB_REQ_GET_DESCRIPTOR    6
#define USB_REQ_SET_DESCRIPTOR    7
#define USB_REQ_GET_CONFIGURATION 8
#define USB_REQ_SET_CONFIGURATION 9
#define USB_REQ_GET_INTERFACE     10
#define USB_REQ_SET_INTERFACE     11

#define USB_DESC_DEVICE           1
#define USB_DESC_CONFIGURATION    2
#define USB_DESC_STRING           3
#define USB_DESC_INTERFACE        4
#define USB_DESC_ENDPOINT         5

#define USB_REQ_DIR_OUT           0x00
#define USB_REQ_DIR_IN            0x80
#define USB_REQ_TYPE_STANDARD     0x00
#define USB_REQ_TYPE_CLASS        0x20
#define USB_REQ_TYPE_VENDOR       0x40
#define USB_REQ_RCPT_DEVICE       0x00
#define USB_REQ_RCPT_INTERFACE    0x01
#define USB_REQ_RCPT_ENDPOINT     0x02

#define USB_CLASS_PER_INTERFACE   0
#define USB_CLASS_COMMUNICATIONS  0x02
#define USB_CLASS_HUB             0x09
#define USB_CLASS_MISC            0xEF
#define USB_CLASS_WIRELESS        0xE0

#define USB_MAX_DEVICES           16
#define USB_MAX_ENDPOINTS         4

typedef struct __attribute__((packed)) {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_setup_packet_t;

typedef struct __attribute__((packed)) {
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
} usb_device_descriptor_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} usb_interface_descriptor_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} usb_config_descriptor_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} usb_endpoint_descriptor_t;

#define USB_EP_ADDR_OUT   0x00
#define USB_EP_ADDR_IN    0x80
#define USB_EP_TYPE_CTRL  0x00
#define USB_EP_TYPE_ISO   0x01
#define USB_EP_TYPE_BULK  0x02
#define USB_EP_TYPE_INT   0x03

typedef struct {
    uint8_t  address;
    uint8_t  config;
    uint8_t  iface;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  protocol;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t  max_packet_size;
    uint8_t  port;
    uint8_t  ep_in[USB_MAX_ENDPOINTS];
    uint8_t  ep_out[USB_MAX_ENDPOINTS];
    uint16_t ep_maxpkt[USB_MAX_ENDPOINTS];
} usb_device_t;

/* Public API. */
void usb_init(void);
int  usb_hcd_present(void);
int  usb_device_count(void);
const usb_device_t* usb_get_device(int index);
const usb_device_t* usb_find_device(uint16_t vendor, uint16_t product);
const usb_device_t* usb_find_class(uint8_t class_code, uint8_t subclass);

int usb_control_transfer(const usb_device_t* dev,
                         uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index,
                         void* data, uint16_t length);

int usb_get_descriptor(const usb_device_t* dev, uint8_t type, uint8_t index,
                       void* buf, uint16_t length);
int usb_set_address(const usb_device_t* dev, uint8_t address);
int usb_set_configuration(const usb_device_t* dev, uint8_t config);

int usb_bulk_transfer(const usb_device_t* dev, uint8_t endpoint,
                      void* data, int length);

void usb_poll(void);

#endif /* USB_HCD_H */
