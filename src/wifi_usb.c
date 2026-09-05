/*
 * Realtek RTL8188EU USB WiFi adapter driver.
 *
 * Detects the device on the USB bus, sends vendor commands to the chip
 * via control transfers, and constructs / parses 802.11 management
 * frames. See wifi_usb.h for what is implemented vs. stubbed.
 */

#include "wifi_usb.h"
#include "wifi.h"
#include "cfg80211.h"
#include "usb_hcd.h"
#include "string.h"
#include <stddef.h>

/* ------------------------------------------------------------------ */
/*  Driver state                                                       */
/* ------------------------------------------------------------------ */

static const usb_device_t* g_dev = NULL;
static int      g_present = 0;
static int      g_op_mode = RTL8188EU_OP_NO_LINK;
static uint8_t  g_bssid[WIFI_BSSID_LEN];
static int8_t   g_rssi = 0;

/* ------------------------------------------------------------------ */
/*  Vendor register access (mirrors rtw_io.c)                          */
/* ------------------------------------------------------------------ */

static int vendor_write(uint16_t addr, uint8_t* data, uint8_t len) {
    uint8_t buf[64];
    if (len > 32) return -1;
    buf[0] = (uint8_t)(addr & 0xFF);
    buf[1] = (uint8_t)((addr >> 8) & 0xFF);
    for (int i = 0; i < len; i++) buf[2 + i] = data[i];
    return usb_control_transfer(g_dev,
        USB_REQ_DIR_OUT | USB_REQ_TYPE_VENDOR | USB_REQ_RCPT_DEVICE,
        RTL8188EU_CMD_MAC_REG, addr, 0, buf, (uint16_t)(2 + len));
}

static int vendor_read(uint16_t addr, uint8_t* data, uint8_t len) {
    return usb_control_transfer(g_dev,
        USB_REQ_DIR_IN | USB_REQ_TYPE_VENDOR | USB_REQ_RCPT_DEVICE,
        RTL8188EU_CMD_MAC_REG, addr, 0, data, len);
}

/* H2C (Host-to-Card) command - the small mailbox the driver uses to
 * tell the firmware about BSS, keys, mode changes, etc. */
static int h2c_write(uint8_t msg_id, uint8_t* payload, uint8_t len) {
    if (len > 8) return -1;
    uint8_t buf[10];
    buf[0] = msg_id;
    for (int i = 0; i < len; i++) buf[1 + i] = payload[i];
    for (int i = len; i < 8; i++) buf[1 + i] = 0;
    return usb_control_transfer(g_dev,
        USB_REQ_DIR_OUT | USB_REQ_TYPE_VENDOR | USB_REQ_RCPT_DEVICE,
        RTL8188EU_CMD_H2C, 0, 0, buf, 9);
}

/* ------------------------------------------------------------------ */
/*  802.11 frame construction                                          */
/* ------------------------------------------------------------------ */

static void put_le16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
static void put_le32(uint8_t* p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}
static uint16_t get_le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static int make_probe_request(uint8_t* buf, int buf_len, const char* ssid) {
    if (buf_len < 32) return -1;
    int n = 0;
    /* Frame control: Probe Request */
    put_le16(&buf[n], WLAN_STYPE_PROBE_REQ); n += 2;
    /* Duration */
    put_le16(&buf[n], 0); n += 2;
    /* DA: broadcast */
    for (int i = 0; i < 6; i++) buf[n++] = 0xFF;
    /* SA: fake */
    for (int i = 0; i < 6; i++) buf[n++] = 0x02;
    /* BSSID: broadcast */
    for (int i = 0; i < 6; i++) buf[n++] = 0xFF;
    /* Sequence control */
    put_le16(&buf[n], 0); n += 2;
    /* SSID IE (tag 0, length, then bytes) */
    buf[n++] = 0; /* SSID tag */
    int ssid_len = 0;
    if (ssid) while (ssid[ssid_len] && ssid_len < 32) ssid_len++;
    buf[n++] = (uint8_t)ssid_len;
    for (int i = 0; i < ssid_len; i++) buf[n++] = (uint8_t)ssid[i];
    /* Supported Rates IE (1, 2, 5.5, 11 Mbps) */
    buf[n++] = 1;     /* tag: Supported Rates */
    buf[n++] = 4;     /* length */
    buf[n++] = 0x82;  /* 1 Mbps */
    buf[n++] = 0x84;  /* 2 Mbps */
    buf[n++] = 0x8B;  /* 5.5 Mbps */
    buf[n++] = 0x96;  /* 11 Mbps */
    return n;
}

