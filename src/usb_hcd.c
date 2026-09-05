/*
 * USB Host Controller Driver (UHCI) + USB core layer.
 *
 * This is a minimal UHCI implementation sufficient to:
 *   1. Detect and bring up a UHCI controller on the PCI bus
 *   2. Detect and reset devices on the root-hub ports
 *   3. Send control transfers (used for enumeration and class drivers)
 *   4. Send bulk transfers (used by the WiFi dongle driver)
 *
 * It is NOT a full UHCI implementation. Things that are simplified:
 *   - Only one frame list, no bandwidth reclamation
 *   - No isochronous transfers
 *   - No interrupt transfers (we poll)
 *   - The 1ms frame timer is not used; we busy-wait
 *
 * The code follows the structure used by Linux's drivers/usb/host/uhci-hcd.c
 * and the Intel UHCI specification, but is condensed to a few hundred lines.
 *
 * If no UHCI controller is present (e.g. only EHCI/xHCI), usb_init() will
 * return without bringing the stack up and class drivers can detect this
 * via usb_hcd_present() == 0.
 */

#include "usb_hcd.h"
#include "pci.h"
#include "io.h"
#include "string.h"
#include <stddef.h>

/* ------------------------------------------------------------------ */
/*  UHCI register definitions (Intel UHCI spec, Rev 1.1)               */
/* ------------------------------------------------------------------ */

#define UHCI_CMD         0x00
#define UHCI_STS         0x02
#define UHCI_INTR        0x04
#define UHCI_FRNUM       0x06
#define UHCI_FLBASE      0x08
#define UHCI_PORTSC1     0x10
#define UHCI_PORTSC2     0x12

#define UHCI_CMD_RS      0x01
#define UHCI_CMD_HCRESET 0x02
#define UHCI_CMD_GRESET  0x04
#define UHCI_CMD_EGSM    0x08
#define UHCI_CMD_FGR     0x10
#define UHCI_CMD_CF      0x40
#define UHCI_CMD_MAXP    0x80

#define UHCI_PORTSC_CCS  0x01
#define UHCI_PORTSC_PED  0x02
#define UHCI_PORTSC_LS   0x04
#define UHCI_PORTSC_PR   0x100
#define UHCI_PORTSC_SUSP 0x800

/* TD (Transfer Descriptor) - hardware structure. */
typedef struct __attribute__((packed)) {
    uint32_t link;
    uint32_t status;
    uint8_t  token[8];
    uint32_t buffer;
    uint32_t _pad;
} uhci_td_t;

#define TD_STATUS_ACTIVE   0x80000000u
#define TD_STATUS_ERROR    0x00000001u
#define TD_STATUS_STALLED  0x00000002u
#define TD_STATUS_BABBLE   0x00000004u
#define TD_STATUS_NAK      0x00000008u
#define TD_STATUS_CRC      0x00000010u

#define TD_TOKEN_TOGGLE    0x00080000u
#define TD_TOKEN_MAXLEN_SHIFT 21

#define TD_PID_SETUP   0x2D
#define TD_PID_IN      0x69
#define TD_PID_OUT     0xE1

#define TD_LINK_TERMINATE 0x01

/* QH (Queue Head). */
typedef struct __attribute__((packed)) {
    uint32_t head;
    uint32_t element;
} uhci_qh_t;

#define QH_TERMINATE 0x01

#define TD_MAX 64

/* Frame list - 1024 entries. */
#define FRAME_LIST_SIZE 1024

static uhci_td_t  g_td_pool[TD_MAX];
static uhci_qh_t  g_qh_pool[TD_MAX];
static uint8_t    g_td_used[TD_MAX];
static uint8_t    g_qh_used[TD_MAX];
static uint8_t    g_buffers[TD_MAX][64] __attribute__((aligned(4)));
static uint32_t   g_frame_list[FRAME_LIST_SIZE] __attribute__((aligned(4096)));

