#ifndef WLAN_COMPAT_H
#define WLAN_COMPAT_H

/*
 * Cross-API compatibility shim.
 *
 * On real Windows, a WiFi driver is an NDIS miniport. On real Linux,
 * it is a cfg80211/softmac driver. CustomOS supports BOTH, so the
 * same chipset driver (rtl8188eu) can be exposed either way.
 *
 * The `wlan_compat_*` helpers below let application code call the
 * appropriate API based on which OS the binary is targeting, without
 * caring which underlying stack is doing the work. This is the
 * abstraction layer that makes the stack "look like" Windows or
 * Linux at the API level.
 */

#include "ndis.h"
#include "cfg80211.h"

typedef enum {
    WLAN_API_AUTO   = 0,  /* pick whichever is initialised first */
    WLAN_API_NDIS   = 1,  /* Windows-style NDIS 6.x */
    WLAN_API_CFG80211 = 2 /* Linux-style cfg80211 */
} wlan_api_t;

/* Set the preferred API (or AUTO). */
void wlan_compat_set_api(wlan_api_t api);
wlan_api_t wlan_compat_get_api(void);

/* Open the WiFi adapter. Returns an opaque handle usable with all
 * the other wlan_compat_* functions, regardless of which API it
 * wraps internally. */
void* wlan_compat_open(void);
void  wlan_compat_close(void* h);

/* High-level operations, all-API equivalent. */
int   wlan_compat_scan(void* h, void* out_list);
int   wlan_compat_connect(void* h, const char* ssid, const char* psk);
int   wlan_compat_disconnect(void* h);
int   wlan_compat_rssi(void* h);
int   wlan_compat_send(void* h, const void* data, uint32_t len);

#endif /* WLAN_COMPAT_H */
