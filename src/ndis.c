/*
 * Mini-NDIS implementation.
 *
 * Provides a minimal Windows-NDIS-6.x-compatible API surface. Drivers
 * register an NDIS_MINIPORT_DRIVER struct; applications call ndis_open
 * / ndis_send / ndis_oid_query using the same names and calling
 * conventions as on Windows.
 *
 * Internally forwards to the same wifi_usb driver that cfg80211
 * also uses, demonstrating the dual-API driver.
 */

#include "ndis.h"
#include "wifi_usb.h"
#include "string.h"
#include <stddef.h>

#define MAX_MINIPORTS 8

typedef struct {
    int bound;
    const NDIS_MINIPORT_DRIVER* drv;
    NDIS_HANDLE mhandle;
} ndis_binding_t;

static ndis_binding_t g_bindings[MAX_MINIPORTS];
static int g_initialized = 0;

static NDIS_STATUS mp_init(NDIS_HANDLE mh, void* ctx);
static void         mp_halt(NDIS_HANDLE mh);
static NDIS_STATUS mp_oid(NDIS_HANDLE mh, void* req, uint32_t oid,
                          void* buf, uint32_t blen, uint32_t* written);
static void         mp_send(NDIS_HANDLE mh, PNET_BUFFER_LIST nbl,
                            uint32_t port, uint32_t flags);

static const NDIS_MINIPORT_DRIVER g_rtl8188eu_miniport = {
    .name = "RTL8188EU NDIS Miniport",
    .vendor_id  = RTL8188EU_VENDOR_ID,
    .product_id = RTL8188EU_PRODUCT_ID,
    .miniport_initialize_ex     = mp_init,
    .miniport_halt_ex           = mp_halt,
    .miniport_oid_request       = mp_oid,
    .miniport_send_net_buffer_lists = mp_send
};

void ndis_init(void) {
    if (g_initialized) return;
    g_initialized = 1;
    for (int i = 0; i < MAX_MINIPORTS; i++)
        memset(&g_bindings[i], 0, sizeof(g_bindings[i]));
    ndis_register_miniport(&g_rtl8188eu_miniport);
}

int ndis_register_miniport(const NDIS_MINIPORT_DRIVER* drv) {
    for (int i = 0; i < MAX_MINIPORTS; i++) {
        if (g_bindings[i].drv == 0) {
            g_bindings[i].drv = drv;
            return i;
        }
    }
    return -1;
}

NDIS_HANDLE ndis_open(uint16_t vid, uint16_t pid) {
    for (int i = 0; i < MAX_MINIPORTS; i++) {
        ndis_binding_t* b = &g_bindings[i];
        if (b->drv && b->drv->vendor_id == vid && b->drv->product_id == pid
            && !b->bound) {
            NDIS_STATUS s = b->drv->miniport_initialize_ex((NDIS_HANDLE)b, NULL);
            if (s == NDIS_STATUS_SUCCESS) {
                b->bound = 1;
                b->mhandle = (NDIS_HANDLE)b;
                return b->mhandle;
            }
        }
    }
    return 0;
}

void ndis_close(NDIS_HANDLE h) {
    ndis_binding_t* b = (ndis_binding_t*)h;
    if (!b || !b->bound) return;
    if (b->drv && b->drv->miniport_halt_ex) b->drv->miniport_halt_ex(h);
    b->bound = 0;
    b->mhandle = 0;
}

