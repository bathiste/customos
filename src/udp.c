#include "udp.h"
#include "virtio.h"
#include "string.h"
#include <stddef.h>

#define MAX_UDP_SOCKETS 4

static udp_socket_t udp_sockets[MAX_UDP_SOCKETS];
static udp_stats_t udp_stats = {0};

static uint16_t checksum16(const void* data, int len) {
    const uint16_t* p = (const uint16_t*)data;
    uint32_t sum = 0;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len == 1) { sum += (*(const uint8_t*)p) << 8; }
    while (sum >> 16) { sum = (sum & 0xFFFF) + (sum >> 16); }
    return (uint16_t)~sum;
}

static udp_socket_t* find_free_socket(void) {
    for (int i = 0; i < MAX_UDP_SOCKETS; i++) {
        if (udp_sockets[i].local_port == 0) return &udp_sockets[i];
    }
    return (udp_socket_t*)0;
}

void udp_init(void) {
    for (int i = 0; i < MAX_UDP_SOCKETS; i++) {
        udp_sockets[i].local_port = 0;
    }
}

void udp_poll(void) {}

udp_socket_t* udp_socket_create(void) {
    udp_socket_t* sock = find_free_socket();
    if (sock) {
        sock->local_ip = 0x0A00020F;
        sock->local_port = 0;
        udp_stats.sockets_created++;
    }
    return sock;
}

void udp_socket_close(udp_socket_t* sock) {
    if (sock) sock->local_port = 0;
}

int udp_bind(udp_socket_t* sock, uint16_t port) {
    if (!sock) return -1;
    sock->local_port = port;
    return 0;
}

int udp_sendto(udp_socket_t* sock, const uint8_t* dst_ip, uint16_t dst_port, const void* data, int len) {
    if (!sock || len <= 0 || len > 1400) return -1;
    uint8_t buffer[1500];
    uint8_t* ip = buffer + 14;
    uint8_t* udp_hdr = ip + 20;
    for (int i = 0; i < 6; i++) buffer[i] = 0xFF;
    for (int i = 0; i < 6; i++) buffer[6 + i] = 0x00;
    buffer[12] = 0x08; buffer[13] = 0x00;
    uint8_t src_ip[4] = {10, 0, 2, 15};
    ip[0] = 0x45; ip[1] = 0x00;
    uint16_t total_len = 20 + 8 + len;
    ip[2] = (total_len >> 8) & 0xFF; ip[3] = total_len & 0xFF;
    ip[4] = 0x00; ip[5] = 0x00; ip[6] = 0x00; ip[7] = 0x00;
    ip[8] = 0x40; ip[9] = 0x11; ip[10] = 0x00; ip[11] = 0x00;
    for (int i = 0; i < 4; i++) ip[12 + i] = src_ip[i];
    for (int i = 0; i < 4; i++) ip[16 + i] = dst_ip[i];
    uint16_t ip_cksum = checksum16(ip, 20);
    ip[10] = (ip_cksum >> 8) & 0xFF; ip[11] = ip_cksum & 0xFF;
    udp_hdr[0] = (sock->local_port >> 8) & 0xFF; udp_hdr[1] = sock->local_port & 0xFF;
    udp_hdr[2] = (dst_port >> 8) & 0xFF; udp_hdr[3] = dst_port & 0xFF;
    uint16_t udp_len = 8 + len;
    udp_hdr[4] = (udp_len >> 8) & 0xFF; udp_hdr[5] = udp_len & 0xFF;
    udp_hdr[6] = 0x00; udp_hdr[7] = 0x00;
    const uint8_t* src_data = (const uint8_t*)data;
    for (int i = 0; i < len; i++) udp_hdr[8 + i] = src_data[i];
    int result = virtio_send_to_ip(dst_ip, buffer, 14 + total_len);
    if (result == 0) { udp_stats.packets_sent++; return len; }
    return -1;
}

int udp_recvfrom(udp_socket_t* sock, void* buffer, int maxlen, uint8_t* src_ip_out, uint16_t* src_port_out) {
    if (!sock) return -1;
    uint8_t eth_buf[1600];
    int len = virtio_net_recv(eth_buf, sizeof(eth_buf));
    if (len < 0) return -1;
    udp_stats.packets_received++;
    if (len < 14 + 20 + 8) return -1;
    uint8_t* ip_hdr = eth_buf + 14;
    if (ip_hdr[9] != 0x11) return -1;
    uint8_t* udp_hdr = ip_hdr + 20;
    uint16_t dst_port = (udp_hdr[2] << 8) | udp_hdr[3];
    if (sock->local_port != 0 && dst_port != sock->local_port) return -1;
    int data_offset = 14 + 20 + 8;
    int data_len = len - data_offset;
    if (data_len <= 0) return 0;
    if (data_len > maxlen) data_len = maxlen;
    char* dst = (char*)buffer;
    for (int i = 0; i < data_len; i++) dst[i] = eth_buf[data_offset + i];
    if (src_ip_out) {
        src_ip_out[0] = ip_hdr[12]; src_ip_out[1] = ip_hdr[13];
        src_ip_out[2] = ip_hdr[14]; src_ip_out[3] = ip_hdr[15];
    }
    if (src_port_out) {
        *src_port_out = (udp_hdr[0] << 8) | udp_hdr[1];
    }
    return data_len;
}

udp_stats_t udp_get_stats(void) { return udp_stats; }
