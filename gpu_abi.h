#ifndef GPU_ABI_H
#define GPU_ABI_H

#include "io.h"
#include "gpu_driver.h"

#define GPU_ABI_VERSION 1

typedef u32 gpu_handle_t;
typedef u32 gpu_client_handle_t;
typedef u32 gpu_generation_t;
typedef u64 gpu_request_id_t;

/* ABI Status Return Codes */
enum {
    GPU_STATUS_OK                  = 0,
    GPU_STATUS_INVALID_ARGUMENT    = -1,
    GPU_STATUS_BAD_VERSION         = -2,
    GPU_STATUS_BUFFER_TOO_SMALL    = -3,
    GPU_STATUS_PERMISSION_DENIED   = -4,
    GPU_STATUS_INVALID_HANDLE      = -5,
    GPU_STATUS_GENERATION_MISMATCH = -6,
    GPU_STATUS_RESOURCE_BUSY       = -7,
    GPU_STATUS_QUOTA_EXCEEDED      = -8,
    GPU_STATUS_QUEUE_FULL          = -9,
    GPU_STATUS_DEVICE_LOST         = -10,
    GPU_STATUS_RECOVERY            = -11,
    GPU_STATUS_UNSUPPORTED         = -12,
    GPU_STATUS_TIMEOUT             = -13,
    GPU_STATUS_NO_PRESENT          = -14
};

/* Syscall Dispatch Operations */
enum {
    GPU_OP_OPEN_CLIENT = 1,
    GPU_OP_CLOSE_CLIENT,
    GPU_OP_QUERY_DEVICE,
    GPU_OP_CREATE_RESOURCE,
    GPU_OP_CLOSE_RESOURCE,
    GPU_OP_SUBMIT_PRESENT,
    GPU_OP_POLL_EVENT,
    GPU_OP_WAIT_EVENT,
    GPU_OP_GET_METRICS
};

/* Unified ABI Header for version and size validation */
typedef struct {
    u16 version;
    u16 size;
    u32 flags;
} gpu_abi_header_t;

/* Opaque token layout: (index & 0xFFFF) | ((generation & 0xFFFF) << 16) */
typedef struct {
    u32 index;
    u32 generation;
} gpu_handle_token_t;

/* Client Open & Result */
typedef struct {
    gpu_abi_header_t header;
    u32              client_type;
    u32              reserved;
} gpu_client_open_t;

typedef struct {
    gpu_abi_header_t    header;
    gpu_client_handle_t client_handle;
    u32                 generation;
    u32                 permissions;
    u32                 reserved;
} gpu_client_open_result_t;

/* Resource Reference & Creation */
typedef struct {
    gpu_abi_header_t header;
    gpu_handle_t     resource;
    gpu_generation_t generation;
    u32              access;
    u32              reserved;
} gpu_resource_ref_t;

typedef struct {
    gpu_abi_header_t header;
    u32              width;
    u32              height;
    u32              bpp;
    u32              pitch_alignment;
    u32              flags;
    u32              reserved;
} gpu_resource_create_t;

typedef struct {
    gpu_abi_header_t header;
    gpu_handle_t     handle;
    gpu_generation_t generation;
    u32              width;
    u32              height;
    u32              pitch;
    u32              bpp;
    u64              size;
} gpu_resource_create_result_t;

/* Device Info Query */
typedef struct {
    gpu_abi_header_t header;
    u32              active_driver;
    u32              device_state;
    u32              width;
    u32              height;
    u32              bpp;
    u32              flags;
    gpu_caps_t       caps;
} gpu_device_info_t;

/* Present Submission & Result */
typedef struct {
    gpu_abi_header_t header;
    gpu_handle_t     resource;
    u32              resource_generation;
    u32              x;
    u32              y;
    u32              width;
    u32              height;
    u32              pitch;
    u32              format;
    u32              flags;
    u64              user_cookie;
} gpu_present_submit_t;

typedef struct {
    gpu_abi_header_t header;
    u64              request_id;
    u32              status;
    u32              queue_position;
    u64              user_cookie;
} gpu_present_result_t;

/* Event Types and User Event Descriptor */
typedef enum {
    GPU_EVENT_COMPLETION  = 1,
    GPU_EVENT_DROPPED     = 2,
    GPU_EVENT_ERROR       = 3,
    GPU_EVENT_RECOVERY    = 4,
    GPU_EVENT_DEVICE_LOSS = 5
} gpu_user_event_type_t;

typedef struct {
    gpu_abi_header_t header;
    u64              request_id;
    u32              event_type;
    u32              status;
    u32              resource_generation;
    u32              reserved;
    u64              user_cookie;
} gpu_user_event_t;

typedef struct {
    gpu_abi_header_t header;
    u32              timeout_ticks;
    u32              reserved;
} gpu_event_wait_t;

/* Client Metrics ABI */
typedef struct {
    gpu_abi_header_t header;
    u64              submitted;
    u64              completed;
    u64              dropped;
    u64              throttled;
    u64              coalesced;
    u64              used_bytes;
    u64              quota_bytes;
    u32              inflight;
    u32              max_inflight;
    u32              queue_depth;
    u32              reserved;
} gpu_client_metrics_abi_t;

const char* gpu_status_string(int status);

#endif /* GPU_ABI_H */
