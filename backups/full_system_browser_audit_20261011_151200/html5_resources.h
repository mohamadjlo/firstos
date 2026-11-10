#ifndef HTML5_RESOURCES_H
#define HTML5_RESOURCES_H

#include "io.h"
#include "html5_engine.h"

#define MAX_RESOURCE_ENTRIES 64
#define MAX_RESOURCE_QUEUE   32
#define MAX_RESOURCE_URL_LEN 1024
#define MAX_RESOURCE_MIME_LEN 64
#define MAX_REDIRECT_HOPS    5

#define RES_MAX_DOC_SIZE     (4194304) /* 4 MiB */
#define RES_MAX_CSS_SIZE     (2097152) /* 2 MiB */
#define RES_MAX_IMG_SIZE     (8388608) /* 8 MiB */

/* Transport return codes */
typedef enum {
    RES_TRANSPORT_OK = 0,
    RES_TRANSPORT_ERR_GENERIC = -1,
    RES_TRANSPORT_ERR_TIMEOUT = -2,
    RES_TRANSPORT_ERR_OVERSIZE = -3,
    RES_TRANSPORT_ERR_REDIRECT_LOOP = -4,
    RES_TRANSPORT_ERR_NOT_FOUND = -5,
    RES_TRANSPORT_ERR_INCOMPLETE = -6
} res_transport_err_t;

/* Transport abstraction */
typedef struct resource_transport_s resource_transport_t;

struct resource_transport_s {
    int (*fetch)(const char *url, void *buf, int max_len,
                 int *out_status, char *out_content_type, int max_ct_len,
                 char *out_redirect_url, int max_redir_len,
                 void *user_data);
    void *user_data;
};

/* Parsed URL structure */
typedef struct {
    char scheme[16];    /* "http", "https", "file", "about" */
    char host[128];     /* host or ip */
    u16  port;          /* e.g. 80, 443 */
    char path[1024];    /* normalized path */
    char query[512];    /* query string without '?' */
    char fragment[128]; /* fragment without '#' */
} res_parsed_url_t;

/* Resource Types */
typedef enum {
    RES_TYPE_UNKNOWN = 0,
    RES_TYPE_DOCUMENT,
    RES_TYPE_STYLESHEET,
    RES_TYPE_IMAGE
} res_type_t;

/* Resource State Machine */
typedef enum {
    RES_STATE_IDLE = 0,
    RES_STATE_QUEUED,
    RES_STATE_FETCHING,
    RES_STATE_LOADED,
    RES_STATE_FAILED
} res_state_t;

/* Request Priorities */
typedef enum {
    RES_PRIORITY_LOW = 0,
    RES_PRIORITY_NORMAL = 1,
    RES_PRIORITY_HIGH = 2,
    RES_PRIORITY_DOC = 3
} res_priority_t;

/* Browser Loading Lifecycle States */
typedef enum {
    LIFECYCLE_IDLE = 0,
    LIFECYCLE_DOC_REQUESTED,
    LIFECYCLE_HTML_RECEIVED,
    LIFECYCLE_PARSED,
    LIFECYCLE_DISCOVER_SUBRESOURCES,
    LIFECYCLE_FETCH_SUBRESOURCES,
    LIFECYCLE_RESTYLE_RELAYOUT_REPAINT,
    LIFECYCLE_COMPLETE,
    LIFECYCLE_ERROR
} browser_lifecycle_state_t;

/* Lifecycle Callback */
typedef void (*res_lifecycle_cb)(browser_lifecycle_state_t state, const char *status, void *user_data);

/* Resource Entry (Cache / Table entry) */
typedef struct {
    char url[MAX_RESOURCE_URL_LEN];
    res_type_t type;
    res_state_t state;
    int status_code;             /* HTTP 200, 301, 404, etc. */
    char mime[MAX_RESOURCE_MIME_LEN];
    void *data;                  /* Raw fetched payload buffer */
    int data_len;
    int max_data_len;
    /* Decoded image attributes (if RES_TYPE_IMAGE) */
    u32 *pixels;
    int width;
    int height;
    int error_code;
    u32 last_used_tick;
    int is_static;               /* Demo/builtin assets immune to page clear */
    u32 generation;              /* Generation counter when resource was acquired */
} resource_entry_t;

/* Backward compatibility asset struct */
typedef struct {
    char url[MAX_RESOURCE_URL_LEN];
    u32 *pixels;
    int w;
    int h;
    int ready;
} html5_cached_asset_t;

#define MAX_CACHED_ASSETS MAX_RESOURCE_ENTRIES

/* Subsystem Lifecycle & Transport API */
void html5_resources_init(void);
void html5_resources_clear(void);
void html5_resources_set_transport(const resource_transport_t *transport);
void html5_resources_set_lifecycle_callback(res_lifecycle_cb cb, void *user_data);
void html5_resources_notify_lifecycle(browser_lifecycle_state_t state, const char *status);
browser_lifecycle_state_t html5_resources_get_lifecycle_state(void);
void html5_resources_set_current_generation(u32 gen);
u32  html5_resources_get_current_generation(void);

/* URL Parsing & Resolution */
int  res_url_parse(const char *raw_url, res_parsed_url_t *out_parsed);
void res_url_normalize_path(char *path);
void res_url_resolve(const char *base_url, const char *rel_url, char *out_url, int max_len);

/* Resource Table / Cache Management */
resource_entry_t *html5_resource_lookup(const char *url);
resource_entry_t *html5_resource_acquire(const char *url, res_type_t type);
int               html5_resource_load(const char *url, res_type_t type, resource_entry_t **out_entry);

/* Request Queue API */
int  html5_resource_queue_subresource(const char *url, res_type_t type, res_priority_t priority, void *target_node);
int  html5_resource_queue_pump(int max_fetches);
void html5_resource_queue_clear(void);

/* DOM Subresource Integration */
void html5_resource_discover_and_fetch(html_node_t *root, const char *base_url, char *css_out, int *css_len, int max_css);

/* Compatibility functions for existing engine callers */
void html5_url_resolve(const char *base_url, const char *rel_url, char *out_url, int max_len);
u32 *html5_resource_get_image(const char *url, int *out_w, int *out_h);
void html5_resource_fetch_for_dom(html_node_t *root, const char *base_url);

#endif /* HTML5_RESOURCES_H */
