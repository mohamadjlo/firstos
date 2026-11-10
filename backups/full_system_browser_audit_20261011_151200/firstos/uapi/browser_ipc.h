#ifndef _FIRSTOS_UAPI_BROWSER_IPC_H
#define _FIRSTOS_UAPI_BROWSER_IPC_H

#include <stdint.h>

#define FIRSTOS_BROWSER_IPC_MAGIC     0x42525753u /* 'BRWS' */
#define FIRSTOS_BROWSER_IPC_VERSION   1u
#define FIRSTOS_BROWSER_SHM_NAME      "firstos_browser_shm"

/* Memory Layout within the 4MB Shared Memory Window (0x04400000..0x04800000) */
#define FIRSTOS_BROWSER_SHM_TOTAL_SIZE (4u * 1024u * 1024u) /* 4 MB */
#define FIRSTOS_BROWSER_CHANNEL_H2R_OFF 0x00000000u          /* 0 MB: Host -> Renderer */
#define FIRSTOS_BROWSER_CHANNEL_R2H_OFF 0x00200000u          /* 2 MB: Renderer -> Host */
#define FIRSTOS_BROWSER_CHANNEL_SIZE    0x00200000u          /* 2 MB per channel */

#define FIRSTOS_BROWSER_MAX_PAYLOAD     (1u * 1024u * 1024u) /* 1 MB max payload */
#define FIRSTOS_BROWSER_MAX_CMDS        1024u
#define FIRSTOS_BROWSER_STRINGS_OFFSET  4096u                /* Offset in R2H data where strings start */

/* IPC Message Types */
typedef enum {
    FIRSTOS_BROWSER_MSG_NONE = 0,
    FIRSTOS_BROWSER_MSG_LOAD_URL = 1,
    FIRSTOS_BROWSER_MSG_HTML_DATA = 2,
    FIRSTOS_BROWSER_MSG_RENDER_FRAME = 3,
    FIRSTOS_BROWSER_MSG_TITLE_UPDATE = 4,
    FIRSTOS_BROWSER_MSG_INPUT_EVENT = 5,
    FIRSTOS_BROWSER_MSG_NAVIGATE_REQUEST = 6,
    FIRSTOS_BROWSER_MSG_ERROR = 7,
    FIRSTOS_BROWSER_MSG_SHUTDOWN = 8,
    FIRSTOS_BROWSER_MSG_ACK = 9,
    FIRSTOS_BROWSER_MSG_RESOURCE_REQUEST = 10,
    FIRSTOS_BROWSER_MSG_RESOURCE_RESPONSE = 11,
    FIRSTOS_BROWSER_MSG_RESOURCE_CANCEL = 12,
    FIRSTOS_BROWSER_MSG_HISTORY_BACK = 13,
    FIRSTOS_BROWSER_MSG_HISTORY_FORWARD = 14,
    FIRSTOS_BROWSER_MSG_RELOAD = 15,
    FIRSTOS_BROWSER_MSG_HISTORY_STATE = 16,
    FIRSTOS_BROWSER_MSG_FORM_SUBMIT = 17,
    FIRSTOS_BROWSER_MSG_FRAME_CREATE = 18,
    FIRSTOS_BROWSER_MSG_FRAME_NAVIGATE = 19,
    FIRSTOS_BROWSER_MSG_FRAME_DESTROY = 20,
    FIRSTOS_BROWSER_MSG_POST_MESSAGE = 21,
    FIRSTOS_BROWSER_MSG_DOWNLOAD_REQUEST = 22,
    FIRSTOS_BROWSER_MSG_MEDIA_COMMAND = 23,
    FIRSTOS_BROWSER_MSG_VIEWPORT_CHANGED = 24,
    FIRSTOS_BROWSER_MSG_NAVIGATE = 25,
    FIRSTOS_BROWSER_MSG_RESPONSE_HEADERS = 26,
    FIRSTOS_BROWSER_MSG_RESPONSE_BODY_READY = 27,
    FIRSTOS_BROWSER_MSG_DOCUMENT_PARSED = 28,
    FIRSTOS_BROWSER_MSG_LAYOUT_READY = 29,
    FIRSTOS_BROWSER_MSG_RENDER_COMPLETE = 30,
    FIRSTOS_BROWSER_MSG_PAGE_ERROR = 31,
    FIRSTOS_BROWSER_MSG_RESOURCE_COMPLETE = 32,
    /* Phase 3: Multi-Service Browser Architecture Messages */
    FIRSTOS_BROWSER_MSG_BROWSER_NAVIGATE = 40,
    FIRSTOS_BROWSER_MSG_NETWORK_FETCH_REQUEST = 41,
    FIRSTOS_BROWSER_MSG_NETWORK_FETCH_COMPLETE = 42,
    FIRSTOS_BROWSER_MSG_NETWORK_FETCH_ERROR = 43,
    FIRSTOS_BROWSER_MSG_RENDERER_CREATE = 44,
    FIRSTOS_BROWSER_MSG_RENDERER_NAVIGATE = 45,
    FIRSTOS_BROWSER_MSG_RENDERER_DOCUMENT_READY = 46,
    FIRSTOS_BROWSER_MSG_RENDERER_FRAME_READY = 47,
    FIRSTOS_BROWSER_MSG_RENDERER_CRASHED = 48,
    FIRSTOS_BROWSER_MSG_FRAME_CREATE_REQ = 49,
    FIRSTOS_BROWSER_MSG_FRAME_DESTROY_REQ = 50,
    FIRSTOS_BROWSER_MSG_SECURITY_CHECK_REQUEST = 52,
    FIRSTOS_BROWSER_MSG_SECURITY_CHECK_RESULT = 53,
    FIRSTOS_BROWSER_MSG_STORAGE_COOKIE_QUERY = 54,
    FIRSTOS_BROWSER_MSG_STORAGE_COOKIE_UPDATE = 55,
    FIRSTOS_BROWSER_MSG_DOWNLOAD_START = 56,
    FIRSTOS_BROWSER_MSG_DOWNLOAD_PROGRESS = 57,
    FIRSTOS_BROWSER_MSG_DOWNLOAD_COMPLETE = 58,
    FIRSTOS_BROWSER_MSG_PRESENT_SURFACE = 59,
    FIRSTOS_BROWSER_MSG_TAB_RELOAD_AFTER_CRASH = 60,
    FIRSTOS_BROWSER_MSG_TAB_CREATE = 61
} firstos_browser_msg_type_t;

