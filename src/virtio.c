#include "virtio.h"
#include "pci.h"
#include "io.h"

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

/* Boot diagnostics: write a copy of all virtio debug output to COM1
   (0x3F8) so it appears in QEMU's -serial output and survives any
   screen clear. This is invaluable when debugging driver init. */
void com1_putc(char c) {
    /* Wait for the UART to be ready (bit 5 of line status = THR empty) */
    int wait;
    for (wait = 0; wait < 10000; wait++) {
        if ((inb(0x3FD) & 0x20) != 0) break;
    }
    outb(0x3F8, (uint8_t)c);
}
void com1_puts(const char* s) {
    while (*s) com1_putc(*s++);
}
void com1_puthex(uint32_t v) {
    int i;
    com1_puts("0x");
    for (i = 7; i >= 0; i--) {
        uint8_t n = (uint8_t)((v >> (i * 4)) & 0xF);
        com1_putc(n < 10 ? '0' + n : 'a' + n - 10);
    }
}
void com1_putdec(int v) {
    char buf[16]; int i = 0;
    if (v < 0) { com1_putc('-'); v = -v; }
    if (v == 0) { com1_putc('0'); return; }
    while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i > 0) com1_putc(buf[--i]);
}

static virtio_net_t g_virtio = {0};

#define VQPOOL_SIZE (128 * 1024)
static uint8_t g_vqpool[VQPOOL_SIZE] __attribute__((aligned(4096)));
static uint32_t g_vqpool_phys;
static uint32_t g_vqpool_used;

#define NET_BUF_SIZE 2048
#define RX_BUF_COUNT 32
#define TX_BUF_COUNT 32

