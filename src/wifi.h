#ifndef WIFI_H
#define WIFI_H

#include <stdint.h>

#define WIFI_MAX_NETWORKS 16
#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASSWORD_MAX_LEN 64
#define WIFI_BSSID_LEN 6

typedef enum {
    WIFI_SECURITY_NONE = 0,
    WIFI_SECURITY_WEP,
    WIFI_SECURITY_WPA,
    WIFI_SECURITY_WPA2,
    WIFI_SECURITY_WPA3
} wifi_security_t;

typedef struct {
    char ssid[WIFI_SSID_MAX_LEN + 1];
    uint8_t bssid[WIFI_BSSID_LEN];
    uint8_t channel;
    int8_t rssi;
    wifi_security_t security;
} wifi_network_t;

typedef struct {
    char ssid[WIFI_SSID_MAX_LEN + 1];
    char password[WIFI_PASSWORD_MAX_LEN + 1];
    wifi_security_t security;
    uint8_t connected;
    int8_t rssi;
    uint8_t ip[4];
    uint8_t gateway[4];
    uint8_t subnet[4];
} wifi_connection_t;

typedef struct {
    int networks_found;
    int connected;
    char current_ssid[WIFI_SSID_MAX_LEN + 1];
} wifi_status_t;

void wifi_init(void);
int wifi_scan(wifi_network_t* networks, int max_networks);
int wifi_connect(const char* ssid, const char* password, wifi_security_t security);
int wifi_connect_with_bssid(const uint8_t* bssid, const char* ssid, const char* password, wifi_security_t security);
void wifi_disconnect(void);
int wifi_is_connected(void);
void wifi_poll(void);
int wifi_get_status(wifi_status_t* status);
int wifi_get_ip(uint8_t* ip_out);
int wifi_get_gateway(uint8_t* gw_out);
int wifi_get_dns(uint8_t* dns_out);
int wifi_probe(const uint8_t target_ip[4], int timeout_ms);

#endif /* WIFI_H */
