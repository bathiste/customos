/*
 * Mini-cfg80211 implementation.
 *
 * Provides a minimal Linux-cfg80211-compatible API surface. Drivers
 * register a `struct wiphy` with the same fields, types, and
 * function-pointer names as Linux's `include/net/cfg80211.h`.
 */

#include "cfg80211.h"
#include "wifi_usb.h"
#include "wifi.h"
#include "string.h"
#include <stddef.h>

#define MAX_WIPHYS 4

static struct wiphy* g_wiphys[MAX_WIPHYS];
static int g_initialized = 0;

void cfg80211_init(void) {
    if (g_initialized) return;
    g_initialized = 1;
    for (int i = 0; i < MAX_WIPHYS; i++) g_wiphys[i] = (struct wiphy*)0;
}

int wiphy_register(struct wiphy* w) {
    if (!w) return -1;
    for (int i = 0; i < MAX_WIPHYS; i++) {
        if (g_wiphys[i] == 0) {
            g_wiphys[i] = w;
            return 0;
        }
    }
    return -1;
}

struct wiphy* wiphy_find_by_addr(const uint8_t bssid[6]) {
    for (int i = 0; i < MAX_WIPHYS; i++) {
        if (g_wiphys[i] && g_wiphys[i]->perm_addr) {
            const uint8_t* a = (const uint8_t*)g_wiphys[i]->perm_addr;
            int match = 1;
            for (int k = 0; k < 6; k++) {
                if (a[k] != bssid[k]) { match = 0; break; }
            }
            if (match) return g_wiphys[i];
        }
    }
    return 0;
}

struct wiphy* wiphy_first(void) {
    for (int i = 0; i < MAX_WIPHYS; i++)
        if (g_wiphys[i]) return g_wiphys[i];
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Driver -> upper layer notifications (Linux: cfg80211_*)            */
/* ------------------------------------------------------------------ */

void cfg80211_scan_done(struct wiphy* w, int aborted) {
    if (w && w->cfg80211_scan_done) w->cfg80211_scan_done(w, aborted);
}

void cfg80211_roamed(struct wiphy* w, const uint8_t* bssid) {
    if (w && w->cfg80211_roamed) w->cfg80211_roamed(w, bssid);
}

void cfg80211_rx_mgmt(struct wiphy* w, int freq, int sig_mbm,
                      const uint8_t* buf, size_t len) {
    if (w && w->cfg80211_rx_mgmt) w->cfg80211_rx_mgmt(w, freq, sig_mbm, buf, len);
}

/* ------------------------------------------------------------------ */
/*  Upper-layer API                                                   */
/* ------------------------------------------------------------------ */

int cfg80211_scan(struct wiphy* w, struct cfg80211_scan_request* req) {
    if (!w || !w->scan) return -1;
    return w->scan(w, req);
}

int cfg80211_connect(struct wiphy* w, const char* ssid, const char* psk) {
    if (!w || !w->connect) return -1;
    return w->connect(w, ssid, psk);
}

int cfg80211_disconnect(struct wiphy* w) {
    if (!w || !w->disconnect) return -1;
    return w->disconnect(w);
}

int cfg80211_get_scan_results(struct wiphy* w, struct cfg80211_bss_list* out) {
    if (!w || !out) return -1;
    wifi_network_t nets[CFG80211_BSS_ENTRIES];
    int n = wifi_scan(nets, CFG80211_BSS_ENTRIES);
    if (n < 0) return -1;
    out->n = (uint32_t)n;
    for (int i = 0; i < n; i++) {
        struct cfg80211_bss* b = &out->bss[i];
        memset(b, 0, sizeof(*b));
        for (int k = 0; k < 6; k++) b->bssid[k] = nets[i].bssid[k];
        b->signal = nets[i].rssi;
        b->channel = nets[i].channel;
        b->capability = (nets[i].security != WIFI_SECURITY_NONE) ? 0x10 : 0x00;
        b->security = (uint8_t)nets[i].security;
        b->ssid_len = (uint8_t)strlen(nets[i].ssid);
        for (int k = 0; k < b->ssid_len; k++) b->ssid[k] = nets[i].ssid[k];
    }
    return n;
}