static uint8_t  g_rx_bufs[RX_BUF_COUNT][NET_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t  g_tx_bufs[TX_BUF_COUNT][NET_BUF_SIZE] __attribute__((aligned(4)));
static uint32_t g_rx_bufs_phys[RX_BUF_COUNT];
static uint32_t g_tx_bufs_phys[TX_BUF_COUNT];
static int      g_rx_buf_used[RX_BUF_COUNT];
static int      g_tx_buf_free[TX_BUF_COUNT];

static virtio_desc_t*  g_rx_desc;
static virtio_avail_t* g_rx_avail;
static virtio_used_t*  g_rx_used;

static virtio_desc_t*  g_tx_desc;
static virtio_avail_t* g_tx_avail;
static virtio_used_t*  g_tx_used;

static uint16_t g_rx_avail_idx = 0;
static uint16_t g_tx_avail_idx = 0;
static uint16_t g_last_rx_idx = 0;

/* -------------------------------------------------------------------------
 * Access path. We support two backends:
 *   - Legacy (transitional) virtio: 8-bit I/O port register interface
 *   - Modern virtio 1.0: 32-bit little-endian MMIO register interface
 *
 * `g_virtio.iobase` is either a 16-bit I/O port (legacy) or a 32-bit
 * physical MMIO address (modern). `g_is_modern` selects the path.
 * --------------------------------------------------------------------- */

/* Modern virtio uses MMIO with 32-bit little-endian registers at fixed
 * offsets in the BAR. For modern devices the BAR is memory-space, and
 * the driver interacts with it through volatile pointers. */
static uint32_t g_mmio_base_phys = 0;
static volatile uint8_t* g_mmio_base = (volatile uint8_t*)0;
static int  g_is_modern = 0;

/* Translate a "logical" register offset to the actual offset for the
   current access mode. Legacy I/O mode has a different status register
   offset (0x12) than modern MMIO mode (0x00), and several other small
   differences. */
static inline uint8_t vreg(uint8_t logical) {
    if (g_is_modern) {
        return logical;
    }
    /* Legacy I/O port offsets */
    switch (logical) {
        case VIRTIO_REG_DEVICE_STATUS: return VIRTIO_LEGACY_DEVICE_STATUS;
        case VIRTIO_REG_GUEST_FEATURES: return VIRTIO_LEGACY_GUEST_FEATURES;
        case VIRTIO_REG_QUEUE_PFN:    return VIRTIO_LEGACY_QUEUE_PFN;
        case VIRTIO_REG_QUEUE_NUM:    return VIRTIO_LEGACY_QUEUE_NUM;
        case VIRTIO_REG_QUEUE_SEL:    return VIRTIO_LEGACY_QUEUE_SEL;
        case VIRTIO_REG_QUEUE_NOTIFY: return VIRTIO_LEGACY_QUEUE_NOTIFY;
        case VIRTIO_REG_DEVICE_CONFIG: return VIRTIO_LEGACY_DEVICE_CONFIG;
        default: return logical;
    }
}

static inline volatile uint8_t* mmio_ptr(uint32_t reg) {
    return g_mmio_base + reg;
}

static void virtio_write8(uint16_t base, uint8_t reg, uint8_t val) {
    uint8_t off = vreg(reg);
    if (g_is_modern) {
        /* Modern virtio has no 8-bit register access; we must RMW a 32-bit slot. */
        uint32_t a = off & ~3;
        uint32_t shift = (off & 3) * 8;
        volatile uint32_t* p = (volatile uint32_t*)mmio_ptr(a);
        uint32_t v = *p;
        v = (v & ~(0xFFu << shift)) | ((uint32_t)val << shift);
        *p = v;
    } else {
        outb(base + off, val);
    }
}
static uint8_t virtio_read8(uint16_t base, uint8_t reg) {
    uint8_t off = vreg(reg);
    if (g_is_modern) {
        uint32_t a = off & ~3;
        uint32_t shift = (off & 3) * 8;
        volatile uint32_t* p = (volatile uint32_t*)mmio_ptr(a);
        return (uint8_t)((*p >> shift) & 0xFF);
    }
    return inb(base + off);
}
static void virtio_write16(uint16_t base, uint8_t reg, uint16_t val) {
    uint8_t off = vreg(reg);
    if (g_is_modern) {
        uint32_t a = off & ~2;
        uint32_t shift = (off & 2) * 8;
        volatile uint32_t* p = (volatile uint32_t*)mmio_ptr(a);
        uint32_t v = *p;
        v = (v & ~(0xFFFFu << shift)) | ((uint32_t)val << shift);
        *p = v;
    } else {
        outw(base + off, val);
    }
}
static uint16_t virtio_read16(uint16_t base, uint8_t reg) {
    uint8_t off = vreg(reg);
    if (g_is_modern) {
        uint32_t a = off & ~2;
        uint32_t shift = (off & 2) * 8;
        volatile uint32_t* p = (volatile uint32_t*)mmio_ptr(a);
        return (uint16_t)((*p >> shift) & 0xFFFF);
    }
    return inw(base + off);
}
static void virtio_write32(uint16_t base, uint8_t reg, uint32_t val) {
    uint8_t off = vreg(reg);
    if (g_is_modern) {
        *(volatile uint32_t*)mmio_ptr(off) = val;
    } else {
        __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"((uint16_t)(base + off)));
    }
}
static uint32_t virtio_read32(uint16_t base, uint8_t reg) {
    uint8_t off = vreg(reg);
    if (g_is_modern) {
        return *(volatile uint32_t*)mmio_ptr(off);
    }
    uint32_t val;
    __asm__ volatile ("inl %1, %0" : "=a"(val) : "Nd"((uint16_t)(base + off)));
    return val;
}

static void virtio_wait_for(uint16_t base, uint8_t reg, uint32_t mask, uint32_t expected) {
    int i;
    for (i = 0; i < 1000; i++) {
        uint32_t val = virtio_read32(base, reg) & mask;
        if (val == expected) return;
        volatile int j;
        for (j = 0; j < 1000; j++) { }
    }
}

/* Modern virtio register offsets (from the BAR base, 32-bit LE MMIO) */
#define VIRTIO_MODERN_MAGIC         0x000
#define VIRTIO_MODERN_VERSION       0x004
#define VIRTIO_MODERN_DEVICE_ID     0x008
#define VIRTIO_MODERN_VENDOR_ID     0x00C
#define VIRTIO_MODERN_DEV_FEATURES  0x010
#define VIRTIO_MODERN_DEV_FEATURES_SEL 0x014
#define VIRTIO_MODERN_DRV_FEATURES  0x020
#define VIRTIO_MODERN_DRV_FEATURES_SEL 0x024
#define VIRTIO_MODERN_QUEUE_SEL     0x030
#define VIRTIO_MODERN_QUEUE_NUM_MAX 0x034
#define VIRTIO_MODERN_QUEUE_ENABLE  0x038
#define VIRTIO_MODERN_QUEUE_DESC_LO 0x040
#define VIRTIO_MODERN_QUEUE_DESC_HI 0x044
#define VIRTIO_MODERN_QUEUE_AVAIL_LO 0x048
#define VIRTIO_MODERN_QUEUE_AVAIL_HI 0x04C
#define VIRTIO_MODERN_QUEUE_USED_LO 0x050
#define VIRTIO_MODERN_QUEUE_USED_HI 0x054
#define VIRTIO_MODERN_QUEUE_NOTIFY  0x064
#define VIRTIO_MODERN_STATUS        0x070
#define VIRTIO_MODERN_CONFIG_BASE   0x100

void virtio_init(void) {
    /* Find VirtIO network device.
       We try the transitional/legacy device ID (0x1000) first, since
       that's what QEMU's default `virtio-net-pci` advertises. We fall
       back to the modern (non-transitional) ID 0x1041. */
    const pci_device_t* dev = pci_find_device(VIRTIO_VENDOR_ID, VIRTIO_LEGACY_ID_NET);
    if (!dev) {
        dev = pci_find_device(VIRTIO_VENDOR_ID, VIRTIO_PCI_ID_NET);
    }
    if (!dev) {
        dev = pci_find_class(0x02, 0x00);
        if (!dev) dev = pci_find_class(0x02, 0xFF);
    }

    if (!dev) {
        terminal_writestring("[virtio] No virtio-net device found.\n");
        com1_puts("[virtio] No virtio-net device found.\r\n");
        return;
    }

    com1_puts("[virtio] Found virtio-net device, dev_id=0x");
    com1_puthex(dev->device_id);
    com1_puts("\r\n");
    terminal_writestring("[virtio] Found virtio-net device.\n");

    pci_enable_device(dev);

    /* Inspect BAR0.
       bit 0 = 1   => I/O space (legacy)
       bit 0 = 0   => MMIO       (modern) */
    uint32_t bar0 = pci_get_bar(dev, 0);
    com1_puts("[virtio] BAR0 = 0x");
    com1_puthex(bar0);
    com1_puts("\r\n");
    if (bar0 & 0x01) {
        g_is_modern = 0;
        g_virtio.iobase = (uint16_t)(bar0 & ~0x03u);
        com1_puts("[virtio] Using legacy I/O port 0x");
        com1_puthex(g_virtio.iobase);
        com1_puts("\r\n");
    } else {
        g_is_modern = 1;
        g_mmio_base_phys = bar0 & ~0x0Fu;
        g_mmio_base = (volatile uint8_t*)(uintptr_t)g_mmio_base_phys;
        g_virtio.iobase = (uint16_t)g_mmio_base_phys;
        com1_puts("[virtio] Using modern MMIO at phys 0x");
        com1_puthex(g_mmio_base_phys);
        com1_puts("\r\n");

        uint32_t magic = *(volatile uint32_t*)mmio_ptr(VIRTIO_MODERN_MAGIC);
        com1_puts("[virtio] MMIO magic = 0x");
        com1_puthex(magic);
        com1_puts("\r\n");
        if (magic != 0x74726976u) {
            terminal_writestring("[virtio] MMIO magic mismatch.\n");
            com1_puts("[virtio] MMIO magic mismatch.\r\n");
            return;
        }
        uint32_t ver = *(volatile uint32_t*)mmio_ptr(VIRTIO_MODERN_VERSION);
        terminal_writestring("       virtio 1.0, version=");
        print_int(ver);
        terminal_writestring("\n");
    }

    g_virtio.present = 0;

    /* Reset the device. According to the virtio spec, writing 0 to
       the status register triggers a reset. We need to keep writing 0
       until the device confirms the reset by reporting status == 0. */
    {
        int tries;
        for (tries = 0; tries < 10; tries++) {
            virtio_write8(g_virtio.iobase, VIRTIO_REG_DEVICE_STATUS, 0);
            /* Small delay for the device to process the reset. */
            volatile int d; for (d = 0; d < 10000; d++) { }
            uint8_t s = virtio_read8(g_virtio.iobase, VIRTIO_REG_DEVICE_STATUS);
            com1_puts("[virtio] Reset attempt "); com1_putdec(tries);
            com1_puts(", status=0x"); com1_puthex(s);
            com1_puts("\r\n");
            if (s == 0) break;
        }
    }

    virtio_write8(g_virtio.iobase, VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACK);

    virtio_write8(g_virtio.iobase, VIRTIO_REG_DEVICE_STATUS,
                  virtio_read8(g_virtio.iobase, VIRTIO_REG_DEVICE_STATUS) | VIRTIO_STATUS_DRIVER);

    com1_puts("[virtio] Driver loaded, status=0x");
    com1_puthex(virtio_read8(g_virtio.iobase, VIRTIO_REG_DEVICE_STATUS));
    com1_puts("\r\n");

    /* Read device features.
       Legacy: GUEST_FEATURES register holds features directly.
       Modern: features are paged via *_SEL registers. */
    uint32_t dev_features;
    if (g_is_modern) {
        *(volatile uint32_t*)mmio_ptr(VIRTIO_MODERN_DEV_FEATURES_SEL) = 0;
        dev_features = *(volatile uint32_t*)mmio_ptr(VIRTIO_MODERN_DEV_FEATURES);
    } else {
        dev_features = virtio_read32(g_virtio.iobase, VIRTIO_REG_GUEST_FEATURES);
    }

    uint32_t driver_features = 0;
    if (dev_features & (1U << VIRTIO_NET_F_MAC)) {
        driver_features |= (1U << VIRTIO_NET_F_MAC);
    }

    if (g_is_modern) {
        *(volatile uint32_t*)mmio_ptr(VIRTIO_MODERN_DRV_FEATURES_SEL) = 0;
        *(volatile uint32_t*)mmio_ptr(VIRTIO_MODERN_DRV_FEATURES) = driver_features;
    } else {
        virtio_write32(g_virtio.iobase, VIRTIO_REG_GUEST_FEATURES, driver_features);
    }

    virtio_write8(g_virtio.iobase, VIRTIO_REG_DEVICE_STATUS,
                  virtio_read8(g_virtio.iobase, VIRTIO_REG_DEVICE_STATUS) | VIRTIO_STATUS_FEATURES_OK);

    uint8_t status = virtio_read8(g_virtio.iobase, VIRTIO_REG_DEVICE_STATUS);
    com1_puts("[virtio] After FEATURES_OK, status=0x");
    com1_puthex(status);
    com1_puts("\r\n");
    if (status & VIRTIO_STATUS_NEEDS_RESET) {
        terminal_writestring("[virtio] Device requires reset.\n");
        com1_puts("[virtio] Device requires reset.\r\n");
        return;
    }
    if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        terminal_writestring("[virtio] Feature negotiation failed.\n");
        com1_puts("[virtio] Feature negotiation failed.\r\n");
        return;
    }

    g_virtio.features = driver_features;

    int i;
    if (g_is_modern) {
        uint32_t w0 = *(volatile uint32_t*)mmio_ptr(VIRTIO_MODERN_CONFIG_BASE + 0);
        uint16_t w1 = *(volatile uint16_t*)mmio_ptr(VIRTIO_MODERN_CONFIG_BASE + 4);
        g_virtio.mac[0] = (uint8_t)(w0      );
        g_virtio.mac[1] = (uint8_t)(w0 >>  8);
        g_virtio.mac[2] = (uint8_t)(w0 >> 16);
        g_virtio.mac[3] = (uint8_t)(w0 >> 24);
        g_virtio.mac[4] = (uint8_t)(w1      );
        g_virtio.mac[5] = (uint8_t)(w1 >>  8);
    } else {
        for (i = 0; i < 6; i++) {
            g_virtio.mac[i] = virtio_read8(g_virtio.iobase, VIRTIO_REG_DEVICE_CONFIG + i);
        }
    }
    com1_puts("[virtio] MAC = ");
    for (i = 0; i < 6; i++) {
        uint8_t b = g_virtio.mac[i];
        com1_puthex(b >> 4);
        com1_puthex(b & 0xf);
        if (i < 5) com1_putc(':');
    }
    com1_puts("\r\n");
    terminal_writestring("       MAC: ");
    for (i = 0; i < 6; i++) {
        char hex[3];
        uint8_t b = g_virtio.mac[i];
        hex[0] = "0123456789ABCDEF"[b >> 4];
        hex[1] = "0123456789ABCDEF"[b & 0x0F];
        hex[2] = '\0';
        terminal_writestring(hex);
        if (i < 5) terminal_putchar(':');
    }
    terminal_putchar('\n');

    g_vqpool_used = 0;
    g_vqpool_phys = (uint32_t)(uintptr_t)g_vqpool;

    /* RX queue: queue 0 */
    virtio_write16(g_virtio.iobase, VIRTIO_REG_QUEUE_SEL, 0);
    uint16_t qsize;
    if (g_is_modern) {
        qsize = (uint16_t)*(volatile uint32_t*)mmio_ptr(VIRTIO_MODERN_QUEUE_NUM_MAX);
    } else {
        qsize = virtio_read16(g_virtio.iobase, VIRTIO_REG_QUEUE_NUM);
    }
    com1_puts("[virtio] RX queue size = ");
    com1_putdec(qsize);
    com1_puts("\r\n");
    terminal_writestring("       RX queue size: "); print_int(qsize); terminal_putchar('\n');
    if (qsize == 0 || qsize > VIRTIO_QUEUE_SIZE) {
        com1_puts("[virtio] Invalid RX queue size, aborting\r\n");
        return;
    }

    g_rx_desc  = (virtio_desc_t*)((uint8_t*)g_vqpool + g_vqpool_used);
    g_rx_avail = (virtio_avail_t*)((uint8_t*)g_rx_desc + VIRTIO_QUEUE_SIZE * sizeof(virtio_desc_t));
    g_rx_used  = (virtio_used_t*)((uint8_t*)g_rx_avail + 6 + VIRTIO_QUEUE_SIZE * sizeof(uint16_t));
    g_vqpool_used += VIRTIO_QUEUE_SIZE * sizeof(virtio_desc_t);
    g_vqpool_used += 6 + VIRTIO_QUEUE_SIZE * sizeof(uint16_t);
    g_vqpool_used += 6 + VIRTIO_QUEUE_SIZE * sizeof(virtio_used_elem_t);
    g_vqpool_used = (g_vqpool_used + 4095) & ~4095;

    int qi;
    for (qi = 0; qi < VIRTIO_QUEUE_SIZE; qi++) {
        g_rx_desc[qi].addr = 0; g_rx_desc[qi].len = 0;
        g_rx_desc[qi].flags = 0; g_rx_desc[qi].next = 0;
    }
    g_rx_avail->flags = 0; g_rx_avail->idx = 0;
    g_rx_used->flags = 0; g_rx_used->idx = 0;

    {
        uint32_t phys_base = (uint32_t)(uintptr_t)g_rx_bufs;
        for (qi = 0; qi < RX_BUF_COUNT; qi++) g_rx_bufs_phys[qi] = phys_base + qi * NET_BUF_SIZE;
    }
    {
        uint32_t phys_base = (uint32_t)(uintptr_t)g_tx_bufs;
        for (qi = 0; qi < TX_BUF_COUNT; qi++) {
            g_tx_bufs_phys[qi] = phys_base + qi * NET_BUF_SIZE;
            g_tx_buf_free[qi] = 1;
        }
    }

    for (qi = 0; qi < RX_BUF_COUNT && qi < VIRTIO_QUEUE_SIZE; qi++) {
        g_rx_desc[qi].addr  = g_rx_bufs_phys[qi];
        g_rx_desc[qi].len   = NET_BUF_SIZE;
        g_rx_desc[qi].flags = VIRTIO_DESC_F_WRITE;
        g_rx_desc[qi].next  = 0;
        g_rx_avail->ring[qi] = qi;
    }
    g_rx_avail->idx = RX_BUF_COUNT;
    g_rx_avail_idx  = RX_BUF_COUNT;

    if (g_is_modern) {
        uint32_t rx_desc_phys  = (uint32_t)(uintptr_t)g_rx_desc;
        uint32_t rx_avail_phys = (uint32_t)(uintptr_t)g_rx_avail;
        uint32_t rx_used_phys  = (uint32_t)(uintptr_t)g_rx_used;
        *(volatile uint32_t*)mmio_ptr(VIRTIO_MODERN_QUEUE_DESC_LO)  = rx_desc_phys;
        *(volatile uint32_t*)mmio_ptr(VIRTIO_MODERN_QUEUE_AVAIL_LO) = rx_avail_phys;
        *(volatile uint32_t*)mmio_ptr(VIRTIO_MODERN_QUEUE_USED_LO)  = rx_used_phys;
        *(volatile uint32_t*)mmio_ptr(VIRTIO_MODERN_QUEUE_ENABLE)   = 1;
    } else {
        uint32_t rx_phys = (uint32_t)(uintptr_t)g_rx_desc;
        virtio_write32(g_virtio.iobase, VIRTIO_REG_QUEUE_PFN, rx_phys >> 12);
    }

    /* TX queue: queue 1 */
    virtio_write16(g_virtio.iobase, VIRTIO_REG_QUEUE_SEL, 1);
    if (g_is_modern) {
        qsize = (uint16_t)*(volatile uint32_t*)mmio_ptr(VIRTIO_MODERN_QUEUE_NUM_MAX);
    } else {
        qsize = virtio_read16(g_virtio.iobase, VIRTIO_REG_QUEUE_NUM);
    }
    terminal_writestring("       TX queue size: "); print_int(qsize); terminal_putchar('\n');
    if (qsize == 0) return;

    g_tx_desc  = (virtio_desc_t*)((uint8_t*)g_vqpool + g_vqpool_used);
    g_tx_avail = (virtio_avail_t*)((uint8_t*)g_tx_desc + VIRTIO_QUEUE_SIZE * sizeof(virtio_desc_t));
    g_tx_used  = (virtio_used_t*)((uint8_t*)g_tx_avail + 6 + VIRTIO_QUEUE_SIZE * sizeof(uint16_t));
    g_vqpool_used += VIRTIO_QUEUE_SIZE * sizeof(virtio_desc_t);
    g_vqpool_used += 6 + VIRTIO_QUEUE_SIZE * sizeof(uint16_t);
    g_vqpool_used += 6 + VIRTIO_QUEUE_SIZE * sizeof(virtio_used_elem_t);
    g_vqpool_used = (g_vqpool_used + 4095) & ~4095;

    for (qi = 0; qi < VIRTIO_QUEUE_SIZE; qi++) {
        g_tx_desc[qi].addr = 0; g_tx_desc[qi].len = 0;
        g_tx_desc[qi].flags = 0; g_tx_desc[qi].next = 0;
    }
    g_tx_avail->flags = 0; g_tx_avail->idx = 0;
    g_tx_used->flags = 0; g_tx_used->idx = 0;

    if (g_is_modern) {
        uint32_t tx_desc_phys  = (uint32_t)(uintptr_t)g_tx_desc;
        uint32_t tx_avail_phys = (uint32_t)(uintptr_t)g_tx_avail;
        uint32_t tx_used_phys  = (uint32_t)(uintptr_t)g_tx_used;
        *(volatile uint32_t*)mmio_ptr(VIRTIO_MODERN_QUEUE_DESC_LO)  = tx_desc_phys;
        *(volatile uint32_t*)mmio_ptr(VIRTIO_MODERN_QUEUE_AVAIL_LO) = tx_avail_phys;
        *(volatile uint32_t*)mmio_ptr(VIRTIO_MODERN_QUEUE_USED_LO)  = tx_used_phys;
        *(volatile uint32_t*)mmio_ptr(VIRTIO_MODERN_QUEUE_ENABLE)   = 1;
    } else {
        uint32_t tx_phys = (uint32_t)(uintptr_t)g_tx_desc;
        virtio_write32(g_virtio.iobase, VIRTIO_REG_QUEUE_PFN, tx_phys >> 12);
    }

    virtio_write8(g_virtio.iobase, VIRTIO_REG_DEVICE_STATUS,
        virtio_read8(g_virtio.iobase, VIRTIO_REG_DEVICE_STATUS) | VIRTIO_STATUS_DRIVER_OK);

    com1_puts("[virtio] DRIVER_OK set, status=0x");
    com1_puthex(virtio_read8(g_virtio.iobase, VIRTIO_REG_DEVICE_STATUS));
    com1_puts("\r\n");

    terminal_writestring("       VirtIO-net initialized successfully.\n");
    com1_puts("[virtio] INIT COMPLETE, present=1\r\n");
    g_virtio.present = 1;
}

