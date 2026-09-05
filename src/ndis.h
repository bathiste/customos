#ifndef NDIS_H
#define NDIS_H

#include <stdint.h>
#include <stddef.h>

/*
 * Mini-NDIS: a minimal subset of Microsoft's NDIS 6.x API for CustomOS.
 *
 * This is NOT a full NDIS implementation. It provides the function names,
 * structures, and calling conventions that a Windows NDIS 6.x miniport
 * driver would use, so that a driver ported from Windows drops in with
 * minimal changes. The goal is API surface compatibility, not binary
 * (.sys) compatibility.
 *
 * See ndis.h in the WDK and "NDIS 6.0 Miniport Driver" reference for
 * the original definitions. CustomOS calls into drivers via the
 * registered NDIS_MINIPORT_DRIVER structure.
 */

typedef uint8_t  UCHAR;
typedef uint16_t USHORT;
typedef uint32_t ULONG;
typedef uint32_t NDIS_STATUS;
typedef void*    NDIS_HANDLE;
typedef void*    PVOID;
typedef uint8_t  BOOLEAN;
#define TRUE  1
#define FALSE 0

#define NDIS_STATUS_SUCCESS             0x00000000u
#define NDIS_STATUS_PENDING             0x00000103u
#define NDIS_STATUS_FAILURE             0xC0000001u
#define NDIS_STATUS_INVALID_DATA        0xC000000Bu
#define NDIS_STATUS_BUFFER_TOO_SHORT    0xC0000023u
#define NDIS_STATUS_NOT_SUPPORTED       0xC00000BBu
#define NDIS_STATUS_ADAPTER_NOT_FOUND   0xC000009Bu

typedef struct _NET_BUFFER {
    uint8_t*  data;
    uint32_t  length;
    uint32_t  capacity;
    struct _NET_BUFFER* next;
} NET_BUFFER, *PNET_BUFFER;

typedef struct _NET_BUFFER_LIST {
    uint32_t  status;
    uint16_t  flags;
    PNET_BUFFER first_buffer;
    PNET_BUFFER curr_buffer;
    void*     context;
    struct _NET_BUFFER_LIST* next;
} NET_BUFFER_LIST, *PNET_BUFFER_LIST;

#define NDIS_PACKET_TYPE_DIRECTED   0x0001
#define NDIS_PACKET_TYPE_MULTICAST  0x0002
#define NDIS_PACKET_TYPE_BROADCAST  0x0004
#define NDIS_PACKET_TYPE_ALL_MULTICAST 0x0008
#define NDIS_PACKET_TYPE_PROMISCUOUS  0x0020

#define OID_802_3_CURRENT_ADDRESS              0x01010102
#define OID_802_3_PERMANENT_ADDRESS            0x01010101
#define OID_GEN_CURRENT_PACKET_FILTER          0x0001010E
#define OID_GEN_STATISTICS                     0x00010106
#define OID_GEN_LINK_STATE                     0x00010107
#define OID_DOT11_SCAN_REQUEST                 0x0D010108
#define OID_DOT11_CONNECT_REQUEST              0x0D010106
#define OID_DOT11_DISCONNECT_REQUEST           0x0D010107
#define OID_DOT11_ENABLED_SSID                 0x0D010114
#define OID_DOT11_BSSID                        0x0D010108
#define OID_DOT11_SSID                         0x0D010105
#define OID_DOT11_RSSI                         0x0D010106
#define OID_DOT11_ENUM_BSS_LIST                0x0D01011A
#define OID_DOT11_FLUSH_BSS_LIST               0x0D01011B

typedef struct __attribute__((packed)) {
    uint8_t  bssid[6];
    uint16_t capabilities;
    uint16_t rssi;
    uint8_t  channel;
    uint8_t  ssid_len;
    uint8_t  ssid[32];
    uint8_t  security;
    uint8_t  pad[3];
} NDIS_WLAN_BSS_ENTRY;

typedef struct __attribute__((packed)) {
    uint32_t num_entries;
    NDIS_WLAN_BSS_ENTRY entries[16];
} NDIS_WLAN_BSS_LIST;

typedef NDIS_STATUS (*MiniportInitializeEx_t)(NDIS_HANDLE miniport_handle,
                                              void* adapter_ctx);
typedef void (*MiniportHaltEx_t)(NDIS_HANDLE miniport_handle);
typedef NDIS_STATUS (*MiniportOidRequest_t)(NDIS_HANDLE miniport_handle,
                                            void* ndis_oid_request,
                                            uint32_t oid,
                                            void* info_buffer,
                                            uint32_t info_buffer_length,
                                            uint32_t* bytes_written);
typedef void (*MiniportSendNetBufferLists_t)(NDIS_HANDLE miniport_handle,
                                             PNET_BUFFER_LIST nbl,
                                             uint32_t port_number,
                                             uint32_t send_flags);

typedef struct {
    const char*               name;
    uint16_t                  vendor_id;
    uint16_t                  product_id;
    MiniportInitializeEx_t    miniport_initialize_ex;
    MiniportHaltEx_t          miniport_halt_ex;
    MiniportOidRequest_t      miniport_oid_request;
    MiniportSendNetBufferLists_t miniport_send_net_buffer_lists;
} NDIS_MINIPORT_DRIVER;

void ndis_init(void);
int  ndis_register_miniport(const NDIS_MINIPORT_DRIVER* drv);
NDIS_HANDLE ndis_open(uint16_t vendor_id, uint16_t product_id);
void        ndis_close(NDIS_HANDLE handle);
NDIS_STATUS ndis_send(NDIS_HANDLE handle, const void* data, uint32_t length);
NDIS_STATUS ndis_oid_query(NDIS_HANDLE handle, uint32_t oid,
                           void* buf, uint32_t buf_len, uint32_t* written);
NDIS_STATUS ndis_oid_set(NDIS_HANDLE handle, uint32_t oid,
                         const void* buf, uint32_t buf_len);
void ndis_poll(void);
int  ndis_wifi_scan(NDIS_HANDLE handle, NDIS_WLAN_BSS_LIST* list);
int  ndis_wifi_connect(NDIS_HANDLE handle, const char* ssid, const char* password);
int  ndis_wifi_disconnect(NDIS_HANDLE handle);
int  ndis_wifi_rssi(NDIS_HANDLE handle);

#endif /* NDIS_H */