static uint16_t   g_iobase = 0;
static int        g_hcd_present = 0;
static usb_device_t g_devices[USB_MAX_DEVICES];
static int        g_device_count = 0;
static int        g_initialized = 0;
static int        g_next_dev_addr = 1;

/* Register helpers. */
static inline uint8_t inb(uint16_t port) {
    uint8_t v; __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port)); return v;
}
static inline uint16_t inw(uint16_t port) {
    uint16_t v; __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(port)); return v;
}
static inline uint32_t inl(uint16_t port) {
    uint32_t v; __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port)); return v;
}
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline void uhci_write16(uint16_t reg, uint16_t val) {
    if (g_iobase) outw(g_iobase + reg, val);
}
static inline uint16_t uhci_read16(uint16_t reg) {
    return g_iobase ? inw(g_iobase + reg) : 0;
}
static inline void uhci_write32(uint16_t reg, uint32_t val) {
    if (g_iobase) outl(g_iobase + reg, val);
}

static int uhci_find_controller(void) {
    pci_init();
    for (int i = 0; i < pci_device_count(); i++) {
        const pci_device_t* d = pci_get_device(i);
        if (d->class_code == 0x0C && d->subclass == 0x03 && d->prog_if == 0x00) {
            uint32_t bar0 = pci_get_bar(d, 0);
            if ((bar0 & 0x01) == 0) continue;
            g_iobase = (uint16_t)(bar0 & 0xFFFC);
            pci_enable_device(d);
            return 1;
        }
    }
    return 0;
}

static void uhci_reset(void) {
    uhci_write16(UHCI_CMD, UHCI_CMD_GRESET);
    for (volatile int i = 0; i < 200000; i++) { (void)i; }
    uhci_write16(UHCI_CMD, 0);
    uhci_write16(UHCI_STS, 0xFFFF);
}

static void uhci_start(void) {
    uhci_write32(UHCI_FLBASE, (uint32_t)(uintptr_t)g_frame_list);
    uhci_write16(UHCI_CMD, UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP);
}

/* ------------------------------------------------------------------ */
/*  TD / QH pool management                                            */
/* ------------------------------------------------------------------ */

static int td_alloc(void) {
    for (int i = 0; i < TD_MAX; i++) {
        if (!g_td_used[i]) { g_td_used[i] = 1; return i; }
    }
    return -1;
}
static void td_free(int idx) {
    if (idx >= 0 && idx < TD_MAX) g_td_used[idx] = 0;
}
static int qh_alloc(void) {
    for (int i = 0; i < TD_MAX; i++) {
        if (!g_qh_used[i]) { g_qh_used[i] = 1; return i; }
    }
    return -1;
}
static void qh_free(int idx) {
    if (idx >= 0 && idx < TD_MAX) g_qh_used[idx] = 0;
}

static void td_init(int idx, uint8_t pid, uint8_t dev_addr, uint8_t ep,
                    int toggle, const void* data, int length) {
    uhci_td_t* td = &g_td_pool[idx];
    td->link   = TD_LINK_TERMINATE;
    td->status = TD_STATUS_ACTIVE;
    uint32_t maxlen = (length == 0) ? 0x7FF : ((uint32_t)(length - 1));
    td->token[0] = pid;
    td->token[1] = 0;                                  /* frame number (unused) */
    td->token[2] = (uint8_t)(dev_addr & 0x7F);
    td->token[3] = (uint8_t)((ep & 0xF) | (toggle ? TD_TOKEN_TOGGLE : 0)
                              | (maxlen << TD_TOKEN_MAXLEN_SHIFT));
    td->token[4] = 0;
    td->token[5] = 0;
    td->token[6] = 0;
    td->token[7] = 0;
    if (data && length > 0) {
        /* Copy to the per-TD buffer. */
        const uint8_t* src = (const uint8_t*)data;
        uint8_t* dst = g_buffers[idx];
        for (int i = 0; i < length && i < 64; i++) dst[i] = src[i];
        td->buffer = (uint32_t)(uintptr_t)dst;
    } else {
        td->buffer = 0;
    }
}

