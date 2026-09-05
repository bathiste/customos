/*
 * wlan_compat: thin abstraction that hides whether the call goes to
 * the NDIS or the cfg80211 layer. The result is the same either way.
 *
 * Application code (or the same driver binary recompiled) can use
 * this layer to target either "Windows" or "Linux" WiFi APIs without
 * caring which one is actually in use.
 */

#include "wlan_compat.h"
#include "wifi_usb.h"
#include "wifi.h"
#include "string.h"
#include <stddef.h>

static wlan_api_t g_api = WLAN_API_AUTO;

void wlan_compat_set_api(wlan_api_t api) { g_api = api; }
wlan_api_t wlan_compat_get_api(void) { return g_api; }

void* wlan_compat_open(void) {
    /* Bring up both stacks. Either may be the one that finds a real
     * adapter; the other one simply returns ADAPTER_NOT_FOUND. */
    ndis_init();
    cfg80211_init();

    /* Decide which API to expose. */
    wlan_api_t use;
    if (g_api == WLAN_API_AUTO) {
        use = rtl8188eu_present() ? WLAN_API_CFG80211 : WLAN_API_NDIS;
    } else {
        use = g_api;
    }

    if (use == WLAN_API_NDIS) {
        return (void*)ndis_open(RTL8188EU_VENDOR_ID, RTL8188EU_PRODUCT_ID);
    } else {
        struct wiphy* w = wiphy_first();
        return (void*)w;
    }
}

void wlan_compat_close(void* h) {
    if (!h) return;
    if (g_api == WLAN_API_NDIS || (g_api == WLAN_API_AUTO && rtl8188eu_present() == 0)) {
        ndis_close((NDIS_HANDLE)h);
    }
    /* cfg80211 wiphy lifetime is process-static - no close. */
}

int wlan_compat_scan(void* h, void* out_list) {
    if (!h || !out_list) return -1;
    wlan_api_t use = g_api;
    if (use == WLAN_API_AUTO)
        use = rtl8188eu_present() ? WLAN_API_CFG80211 : WLAN_API_NDIS;

    if (use == WLAN_API_NDIS) {
        return ndis_wifi_scan((NDIS_HANDLE)h, (NDIS_WLAN_BSS_LIST*)out_list);
    } else {
        return cfg80211_get_scan_results((struct wiphy*)h,
                                          (struct cfg80211_bss_list*)out_list);
    }
}

int wlan_compat_connect(void* h, const char* ssid, const char* psk) {
    if (!h) return -1;
    wlan_api_t use = g_api;
    if (use == WLAN_API_AUTO)
        use = rtl8188eu_present() ? WLAN_API_CFG80211 : WLAN_API_NDIS;
    if (use == WLAN_API_NDIS) return ndis_wifi_connect((NDIS_HANDLE)h, ssid, psk);
    return cfg80211_connect((struct wiphy*)h, ssid, psk);
}

int wlan_compat_disconnect(void* h) {
    if (!h) return -1;
    wlan_api_t use = g_api;
    if (use == WLAN_API_AUTO)
        use = rtl8188eu_present() ? WLAN_API_CFG80211 : WLAN_API_NDIS;
    if (use == WLAN_API_NDIS) return ndis_wifi_disconnect((NDIS_HANDLE)h);
    return cfg80211_disconnect((struct wiphy*)h);
}

int wlan_compat_rssi(void* h) {
    if (!h) return -1;
    wlan_api_t use = g_api;
    if (use == WLAN_API_AUTO)
        use = rtl8188eu_present() ? WLAN_API_CFG80211 : WLAN_API_NDIS;
    if (use == WLAN_API_NDIS) return ndis_wifi_rssi((NDIS_HANDLE)h);
    return rtl8188eu_get_rssi();
}

int wlan_compat_send(void* h, const void* data, uint32_t len) {
    if (!h) return -1;
    wlan_api_t use = g_api;
    if (use == WLAN_API_AUTO)
        use = rtl8188eu_present() ? WLAN_API_CFG80211 : WLAN_API_NDIS;
    if (use == WLAN_API_NDIS)
        return (ndis_send((NDIS_HANDLE)h, data, len) == NDIS_STATUS_SUCCESS) ? 0 : -1;
    /* cfg80211 mgmt tx path */
    if (len > 0) rtl8188eu_probe((const char*)data);
    return 0;
}