static inline const char *firstos_browser_msg_name(uint32_t type) {
    switch (type) {
        case FIRSTOS_BROWSER_MSG_LOAD_URL: return "LOAD_URL";
        case FIRSTOS_BROWSER_MSG_HTML_DATA: return "HTML_DATA";
        case FIRSTOS_BROWSER_MSG_RENDER_FRAME: return "RENDER_FRAME";
        case FIRSTOS_BROWSER_MSG_NAVIGATE: return "NAVIGATE";
        case FIRSTOS_BROWSER_MSG_BROWSER_NAVIGATE: return "BROWSER_NAVIGATE";
        case FIRSTOS_BROWSER_MSG_NETWORK_FETCH_REQUEST: return "NETWORK_FETCH_REQUEST";
        case FIRSTOS_BROWSER_MSG_NETWORK_FETCH_COMPLETE: return "NETWORK_FETCH_COMPLETE";
        case FIRSTOS_BROWSER_MSG_NETWORK_FETCH_ERROR: return "NETWORK_FETCH_ERROR";
        case FIRSTOS_BROWSER_MSG_RENDERER_CREATE: return "RENDERER_CREATE";
        case FIRSTOS_BROWSER_MSG_RENDERER_NAVIGATE: return "RENDERER_NAVIGATE";
        case FIRSTOS_BROWSER_MSG_RENDERER_DOCUMENT_READY: return "RENDERER_DOCUMENT_READY";
        case FIRSTOS_BROWSER_MSG_RENDERER_FRAME_READY: return "RENDERER_FRAME_READY";
        case FIRSTOS_BROWSER_MSG_RENDERER_CRASHED: return "RENDERER_CRASHED";
        case FIRSTOS_BROWSER_MSG_FRAME_CREATE: return "FRAME_CREATE";
        case FIRSTOS_BROWSER_MSG_FRAME_DESTROY: return "FRAME_DESTROY";
        case FIRSTOS_BROWSER_MSG_POST_MESSAGE: return "FRAME_POST_MESSAGE";
        case FIRSTOS_BROWSER_MSG_SECURITY_CHECK_REQUEST: return "SECURITY_CHECK_REQUEST";
        case FIRSTOS_BROWSER_MSG_SECURITY_CHECK_RESULT: return "SECURITY_CHECK_RESULT";
        case FIRSTOS_BROWSER_MSG_STORAGE_COOKIE_QUERY: return "STORAGE_COOKIE_QUERY";
        case FIRSTOS_BROWSER_MSG_STORAGE_COOKIE_UPDATE: return "STORAGE_COOKIE_UPDATE";
        case FIRSTOS_BROWSER_MSG_DOWNLOAD_START: return "DOWNLOAD_START";
        case FIRSTOS_BROWSER_MSG_DOWNLOAD_PROGRESS: return "DOWNLOAD_PROGRESS";
        case FIRSTOS_BROWSER_MSG_DOWNLOAD_COMPLETE: return "DOWNLOAD_COMPLETE";
        case FIRSTOS_BROWSER_MSG_PRESENT_SURFACE: return "PRESENT_SURFACE";
        case FIRSTOS_BROWSER_MSG_TAB_RELOAD_AFTER_CRASH: return "TAB_RELOAD_AFTER_CRASH";
        case FIRSTOS_BROWSER_MSG_TAB_CREATE: return "TAB_CREATE";
        default: return "UNKNOWN";
    }
}

