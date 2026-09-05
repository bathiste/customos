#ifndef TCP_H
#define TCP_H

#include <stdint.h>

/* TCP state machine states */
#define TCP_CLOSED      0
#define TCP_LISTEN      1
#define TCP_SYN_SENT    2
#define TCP_SYN_RECEIVED 3
#define TCP_ESTABLISHED 4
#define TCP_FIN_WAIT_1  5
#define TCP_FIN_WAIT_2  6
#define TCP_CLOSE_WAIT  7
#define TCP_CLOSING     8
#define TCP_LAST_ACK    9
#define TCP_TIME_WAIT   10

/* TCP socket structure */
typedef struct {
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t window;
    uint8_t state;
    uint8_t flags;
    uint8_t* recv_buffer;
    int recv_len;
} tcp_socket_t;

/* TCP statistics */
typedef struct {
    int connections;
    int active_sockets;
    int packets_sent;
    int packets_received;
    int retransmits;
} tcp_stats_t;

void tcp_init(void);
void tcp_poll(void);
tcp_socket_t* tcp_socket_create(void);
void tcp_socket_close(tcp_socket_t* sock);
int tcp_connect(tcp_socket_t* sock, const uint8_t* dst_ip, uint16_t dst_port);
int tcp_listen(tcp_socket_t* sock, uint16_t port);
tcp_socket_t* tcp_accept(tcp_socket_t* sock);
int tcp_send(tcp_socket_t* sock, const void* data, int len);
int tcp_recv(tcp_socket_t* sock, void* buffer, int maxlen);
tcp_stats_t tcp_get_stats(void);
int tcp_is_connected(tcp_socket_t* sock);
void tcp_get_local_ip(uint8_t* ip_out);

#endif /* TCP_H */