#include "tcp.h"
#include "virtio.h"
#include "string.h"
#include <stddef.h>

#define MAX_TCP_SOCKETS 4

static tcp_socket_t tcp_sockets[MAX_TCP_SOCKETS];
static tcp_stats_t tcp_stats = {0};
static uint32_t tcp_sequence = 0x12345678;

static uint16_t checksum16(const void* data, int len) {
    const uint16_t* p = (const uint16_t*)data;
    uint32_t sum = 0;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len == 1) { sum += (*(const uint8_t*)p) << 8; }
    while (sum >> 16) { sum = (sum & 0xFFFF) + (sum >> 16); }
    return (uint16_t)~sum;
}

static tcp_socket_t* find_free_socket(void) {
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        if (tcp_sockets[i].state == TCP_CLOSED) return &tcp_sockets[i];
    }
    return (tcp_socket_t*)0;
}

void tcp_init(void) {
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        tcp_sockets[i].state = TCP_CLOSED;
        tcp_sockets[i].window = 1024;
    }
}

void tcp_poll(void) {
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        if (tcp_sockets[i].state == TCP_SYN_SENT) {
            tcp_sockets[i].state = TCP_ESTABLISHED;
            tcp_stats.connections++;
        }
    }
}

tcp_socket_t* tcp_socket_create(void) {
    tcp_socket_t* sock = find_free_socket();
    if (sock) {
        sock->state = TCP_CLOSED;
        sock->window = 1024;
        sock->recv_buffer = (uint8_t*)0;
        sock->recv_len = 0;
        tcp_stats.active_sockets++;
    }
    return sock;
}

void tcp_socket_close(tcp_socket_t* sock) {
    if (sock && sock->state != TCP_CLOSED) {
        sock->state = TCP_CLOSED;
        if (tcp_stats.active_sockets > 0) tcp_stats.active_sockets--;
    }
}

int tcp_connect(tcp_socket_t* sock, const uint8_t* dst_ip, uint16_t dst_port) {
    if (!sock || sock->state != TCP_CLOSED) return -1;
    sock->local_ip = 0x0A00020F;
    sock->local_port = 49152 + (tcp_sequence++ % 16384);
    sock->remote_ip = (dst_ip[0] << 24) | (dst_ip[1] << 16) | (dst_ip[2] << 8) | dst_ip[3];
    sock->remote_port = dst_port;
    sock->seq_num = tcp_sequence++;
    sock->state = TCP_SYN_SENT;
    tcp_stats.packets_sent++;
    return 0;
}

int tcp_listen(tcp_socket_t* sock, uint16_t port) {
    if (!sock || sock->state != TCP_CLOSED) return -1;
    sock->local_ip = 0x0A00020F;
    sock->local_port = port;
    sock->state = TCP_LISTEN;
    return 0;
}

tcp_socket_t* tcp_accept(tcp_socket_t* sock) {
    if (!sock || sock->state != TCP_LISTEN) return (tcp_socket_t*)0;
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        if (tcp_sockets[i].state == TCP_SYN_RECEIVED) {
            tcp_sockets[i].state = TCP_ESTABLISHED;
            return &tcp_sockets[i];
        }
    }
    return (tcp_socket_t*)0;
}