static inline void firstos_ipc_log_msg(uint32_t type) {
    extern void serial_write(const char *str);
    serial_write("[ipc] msg type=");
    serial_write(firstos_browser_msg_name(type));
    serial_write("\n");
}

/* Resource Types */
typedef enum {
    FIRSTOS_RES_TYPE_UNKNOWN = 0,
    FIRSTOS_RES_TYPE_STYLESHEET = 1,
    FIRSTOS_RES_TYPE_IMAGE = 2,
    FIRSTOS_RES_TYPE_FONT = 3
} firstos_resource_type_t;

#define FIRSTOS_BROWSER_RESOURCE_REQ_OFFSET  16384u
#define FIRSTOS_BROWSER_FORM_SUBMIT_OFFSET   20480u
#define FIRSTOS_BROWSER_POST_MESSAGE_OFFSET  24576u
#define FIRSTOS_BROWSER_DOWNLOAD_REQ_OFFSET  28672u
#define FIRSTOS_BROWSER_MEDIA_CMD_OFFSET     32768u
#define FIRSTOS_BROWSER_DAMAGE_OFFSET        36864u
#define FIRSTOS_BROWSER_RESOURCE_RESP_OFFSET 0x00100000u

/* Phase 4: Compositor Invalidation Flags */
#define FIRSTOS_RENDER_FLAG_FULL_REPAINT     0x0001u
#define FIRSTOS_RENDER_FLAG_DAMAGE_VALID     0x0002u

#define FIRSTOS_MAX_MESSAGE_BYTES 1024
#define FIRSTOS_MAX_MESSAGE_QUEUE 32

/* Secure postMessage Descriptor (1332 bytes, packed, no pointers) */
typedef struct {
    uint32_t version;
    uint32_t type;
    uint32_t size;
    uint32_t message_id;
    uint32_t sender_tab_id;
    uint32_t sender_tab_generation;
    uint32_t sender_frame_id;
    uint32_t sender_frame_generation;
    uint32_t target_tab_id;
    uint32_t target_frame_id;
    uint32_t target_frame_generation;
    uint32_t sender_security_generation;
    char     sender_origin[128];
    char     target_origin[128];
    uint32_t payload_length;
    char     payload[FIRSTOS_MAX_MESSAGE_BYTES];
} __attribute__((packed)) firstos_post_message_t;
typedef char firstos_post_message_size_check[(sizeof(firstos_post_message_t) == 1332) ? 1 : -1];