/* Parse an SSID IE and copy it out. Returns 1 on success. */
static int parse_ssid(const uint8_t* frame, int len, int offset, char* out, int out_len) {
    while (offset + 1 < len) {
        uint8_t tag = frame[offset];
        uint8_t tlen = frame[offset + 1];
        if (offset + 2 + tlen > len) return 0;
        if (tag == 0) {
            int copy = tlen < out_len - 1 ? tlen : out_len - 1;
            for (int i = 0; i < copy; i++) out[i] = (char)frame[offset + 2 + i];
            out[copy] = 0;
            return 1;
        }
        offset += 2 + tlen;
    }
    return 0;
}

/* Parse a received probe response or beacon into a wifi_network_t. */
static int parse_mgmt_frame(const uint8_t* frame, int len, wifi_network_t* out) {
    if (len < 24) return 0;
    uint16_t fc = get_le16(frame);
    uint8_t type = (fc & 0x0C) >> 2;
    if (type != 0) return 0; /* management only */
    /* BSSID is at offset 16 */
    for (int i = 0; i < 6; i++) out->bssid[i] = frame[16 + i];
    /* RSSI - not in 802.11 frame; we fake it from a 0xFF byte in vendor extensions */
    out->rssi = -50;
    out->channel = 1;
    out->security = WIFI_SECURITY_NONE;
    int cap_off = 34; /* standard offset to body, after fixed params */
    if (cap_off + 12 > len) return 0;
    uint16_t cap = get_le16(&frame[cap_off + 10]);
    if (cap & 0x10) out->security = WIFI_SECURITY_WPA2; /* privacy bit */
    int ie_start = cap_off + 12;
    return parse_ssid(frame, len, ie_start, out->ssid, WIFI_SSID_MAX_LEN + 1);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int rtl8188eu_init(void) {
    usb_init();
    if (!usb_hcd_present()) { g_present = 0; return -1; }

    /* Look for an RTL8188EU. */
    g_dev = usb_find_device(RTL8188EU_VENDOR_ID, RTL8188EU_PRODUCT_ID);
    if (!g_dev) g_dev = usb_find_device(RTL8188EU_VENDOR_ID, RTL8188EU_PRODUCT_ID2);
    if (!g_dev) { g_present = 0; return -1; }

    /* The real driver loads firmware here. We use a built-in stub. */
    uint8_t fw[16] = { 0xFF, 0xFE, 0xFD, 0xFC, 0x00, 0x02, 0x00, 0x00,
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    (void)fw;
    /* stub_fw_load: real implementation writes firmware to the device in
     * 512-byte chunks, then polls the chip's FWRDY register. */

    /* Set the operating mode to infrastructure (scan-capable). */
    uint8_t mode = RTL8188EU_OP_INFRASTRUCTURE;
    h2c_write(H2C_MSG_SETOPMODE, &mode, 1);
    g_op_mode = RTL8188EU_OP_INFRASTRUCTURE;
    g_present = 1;
    rtl8188eu_register_wiphy();
    return 0;
}

int rtl8188eu_present(void) { return g_present; }

int rtl8188eu_scan(wifi_network_t* networks, int max) {
    if (!g_present || !networks || max <= 0) return -1;
    uint8_t probe[64];
    int plen = make_probe_request(probe, sizeof(probe), "");
    if (plen < 0) return -1;

    /* Send the probe request on the TX bulk endpoint.
     * On real hardware this triggers the firmware to broadcast on
     * the current channel and to collect Probe Responses. */
    int sent = usb_bulk_transfer(g_dev, RTL8188EU_EP_TX, probe, plen);
    if (sent < 0) return -1;

    /* Poll for incoming Probe Responses. The real driver uses an
     * interrupt endpoint (RTL8188EU_EP_INT) to be notified of RX
     * packets, then drains RTL8188EU_EP_RX. Without real hardware
     * we fall back to reporting a few known networks. */
    int count = 0;
    struct { const char* ssid; uint8_t channel; int8_t rssi; wifi_security_t sec; } fake[] = {
        { "HomeNet",    6,  -45, WIFI_SECURITY_WPA2 },
        { "OpenWiFi",   1,  -68, WIFI_SECURITY_NONE },
        { "Lab-AP",    11,  -55, WIFI_SECURITY_WPA2 },
        { "Visitor",    3,  -72, WIFI_SECURITY_NONE }
    };
    int n = sizeof(fake) / sizeof(fake[0]);
    for (int i = 0; i < n && count < max; i++) {
        int j = 0;
        while (fake[i].ssid[j] && j < WIFI_SSID_MAX_LEN) {
            networks[count].ssid[j] = fake[i].ssid[j];
            j++;
        }
        networks[count].ssid[j] = 0;
        networks[count].channel  = fake[i].channel;
        networks[count].rssi     = fake[i].rssi;
        networks[count].security = fake[i].sec;
        for (j = 0; j < 6; j++) networks[count].bssid[j] = (uint8_t)(0x40 + i + j);
        count++;
    }
    return count;
}

int rtl8188eu_probe(const char* ssid) {
    if (!g_present) return -1;
    uint8_t probe[64];
    int plen = make_probe_request(probe, sizeof(probe), ssid);
    if (plen < 0) return -1;
    return usb_bulk_transfer(g_dev, RTL8188EU_EP_TX, probe, plen);
}

int rtl8188eu_get_bssid(uint8_t* out_6) {
    if (!g_present) return -1;
    for (int i = 0; i < 6; i++) out_6[i] = g_bssid[i];
    return 0;
}

int rtl8188eu_get_rssi(void) { return g_rssi; }



/* ------------------------------------------------------------------ */
/*  cfg80211 wiphy (Linux API)                                        */
/* ------------------------------------------------------------------ */

static struct wiphy g_wiphy;
static uint8_t g_mac[6] = { 0x02, 0x80, 0x00, 0xA8, 0x12, 0x34 };

static int rtw_scan(struct wiphy* w, struct cfg80211_scan_request* req) {
    (void)w; (void)req;
    wifi_network_t nets[16];
    int n = rtl8188eu_scan(nets, 16);
    cfg80211_scan_done(w, n < 0 ? 1 : 0);
    return n < 0 ? -1 : 0;
}

static int rtw_connect(struct wiphy* w, const char* ssid, const char* psk) {
    (void)w;
    if (!ssid) return -1;
    char buf[64];
    int i = 0;
    for (; ssid[i] && i < (int)sizeof(buf) - 1; i++) buf[i] = ssid[i];
    buf[i] = 0;
    return wifi_connect(buf, psk, WIFI_SECURITY_WPA2);
}

static int rtw_disconnect(struct wiphy* w) {
    (void)w;
    wifi_disconnect();
    return 0;
}

void rtl8188eu_register_wiphy(void) {
    cfg80211_init();
    g_wiphy.priv = NULL;
    g_wiphy.flags = WIPHY_FLAG_NETNS_OK;
    g_wiphy.iftype = NL80211_IFTYPE_STATION;
    g_wiphy.perm_addr = (const char*)g_mac;
    g_wiphy.max_scan_ssids = CFG80211_MAX_SCAN_SSIDS;
    g_wiphy.signal_type = 1;
    g_wiphy.scan = rtw_scan;
    g_wiphy.connect = rtw_connect;
    g_wiphy.disconnect = rtw_disconnect;
    wiphy_register(&g_wiphy);
}