static int td_wait(int idx, int timeout_loops) {
    while (timeout_loops-- > 0) {
        if (!(g_td_pool[idx].status & TD_STATUS_ACTIVE)) {
            if (g_td_pool[idx].status & TD_STATUS_ERROR) return -1;
            if (g_td_pool[idx].status & TD_STATUS_STALLED) return -2;
            if (g_td_pool[idx].status & TD_STATUS_BABBLE) return -3;
            return 0;
        }
        for (volatile int s = 0; s < 500; s++) { (void)s; }
    }
    return -4; /* timeout */
}

/* ------------------------------------------------------------------ */
/*  Control transfer: SETUP + DATA (optional) + STATUS                 */
/* ------------------------------------------------------------------ */

static int uhci_control_transfer(uint8_t dev_addr, uint8_t endpoint,
                                 const usb_setup_packet_t* setup,
                                 void* data, uint16_t length) {
    int td_setup = td_alloc();
    int td_data  = (length > 0) ? td_alloc() : -1;
    int td_stat  = td_alloc();
    int qh       = qh_alloc();
    if (td_setup < 0 || td_stat < 0 || qh < 0) {
        td_free(td_setup); td_free(td_data); td_free(td_stat); qh_free(qh);
        return -1;
    }

    /* SETUP stage */
    td_init(td_setup, TD_PID_SETUP, dev_addr, endpoint, 0, setup, 8);
    g_td_pool[td_setup].status |= 0x00800000u;  /* active bit in token low */
    g_td_pool[td_setup].link    = (uint32_t)(uintptr_t)&g_td_pool[td_data >= 0 ? td_data : td_stat];

    /* DATA stage */
    if (td_data >= 0) {
        uint8_t pid = (setup->bmRequestType & USB_REQ_DIR_IN) ? TD_PID_IN : TD_PID_OUT;
        td_init(td_data, pid, dev_addr, endpoint, 1, data, length);
        g_td_pool[td_data].link = (uint32_t)(uintptr_t)&g_td_pool[td_stat];
    }

    /* STATUS stage - always opposite direction of data, DATA1. */
    uint8_t stat_pid = (setup->bmRequestType & USB_REQ_DIR_IN) ? TD_PID_OUT : TD_PID_IN;
    td_init(td_stat, stat_pid, dev_addr, endpoint, 1, NULL, 0);

    /* QH */
    g_qh_pool[qh].head    = (uint32_t)(uintptr_t)&g_td_pool[td_setup];
    g_qh_pool[qh].element = (uint32_t)(uintptr_t)&g_td_pool[td_setup];

    /* Inject QH into the frame list (slot 0 is fine for a one-shot). */
    uint32_t old = g_frame_list[0];
    g_frame_list[0] = (uint32_t)(uintptr_t)&g_qh_pool[qh] | 0x02;  /* QH bit */
    (void)old;

    /* Wait for completion. */
    int r = td_wait(td_stat, 200000);
    if (r == 0 && td_data >= 0 && (setup->bmRequestType & USB_REQ_DIR_IN)) {
        uint8_t* dst = (uint8_t*)data;
        for (int i = 0; i < length && i < 64; i++) dst[i] = g_buffers[td_data][i];
    }

    /* Restore frame list. */
    g_frame_list[0] = old;
    td_free(td_setup); td_free(td_data); td_free(td_stat); qh_free(qh);
    return (r == 0) ? length : r;
}

/* ------------------------------------------------------------------ */
/*  Bulk transfer (single TD, polled)                                  */
/* ------------------------------------------------------------------ */