int virtio_net_present(void) { return g_virtio.present; }

void virtio_get_mac(uint8_t* mac_out) {
    int i;
    for (i = 0; i < 6; i++) mac_out[i] = g_virtio.mac[i];
}

uint32_t virtio_get_features(void) { return g_virtio.features; }

int virtio_net_send(const uint8_t* data, uint32_t len) {
    if (!g_virtio.present || len == 0 || len > NET_BUF_SIZE - 64) return -1;

    /* Need 2 consecutive TX slots: slot 0 = virtio_net_hdr (10 bytes),
       slot 1 = ethernet frame data. We use NEXT to chain them. */
    int s0 = -1, s1 = -1;
    int si;
    for (si = 0; si < TX_BUF_COUNT - 1; si++) {
        if (g_tx_buf_free[si] && g_tx_buf_free[si + 1]) {
            s0 = si; s1 = si + 1;
            g_tx_buf_free[s0] = 0; g_tx_buf_free[s1] = 0;
            break;
        }
    }
    if (s0 < 0) return -1;

    /* Slot 0: virtio_net_hdr (10 bytes, all zero - no checksum offload) */
    volatile uint8_t* dst0 = g_tx_bufs[s0];
    int k;
    for (k = 0; k < 10; k++) dst0[k] = 0;

    g_tx_desc[s0].addr  = g_tx_bufs_phys[s0];
    g_tx_desc[s0].len   = 10;          /* sizeof(virtio_net_hdr) */
    g_tx_desc[s0].flags = VIRTIO_DESC_F_NEXT;  /* chain to slot 1 */
    g_tx_desc[s0].next  = (uint16_t)s1;

    /* Slot 1: ethernet frame data (device-readable, guest->host) */
    volatile uint8_t* dst1 = g_tx_bufs[s1];
    uint32_t ci;
    for (ci = 0; ci < len; ci++) dst1[ci] = data[ci];

    g_tx_desc[s1].addr  = g_tx_bufs_phys[s1];
    g_tx_desc[s1].len   = len;
    g_tx_desc[s1].flags = 0;            /* device-readable (out) */
    g_tx_desc[s1].next  = 0;

    g_tx_avail->ring[g_tx_avail_idx % VIRTIO_QUEUE_SIZE] = (uint16_t)s0;
    g_tx_avail_idx++;
    g_tx_avail->idx = g_tx_avail_idx;
    if (g_is_modern) {
        *(volatile uint32_t*)mmio_ptr(VIRTIO_MODERN_QUEUE_NOTIFY) = 1;
    } else {
        virtio_write16(g_virtio.iobase, VIRTIO_REG_QUEUE_NOTIFY, 1);
    }
    return 0;
}

