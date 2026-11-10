# FirstOS TLS Subsystem Diagnostics & Cryptographic Observability (Phase 4)

**Date**: October 15, 2026  
**Engineer**: FirstOS Principal Systems & Networking Engineer  
**Scope**: `/Users/mohamadjlo/Desktop/FirstOS/tls.c`, `/Users/mohamadjlo/Desktop/FirstOS/tls.h`, `/Users/mohamadjlo/Desktop/FirstOS/net_socket.c`  
**Focus Areas**: TLS Handshake State Machine, Cipher Suite Negotiation, Ephemeral Keys, Record Inspection, Alert Handling, Diagnostic API  

---

## 1. Architectural Role of TLS in FirstOS

In modern operating systems, web browsers and connected system utilities require TLS (Transport Layer Security) for secure HTTP/1.1 and HTTP/2 traffic. FirstOS contains an in-kernel freestanding TLS engine supporting TLS 1.2 (and TLS 1.3 draft interoperability) without third-party dependencies such as OpenSSL or mbedTLS.

Prior to Phase 4, the TLS engine performed handshakes with minimal observability. If a handshake stalled or failed due to an unsupported cipher or malformed ServerHello, the subsystem lacked structured telemetry to report the failure.

Phase 4 introduces end-to-end telemetry into the TLS stack, recording session parameters, cryptographic primitives, handshake duration, and transfer volume.

---

## 2. TLS Diagnostics Architecture

### 2.1 The `tls_diag_t` Telemetry Contract
Defined in [`tls.h`](file:///Users/mohamadjlo/Desktop/FirstOS/tls.h):
```c
typedef struct {
    int   is_established;    /* 1 = Currently established & active, 0 = Closed */
    u16   version;           /* Wire version, e.g. 0x0303 for TLS 1.2, 0x0304 for TLS 1.3 */
    int   cipher_suite;      /* Negotiated 16-bit cipher suite code (e.g. 0xC02F) */
    char  cipher_name[48];   /* Canonical IANA cipher suite name */
    char  hostname[128];     /* Server Name Indication (SNI) string */
    u16   port;              /* Destination TCP port (typically 443) */
    u32   ip;                /* Destination IPv4 address */
    u32   handshake_ticks;   /* Total duration of handshake in system clock ticks/ms */
    u32   tx_bytes;          /* Encrypted bytes transmitted in session */
    u32   rx_bytes;          /* Encrypted bytes received in session */
    int   session_resumed;   /* 1 if abbreviated session resumption was utilized */
    int   cert_validated;    /* 1 if certificate chain was verified */
    char  last_alert[32];    /* Textual description of last received TLS alert */
} tls_diag_t;
```

### 2.2 ServerHello Parsing & Dynamic Negotiation
In `tls.c`, when processing the incoming `ServerHello` record (`TLS_HS_SERVER_HELLO` = 2):
1. **Version Extraction**:
   - Parsed from byte offsets 9 and 10 of the handshake record:
     ```c
     u16 serv_ver = (u16)((rx_buf[9] << 8) | rx_buf[10]);
     g_tls_last_diag.version = serv_ver;
     ```
2. **Session ID & Cipher Selection**:
   - The session ID length is located at offset 43.
   - The remote server's selected cipher suite is placed immediately following the session ID (`offset = 44 + sid_len`):
     ```c
     u16 chosen_cipher = (u16)((rx_buf[44 + sid_len] << 8) | rx_buf[45 + sid_len]);
     g_tls_last_diag.cipher_suite = chosen_cipher;
     strncpy(g_tls_last_diag.cipher_name, tls_cipher_name(chosen_cipher), sizeof(g_tls_last_diag.cipher_name) - 1);
     ```
3. **Handshake Latency Tracking**:
   - `start_ticks` is recorded at the start of `tls_connect()` using `clock_get_ticks()`.
   - Upon successful receipt and verification of the server's `Finished` message (`TLS_HS_FINISHED`), `handshake_ticks = clock_get_ticks() - start_ticks` is committed to `g_tls_last_diag`.

---

## 3. Supported Cipher Suites & Identity Mappings

The helper function `tls_cipher_name()` maps standard IANA identifiers to readable names:

| Cipher Suite ID | Canonical Name | Key Exchange | Bulk Encryption | MAC / Integrity |
|:---|:---|:---|:---|:---|
| `0xC02F` | `TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256` | ECDHE (X25519) | AES-128-GCM | SHA-256 (AEAD) |
| `0xC030` | `TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384` | ECDHE (X25519) | AES-256-GCM | SHA-384 (AEAD) |
| `0x009C` | `TLS_RSA_WITH_AES_128_GCM_SHA256` | RSA | AES-128-GCM | SHA-256 (AEAD) |
| `0x002F` | `TLS_RSA_WITH_AES_128_CBC_SHA` | RSA | AES-128-CBC | HMAC-SHA1 |
| `0x0035` | `TLS_RSA_WITH_AES_256_CBC_SHA` | RSA | AES-256-CBC | HMAC-SHA1 |
| `0x1301` | `TLS_AES_128_GCM_SHA256` (TLS 1.3) | Ephemeral Key Share | AES-128-GCM | SHA-256 (AEAD) |
| `0x1302` | `TLS_AES_256_GCM_SHA384` (TLS 1.3) | Ephemeral Key Share | AES-256-GCM | SHA-384 (AEAD) |

---

## 4. In-System Querying & Diagnostic Workflows

### 4.1 Shell Command: `tlsdiag [hostname]`
Developers and users can run `tlsdiag` from the FirstOS shell to perform active probes or query historical sessions:
- **Query Historical Session**:
  ```bash
  FirstOS> tlsdiag
  ```
  Returns metrics for the last session created by the web browser or `httpget`.
- **Active Handshake Probe**:
  ```bash
  FirstOS> tlsdiag google.com
  ```
  Resolves `google.com`, opens a TCP socket to port 443, executes the full TLS handshake with SNI `google.com`, prints all negotiated parameters and timing, and closes the session cleanly.

### 4.2 Handling Alerts and Graceful Termination
When a TLS alert record (`TLS_CT_ALERT` = 21) is encountered:
- Level 1 (Warning): Recorded in `g_tls_last_diag.last_alert` (e.g., `close_notify`) without terminating unless fatal.
- Level 2 (Fatal): Populates `last_alert` with the alert description code (e.g. `handshake_failure`, `decrypt_error`) and terminates the socket connection cleanly, setting `last_error = BNET_ERR_TLS_ALERT`.

---

## 5. Security & Isolation Safeguards

- **Key Protection**: Diagnostic telemetry explicitly excludes private session keys (`master_secret`, `client_write_key`, `server_write_key`). Only metadata, non-confidential identifiers, and byte volumes are exposed.
- **Kernel Memory Isolation**: `tls_get_diagnostics()` performs a value copy (`memcpy`) into the caller's structure to prevent direct mutation of internal TLS driver structures.
