#ifndef CFG80211_H
#define CFG80211_H

#include <stdint.h>
#include <stddef.h>

/*
 * Mini-cfg80211: a minimal subset of the Linux kernel's wireless
 * configuration API (cfg80211). This gives us a Linux-compatible
 * driver interface so a driver ported from Linux's `r8188eu`,
 * `rtw88`, `ath9k`, etc. can plug in with minimal changes.
 *
 * Reference (Linux kernel): include/net/cfg80211.h, include/linux/ieee80211.h
 *
 * The structs and function names below follow the kernel naming,
 * but stripped down to what fits a hobby OS.
 */

/* ------------------------------------------------------------------ */
/*  Common enums / flags                                              */
/* ------------------------------------------------------------------ */

enum nl80211_iftype {
    NL80211_IFTYPE_STATION = 2,
    NL80211_IFTYPE_AP      = 3,
    NL80211_IFTYPE_MONITOR = 6,
    NL80211_IFTYPE_P2P_CLIENT = 8
};

enum ieee80211_band {
    IEEE80211_BAND_2GHZ = 0,
    IEEE80211_BAND_5GHZ = 1
};

#define IEEE80211_MAX_SSID_LEN     32
#define IEEE80211_NUM_BANDS        2
#define IEEE80211_MAX_TXPOWER      20
#define CFG80211_MAX_SCAN_SSIDS    4

/* Bit flags for wiphy capabilities (linux: wiphy->flags). */
#define WIPHY_FLAG_NETNS_OK        0x00000001
#define WIPHY_FLAG_PS_ON_BY_DEFAULT 0x00000002
#define WIPHY_FLAG_4ADDR_AP        0x00000004
#define WIPHY_FLAG_SUPPORTS_TDLS   0x00000008

/* ------------------------------------------------------------------ */
/*  Scan results (matches struct cfg80211_bss_ies)                     */
/* ------------------------------------------------------------------ */

struct cfg80211_bss {
    uint8_t  bssid[6];
    int8_t   signal;            /* dBm */
    uint16_t capability;
    uint8_t  channel;
    uint8_t  ssid_len;
    char     ssid[IEEE80211_MAX_SSID_LEN + 1];
    uint8_t  security;
};

#define CFG80211_BSS_ENTRIES 16

struct cfg80211_bss_list {
    uint32_t           n;
    struct cfg80211_bss bss[CFG80211_BSS_ENTRIES];
};

/* ------------------------------------------------------------------ */
/*  Scan request (from upper layer to driver)                          */
/* ------------------------------------------------------------------ */

struct cfg80211_scan_request {
    uint8_t  n_ssids;
    struct {
        uint8_t len;
        char    ssid[IEEE80211_MAX_SSID_LEN + 1];
    } ssids[CFG80211_MAX_SCAN_SSIDS];
};

/* ------------------------------------------------------------------ */
/*  Wiphy (the wireless device, the cfg80211 equivalent of "adapter")  */
/* ------------------------------------------------------------------ */

struct wiphy {
    void*    priv;              /* driver-private data pointer */
    uint32_t flags;
    enum nl80211_iftype iftype;
    const char*   perm_addr;
    int      max_scan_ssids;
    int      signal_type;       /* CFG80211_SIGNAL_TYPE_* */
    /* Driver callbacks - these are the same as Linux's cfg80211 ops. */
    int  (*scan)(struct wiphy* w, struct cfg80211_scan_request* req);
    int  (*connect)(struct wiphy* w, const char* ssid, const char* psk);
    int  (*disconnect)(struct wiphy* w);
    /* Upper-layer notifications the driver calls. */
    void (*cfg80211_scan_done)(struct wiphy* w, int aborted);
    void (*cfg80211_roamed)(struct wiphy* w, const uint8_t* bssid);
    void (*cfg80211_rx_mgmt)(struct wiphy* w, int freq, int sig_mbm,
                             const uint8_t* buf, size_t len);
};

/* Notification functions a driver calls when something happens. */
void cfg80211_scan_done(struct wiphy* w, int aborted);
void cfg80211_roamed(struct wiphy* w, const uint8_t* bssid);
void cfg80211_rx_mgmt(struct wiphy* w, int freq, int sig_mbm,
                      const uint8_t* buf, size_t len);

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

void   cfg80211_init(void);

/* Register a wireless device (called by the driver at init). */
int    wiphy_register(struct wiphy* w);

/* Find a registered wiphy. */
struct wiphy* wiphy_find_by_addr(const uint8_t bssid[6]);
struct wiphy* wiphy_first(void);

/* Upper-layer calls (user app / connection manager). */
int    cfg80211_scan(struct wiphy* w, struct cfg80211_scan_request* req);
int    cfg80211_connect(struct wiphy* w, const char* ssid, const char* psk);
int    cfg80211_disconnect(struct wiphy* w);
int    cfg80211_get_scan_results(struct wiphy* w, struct cfg80211_bss_list* out);

#endif /* CFG80211_H */