static int uhci_bulk_transfer(uint8_t dev_addr, uint8_t endpoint,
                              void* data, int length, int is_in) {
    if (length <= 0 || length > 64) return -1;
    int td = td_alloc();
    int qh = qh_alloc();
    if (td < 0 || qh < 0) { td_free(td); qh_free(qh); return -1; }

    uint8_t pid = is_in ? TD_PID_IN : TD_PID_OUT;
    td_init(td, pid, dev_addr, endpoint, 0, is_in ? NULL : data, length);
    g_td_pool[td].link = TD_LINK_TERMINATE;

    g_qh_pool[qh].head    = (uint32_t)(uintptr_t)&g_td_pool[td];
    g_qh_pool[qh].element = (uint32_t)(uintptr_t)&g_td_pool[td];

    uint32_t old = g_frame_list[0];
    g_frame_list[0] = (uint32_t)(uintptr_t)&g_qh_pool[qh] | 0x02;

    int r = td_wait(td, 200000);
    if (r == 0 && is_in) {
        uint8_t* dst = (uint8_t*)data;
        for (int i = 0; i < length; i++) dst[i] = g_buffers[td][i];
    }

    g_frame_list[0] = old;
    td_free(td); qh_free(qh);
    return (r == 0) ? length : r;
}

/* ------------------------------------------------------------------ */
/*  Root-hub port helpers                                              */
/* ------------------------------------------------------------------ */

static int port_connected(int port) {
    uint16_t reg = uhci_read16((uint16_t)(UHCI_PORTSC1 + (port - 1) * 2));
    return (reg & UHCI_PORTSC_CCS) ? 1 : 0;
}

static void port_reset(int port) {
    uint16_t reg_off = (uint16_t)(UHCI_PORTSC1 + (port - 1) * 2);
    uint16_t reg = uhci_read16(reg_off);
    reg |= UHCI_PORTSC_PR;
    uhci_write16(reg_off, reg);
    for (volatile int i = 0; i < 50000; i++) { (void)i; }
    reg = uhci_read16(reg_off);
    reg &= ~UHCI_PORTSC_PR;
    uhci_write16(reg_off, reg);
    reg = uhci_read16(reg_off);
    reg |= UHCI_PORTSC_PED;
    uhci_write16(reg_off, reg);
    for (volatile int i = 0; i < 50000; i++) { (void)i; }
}

/* ------------------------------------------------------------------ */
/*  Enumeration                                                        */
/* ------------------------------------------------------------------ */