int tcp_send(tcp_socket_t* sock, const void* data, int len) {
    if (!sock || sock->state != TCP_ESTABLISHED || len <= 0) return -1;
    if (len > 1400) len = 1400;
    uint8_t buffer[1500];
    uint8_t* ip = buffer + 14;
    uint8_t* tcp_hdr = ip + 20;
    for (int i = 0; i < 6; i++) buffer[i] = 0xFF;
    for (int i = 0; i < 6; i++) buffer[6 + i] = 0x00;
    buffer[12] = 0x08; buffer[13] = 0x00;
    uint8_t src_ip[4] = {10, 0, 2, 15};
    uint8_t dst_ip[4];
    dst_ip[0] = (sock->remote_ip >> 24) & 0xFF;
    dst_ip[1] = (sock->remote_ip >> 16) & 0xFF;
    dst_ip[2] = (sock->remote_ip >> 8) & 0xFF;
    dst_ip[3] = sock->remote_ip & 0xFF;
    ip[0] = 0x45; ip[1] = 0x00;
    uint16_t total_len = 20 + 20 + len;
    ip[2] = (total_len >> 8) & 0xFF; ip[3] = total_len & 0xFF;
    ip[4] = 0x00; ip[5] = 0x00; ip[6] = 0x00; ip[7] = 0x00;
    ip[8] = 0x40; ip[9] = 0x06; ip[10] = 0x00; ip[11] = 0x00;
    for (int i = 0; i < 4; i++) ip[12 + i] = src_ip[i];
    for (int i = 0; i < 4; i++) ip[16 + i] = dst_ip[i];
    uint16_t ip_cksum = checksum16(ip, 20);
    ip[10] = (ip_cksum >> 8) & 0xFF; ip[11] = ip_cksum & 0xFF;
    tcp_hdr[0] = (sock->local_port >> 8) & 0xFF; tcp_hdr[1] = sock->local_port & 0xFF;
    tcp_hdr[2] = (sock->remote_port >> 8) & 0xFF; tcp_hdr[3] = sock->remote_port & 0xFF;
    tcp_hdr[4] = (sock->seq_num >> 24) & 0xFF; tcp_hdr[5] = (sock->seq_num >> 16) & 0xFF;
    tcp_hdr[6] = (sock->seq_num >> 8) & 0xFF; tcp_hdr[7] = sock->seq_num & 0xFF;
    tcp_hdr[8] = (sock->ack_num >> 24) & 0xFF; tcp_hdr[9] = (sock->ack_num >> 16) & 0xFF;
    tcp_hdr[10] = (sock->ack_num >> 8) & 0xFF; tcp_hdr[11] = sock->ack_num & 0xFF;
    tcp_hdr[12] = 0x50; tcp_hdr[13] = 0x10;
    tcp_hdr[14] = (sock->window >> 8) & 0xFF; tcp_hdr[15] = sock->window & 0xFF;
    tcp_hdr[16] = 0x00; tcp_hdr[17] = 0x00; tcp_hdr[18] = 0x00; tcp_hdr[19] = 0x00;
    const uint8_t* src_data = (const uint8_t*)data;
    for (int i = 0; i < len; i++) tcp_hdr[20 + i] = src_data[i];
    int result = virtio_send_to_ip(dst_ip, buffer, 14 + total_len);
    if (result == 0) { sock->seq_num += len; tcp_stats.packets_sent++; return len; }
    return -1;
}

int tcp_recv(tcp_socket_t* sock, void* buffer, int maxlen) {
    if (!sock || sock->state != TCP_ESTABLISHED) return -1;
    uint8_t eth_buf[1600];
    int len = virtio_net_recv(eth_buf, sizeof(eth_buf));
    if (len < 0) return -1;
    tcp_stats.packets_received++;
    if (len < 14 + 20 + 20) return -1;
    uint8_t* ip_hdr = eth_buf + 14;
    if (ip_hdr[9] != 6) return -1;
    uint8_t* tcp_hdr = ip_hdr + 20;
    uint16_t dst_port = (tcp_hdr[2] << 8) | tcp_hdr[3];
    if (dst_port != sock->local_port) return -1;
    int data_offset = 14 + 20 + 20;
    int data_len = len - data_offset;
    if (data_len <= 0) return 0;
    if (data_len > maxlen) data_len = maxlen;
    char* dst = (char*)buffer;
    for (int i = 0; i < data_len; i++) dst[i] = eth_buf[data_offset + i];
    return data_len;
}

int tcp_is_connected(tcp_socket_t* sock) { return sock && sock->state == TCP_ESTABLISHED; }
tcp_stats_t tcp_get_stats(void) { return tcp_stats; }
void tcp_get_local_ip(uint8_t* ip_out) { if (ip_out) { ip_out[0] = 10; ip_out[1] = 0; ip_out[2] = 2; ip_out[3] = 15; } }
