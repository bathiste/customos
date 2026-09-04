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
static uint16_t g_last_tx_used_idx = 0;

static uint16_t find_iobase(const pci_device_t* dev) {
    uint32_t bar = pci_get_bar(dev, 0);
    if ((bar & 0x01) == 0) {
        return 0;
    }
    return (uint16_t)(bar & ~0x03);
}

static void virtio_write8(uint16_t base, uint8_t reg, uint8_t val) {
    outb(base + reg, val);
}
static uint8_t virtio_read8(uint16_t base, uint8_t reg) {
    return inb(base + reg);
}
static void virtio_write16(uint16_t base, uint8_t reg, uint16_t val) {
    outw(base + reg, val);
}
static uint16_t virtio_read16(uint16_t base, uint8_t reg) {
    return inw(base + reg);
}
static void virtio_write32(uint16_t base, uint8_t reg, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"((uint16_t)(base + reg)));
}
static uint32_t virtio_read32(uint16_t base, uint8_t reg) {
    uint32_t val;
    __asm__ volatile ("inl %1, %0" : "=a"(val) : "Nd"((uint16_t)(base + reg)));
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

void virtio_init(void) {
    /* Find VirtIO network device */
    const pci_device_t* dev = pci_find_device(VIRTIO_VENDOR_ID, VIRTIO_PCI_ID_NET);
    if (!dev) {
        dev = pci_find_device(VIRTIO_VENDOR_ID, VIRTIO_LEGACY_ID_NET);
    }
    if (!dev) {
        dev = pci_find_class(0x02, 0x00);
        if (!dev) dev = pci_find_class(0x02, 0xFF);
    }

    if (!dev) {
        terminal_writestring("[virtio] No virtio-net device found.\n");
        return;
    }

    terminal_writestring("[virtio] Found virtio-net device.\n");

    uint16_t iobase = find_iobase(dev);
    if (!iobase) {
        terminal_writestring("[virtio] No I/O base found.\n");
        return;
    }

    g_virtio.iobase = iobase;
    g_virtio.present = 0;

    pci_enable_device(dev);

    virtio_write8(iobase, VIRTIO_REG_DEVICE_STATUS, 0);
    virtio_wait_for(iobase, VIRTIO_REG_DEVICE_STATUS, 0xFF, 0);

    virtio_write8(iobase, VIRTIO_REG_DEVICE_STATUS,
                  virtio_read8(iobase, VIRTIO_REG_DEVICE_STATUS) | VIRTIO_STATUS_ACK);

    virtio_write8(iobase, VIRTIO_REG_DEVICE_STATUS,
                  virtio_read8(iobase, VIRTIO_REG_DEVICE_STATUS) | VIRTIO_STATUS_DRIVER);

    uint32_t dev_features = virtio_read32(iobase, VIRTIO_REG_GUEST_FEATURES);

    uint32_t driver_features = 0;
    if (dev_features & (1U << VIRTIO_NET_F_MAC)) {
        driver_features |= (1U << VIRTIO_NET_F_MAC);
    }

    virtio_write32(iobase, VIRTIO_REG_GUEST_FEATURES, driver_features);

    virtio_write8(iobase, VIRTIO_REG_DEVICE_STATUS,
                  virtio_read8(iobase, VIRTIO_REG_DEVICE_STATUS) | VIRTIO_STATUS_FEATURES_OK);

    uint8_t status = virtio_read8(iobase, VIRTIO_REG_DEVICE_STATUS);
    if (status & VIRTIO_STATUS_NEEDS_RESET) {
        terminal_writestring("[virtio] Device requires reset.\n");
        return;
    }
    if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        terminal_writestring("[virtio] Feature negotiation failed.\n");
        return;
    }

    g_virtio.features = driver_features;

    int i;
    for (i = 0; i < 6; i++) {
        g_virtio.mac[i] = virtio_read8(iobase, VIRTIO_REG_DEVICE_CONFIG + i);
    }
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
    virtio_write16(iobase, VIRTIO_REG_QUEUE_SEL, 0);
    uint16_t qsize = virtio_read16(iobase, VIRTIO_REG_QUEUE_NUM);
    terminal_writestring("       RX queue size: "); print_int(qsize); terminal_putchar('\n');
    if (qsize == 0 || qsize > VIRTIO_QUEUE_SIZE) return;

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

    uint32_t rx_phys = (uint32_t)(uintptr_t)g_rx_desc;
    virtio_write32(iobase, VIRTIO_REG_QUEUE_PFN, rx_phys >> 12);

    /* TX queue: queue 1 */
    virtio_write16(iobase, VIRTIO_REG_QUEUE_SEL, 1);
    qsize = virtio_read16(iobase, VIRTIO_REG_QUEUE_NUM);
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

    uint32_t tx_phys = (uint32_t)(uintptr_t)g_tx_desc;
    virtio_write32(iobase, VIRTIO_REG_QUEUE_PFN, tx_phys >> 12);

    virtio_write8(iobase, VIRTIO_REG_DEVICE_STATUS,
        virtio_read8(iobase, VIRTIO_REG_DEVICE_STATUS) | VIRTIO_STATUS_DRIVER_OK);

    terminal_writestring("       VirtIO-net initialized successfully.\n");
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

    int slot = -1;
    int si;
    for (si = 0; si < TX_BUF_COUNT; si++) {
        if (g_tx_buf_free[si]) { slot = si; g_tx_buf_free[si] = 0; break; }
    }
    if (slot < 0) return -1;

    volatile uint8_t* dst = g_tx_bufs[slot];
    uint32_t ci;
    for (ci = 0; ci < len; ci++) dst[ci] = data[ci];

    g_tx_desc[slot].addr  = g_tx_bufs_phys[slot];
    g_tx_desc[slot].len   = len;
    g_tx_desc[slot].flags = VIRTIO_DESC_F_WRITE;
    g_tx_desc[slot].next  = 0;

    g_tx_avail->ring[g_tx_avail->idx % VIRTIO_QUEUE_SIZE] = (uint16_t)slot;
    g_tx_avail->idx++;
    __asm__ volatile ("" : : "r"(&g_tx_avail->idx) : "memory");
    virtio_write16(g_virtio.iobase, VIRTIO_REG_QUEUE_NOTIFY, 1);
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

static uint16_t g_last_rx_idx = 0;

void virtio_net_poll(void) {
    if (!g_virtio.present) return;
    while (g_last_rx_idx != g_rx_used->idx) {
        uint16_t slot = g_rx_used->ring[g_last_rx_idx % VIRTIO_QUEUE_SIZE].id;
        uint32_t len  = g_rx_used->ring[g_last_rx_idx % VIRTIO_QUEUE_SIZE].len;
        if (slot < RX_BUF_COUNT && len >= 14) {
            uint8_t* buf = g_rx_bufs[slot];
            uint16_t etype = (buf[12] << 8) | buf[13];
            if (etype == 0x0800 && len >= 34 && buf[23] == 1 && buf[34] == 8) {
                uint8_t reply[64];
                build_icmp_reply(buf, reply, len);
                virtio_net_send(reply, (len < 64) ? len : 64);
            }
        }
        if (slot < RX_BUF_COUNT) {
            g_rx_desc[slot].addr = g_rx_bufs_phys[slot];
            g_rx_desc[slot].len  = NET_BUF_SIZE;
            g_rx_desc[slot].flags = VIRTIO_DESC_F_WRITE;
            g_rx_desc[slot].next  = 0;
            g_rx_avail->ring[g_rx_avail_idx % VIRTIO_QUEUE_SIZE] = slot;
            g_rx_avail_idx++;
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

