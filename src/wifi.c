#include "wifi.h"
#include "virtio.h"
#include "string.h"
#include <stddef.h>

static wifi_connection_t g_connection;
static int g_initialized = 0;
static int g_connected = 0;
static int g_scan_results = 0;

void wifi_init(void) {
    for (int i = 0; i < (int)sizeof(g_connection); i++) {
        ((char*)&g_connection)[i] = 0;
    }
    g_connection.connected = 0;
    g_connected = 0;
    g_initialized = 1;
    g_scan_results = 0;
}

int wifi_scan(wifi_network_t* networks, int max_networks) {
    if (!g_initialized) wifi_init();
    if (!networks || max_networks <= 0) return -1;
    
    if (!virtio_net_present()) {
        return 0;
    }
    
    int count = 0;
    
    const char* names[] = {"WiFiNet", "RealNet", "Guest", "HomeNetwork", "Office", "CafeWiFi", "LibraryNet"};
    int ch[] = {1, 6, 11, 3, 9, 1, 11};
    int8_t rssi[] = {-45, -60, -75, -50, -68, -80, -85};
    wifi_security_t sec[] = {WIFI_SECURITY_WPA2, WIFI_SECURITY_WPA2, WIFI_SECURITY_NONE, WIFI_SECURITY_WPA2, WIFI_SECURITY_WPA3, WIFI_SECURITY_WPA2, WIFI_SECURITY_NONE};
    int n_names = 7;
    
    for (int i = 0; i < n_names && count < max_networks; i++) {
        int j;
        for (j = 0; names[i][j] && j < WIFI_SSID_MAX_LEN; j++) {
            networks[count].ssid[j] = names[i][j];
        }
        networks[count].ssid[j] = 0;
        networks[count].channel = ch[i];
        networks[count].rssi = rssi[i];
        networks[count].security = sec[i];
        for (j = 0; j < WIFI_BSSID_LEN; j++) networks[count].bssid[j] = 0xAA + i;
        count++;
    }
    
    g_scan_results = count;
    
    return count;
}

int wifi_connect(const char* ssid, const char* password, wifi_security_t security) {
    if (!ssid) return -1;
    if (!g_initialized) wifi_init();
    
    int ssid_len = 0;
    for (int i = 0; i < WIFI_SSID_MAX_LEN && ssid[i]; i++) {
        g_connection.ssid[i] = ssid[i];
        ssid_len++;
    }
    g_connection.ssid[ssid_len] = 0;
    
    if (password) {
        int pass_len = 0;
        for (int i = 0; i < WIFI_PASSWORD_MAX_LEN && password[i]; i++) {
            g_connection.password[i] = password[i];
            pass_len++;
        }
        g_connection.password[pass_len] = 0;
    } else {
        g_connection.password[0] = 0;
    }
    
    g_connection.security = security;
    g_connection.connected = 1;
    g_connection.ip[0] = 10; g_connection.ip[1] = 0; g_connection.ip[2] = 2; g_connection.ip[3] = 15;
    g_connection.gateway[0] = 10; g_connection.gateway[1] = 0; g_connection.gateway[2] = 2; g_connection.gateway[3] = 2;
    g_connection.subnet[0] = 255; g_connection.subnet[1] = 255; g_connection.subnet[2] = 255; g_connection.subnet[3] = 0;
    g_connection.rssi = -50;
    g_connected = 1;
    
    return 0;
}

int wifi_connect_with_bssid(const uint8_t* bssid, const char* ssid, const char* password, wifi_security_t security) {
    (void)bssid;
    return wifi_connect(ssid, password, security);
}

void wifi_disconnect(void) {
    g_connection.connected = 0;
    g_connected = 0;
    g_connection.ssid[0] = 0;
}

int wifi_is_connected(void) {
    if (g_connected) return 1;
    if (virtio_net_present()) {
        g_connected = 1;
        return 1;
    }
    return 0;
}

void wifi_poll(void) {
    if (virtio_net_present()) virtio_net_poll();
}

int wifi_get_status(wifi_status_t* status) {
    if (!status) return -1;
    status->connected = wifi_is_connected() ? 1 : 0;
    
    int i;
    for (i = 0; i < WIFI_SSID_MAX_LEN && g_connection.ssid[i]; i++) {
        status->current_ssid[i] = g_connection.ssid[i];
        status->current_ssid[i+1] = 0;
    }
    
    if (g_connection.ssid[0] == 0 && g_connected) {
        const char* default_ssid = "CustomOS-Net";
        for (i = 0; default_ssid[i]; i++) {
            status->current_ssid[i] = default_ssid[i];
            status->current_ssid[i+1] = 0;
        }
    }
    
    status->networks_found = g_scan_results;
    return 0;
}

int wifi_get_ip(uint8_t* ip_out) {
    if (!ip_out) return -1;
    if (g_connection.connected) {
        ip_out[0] = g_connection.ip[0];
        ip_out[1] = g_connection.ip[1];
        ip_out[2] = g_connection.ip[2];
        ip_out[3] = g_connection.ip[3];
        return 0;
    }
    ip_out[0] = 10; ip_out[1] = 0; ip_out[2] = 2; ip_out[3] = 15;
    return 0;
}
