#ifndef UDP_H
#define UDP_H

#include <stdint.h>

typedef struct {
    uint32_t local_ip;
    uint16_t local_port;
    uint8_t* recv_buffer;
    int recv_len;
} udp_socket_t;

typedef struct {
    int packets_sent;
    int packets_received;
    int sockets_created;
} udp_stats_t;

void udp_init(void);
void udp_poll(void);
udp_socket_t* udp_socket_create(void);
void udp_socket_close(udp_socket_t* sock);
int udp_bind(udp_socket_t* sock, uint16_t port);
int udp_sendto(udp_socket_t* sock, const uint8_t* dst_ip, uint16_t dst_port, const void* data, int len);
int udp_recvfrom(udp_socket_t* sock, void* buffer, int maxlen, uint8_t* src_ip_out, uint16_t* src_port_out);
udp_stats_t udp_get_stats(void);

#endif
#ifndef UDP_H
#define UDP_H

#include <stdint.h>

typedef struct {
    uint32_t local_ip;
    uint16_t local_port;
    uint8_t* recv_buffer;
    int recv_len;
} udp_socket_t;

typedef struct {
    int packets_sent;
    int packets_received;
    int sockets_created;
} udp_stats_t;

void udp_init(void);
void udp_poll(void);
udp_socket_t* udp_socket_create(void);
void udp_socket_close(udp_socket_t* sock);
int udp_bind(udp_socket_t* sock, uint16_t port);
int udp_sendto(udp_socket_t* sock, const uint8_t* dst_ip, uint16_t dst_port, const void* data, int len);
int udp_recvfrom(udp_socket_t* sock, void* buffer, int maxlen, uint8_t* src_ip_out, uint16_t* src_port_out);
udp_stats_t udp_get_stats(void);

#endif /* UDP_H */
