#include "http.h"
#include "tcp.h"
#include "string.h"
#include "io.h"

void http_init(void) {}

int http_client_connect(http_client_t* client, const uint8_t* server_ip, uint16_t port) {
    if (!client || !server_ip) return -1;
    client->server_ip[0] = server_ip[0]; client->server_ip[1] = server_ip[1];
    client->server_ip[2] = server_ip[2]; client->server_ip[3] = server_ip[3];
    client->port = port;
    client->connected = 1;
    return 0;
}

void http_client_disconnect(http_client_t* client) {
    if (client) client->connected = 0;
}

static int http_request(http_client_t* client, const char* method, const char* path,
                        const char* body, int body_len, http_response_t* response) {
    if (!client || !client->connected) return -1;
    tcp_socket_t* sock = tcp_socket_create();
    if (!sock) return -1;
    if (tcp_connect(sock, client->server_ip, client->port) != 0) {
        tcp_socket_close(sock);
        return -1;
    }
    char request[HTTP_BUFFER_SIZE];
    int idx = 0;
    for (int i = 0; method[i] && idx < HTTP_BUFFER_SIZE - 1; i++) request[idx++] = method[i];
    request[idx++] = ' ';
    for (int i = 0; path[i] && idx < HTTP_BUFFER_SIZE - 1; i++) request[idx++] = path[i];
    const char* suffix = " HTTP/1.1\r\nHost: ";
    for (int i = 0; suffix[i] && idx < HTTP_BUFFER_SIZE - 1; i++) request[idx++] = suffix[i];
    char ip_str[16];
    ip_str[0] = '0' + (client->server_ip[0] / 100);
    ip_str[1] = '0' + ((client->server_ip[0] / 10) % 10);
    ip_str[2] = '0' + (client->server_ip[0] % 10);
    ip_str[3] = '.';
    ip_str[4] = '0' + (client->server_ip[1] / 100);
    ip_str[5] = '0' + ((client->server_ip[1] / 10) % 10);
    ip_str[6] = '0' + (client->server_ip[1] % 10);
    ip_str[7] = '.';
    ip_str[8] = '0' + (client->server_ip[2] / 100);
    ip_str[9] = '0' + ((client->server_ip[2] / 10) % 10);
    ip_str[10] = '0' + (client->server_ip[2] % 10);
    ip_str[11] = '.';
    ip_str[12] = '0' + (client->server_ip[3] / 100);
    ip_str[13] = '0' + ((client->server_ip[3] / 10) % 10);
    ip_str[14] = '0' + (client->server_ip[3] % 10);
    ip_str[15] = 0;
    for (int i = 0; ip_str[i] && idx < HTTP_BUFFER_SIZE - 1; i++) request[idx++] = ip_str[i];
    const char* crlf = "\r\nConnection: close\r\n\r\n";
    for (int i = 0; crlf[i] && idx < HTTP_BUFFER_SIZE - 1; i++) request[idx++] = crlf[i];
    if (tcp_send(sock, request, idx) < 0) {
        tcp_socket_close(sock);
        return -1;
    }
    char response_buf[2048];
    int total = 0;
    for (int i = 0; i < 50; i++) {
        int n = tcp_recv(sock, response_buf + total, sizeof(response_buf) - total - 1);
        if (n > 0) total += n;
        if (total >= 4) {
            int found = 0;
            for (int j = 0; j < total - 3; j++) {
                if (response_buf[j] == '\r' && response_buf[j+1] == '\n' &&
                    response_buf[j+2] == '\r' && response_buf[j+3] == '\n') {
                    found = 1;
                    break;
                }
            }
            if (found) break;
        }
    }
    response_buf[total] = 0;
    if (response) {
        if (total > 5 && response_buf[0] == 'H' && response_buf[1] == 'T' &&
            response_buf[2] == 'T' && response_buf[3] == 'P') {
            int code = 0;
            int j = 9;
            while (response_buf[j] >= '0' && response_buf[j] <= '9') {
                code = code * 10 + (response_buf[j] - '0');
                j++;
            }
            response->status_code = code;
            int l = 0;
            for (int j = 0; j < total && l < 2047; j++) {
                response->body[l++] = response_buf[j];
            }
            response->body[l] = 0;
            response->body_len = l;
        } else {
            response->status_code = 0;
            response->body_len = 0;
        }
    }
    tcp_socket_close(sock);
    return 0;
}

int http_get(http_client_t* client, const char* path, http_response_t* response) {
    if (!path) path = "/";
    return http_request(client, "GET", path, (const char*)0, 0, response);
}

int http_post(http_client_t* client, const char* path, const char* data, int len, http_response_t* response) {
    if (!path) path = "/";
    return http_request(client, "POST", path, data, len, response);
}
#include "http.h"
#include "tcp.h"
#include "string.h"
#include "io.h"