static void build_icmp_reply(const uint8_t* req, uint8_t* resp, uint32_t req_len) {
    int k;
    if (req_len > 64) req_len = 64;
    for (k = 0; k < (int)req_len; k++) resp[k] = req[k];
    for (k = 0; k < 6; k++) { uint8_t t = resp[k]; resp[k] = resp[6+k]; resp[6+k] = t; }
    resp[12] = 0x08; resp[13] = 0x00;
    resp[14] = 0x45; resp[15] = 0x00;
    uint16_t ip_len = (req_len - 14 < 20) ? 20 : (req_len - 14);
    if (ip_len > 64) ip_len = 64;
    resp[16] = (uint8_t)(ip_len >> 8); resp[17] = (uint8_t)(ip_len & 0xFF);
    resp[18] = 0; resp[19] = 0; resp[20] = 0; resp[21] = 0;
    resp[22] = 0x40; resp[23] = 0x01;
    for (k = 0; k < 4; k++) { uint8_t t = resp[26+k]; resp[26+k] = resp[30+k]; resp[30+k] = t; }
    uint32_t sum = 0;
    for (k = 0; k < 20; k += 2) sum += ((uint32_t)resp[14+k] << 8) | resp[15+k];
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    sum = ~sum;
    resp[24] = (uint8_t)(sum >> 8); resp[25] = (uint8_t)(sum & 0xFF);
    resp[34] = 0x00; resp[35] = req[35];
    resp[36] = 0; resp[37] = 0;
    resp[38] = req[38]; resp[39] = req[39]; resp[40] = req[40]; resp[41] = req[41];
    sum = 0;
    uint16_t icmp_len = ip_len - 20;
    if (icmp_len > (uint16_t)(req_len - 34)) icmp_len = (uint16_t)(req_len - 34);
    for (k = 0; k + 1 < icmp_len; k += 2) sum += ((uint32_t)resp[34+k] << 8) | resp[35+k];
    if (k < icmp_len) sum += ((uint32_t)resp[34+k] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    sum = ~sum;
    resp[36] = (uint8_t)(sum >> 8); resp[37] = (uint8_t)(sum & 0xFF);
}

/* -------------------------------------------------------------------------
 * Minimal ARP cache.
 * One slot per entry. For our tiny network this is plenty.
 * --------------------------------------------------------------------- */
#define ARP_CACHE_SIZE 8
typedef struct {
    uint8_t  ip[4];
    uint8_t  mac[6];
    uint8_t  valid;
} arp_entry_t;
static arp_entry_t g_arp[ARP_CACHE_SIZE];

static const uint8_t ETH_BROADCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

static const uint8_t* arp_lookup(const uint8_t* ip) {
    int i;
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!g_arp[i].valid) continue;
        if (g_arp[i].ip[0]==ip[0] && g_arp[i].ip[1]==ip[1] &&
            g_arp[i].ip[2]==ip[2] && g_arp[i].ip[3]==ip[3]) {
            return g_arp[i].mac;
        }
    }
    return (const uint8_t*)0;
}

