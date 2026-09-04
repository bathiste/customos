#ifndef NET_H
#define NET_H

#include <stdint.h>

/* Network utility functions */
void net_init(void);
void net_poll(void);

/* IP address utilities */
int ip_aton(const char* str, uint8_t* out);
uint16_t htons(uint16_t h);
uint32_t htonl(uint32_t h);
uint16_t checksum(const void* data, int len);

/* Build Ethernet + IPv4 + ICMP packet.
 * Caller provides buf (at least 98 bytes).
 * Returns total packet length. */
int build_icmp_echo(uint8_t* buf, uint8_t* dst_mac, uint8_t* src_mac,
                    uint32_t src_ip, uint32_t dst_ip,
                    uint16_t id, uint16_t seq, const void* data, int data_len);

#endif
