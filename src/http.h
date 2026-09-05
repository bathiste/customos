#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>

#define HTTP_PORT 80
#define HTTP_BUFFER_SIZE 4096

typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_RESPONSE
} http_msg_type_t;

typedef struct {
    http_msg_type_t type;
    char method[8];
    char uri[256];
    char host[128];
    char headers[512];
    char body[1024];
    int body_len;
    int status_code;
} http_request_t;

typedef struct {
    int status_code;
    char status_text[32];
    char headers[512];
    char body[2048];
    int body_len;
} http_response_t;

typedef struct {
    uint8_t server_ip[4];
    uint16_t port;
    uint8_t connected;
} http_client_t;

void http_init(void);
int http_client_connect(http_client_t* client, const uint8_t* server_ip, uint16_t port);
void http_client_disconnect(http_client_t* client);
int http_get(http_client_t* client, const char* path, http_response_t* response);
int http_post(http_client_t* client, const char* path, const char* data, int len, http_response_t* response);

#endif
#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>

#define HTTP_PORT 80
#define HTTP_BUFFER_SIZE 4096

typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_RESPONSE
} http_msg_type_t;

typedef struct {
    http_msg_type_t type;
    char method[8];
    char uri[256];
    char host[128];
    char headers[512];
    char body[1024];
    int body_len;
    int status_code;
} http_request_t;

typedef struct {
    int status_code;
    char status_text[32];
    char headers[512];
    char body[2048];
    int body_len;
} http_response_t;

typedef struct {
    uint8_t server_ip[4];
    uint16_t port;
    uint8_t connected;
} http_client_t;

void http_init(void);
int http_client_connect(http_client_t* client, const uint8_t* server_ip, uint16_t port);
void http_client_disconnect(http_client_t* client);
int http_get(http_client_t* client, const char* path, http_response_t* response);
int http_post(http_client_t* client, const char* path, const char* data, int len, http_response_t* response);

#endif /* HTTP_H */
