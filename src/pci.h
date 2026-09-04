#ifndef PCI_H
#define PCI_H

#include <stdint.h>

/* PCI Configuration Space registers (offset from function's base address) */
#define PCI_REG_VENDOR_ID    0x00
#define PCI_REG_DEVICE_ID    0x02
#define PCI_REG_COMMAND      0x04
#define PCI_REG_STATUS       0x06
#define PCI_REG_REVISION     0x08
#define PCI_REG_CLASS        0x0B  /* class code high byte */
#define PCI_REG_HEADER_TYPE  0x0E
#define PCI_REG_BAR0         0x10
#define PCI_REG_BAR1         0x14
#define PCI_REG_BAR2         0x18
#define PCI_REG_BAR3         0x1C
#define PCI_REG_BAR4         0x20
#define PCI_REG_BAR5         0x24
#define PCI_REG_SUBSYS_VENDOR 0x2C
#define PCI_REG_SUBSYS_ID    0x2E
#define PCI_REG_INTERRUPT    0x3C

/* Command register bits */
#define PCI_CMD_IO_SPACE     0x01
#define PCI_CMD_MEM_SPACE    0x02
#define PCI_CMD_BUS_MASTER   0x04
#define PCI_CMD_INT_DISABLE  0x400

#define PCI_MAX_DEVICES  64

typedef struct {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  header_type;
    uint32_t bar[6];
    uint8_t  irq_line;
} pci_device_t;

/* Initialize: scan the PCI bus and find all devices. */
void pci_init(void);

/* Number of devices found. */
int  pci_device_count(void);

/* Get a device by index (0..pci_device_count()-1). */
const pci_device_t* pci_get_device(int index);

/* Read a 32-bit value from PCI configuration space. */
uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);

/* Write a 32-bit value to PCI configuration space. */
void     pci_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);

/* Read 16-bit and 8-bit helpers. */
uint16_t pci_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint8_t  pci_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);

/* Find the first device matching vendor:device_id. Returns NULL if not found. */
const pci_device_t* pci_find_device(uint16_t vendor_id, uint16_t device_id);

/* Find a device by class code. subclass = 0xFF to wildcard. */
const pci_device_t* pci_find_class(uint8_t class_code, uint8_t subclass);

/* Enable bus mastering and memory space for a device. */
void pci_enable_device(const pci_device_t* dev);

/* Get the BAR value for a given BAR index (0..5). */
uint32_t pci_get_bar(const pci_device_t* dev, int bar_index);

/* Get the IRQ line for a device. */
uint8_t pci_get_irq(const pci_device_t* dev);

#endif /* PCI_H */