static void arp_insert(const uint8_t* ip, const uint8_t* mac) {
    int i, free = -1;
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp[i].valid &&
            g_arp[i].ip[0]==ip[0] && g_arp[i].ip[1]==ip[1] &&
            g_arp[i].ip[2]==ip[2] && g_arp[i].ip[3]==ip[3]) {
            int k; for (k=0;k<6;k++) g_arp[i].mac[k]=mac[k];
            return;
        }
        if (!g_arp[i].valid && free < 0) free = i;
    }
    if (free < 0) free = 0;   /* overwrite oldest */
    for (i = 0; i < 4; i++) g_arp[free].ip[i]  = ip[i];
    for (i = 0; i < 6; i++) g_arp[free].mac[i] = mac[i];
    g_arp[free].valid = 1;
}

/* Build and transmit an ARP request for `target_ip` from `src_ip`. */
static void arp_request(const uint8_t* src_ip, const uint8_t* target_ip) {
    uint8_t pkt[42];
    int i;
    for (i = 0; i < 6; i++) pkt[i]     = ETH_BROADCAST[i];
    for (i = 0; i < 6; i++) pkt[6 + i] = g_virtio.mac[i];
    pkt[12] = 0x08; pkt[13] = 0x06;            /* EtherType: ARP */
    pkt[14] = 0x00; pkt[15] = 0x01;            /* HTYPE: Ethernet */
    pkt[16] = 0x08; pkt[17] = 0x00;            /* PTYPE: IPv4    */
    pkt[18] = 6;     pkt[19] = 4;              /* HLEN, PLEN    */
    pkt[20] = 0x00; pkt[21] = 0x01;            /* OPER: request */
    for (i = 0; i < 6; i++) pkt[22 + i] = g_virtio.mac[i];
    for (i = 0; i < 4; i++) pkt[28 + i] = src_ip[i];
    for (i = 0; i < 6; i++) pkt[32 + i] = 0;   /* THA: unknown */
    for (i = 0; i < 4; i++) pkt[38 + i] = target_ip[i];
    virtio_net_send(pkt, sizeof(pkt));
}

