#include "io.h"
#include "keyboard.h"
#include "mouse.h"
#include "fs.h"
#include "pci.h"
#include "virtio.h"

extern void shell_run(void);

/* Simple auto-ping test that fires 4 ICMP echo requests to the SLIRP gateway
   and prints round-trip results to COM1. Removes the need for manual input. */
static void auto_ping_test(void) {
    /* COM1 UART for debug output */
    extern void com1_puts(const char* s);
    extern void com1_puthex(uint32_t v);
    extern void com1_putdec(int v);
    extern void com1_putc(char c);

    extern void virtio_net_poll(void);
    extern int  virtio_send_to_ip(const uint8_t* dst, const uint8_t* payload, uint32_t plen);
    extern void virtio_arp_resolve(const uint8_t* ip);
    extern int  virtio_net_present(void);
    extern uint32_t virtio_get_features(void);

    if (!virtio_net_present()) {
        com1_puts("[ping] Network not available\r\n");
        return;
    }

    uint8_t dst_ip[4] = {10, 0, 2, 2};
    uint8_t src_ip[4] = {10, 0, 2, 15};

    com1_puts("[ping] Resolving "); com1_putdec(dst_ip[0]);
    com1_putc('.'); com1_putdec(dst_ip[1]); com1_putc('.');
    com1_putdec(dst_ip[2]); com1_putc('.'); com1_putdec(dst_ip[3]);
    com1_puts("\r\n");

    /* Trigger ARP resolution and wait for reply */
    virtio_arp_resolve(dst_ip);
    int w;
    for (w = 0; w < 100000; w++) virtio_net_poll();

    com1_puts("[ping] Features = 0x"); com1_puthex(virtio_get_features()); com1_puts("\r\n");

    /* Send 4 ICMP echo requests */
    int i;
    for (i = 0; i < 4; i++) {
        uint8_t payload[64];
        payload[0] = 0x45; payload[1] = 0x00;
        payload[2] = 0x00; payload[3] = 0x54;
        payload[4] = 0x00; payload[5] = 0x00;
        payload[6] = 0x00; payload[7] = 0x00;
        payload[8] = 0x40; payload[9] = 0x01;   /* TTL=64, ICMP */
        payload[10] = 0x00; payload[11] = 0x00; /* checksum placeholder */

        int pi;
        for (pi = 0; pi < 4; pi++) payload[12 + pi] = src_ip[pi];
        for (pi = 0; pi < 4; pi++) payload[16 + pi] = dst_ip[pi];

        /* IP header checksum */
        {
            uint32_t sum = 0;
            int si;
            for (si = 0; si < 20; si += 2)
                sum += ((uint32_t)payload[si] << 8) | payload[si + 1];
            while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
            sum = ~sum;
            payload[10] = (uint8_t)(sum >> 8);
            payload[11] = (uint8_t)(sum & 0xFF);
        }

        /* ICMP echo request */
        payload[20] = 0x08; payload[21] = 0x00;
        payload[22] = 0x00; payload[23] = 0x00;
        payload[24] = (uint8_t)(0x1234 >> 8);
        payload[25] = (uint8_t)(0x1234 & 0xFF);
        payload[26] = (uint8_t)((i + 1) >> 8);
        payload[27] = (uint8_t)((i + 1) & 0xFF);
        for (pi = 28; pi < 64; pi++) payload[pi] = (uint8_t)(pi - 28);

        /* ICMP checksum */
        {
            uint32_t sum = 0;
            for (pi = 20; pi < 64; pi += 2)
                sum += ((uint32_t)payload[pi] << 8) | payload[pi + 1];
            while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
            sum = ~sum;
            payload[22] = (uint8_t)(sum >> 8);
            payload[23] = (uint8_t)(sum & 0xFF);
        }

        com1_puts("[ping] seq="); com1_putdec(i + 1);
        com1_puts(" ... ");

        int result = virtio_send_to_ip(dst_ip, payload, 50);
        if (result == 0) {
            com1_puts("TX OK\r\n");
        } else {
            com1_puts("TX FAIL\r\n");
        }

        /* Poll for reply */
        int j;
        for (j = 0; j < 50000; j++) virtio_net_poll();
    }
    com1_puts("[ping] Done.\r\n");
}

void main(void) {
    terminal_init();
    keyboard_init();
    mouse_init();
    fs_init();

    terminal_setcolor(VGA_COLOR_LIGHT_CYAN);
    terminal_writestring("CustomOS 0.1 loaded successfully!\n");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY);

    /* Initialize network stack */
    pci_init();
    virtio_init();

    /* Auto-ping test to verify networking works */
    auto_ping_test();

    shell_run();

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