/* Resource Request Descriptor (Renderer -> Host, 32 bytes) */
typedef struct {
    uint32_t request_id;     /* Monotonic per-renderer request ID */
    uint32_t navigation_id;  /* Target navigation generation */
    uint16_t resource_type;  /* firstos_resource_type_t */
    uint16_t flags;          /* Priority or caching flags */
    uint32_t url_offset;     /* Offset in R2H data buffer */
    uint32_t url_len;        /* URL string length (max 1024) */
    uint32_t reserved[4];
} __attribute__((packed)) firstos_resource_request_t;

/* Form Submit Request Descriptor (Renderer -> Host, 32 bytes) */
typedef struct {
    uint32_t request_id;     /* Monotonic sequence ID */
    uint32_t navigation_id;  /* Target navigation generation */
    uint32_t action_offset;  /* Offset in R2H data buffer */
    uint32_t action_len;     /* Action URL length */
    uint32_t method;         /* 0 = GET, 1 = POST */
    uint32_t query_offset;   /* Offset in R2H data buffer */
    uint32_t query_len;      /* Encoded query string length */
    uint32_t reserved;
} __attribute__((packed)) firstos_form_submit_request_t;

/* Download Request Descriptor (Renderer -> Host, 32 bytes) */
typedef struct {
    uint32_t request_id;         /* Monotonic request sequence ID */
    uint32_t navigation_id;      /* Document navigation generation */
    uint32_t url_offset;         /* Offset in R2H data buffer */
    uint32_t url_len;            /* URL string length (max 1024) */
    uint32_t filename_offset;    /* Suggested filename offset from <a download> */
    uint32_t filename_len;       /* Filename length (max 128) */
    uint16_t is_user_gesture;    /* Renderer hint; Host re-verifies user gesture */
    uint16_t reserved1;
    uint32_t reserved2;
} __attribute__((packed)) firstos_download_request_t;
typedef char firstos_download_request_size_check[(sizeof(firstos_download_request_t) == 32) ? 1 : -1];

/* Phase 6B: Media Command Descriptor (Renderer -> Host, 32 bytes) */
typedef enum {
    FIRSTOS_MEDIA_CMD_NONE = 0,
    FIRSTOS_MEDIA_CMD_PLAY = 1,
    FIRSTOS_MEDIA_CMD_PAUSE = 2,
    FIRSTOS_MEDIA_CMD_SEEK = 3,
    FIRSTOS_MEDIA_CMD_SET_VOLUME = 4,
    FIRSTOS_MEDIA_CMD_MUTE = 5
} firstos_media_cmd_type_t;

typedef struct {
    uint32_t request_id;        /* Generation / monotonic counter */
    uint32_t media_id;          /* 1-based node / stream identifier */
    uint16_t command;           /* firstos_media_cmd_type_t */
    uint16_t volume;            /* 0 - 100 */
    uint32_t seek_time_ms;      /* Target seek in milliseconds */
    uint32_t src_offset;        /* Offset to URL/path in strings table */
    uint32_t src_len;           /* Length of URL string */
    uint32_t reserved[2];       /* Padding to ensure exact 32 bytes */
} __attribute__((packed)) firstos_media_command_t;
typedef char firstos_media_command_size_check[(sizeof(firstos_media_command_t) == 32) ? 1 : -1];

/* Phase 4: Compositor Damage Rectangle Descriptor (Renderer -> Host, 32 bytes) */
typedef struct {
    int32_t  x;                 /* Invalidation X relative to viewport */
    int32_t  y;                 /* Invalidation Y relative to viewport */
    int32_t  w;                 /* Invalidation Width */
    int32_t  h;                 /* Invalidation Height */
    uint32_t flags;             /* FIRSTOS_RENDER_FLAG_* */
    uint32_t reserved[3];       /* Padding to ensure exact 32 bytes */
} __attribute__((packed)) firstos_damage_rect_t;
typedef char firstos_damage_rect_size_check[(sizeof(firstos_damage_rect_t) == 32) ? 1 : -1];