void virtio_arp_resolve(const uint8_t* ip) {
    if (arp_lookup(ip)) return;
    static const uint8_t our_ip[4] = {10, 0, 2, 15};
    arp_request(our_ip, ip);
}

/* Resolve IP to MAC, or broadcast if not in cache. */
static const uint8_t* resolve_dest_mac(const uint8_t* ip) {
    const uint8_t* m = arp_lookup(ip);
    return m ? m : ETH_BROADCAST;
}

/* Send an Ethernet frame to a given IP. Builds dst MAC from ARP cache
   (or broadcasts the frame if no entry exists). The caller fills the
   payload starting at offset 14 (already including EtherType). */
int virtio_send_to_ip(const uint8_t* dst_ip, const uint8_t* payload, uint32_t plen) {
    if (plen + 14 > NET_BUF_SIZE) return -1;
    static uint8_t frame[NET_BUF_SIZE];
    const uint8_t* mac = resolve_dest_mac(dst_ip);
    int i;
    for (i = 0; i < 6; i++) frame[i] = mac[i];
    for (i = 0; i < 6; i++) frame[6 + i] = g_virtio.mac[i];
    for (i = 0; i < (int)plen; i++) frame[14 + i] = payload[i];
    return virtio_net_send(frame, plen + 14);
}