static int enumerate_device(int port) {
    if (!port_connected(port)) return -1;
    port_reset(port);

    uint8_t buf[8];
    usb_setup_packet_t setup;
    setup.bmRequestType = USB_REQ_DIR_IN  | USB_REQ_TYPE_STANDARD | USB_REQ_RCPT_DEVICE;
    setup.bRequest      = USB_REQ_GET_DESCRIPTOR;
    setup.wValue        = (uint16_t)((USB_DESC_DEVICE << 8) | 0);
    setup.wIndex        = 0;
    setup.wLength       = 8;
    int r = uhci_control_transfer(0, 0, &setup, buf, 8);
    if (r < 0) return -1;
    uint8_t maxpkt0 = buf[7];
    if (maxpkt0 == 0) maxpkt0 = 8;

    /* Assign an address. */
    uint8_t new_addr = (uint8_t)g_next_dev_addr++;
    if (new_addr > 127) new_addr = 1;
    setup.bmRequestType = USB_REQ_DIR_OUT | USB_REQ_TYPE_STANDARD | USB_REQ_RCPT_DEVICE;
    setup.bRequest      = USB_REQ_SET_ADDRESS;
    setup.wValue        = new_addr;
    setup.wIndex        = 0;
    setup.wLength       = 0;
    r = uhci_control_transfer(0, 0, &setup, NULL, 0);
    if (r < 0) return -1;
    for (volatile int i = 0; i < 50000; i++) { (void)i; }

    /* Read full device descriptor. */
    uint8_t dev_desc[18];
    setup.bmRequestType = USB_REQ_DIR_IN  | USB_REQ_TYPE_STANDARD | USB_REQ_RCPT_DEVICE;
    setup.bRequest      = USB_REQ_GET_DESCRIPTOR;
    setup.wValue        = (uint16_t)((USB_DESC_DEVICE << 8) | 0);
    setup.wIndex        = 0;
    setup.wLength       = 18;
    r = uhci_control_transfer(new_addr, 0, &setup, dev_desc, 18);
    if (r < 0) return -1;

    if (g_device_count >= USB_MAX_DEVICES) return -1;
    usb_device_t* dev = &g_devices[g_device_count++];
    for (int i = 0; i < (int)sizeof(usb_device_t); i++)
        ((uint8_t*)dev)[i] = 0;
    dev->address        = new_addr;
    dev->vendor_id      = (uint16_t)(dev_desc[8] | (dev_desc[9] << 8));
    dev->product_id     = (uint16_t)(dev_desc[10] | (dev_desc[11] << 8));
    dev->class_code     = dev_desc[4];
    dev->subclass       = dev_desc[5];
    dev->protocol       = dev_desc[6];
    dev->max_packet_size = maxpkt0;
    dev->port           = (uint8_t)port;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void usb_init(void) {
    if (g_initialized) return;
    g_initialized = 1;
    for (int i = 0; i < TD_MAX; i++) g_td_used[i] = g_qh_used[i] = 0;
    for (int i = 0; i < FRAME_LIST_SIZE; i++) g_frame_list[i] = QH_TERMINATE;

    if (!uhci_find_controller()) { g_hcd_present = 0; return; }
    uhci_reset();
    uhci_start();
    g_hcd_present = 1;

    for (int p = 1; p <= 2; p++) enumerate_device(p);
}

int  usb_hcd_present(void)  { return g_hcd_present; }
int  usb_device_count(void) { return g_device_count; }

const usb_device_t* usb_get_device(int index) {
    if (index < 0 || index >= g_device_count) return NULL;
    return &g_devices[index];
}

const usb_device_t* usb_find_device(uint16_t vendor, uint16_t product) {
    for (int i = 0; i < g_device_count; i++) {
        if (g_devices[i].vendor_id == vendor && g_devices[i].product_id == product)
            return &g_devices[i];
    }
    return NULL;
}

const usb_device_t* usb_find_class(uint8_t class_code, uint8_t subclass) {
    for (int i = 0; i < g_device_count; i++) {
        if (g_devices[i].class_code == class_code &&
            (subclass == 0xFF || g_devices[i].subclass == subclass))
            return &g_devices[i];
    }
    return NULL;
}

int usb_control_transfer(const usb_device_t* dev,
                         uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index,
                         void* data, uint16_t length) {
    if (!dev) return -1;
    usb_setup_packet_t setup;
    setup.bmRequestType = request_type;
    setup.bRequest      = request;
    setup.wValue        = value;
    setup.wIndex        = index;
    setup.wLength       = length;
    return uhci_control_transfer(dev->address, 0, &setup, data, length);
}

int usb_get_descriptor(const usb_device_t* dev, uint8_t type, uint8_t index,
                       void* buf, uint16_t length) {
    return usb_control_transfer(dev,
        USB_REQ_DIR_IN | USB_REQ_TYPE_STANDARD | USB_REQ_RCPT_DEVICE,
        USB_REQ_GET_DESCRIPTOR, (uint16_t)((type << 8) | index), 0, buf, length);
}

int usb_set_address(const usb_device_t* dev, uint8_t address) {
    return usb_control_transfer(dev,
        USB_REQ_DIR_OUT | USB_REQ_TYPE_STANDARD | USB_REQ_RCPT_DEVICE,
        USB_REQ_SET_ADDRESS, address, 0, NULL, 0);
}

int usb_set_configuration(const usb_device_t* dev, uint8_t config) {
    return usb_control_transfer(dev,
        USB_REQ_DIR_OUT | USB_REQ_TYPE_STANDARD | USB_REQ_RCPT_DEVICE,
        USB_REQ_SET_CONFIGURATION, config, 0, NULL, 0);
}

int usb_bulk_transfer(const usb_device_t* dev, uint8_t endpoint,
                      void* data, int length) {
    if (!dev) return -1;
    int is_in = (endpoint & 0x80) ? 1 : 0;
    return uhci_bulk_transfer(dev->address, endpoint, data, length, is_in);
}

void usb_poll(void) { /* transfers are synchronous */ }