/* Resource Response Descriptor (Host -> Renderer, 64 bytes) */
typedef struct {
    uint32_t request_id;     /* Matches request_id */
    uint32_t navigation_id;  /* Matches request navigation_id */
    int32_t  status_code;    /* 200, 404, -errno */
    uint16_t resource_type;  /* firstos_resource_type_t */
    uint16_t mime_len;       /* Length of MIME string */
    char     mime_type[32];  /* e.g. "text/css", "image/png" */
    uint32_t data_offset;    /* Offset in H2R data buffer */
    uint32_t data_len;       /* Payload bytes */
    uint16_t meta_w;         /* Metadata width (e.g. image width) */
    uint16_t meta_h;         /* Metadata height (e.g. image height) */
    uint32_t image_id;       /* Phase 4D: Host Decoded Image Store ID */
    uint32_t reserved[1];
} __attribute__((packed)) firstos_resource_response_t;

/* Render Command Types */
typedef enum {
    FIRSTOS_RENDER_CMD_NONE = 0,
    FIRSTOS_RENDER_CMD_CLEAR = 1,
    FIRSTOS_RENDER_CMD_RECT = 2,
    FIRSTOS_RENDER_CMD_TEXT = 3,
    FIRSTOS_RENDER_CMD_CLIP = 4,
    FIRSTOS_RENDER_CMD_END = 5,
    FIRSTOS_RENDER_CMD_IMAGE = 6
} firstos_render_cmd_type_t;

/* Input Event Types */
typedef enum {
    FIRSTOS_BROWSER_INPUT_NONE = 0,
    FIRSTOS_BROWSER_INPUT_CLICK = 1,
    FIRSTOS_BROWSER_INPUT_KEY = 2,
    FIRSTOS_BROWSER_INPUT_SCROLL = 3
} firstos_browser_input_type_t;

/* Render Command (Fixed Size: 32 bytes, No Pointers) */
typedef struct {
    uint32_t type;         /* firstos_render_cmd_type_t */
    int32_t  x;            /* X coordinate within viewport */
    int32_t  y;            /* Y coordinate within viewport */
    int32_t  w;            /* Width */
    int32_t  h;            /* Height */
    uint32_t color;        /* 0x00RRGGBB */
    uint32_t text_offset;  /* Offset in channel payload buffer */
    uint32_t text_len;     /* String length */
} __attribute__((packed)) firstos_render_command_t;
typedef char firstos_render_cmd_size_check[(sizeof(firstos_render_command_t) == 32) ? 1 : -1];

/* Fixed-size Browser IPC Message Header (32 bytes) */
typedef struct {
    uint32_t version;        /* Must match FIRSTOS_BROWSER_IPC_VERSION */
    uint32_t type;           /* firstos_browser_msg_type_t */
    uint32_t request_id;     /* Monotonic sequence ID */
    uint32_t payload_offset; /* Offset relative to channel data buffer */
    uint32_t payload_length; /* Payload size in bytes */
    int32_t  status;         /* 0 = success, negative = -errno */
    uint32_t param1;         /* e.g. viewport width or input x */
    uint32_t param2;         /* e.g. viewport height or input y */
} __attribute__((packed)) firstos_browser_message_t;
typedef char firstos_browser_msg_size_check[(sizeof(firstos_browser_message_t) == 32) ? 1 : -1];

/* Compile-time checks for resource request and response descriptors */
typedef char firstos_resource_req_size_check[(sizeof(firstos_resource_request_t) == 36) ? 1 : -1];
typedef char firstos_resource_resp_size_check[(sizeof(firstos_resource_response_t) == 68) ? 1 : -1];

/* Channel Control Header (64 bytes aligned at start of each 2MB half) */
typedef struct {
    uint32_t magic;          /* FIRSTOS_BROWSER_IPC_MAGIC */
    uint32_t version;        /* FIRSTOS_BROWSER_IPC_VERSION */
    uint32_t capacity;       /* Channel capacity (e.g. 2MB) */
    uint32_t lock_word;      /* Fast userspace atomic mutex */
    uint32_t has_message;    /* 1 if message ready, 0 if empty */
    uint32_t seq;            /* Monotonic counter */
    uint32_t sender_pid;     /* Originating PID */
    uint32_t reserved[9];    /* Padding to 64 bytes */
    firstos_browser_message_t msg; /* Current message in mailbox (32 bytes) */
} __attribute__((aligned(4))) firstos_browser_channel_t;
typedef char firstos_browser_channel_size_check[(sizeof(firstos_browser_channel_t) == 96) ? 1 : -1];

#endif /* _FIRSTOS_UAPI_BROWSER_IPC_H */