void virtio_net_poll(void) {
    if (!g_virtio.present) return;

    /* Check ISR status - tells us what kind of interrupt occurred.
       Bit 0: queue interrupt (RX/TX descriptor used)
       Bit 1: config change interrupt */
    if (!g_is_modern) {
        uint8_t isr = inb(g_virtio.iobase + VIRTIO_LEGACY_ISR_STATUS);
        if (isr != 0) {
            com1_puts("[poll] ISR=0x"); com1_puthex(isr);
            com1_puts(" rx_used="); com1_putdec(g_rx_used->idx);
            com1_puts(" last_rx="); com1_putdec(g_last_rx_idx);
            com1_puts("\r\n");
        }
    }

    /* Reclaim TX buffers: any used-ring entry for the TX queue means
       the host has finished sending that packet, so its slot is free again.
       TX uses a 2-slot chain (header + data), so free both. */
    static uint16_t last_tx_used = 0;
    while (last_tx_used != g_tx_used->idx) {
        uint16_t slot = g_tx_used->ring[last_tx_used % VIRTIO_QUEUE_SIZE].id;
        if (slot < TX_BUF_COUNT) {
            g_tx_buf_free[slot] = 1;
            if (slot + 1 < TX_BUF_COUNT) g_tx_buf_free[slot + 1] = 1;
        }
        last_tx_used++;
    }

    /* Drain incoming packets, auto-respond to ARP and ICMP echo requests. */
    /* RX buffer layout: [virtio_net_hdr (10 bytes)][ethernet frame]
       The ethernet frame starts at offset RX_HDR_SIZE. */
    #define RX_HDR_SIZE 10
    while (g_last_rx_idx != g_rx_used->idx) {
        uint16_t slot = g_rx_used->ring[g_last_rx_idx % VIRTIO_QUEUE_SIZE].id;
        uint32_t len  = g_rx_used->ring[g_last_rx_idx % VIRTIO_QUEUE_SIZE].len;
        com1_puts("[rx] used event: slot="); com1_putdec(slot);
        com1_puts(" len="); com1_putdec(len);
        com1_puts(" g_last_rx_idx="); com1_putdec(g_last_rx_idx);
        com1_puts(" used_idx="); com1_putdec(g_rx_used->idx);
        com1_puts("\r\n");
        if (slot < RX_BUF_COUNT && len >= RX_HDR_SIZE + 14) {
            uint8_t* buf = g_rx_bufs[slot];
            uint8_t* eth = buf + RX_HDR_SIZE;  /* ethernet frame starts here */
            uint16_t etype = (eth[12] << 8) | eth[13];
            com1_puts("[rx] slot="); com1_putdec(slot);
            com1_puts(" len="); com1_putdec(len);
            com1_puts(" etype=0x"); com1_puthex(etype);
            com1_puts("\r\n");

            /* ARP */
            if (etype == 0x0806 && len >= RX_HDR_SIZE + 42) {
                uint16_t oper = (eth[20] << 8) | eth[21];
                uint8_t  sha[6], sip[4], tip[4];
                int k;
                for (k = 0; k < 6; k++) sha[k] = eth[22 + k];
                for (k = 0; k < 4; k++) sip[k] = eth[28 + k];
                for (k = 0; k < 4; k++) tip[k] = eth[38 + k];
                /* Learn sender regardless of request/reply */
                arp_insert(sip, sha);
                com1_puts("[rx] ARP oper="); com1_putdec(oper);
                com1_puts(" from="); com1_putdec(sip[0]);
                com1_putc('.'); com1_putdec(sip[1]);
                com1_putc('.'); com1_putdec(sip[2]);
                com1_putc('.'); com1_putdec(sip[3]);
                com1_puts("\r\n");

                /* Reply to a request that targets our IP */
                static const uint8_t our_ip[4] = {10, 0, 2, 15};
                int is_for_us = 1;
                for (k = 0; k < 4; k++) if (tip[k] != our_ip[k]) is_for_us = 0;
                if (oper == 1 && is_for_us) {
                    /* Build ARP reply */
                    uint8_t reply[42];
                    for (k = 0; k < 6; k++) reply[k]      = sha[k];
                    for (k = 0; k < 6; k++) reply[6 + k]  = g_virtio.mac[k];
                    reply[12] = 0x08; reply[13] = 0x06;
                    reply[14] = 0x00; reply[15] = 0x01;
                    reply[16] = 0x08; reply[17] = 0x00;
                    reply[18] = 6;     reply[19] = 4;
                    reply[20] = 0x00; reply[21] = 0x02;       /* reply */
                    for (k = 0; k < 6; k++) reply[22 + k] = g_virtio.mac[k];
                    for (k = 0; k < 4; k++) reply[28 + k] = our_ip[k];
                    for (k = 0; k < 6; k++) reply[32 + k] = sha[k];
                    for (k = 0; k < 4; k++) reply[38 + k] = sip[k];
                    virtio_net_send(reply, sizeof(reply));
                    com1_puts("[rx] Sent ARP reply\r\n");
                }
            }
            /* ICMP echo request -> reply */
            else if (etype == 0x0800 && len >= RX_HDR_SIZE + 34 && eth[23] == 1 && eth[34] == 8) {
                com1_puts("[rx] ICMP echo request!\r\n");
                uint8_t reply[96];
                build_icmp_reply(eth, reply, (len < 96) ? len : 96);
                /* Use the source MAC from the request to reply */
                uint8_t dst_mac[6];
                int k;
                for (k = 0; k < 6; k++) dst_mac[k] = eth[6 + k];
                for (k = 0; k < 6; k++) reply[k] = dst_mac[k];
                for (k = 0; k < 6; k++) reply[6 + k] = g_virtio.mac[k];
                virtio_net_send(reply, (len < 96) ? len : 96);
                com1_puts("[rx] Sent ICMP reply\r\n");
            }
        }
        if (slot < RX_BUF_COUNT) {
            g_rx_desc[slot].addr = g_rx_bufs_phys[slot];
            g_rx_desc[slot].len  = NET_BUF_SIZE;
            g_rx_desc[slot].flags = VIRTIO_DESC_F_WRITE;
            g_rx_desc[slot].next  = 0;
            g_rx_avail->ring[g_rx_avail_idx % VIRTIO_QUEUE_SIZE] = slot;
            g_rx_avail_idx++;
            g_rx_avail->idx = g_rx_avail_idx;
        }
        g_last_rx_idx++;
    }
}

int virtio_net_recv(uint8_t* buffer, uint32_t max_len) {
    if (!g_virtio.present) return -1;
    if (g_last_rx_idx == g_rx_used->idx) return -1;
    uint16_t slot = g_rx_used->ring[g_last_rx_idx % VIRTIO_QUEUE_SIZE].id;
    uint32_t len  = g_rx_used->ring[g_last_rx_idx % VIRTIO_QUEUE_SIZE].len;
    if (slot >= RX_BUF_COUNT || len == 0) { g_last_rx_idx++; return -1; }
    uint32_t copy_len = (len < max_len) ? len : max_len;
    uint32_t ci;
    for (ci = 0; ci < copy_len; ci++) buffer[ci] = g_rx_bufs[slot][ci];
    g_rx_desc[slot].addr = g_rx_bufs_phys[slot];
    g_rx_desc[slot].len  = NET_BUF_SIZE;
    g_rx_desc[slot].flags = VIRTIO_DESC_F_WRITE;
    g_rx_desc[slot].next  = 0;
    g_rx_avail->ring[g_rx_avail_idx % VIRTIO_QUEUE_SIZE] = slot;
    g_rx_avail_idx++;
    g_last_rx_idx++;
    return copy_len;
}

