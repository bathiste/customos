#include "pci.h"
#include "io.h"

#define PCI_CONFIG_ADDR  0xCF8
#define PCI_CONFIG_DATA  0xCFC

/* Internal device table */
static pci_device_t g_devices[PCI_MAX_DEVICES];
static int          g_device_count = 0;

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t val;
    __asm__ volatile ("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = (uint32_t)((1 << 31) |
                                  ((uint32_t)bus      << 16) |
                                  ((uint32_t)device   << 11) |
                                  ((uint32_t)function <<  8) |
                                  (offset & 0xFC));
    outl(PCI_CONFIG_ADDR, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)((1 << 31) |
                                  ((uint32_t)bus      << 16) |
                                  ((uint32_t)device   << 11) |
                                  ((uint32_t)function <<  8) |
                                  (offset & 0xFC));
    outl(PCI_CONFIG_ADDR, address);
    outl(PCI_CONFIG_DATA, value);
}

uint16_t pci_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t v = pci_read32(bus, device, function, offset & 0xFC);
    return (uint16_t)((v >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t v = pci_read32(bus, device, function, offset & 0xFC);
    return (uint8_t)((v >> ((offset & 3) * 8)) & 0xFF);
}

static int has_function(uint8_t bus, uint8_t device) {
    /* If bit 7 of header type is set, device is multi-function */
    uint8_t h = pci_read8(bus, device, 0, PCI_REG_HEADER_TYPE);
    return (h & 0x80) != 0;
}

static void scan_device(uint8_t bus, uint8_t device, int function) {
    uint16_t vendor = pci_read16(bus, device, function, PCI_REG_VENDOR_ID);
    if (vendor == 0xFFFF) return;

    if (g_device_count >= PCI_MAX_DEVICES) return;

    pci_device_t* d = &g_devices[g_device_count++];
    d->bus      = bus;
    d->device   = device;
    d->function = function;
    d->vendor_id = vendor;
    d->device_id = pci_read16(bus, device, function, PCI_REG_DEVICE_ID);
    d->class_code = pci_read8(bus, device, function, PCI_REG_CLASS);
    d->subclass   = pci_read8(bus, device, function, PCI_REG_CLASS + 1);
    d->prog_if    = pci_read8(bus, device, function, PCI_REG_CLASS + 2);
    d->header_type = pci_read8(bus, device, function, PCI_REG_HEADER_TYPE);
    d->irq_line   = pci_read8(bus, device, function, PCI_REG_INTERRUPT);

    int i;
    for (i = 0; i < 6; i++) {
        d->bar[i] = pci_read32(bus, device, function, PCI_REG_BAR0 + i * 4);
    }
}

void pci_init(void) {
    g_device_count = 0;
    int i;

    for (i = 0; i < PCI_MAX_DEVICES; i++) g_devices[i].vendor_id = 0xFFFF;

    /* PCI-to-PCI bridges are not handled - scan bus 0 only (sufficient for QEMU) */
    for (uint8_t bus = 0; bus < 1; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            uint16_t vendor = pci_read16(bus, device, 0, PCI_REG_VENDOR_ID);
            if (vendor == 0xFFFF) continue;

            scan_device(bus, device, 0);
            if (has_function(bus, device)) {
                for (uint8_t function = 1; function < 8; function++) {
                    scan_device(bus, device, function);
                }
            }
        }
    }
}

int pci_device_count(void) { return g_device_count; }

const pci_device_t* pci_get_device(int index) {
    if (index < 0 || index >= g_device_count) return (const pci_device_t*)0;
    return &g_devices[index];
}

const pci_device_t* pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    int i;
    for (i = 0; i < g_device_count; i++) {
        if (g_devices[i].vendor_id == vendor_id && g_devices[i].device_id == device_id) {
            return &g_devices[i];
        }
    }
    return (const pci_device_t*)0;
}

const pci_device_t* pci_find_class(uint8_t class_code, uint8_t subclass) {
    int i;
    for (i = 0; i < g_device_count; i++) {
        if (g_devices[i].class_code == class_code &&
            (subclass == 0xFF || g_devices[i].subclass == subclass)) {
            return &g_devices[i];
        }
    }
    return (const pci_device_t*)0;
}

void pci_enable_device(const pci_device_t* dev) {
    if (!dev) return;
    uint32_t cmd = pci_read32(dev->bus, dev->device, dev->function, PCI_REG_COMMAND);
    cmd |= PCI_CMD_MEM_SPACE | PCI_CMD_BUS_MASTER;
    cmd &= ~PCI_CMD_INT_DISABLE;
    pci_write32(dev->bus, dev->device, dev->function, PCI_REG_COMMAND, cmd);
}

uint32_t pci_get_bar(const pci_device_t* dev, int bar_index) {
    if (!dev || bar_index < 0 || bar_index > 5) return 0;
    return dev->bar[bar_index];
}

uint8_t pci_get_irq(const pci_device_t* dev) {
    if (!dev) return 0;
    return dev->irq_line;
}
