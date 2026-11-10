#include "bnet_error.h"

const char *bnet_error_name(bnet_error_t err) {
    switch (err) {
        case BNET_OK:                    return "BNET_OK";
        case BNET_ERR_BAD_URL:           return "BNET_ERR_BAD_URL";
        case BNET_ERR_UNSUPPORTED_SCHEME: return "BNET_ERR_UNSUPPORTED_SCHEME";
        case BNET_ERR_DNS_START_FAILED:  return "BNET_ERR_DNS_START_FAILED";
        case BNET_ERR_DNS_TIMEOUT:       return "BNET_ERR_DNS_TIMEOUT";
        case BNET_ERR_DNS_NO_RECORD:     return "BNET_ERR_DNS_NO_RECORD";
        case BNET_ERR_TCP_CONNECT_FAILED: return "BNET_ERR_TCP_CONNECT_FAILED";
        case BNET_ERR_TCP_TIMEOUT:       return "BNET_ERR_TCP_TIMEOUT";
        case BNET_ERR_TCP_RESET:         return "BNET_ERR_TCP_RESET";
        case BNET_ERR_TLS_NOT_AVAILABLE: return "BNET_ERR_TLS_NOT_AVAILABLE";
        case BNET_ERR_TLS_HANDSHAKE_FAILED: return "BNET_ERR_TLS_HANDSHAKE_FAILED";
        case BNET_ERR_TLS_TIMEOUT:       return "BNET_ERR_TLS_TIMEOUT";
        case BNET_ERR_HTTP_SEND_FAILED:  return "BNET_ERR_HTTP_SEND_FAILED";
        case BNET_ERR_HTTP_RECV_TIMEOUT: return "BNET_ERR_HTTP_RECV_TIMEOUT";
        case BNET_ERR_HTTP_BAD_RESPONSE: return "BNET_ERR_HTTP_BAD_RESPONSE";
        case BNET_ERR_BODY_TOO_LARGE:    return "BNET_ERR_BODY_TOO_LARGE";
        case BNET_ERR_ABORTED:           return "BNET_ERR_ABORTED";
        case BNET_ERR_INTERNAL:          return "BNET_ERR_INTERNAL";
        case BNET_ERR_PROXY_REQUIRED:    return "BNET_ERR_PROXY_REQUIRED";
        case BNET_ERR_PROXY_NOT_CONFIGURED: return "BNET_ERR_PROXY_NOT_CONFIGURED";
        case BNET_ERR_PROXY_CONNECT_FAILED: return "BNET_ERR_PROXY_CONNECT_FAILED";
        case BNET_ERR_PROXY_TIMEOUT:     return "BNET_ERR_PROXY_TIMEOUT";
        case BNET_ERR_PROXY_BAD_RESPONSE: return "BNET_ERR_PROXY_BAD_RESPONSE";
        case BNET_ERR_PROXY_HTTP_ERROR:  return "BNET_ERR_PROXY_HTTP_ERROR";
        case BNET_ERR_HTTPS_BRIDGE_UNAVAILABLE: return "BNET_ERR_HTTPS_BRIDGE_UNAVAILABLE";
        case BNET_ERR_BODY_TRUNCATED:     return "BNET_ERR_BODY_TRUNCATED";
        case BNET_ERR_UNSUPPORTED_ENCODING: return "BNET_ERR_UNSUPPORTED_ENCODING";
        default:                         return "BNET_ERR_UNKNOWN";
    }
}

const char *bnet_error_description(bnet_error_t err) {
    switch (err) {
        case BNET_OK:
            return "Operation successful";
        case BNET_ERR_BAD_URL:
            return "Invalid or malformed URL syntax";
        case BNET_ERR_UNSUPPORTED_SCHEME:
            return "Unsupported URL scheme (only http:// and https:// supported)";
        case BNET_ERR_DNS_START_FAILED:
            return "DNS lookup could not be sent (gateway ARP or network unconfigured)";
        case BNET_ERR_DNS_TIMEOUT:
            return "DNS server query timed out (no response received)";
        case BNET_ERR_DNS_NO_RECORD:
            return "Domain name does not exist or has no 'A' IPv4 record";
        case BNET_ERR_TCP_CONNECT_FAILED:
            return "TCP connection failed (next-hop gateway ARP unreachable)";
        case BNET_ERR_TCP_TIMEOUT:
            return "TCP connection timed out (server did not acknowledge SYN)";
        case BNET_ERR_TCP_RESET:
            return "TCP connection reset / refused by server (port closed)";
        case BNET_ERR_TLS_NOT_AVAILABLE:
            return "HTTPS requires host proxy bridge (run tools/firstos_webproxy.py on 10.0.2.2:18080)";
        case BNET_ERR_TLS_HANDSHAKE_FAILED:
            return "TLS handshake failed or rejected by remote server";
        case BNET_ERR_TLS_TIMEOUT:
            return "TLS handshake timed out waiting for server";
        case BNET_ERR_HTTP_SEND_FAILED:
            return "Failed to transmit HTTP request over socket";
        case BNET_ERR_HTTP_RECV_TIMEOUT:
            return "HTTP response timed out waiting for server data";
        case BNET_ERR_HTTP_BAD_RESPONSE:
            return "Invalid or empty HTTP response (zero bytes or missing headers)";
        case BNET_ERR_BODY_TOO_LARGE:
            return "HTTP response body exceeded memory budget";
        case BNET_ERR_ABORTED:
            return "Request cancelled by user or navigation replaced";
        case BNET_ERR_INTERNAL:
            return "Internal network subsystem error (socket pool exhausted)";
        case BNET_ERR_PROXY_REQUIRED:
            return "HTTPS proxy bridge is required for secure sites";
        case BNET_ERR_PROXY_NOT_CONFIGURED:
            return "HTTPS proxy bridge host/port not configured";
        case BNET_ERR_PROXY_CONNECT_FAILED:
            return "Could not connect to host proxy bridge (is tools/firstos_webproxy.py running on 10.0.2.2:18080?)";
        case BNET_ERR_PROXY_TIMEOUT:
            return "HTTPS proxy bridge timed out waiting for connection";
        case BNET_ERR_PROXY_BAD_RESPONSE:
            return "Proxy bridge returned invalid or corrupted response";
        case BNET_ERR_PROXY_HTTP_ERROR:
            return "Proxy bridge upstream fetch failed (check host network or URL)";
        case BNET_ERR_HTTPS_BRIDGE_UNAVAILABLE:
            return "HTTPS bridge unavailable in this build";
        case BNET_ERR_BODY_TRUNCATED:
            return "Resource body incomplete or exceeded maximum capacity";
        case BNET_ERR_UNSUPPORTED_ENCODING:
            return "Server returned unsupported Content-Encoding (compression not supported)";
        default:
            return "Unknown network transport failure";
    }
}
