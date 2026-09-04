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
static uint16_t g_tx_avail_idx = 0;
static uint16_t g_last_rx_idx = 0;

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

    g_tx_avail->ring[g_tx_avail_idx % VIRTIO_QUEUE_SIZE] = (uint16_t)slot;
    g_tx_avail_idx++;
    g_tx_avail->idx = g_tx_avail_idx;
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

    /* Reclaim TX buffers: any used-ring entry for the TX queue means
       the host has finished sending that packet, so its slot is free again. */
    static uint16_t last_tx_used = 0;
    while (last_tx_used != g_tx_used->idx) {
        uint16_t slot = g_tx_used->ring[last_tx_used % VIRTIO_QUEUE_SIZE].id;
        if (slot < TX_BUF_COUNT) g_tx_buf_free[slot] = 1;
        last_tx_used++;
    }

    /* Drain incoming packets, auto-respond to ARP and ICMP echo requests. */
    while (g_last_rx_idx != g_rx_used->idx) {
        uint16_t slot = g_rx_used->ring[g_last_rx_idx % VIRTIO_QUEUE_SIZE].id;
        uint32_t len  = g_rx_used->ring[g_last_rx_idx % VIRTIO_QUEUE_SIZE].len;
        if (slot < RX_BUF_COUNT && len >= 14) {
            uint8_t* buf = g_rx_bufs[slot];
            uint16_t etype = (buf[12] << 8) | buf[13];

            /* ARP */
            if (etype == 0x0806 && len >= 42) {
                uint16_t oper = (buf[20] << 8) | buf[21];
                uint8_t  sha[6], sip[4], tip[4];
                int k;
                for (k = 0; k < 6; k++) sha[k] = buf[22 + k];
                for (k = 0; k < 4; k++) sip[k] = buf[28 + k];
                for (k = 0; k < 4; k++) tip[k] = buf[38 + k];
                /* Learn sender regardless of request/reply */
                arp_insert(sip, sha);

                /* Reply to a request that targets our MAC */
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
                }
            }
            /* ICMP echo request -> reply */
            else if (etype == 0x0800 && len >= 34 && buf[23] == 1 && buf[34] == 8) {
                uint8_t reply[96];
                build_icmp_reply(buf, reply, (len < 96) ? len : 96);
                /* Use the source MAC from the request to reply */
                uint8_t dst_mac[6];
                int k;
                for (k = 0; k < 6; k++) dst_mac[k] = buf[6 + k];
                for (k = 0; k < 6; k++) reply[k] = dst_mac[k];
                for (k = 0; k < 6; k++) reply[6 + k] = g_virtio.mac[k];
                virtio_net_send(reply, (len < 96) ? len : 96);
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

