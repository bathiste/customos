#ifndef VIRTIO_H
#define VIRTIO_H

#include <stdint.h>

/* VirtIO PCI constants */
#define VIRTIO_VENDOR_ID    0x1AF4

/* VirtIO device IDs */
#define VIRTIO_ID_NETWORK   0x01
#define VIRTIO_ID_BLOCK     0x02
#define VIRTIO_ID_CONSOLE   0x03
#define VIRTIO_ID_BALLOON   0x05

/* VirtIO 1.0 device IDs (in PCI device ID field) */
#define VIRTIO_PCI_ID_NET   0x1041

/* VirtIO legacy device IDs (transitional) */
#define VIRTIO_LEGACY_ID_NET 0x1000

/* VirtIO status register bits */
#define VIRTIO_STATUS_ACK         0x01
#define VIRTIO_STATUS_DRIVER      0x02
#define VIRTIO_STATUS_DRIVER_OK   0x04
#define VIRTIO_STATUS_FEATURES_OK 0x08
#define VIRTIO_STATUS_NEEDS_RESET 0x40
#define VIRTIO_STATUS_FAILED      0x80

/* VirtIO modern (1.0) configuration registers (offset from MMIO base) */
#define VIRTIO_REG_DEVICE_STATUS    0x00   /* also used as legacy alias */
#define VIRTIO_REG_GUEST_FEATURES   0x04
#define VIRTIO_REG_QUEUE_PFN        0x08
#define VIRTIO_REG_QUEUE_NUM        0x0C
#define VIRTIO_REG_QUEUE_SEL        0x0E
#define VIRTIO_REG_QUEUE_NOTIFY     0x10
#define VIRTIO_REG_DEVICE_CONFIG    0x14

/* VirtIO legacy (transitional) register offsets (I/O port). */
#define VIRTIO_LEGACY_DEVICE_FEATURES  0x00
#define VIRTIO_LEGACY_GUEST_FEATURES   0x04
#define VIRTIO_LEGACY_QUEUE_PFN        0x08
#define VIRTIO_LEGACY_QUEUE_NUM        0x0C
#define VIRTIO_LEGACY_QUEUE_SEL        0x0E
#define VIRTIO_LEGACY_QUEUE_NOTIFY     0x10
#define VIRTIO_LEGACY_DEVICE_STATUS    0x12
#define VIRTIO_LEGACY_ISR_STATUS       0x13
#define VIRTIO_LEGACY_DEVICE_CONFIG    0x14

/* VirtIO feature bits (common) */
#define VIRTIO_F_RING_INDIRECT_DESC  28
#define VIRTIO_F_RING_EVENT_IDX      29
#define VIRTIO_F_VERSION_1           32
#define VIRTIO_F_ACCESS_PLATFORM     33

/* VirtIO net feature bits */
#define VIRTIO_NET_F_MAC             5
#define VIRTIO_NET_F_STATUS          16
#define VIRTIO_NET_F_CTRL_VQ         17
#define VIRTIO_NET_F_CTRL_RX         18
#define VIRTIO_NET_F_GUEST_CSUM      21
#define VIRTIO_NET_F_GUEST_TSO4      22
#define VIRTIO_NET_F_HOST_TSO4       23
#define VIRTIO_NET_F_MRG_RXBUF       63

/* VirtIO net configuration (at device_config offset) */
typedef struct __attribute__((packed)) {
    uint8_t  mac[6];
    uint16_t status;
    uint16_t max_virtqueue_pairs;
    uint16_t mtu;
} virtio_net_config_t;

/* VirtIO ring constants */
#define VIRTIO_DESC_F_NEXT      0x01
#define VIRTIO_DESC_F_WRITE     0x02
#define VIRTIO_DESC_F_INDIRECT  0x04

#define VIRTQ_AVAIL_F_NO_INTERRUPT  1
#define VIRTQ_USED_F_NO_NOTIFY      1

/* VirtIO queue size (power of 2, max 32768) */
#define VIRTIO_QUEUE_SIZE  256

/* VirtIO network driver state */
typedef struct {
    uint8_t  present;          /* 1 if a virtio-net device was found and initialized */
    uint8_t  mac[6];           /* MAC address */
    uint32_t features;         /* negotiated features */

    /* Virtual queue addresses (physical, from host) */
    uint32_t rx_queue_pfn;
    uint32_t tx_queue_pfn;

    /* ISR status (read from PCI config space at offset 0x1C) */
    volatile uint8_t* isr_status;

    /* I/O base (for in/out instructions) */
    uint16_t iobase;
} virtio_net_t;

/* VirtIO descriptor table entry (stored in guest memory) */
typedef struct virtio_desc {
    uint64_t addr;   /* physical address of buffer */
    uint32_t len;    /* length of buffer */
    uint16_t flags;  /* VIRTIO_DESC_F_* */
    uint16_t next;   /* next descriptor index in chain */
} __attribute__((packed)) virtio_desc_t;

/* VirtIO available ring entry (guest->host notification) */
typedef struct virtio_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTIO_QUEUE_SIZE];
} __attribute__((packed)) virtio_avail_t;

/* VirtIO used ring entry (host->guest completion) */
typedef struct virtio_used_elem {
    uint32_t id;   /* index of descriptor chain that was used */
    uint32_t len;  /* number of bytes written by device */
} __attribute__((packed)) virtio_used_elem_t;

typedef struct virtio_used {
    uint16_t flags;
    uint16_t idx;
    virtio_used_elem_t ring[VIRTIO_QUEUE_SIZE];
} __attribute__((packed)) virtio_used_t;

/* Initialize the VirtIO subsystem: scan PCI, find virtio-net. */
void virtio_init(void);

/* Check if virtio-net is available. */
int  virtio_net_present(void);

/* Get the MAC address of virtio-net. */
void virtio_get_mac(uint8_t* mac_out);

/* Send a raw ethernet frame. Returns 0 on success, -1 on failure. */
int  virtio_net_send(const uint8_t* data, uint32_t len);

/* Receive a raw ethernet frame. Returns number of bytes received, or -1 if none. */
int  virtio_net_recv(uint8_t* buffer, uint32_t max_len);

/* Poll for received packets (call this in the main loop). */
void virtio_net_poll(void);

/* Send an Ethernet frame to a given IP. Builds dst MAC from ARP cache
   (or broadcasts the frame if no entry exists). The payload starts at
   offset 14 of the Ethernet frame and should already include the EtherType. */
int  virtio_send_to_ip(const uint8_t* dst_ip, const uint8_t* payload, uint32_t plen);

/* Kick off ARP resolution for an IP (no-op if already cached). */
void virtio_arp_resolve(const uint8_t* ip);

/* Get the negotiated feature bits. */
uint32_t virtio_get_features(void);

/* COM1 serial helpers (used by kernel.c for debug output) */
void com1_putc(char c);
void com1_puts(const char* s);
void com1_puthex(uint32_t v);
void com1_putdec(int v);

#endif /* VIRTIO_H */
