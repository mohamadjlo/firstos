#ifndef BROWSER_NETWORK_SERVICE_H
#define BROWSER_NETWORK_SERVICE_H

#include "io.h"
#include "bnet_error.h"
#include "net_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NET_SERVICE_STATE_IDLE = 0,
    NET_SERVICE_STATE_RESOLVING,
    NET_SERVICE_STATE_CONNECTING,
    NET_SERVICE_STATE_SENDING,
    NET_SERVICE_STATE_RECEIVING,
    NET_SERVICE_STATE_COMPLETE,
    NET_SERVICE_STATE_ERROR
} browser_net_state_t;

typedef struct {
    browser_net_state_t state;
    u32 generation;
    int status_code;
    int body_len;
    char mime[64];
    char error_msg[128];
    bnet_error_t error_code;
} browser_network_result_t;

typedef struct {
    void (*on_headers)(int status_code, const char *mime, u32 generation);
    void (*on_body_ready)(const char *body, int body_len, u32 generation);
    void (*on_error)(const char *err_msg, bnet_error_t code, u32 generation);
    void (*on_status)(const char *status_msg);
} browser_network_callbacks_t;

void browser_network_service_init(void);
int  browser_network_service_start_fetch(const char *url, u32 generation, const browser_network_callbacks_t *cbs);
int  browser_network_service_step(char *out_body, int max_len, browser_network_result_t *out_res);
void browser_network_service_cancel(void);
int  browser_network_service_is_in_flight(void);

#ifdef __cplusplus
}
#endif

#endif /* BROWSER_NETWORK_SERVICE_H */
