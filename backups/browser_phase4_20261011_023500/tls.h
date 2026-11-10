#ifndef TLS_H
#define TLS_H

#include "io.h"
#include "tcp.h"

#define TLS_VERSION_1_2 0x0303
#define TLS_VERSION_1_3 0x0304

/* TLS Content Types */
#define TLS_CT_CHANGE_CIPHER_SPEC 20
#define TLS_CT_ALERT              21
#define TLS_CT_HANDSHAKE          22
#define TLS_CT_APPLICATION_DATA   23

/* TLS Handshake Types */
#define TLS_HS_HELLO_REQUEST        0
#define TLS_HS_CLIENT_HELLO         1
#define TLS_HS_SERVER_HELLO         2
#define TLS_HS_NEW_SESSION_TICKET   4
#define TLS_HS_CERTIFICATE         11
#define TLS_HS_SERVER_KEY_EXCHANGE 12
#define TLS_HS_CERT_REQUEST        13
#define TLS_HS_SERVER_HELLO_DONE   14
#define TLS_HS_CERT_VERIFY         15
#define TLS_HS_CLIENT_KEY_EXCHANGE 16
#define TLS_HS_FINISHED            20

/* Session structure */
typedef struct {
    tcp_socket_t *sock;
    char hostname[128];
    u16 port;
    int is_established;
    int cipher_suite;
    u16 version;

    /* Nonces */
    u8 client_random[32];
    u8 server_random[32];

    /* Session keys */
    u8 master_secret[48];
    u8 client_write_key[32];
    u8 server_write_key[32];
    u8 client_write_iv[16];
    u8 server_write_iv[16];
    u8 client_write_mac[32];
    u8 server_write_mac[32];

    u64 client_seq;
    u64 server_seq;

    /* Receive buffer for decrypted application data */
    u8 rx_plain[8192];
    u16 rx_plain_len;
    u16 rx_plain_pos;
} tls_session_t;

/* API */
void tls_init(void);
tls_session_t *tls_connect(u32 dst_ip, u16 dst_port, const char *hostname, u32 timeout_polls);
int tls_send(tls_session_t *s, const void *data, u16 len);
int tls_recv(tls_session_t *s, void *buf, u16 max_len, u32 timeout_polls);
void tls_close(tls_session_t *s);

#endif /* TLS_H */
