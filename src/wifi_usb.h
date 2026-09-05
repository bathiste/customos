#ifndef WIFI_USB_H
#define WIFI_USB_H

#include <stdint.h>
#include "usb_hcd.h"
#include "wifi.h"

/*
 * Realtek RTL8188EU 802.11n USB WiFi adapter driver.
 *
 * This driver targets the RTL8188EU chipset used in many low-cost USB
 * WiFi dongles (the same family supported by Linux's r8188eu module and
 * by the rtl8188eu Windows driver set). It implements the same vendor
 * command surface that those drivers use, so the OS side of the stack
 * is faithful to the real device.
 *
 * Hardware reference: Realtek "RTL8188EUS" datasheet, rev 1.4.
 *
 * What this driver implements (functional):
 *   - Chip detection via USB device VID/PID (0x0BDA / 0x8179)
 *   - Vendor command set used for register access (CMD_MAC / CMD_BB /
 *     CMD_RF) and for H2C / C2H mailbox transfers
 *   - 802.11 management frame send/receive over the bulk endpoints
 *   - Scan (broadcast Probe Request, collect Probe Responses)
 *   - WPA2 4-way handshake (uses CCMP / AES-CCM, simplified)
 *
 * What is stubbed (clearly marked, with `stub_` prefix):
 *   - Firmware loading (we use a built-in minimal firmware blob)
 *   - Rate control / TX aggregation / AMPDU
 *   - Hardware crypto acceleration (we use software AES-CCM)
 *   - RF calibration, power management, antenna diversity
 *   - 802.11n HT capabilities / MIMO
 *
 * The intent is to provide a working, demonstrable USB WiFi stack that
 * scans and reports networks, and to make the structure of the driver
 * match the production driver so that the stubs are obvious.
 */

/* ------------------------------------------------------------------ */
/*  Known RTL8188EU device IDs (from Linux r8188eu / rtl8188eu driver)  */
/* ------------------------------------------------------------------ */

#define RTL8188EU_VENDOR_ID  0x0BDA
#define RTL8188EU_PRODUCT_ID 0x8179
#define RTL8188EU_PRODUCT_ID2 0x0179

/* ------------------------------------------------------------------ */
/*  Endpoint numbers (after SET_CONFIGURATION)                         */
/* ------------------------------------------------------------------ */

#define RTL8188EU_EP_CMD     0x00    /* OUT - H2C commands */
#define RTL8188EU_EP_TX      0x02    /* OUT - 802.11 frames */
#define RTL8188EU_EP_RX      0x81    /* IN  - 802.11 frames */
#define RTL8188EU_EP_INT     0x83    /* IN  - C2H events */

/* ------------------------------------------------------------------ */
/*  Vendor command codes (from rtw_cmd.c)                              */
/* ------------------------------------------------------------------ */

#define RTL8188EU_CMD_MAC_REG    0x00
#define RTL8188EU_CMD_BB_REG     0x01
#define RTL8188EU_CMD_RF_REG     0x02
#define RTL8188EU_CMD_H2C        0x03
#define RTL8188EU_CMD_C2H        0x04
#define RTL8188EU_CMD_FW_DOWNLOAD 0x05

/* H2C message IDs (subset) */
#define H2C_MSG_JOINBSS       0x0B
#define H2C_MSG_SITESURVEY    0x0C
#define H2C_MSG_SETOPMODE     0x0F
#define H2C_MSG_SETSECURITY   0x10
#define H2C_MSG_SETKEY        0x11
#define H2C_MSG_SETSTAKEY     0x12
#define H2C_MSG_SETPHY        0x18
#define H2C_MSG_MP_START      0xB1

/* Operating modes */
#define RTL8188EU_OP_NO_LINK     0
#define RTL8188EU_OP_ADHOC       1
#define RTL8188EU_OP_INFRASTRUCTURE 2

/* ------------------------------------------------------------------ */
/*  802.11 frame constants                                             */
/* ------------------------------------------------------------------ */

#define WLAN_FTYPE_MGMT        0x00
#define WLAN_FTYPE_CTL         0x04
#define WLAN_FTYPE_DATA        0x08

#define WLAN_STYPE_PROBE_REQ   (WLAN_FTYPE_MGMT | 0x04)
#define WLAN_STYPE_PROBE_RESP  (WLAN_FTYPE_MGMT | 0x05)
#define WLAN_STYPE_BEACON      (WLAN_FTYPE_MGMT | 0x08)
#define WLAN_STYPE_AUTH        (WLAN_FTYPE_MGMT | 0x0B)
#define WLAN_STYPE_DEAUTH      (WLAN_FTYPE_MGMT | 0x0C)
#define WLAN_STYPE_ASSOC_REQ   (WLAN_FTYPE_MGMT | 0x00)
#define WLAN_STYPE_ASSOC_RESP  (WLAN_FTYPE_MGMT | 0x01)

#define MAX_RX_FRAME 1024

/* ------------------------------------------------------------------ */
/*  Driver API                                                         */
/* ------------------------------------------------------------------ */

/* Initialise the WiFi subsystem (also calls usb_init internally). */
int  rtl8188eu_init(void);

/* Returns 1 if a supported adapter was found. */
int  rtl8188eu_present(void);

/* Send a probe request on each channel and collect responses.
 * Returns number of networks found, or -1 on error. */
int  rtl8188eu_scan(wifi_network_t* networks, int max);

/* Send a probe request to a specific SSID (active scan). */
int  rtl8188eu_probe(const char* ssid);

/* Get current BSSID / RSSI of the most recent scan. */
int  rtl8188eu_get_bssid(uint8_t* out_6);
int  rtl8188eu_get_rssi(void);

#endif /* WIFI_USB_H */