NDIS_STATUS ndis_send(NDIS_HANDLE h, const void* data, uint32_t length) {
    ndis_binding_t* b = (ndis_binding_t*)h;
    if (!b || !b->bound || !b->drv) return NDIS_STATUS_FAILURE;
    if (!b->drv->miniport_send_net_buffer_lists) return NDIS_STATUS_NOT_SUPPORTED;
    NET_BUFFER nb;
    nb.data = (uint8_t*)data;
    nb.length = length;
    nb.capacity = length;
    nb.next = NULL;
    NET_BUFFER_LIST nbl;
    nbl.status = 0;
    nbl.flags = NDIS_PACKET_TYPE_DIRECTED;
    nbl.first_buffer = &nb;
    nbl.curr_buffer = &nb;
    nbl.context = NULL;
    nbl.next = NULL;
    b->drv->miniport_send_net_buffer_lists(h, &nbl, 0, 0);
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS ndis_oid_query(NDIS_HANDLE h, uint32_t oid,
                           void* buf, uint32_t blen, uint32_t* written) {
    ndis_binding_t* b = (ndis_binding_t*)h;
    if (!b || !b->bound || !b->drv || !b->drv->miniport_oid_request)
        return NDIS_STATUS_FAILURE;
    return b->drv->miniport_oid_request(h, NULL, oid, buf, blen, written);
}

NDIS_STATUS ndis_oid_set(NDIS_HANDLE h, uint32_t oid,
                         const void* buf, uint32_t blen) {
    ndis_binding_t* b = (ndis_binding_t*)h;
    if (!b || !b->bound || !b->drv || !b->drv->miniport_oid_request)
        return NDIS_STATUS_FAILURE;
    return b->drv->miniport_oid_request(h, NULL, oid, (void*)buf, blen, NULL);
}

void ndis_poll(void) { }

int ndis_wifi_scan(NDIS_HANDLE h, NDIS_WLAN_BSS_LIST* list) {
    ndis_binding_t* b = (ndis_binding_t*)h;
    if (!b || !b->bound) return -1;
    if (!list) return -1;
    ndis_oid_query(h, OID_DOT11_FLUSH_BSS_LIST, NULL, 0, NULL);
    wifi_network_t nets[16];
    int n = wifi_scan(nets, 16);
    if (n < 0) return -1;
    list->num_entries = (n > 16) ? 16 : (uint32_t)n;
    for (int i = 0; i < (int)list->num_entries; i++) {
        NDIS_WLAN_BSS_ENTRY* e = &list->entries[i];
        memset(e, 0, sizeof(*e));
        for (int k = 0; k < 6; k++) e->bssid[k] = nets[i].bssid[k];
        e->rssi = (uint16_t)(-nets[i].rssi);
        e->channel = nets[i].channel;
        e->capabilities = (nets[i].security != WIFI_SECURITY_NONE) ? 0x10 : 0x00;
        e->security = (uint8_t)nets[i].security;
        e->ssid_len = (uint8_t)strlen(nets[i].ssid);
        for (int k = 0; k < e->ssid_len; k++) e->ssid[k] = (uint8_t)nets[i].ssid[k];
    }
    return list->num_entries;
}

int ndis_wifi_connect(NDIS_HANDLE h, const char* ssid, const char* password) {
    ndis_binding_t* b = (ndis_binding_t*)h;
    if (!b || !b->bound) return -1;
    char buf[128];
    int i = 0;
    for (; ssid && ssid[i] && i < (int)sizeof(buf) - 1; i++) buf[i] = ssid[i];
    buf[i] = 0;
    return wifi_connect(buf, password, WIFI_SECURITY_WPA2);
}

int ndis_wifi_disconnect(NDIS_HANDLE h) {
    ndis_binding_t* b = (ndis_binding_t*)h;
    if (!b || !b->bound) return -1;
    wifi_disconnect();
    return 0;
}

int ndis_wifi_rssi(NDIS_HANDLE h) {
    ndis_binding_t* b = (ndis_binding_t*)h;
    if (!b || !b->bound) return -1;
    return rtl8188eu_get_rssi();
}

static NDIS_STATUS mp_init(NDIS_HANDLE mh, void* ctx) {
    (void)mh; (void)ctx;
    int r = rtl8188eu_init();
    if (r < 0 && !rtl8188eu_present()) return NDIS_STATUS_ADAPTER_NOT_FOUND;
    return NDIS_STATUS_SUCCESS;
}

static void mp_halt(NDIS_HANDLE mh) {
    (void)mh;
}

static NDIS_STATUS mp_oid(NDIS_HANDLE mh, void* req, uint32_t oid,
                          void* buf, uint32_t blen, uint32_t* written) {
    (void)mh; (void)req;
    if (written) *written = 0;
    switch (oid) {
        case OID_802_3_CURRENT_ADDRESS:
        case OID_802_3_PERMANENT_ADDRESS: {
            if (!buf || blen < 6) return NDIS_STATUS_BUFFER_TOO_SHORT;
            uint8_t mac[6] = { 0x02, 0x80, 0x00, 0xA8, 0x12, 0x34 };
            memcpy(buf, mac, 6);
            if (written) *written = 6;
            return NDIS_STATUS_SUCCESS;
        }
        case OID_DOT11_RSSI: {
            if (!buf || blen < 4) return NDIS_STATUS_BUFFER_TOO_SHORT;
            int32_t r = rtl8188eu_get_rssi();
            memcpy(buf, &r, sizeof(r));
            if (written) *written = 4;
            return NDIS_STATUS_SUCCESS;
        }
        case OID_DOT11_SCAN_REQUEST:
            return NDIS_STATUS_SUCCESS;
        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static void mp_send(NDIS_HANDLE mh, PNET_BUFFER_LIST nbl,
                    uint32_t port, uint32_t flags) {
    (void)mh; (void)port; (void)flags;
    if (!nbl || !nbl->first_buffer) return;
    NET_BUFFER* nb = nbl->first_buffer;
    if (nb->length > 0) rtl8188eu_probe((const char*)nb->data);
}
