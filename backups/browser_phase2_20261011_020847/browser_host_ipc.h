#ifndef BROWSER_HOST_IPC_H
#define BROWSER_HOST_IPC_H

#include "include/firstos/uapi/browser_ipc.h"
#include "gui_internal.h"
#include "include/firstos/security/browser_security.h"

int  browser_host_ipc_init(void);
int  browser_host_ipc_spawn_renderer(void);
int  browser_host_ipc_is_active(void);
int  browser_host_ipc_send_page(const char *url, const char *html, int len, int view_w, int view_h);
int  browser_host_ipc_poll_render_commands(app_window_t *app, int content_x, int content_y, int content_w, int content_h);
int  browser_host_ipc_send_input(int input_type, int mx, int my, int key);
void browser_host_ipc_shutdown(void);
void browser_host_ipc_invalidate_cached_frame(void);
int  browser_host_ipc_get_renderer_pid(void);
const firstos_damage_rect_t *browser_host_get_cached_damage(void);

/* Phase 4C: Navigation, Security Policy & Resource Host Management */
int      browser_host_validate_url(const char *url, const char *base_origin);
uint32_t browser_host_get_navigation_id(void);
uint32_t browser_host_bump_navigation_id(void);
int      browser_host_cache_lookup(const char *url, char *out_buf, int max_len, char *out_mime, int *out_len, uint16_t *out_w, uint16_t *out_h);
int      browser_host_cache_insert(const char *url, const char *mime, const char *data, int len, uint16_t meta_w, uint16_t meta_h);
void     browser_host_cache_clear(void);

/* Phase 5A: Browser Security Model, Origins & Partitioned Cache */
int      browser_host_set_committed_origin(const char *url);
const firstos_renderer_security_context_t *browser_host_get_security_context(void);
int      browser_host_partitioned_cache_lookup(const firstos_origin_t *origin, const char *url, char *out_buf, int max_len, char *out_mime, int *out_len, uint16_t *out_w, uint16_t *out_h);
int      browser_host_partitioned_cache_insert(const firstos_origin_t *origin, const char *url, const char *mime, const char *data, int len, uint16_t meta_w, uint16_t meta_h);

/* Phase 4D: Navigation History & Host Decoded Image Store */
int      browser_host_history_push(const char *url);
int      browser_host_history_back(char *out_url, int max_len);
int      browser_host_history_forward(char *out_url, int max_len);
int      browser_host_history_reload(char *out_url, int max_len);
int      browser_host_history_get_state(int *out_index, int *out_count);
void     browser_host_history_clear(void);

uint32_t browser_host_image_store_insert(const uint32_t *pixels, int w, int h);
const uint32_t *browser_host_image_store_get(uint32_t image_id, int *out_w, int *out_h);
void     browser_host_image_store_clear(void);

/* Phase 5B: Multi-Tab Browser, Renderer Slots, and Per-Tab Session State */
#define FIRSTOS_MAX_TABS 8
#define FIRSTOS_MAX_TAB_URL 512
#define FIRSTOS_MAX_TAB_TITLE 128
#define FIRSTOS_MAX_TAB_HISTORY 32
#define FIRSTOS_MAX_RENDERER_SLOTS 8

typedef enum {
    FIRSTOS_TAB_FREE = 0,
    FIRSTOS_TAB_CREATING,
    FIRSTOS_TAB_LOADING,
    FIRSTOS_TAB_READY,
    FIRSTOS_TAB_ERROR,
    FIRSTOS_TAB_CRASHED,
    FIRSTOS_TAB_CLOSING
} firstos_tab_state_t;

typedef struct {
    char url[FIRSTOS_MAX_TAB_URL];
    uint32_t navigation_id;
} firstos_tab_history_entry_t;

typedef struct {
    firstos_tab_history_entry_t entries[FIRSTOS_MAX_TAB_HISTORY];
    uint32_t count;
    int32_t  index;
} firstos_tab_history_t;

/* Phase 5C: Iframes, Frame Tree, and Secure postMessage */
#define FIRSTOS_MAX_FRAMES_PER_TAB 8
#define FIRSTOS_MAX_TOTAL_FRAMES   32
#define FIRSTOS_MAX_FRAME_DEPTH    4
#define FIRSTOS_MAX_FRAME_URL      512
#define FIRSTOS_MAX_FRAME_TITLE    128

typedef enum {
    FIRSTOS_FRAME_FREE = 0,
    FIRSTOS_FRAME_CREATED,
    FIRSTOS_FRAME_LOADING,
    FIRSTOS_FRAME_READY,
    FIRSTOS_FRAME_ERROR,
    FIRSTOS_FRAME_CRASHED,
    FIRSTOS_FRAME_CLOSING
} firstos_frame_state_t;

typedef enum {
    FIRSTOS_FRAME_ACCESS_ALLOW = 0,
    FIRSTOS_FRAME_ACCESS_DENY_ORIGIN = -1,
    FIRSTOS_FRAME_ACCESS_DENY_PARENT = -2,
    FIRSTOS_FRAME_ACCESS_DENY_STALE = -3,
    FIRSTOS_FRAME_ACCESS_DENY_DEPTH = -4,
    FIRSTOS_FRAME_ACCESS_DENY_LIMIT = -5,
    FIRSTOS_FRAME_ACCESS_DENY_SCHEME = -6,
    FIRSTOS_FRAME_ACCESS_DENY_POLICY = -7
} firstos_frame_access_result_t;

typedef struct {
    uint32_t frame_id;
    uint32_t generation;
    uint32_t tab_id;
    uint32_t tab_generation;
    uint32_t parent_frame_id;
    uint32_t parent_frame_generation;
    uint32_t depth;
    uint32_t state;
    uint32_t renderer_slot_id;
    uint32_t renderer_slot_generation;
    uint32_t renderer_pid;
    uint32_t navigation_id;
    uint32_t document_generation;
    uint32_t security_generation;
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
    int32_t  scroll_x;
    int32_t  scroll_y;
    char     url[FIRSTOS_MAX_FRAME_URL];
    char     title[FIRSTOS_MAX_FRAME_TITLE];
    firstos_origin_t committed_origin;
} firstos_frame_t;

typedef struct {
    firstos_frame_t frames[FIRSTOS_MAX_FRAMES_PER_TAB];
    uint32_t tab_id;
    uint32_t tab_generation;
    uint32_t root_frame_id;
    uint32_t frame_count;
} firstos_frame_tree_t;

typedef struct {
    uint32_t frame_id;
    uint32_t frame_generation;
    uint32_t tab_id;
    uint32_t tab_generation;
    uint32_t width;
    uint32_t height;
    uint32_t shm_id;
    uint32_t valid;
} firstos_frame_surface_t;

typedef struct {
    uint32_t tab_id;
    uint32_t generation;
    uint32_t state;
    uint32_t renderer_slot_id;
    uint32_t renderer_pid;
    uint32_t active;
    uint32_t dirty;
    uint32_t loading;
    uint32_t has_error;
    uint32_t security_context_generation;
    uint32_t navigation_id;
    uint32_t document_generation;
    int32_t  scroll_x;
    int32_t  scroll_y;
    char     url[FIRSTOS_MAX_TAB_URL];
    char     title[FIRSTOS_MAX_TAB_TITLE];
    firstos_origin_t committed_origin;
    firstos_tab_history_t history;
    /* Phase 5C: Frame Tree */
    firstos_frame_tree_t frame_tree;
} firstos_tab_t;

typedef struct {
    firstos_tab_t tabs[FIRSTOS_MAX_TABS];
    uint32_t active_tab_id;
    uint32_t next_tab_id;
    uint32_t tab_count;
} firstos_tab_manager_t;

typedef enum {
    FIRSTOS_RENDERER_SLOT_FREE = 0,
    FIRSTOS_RENDERER_SLOT_STARTING,
    FIRSTOS_RENDERER_SLOT_READY,
    FIRSTOS_RENDERER_SLOT_CRASHED,
    FIRSTOS_RENDERER_SLOT_CLOSING
} firstos_renderer_slot_state_t;

typedef struct {
    uint32_t slot_id;
    uint32_t generation;
    uint32_t tab_id;
    uint32_t tab_generation;
    uint32_t frame_id;
    uint32_t frame_generation;
    uint32_t pid;
    uint32_t state;
    uint32_t shm_id;
    uint32_t security_generation;
    uint32_t active;
} firstos_renderer_slot_t;

typedef struct {
    firstos_renderer_slot_t slots[FIRSTOS_MAX_RENDERER_SLOTS];
    uint32_t active_slot_id;
} firstos_renderer_slot_manager_t;

/* Tab lifecycle API */
void     browser_host_tab_manager_init(void);
void     browser_host_tab_manager_reset(void);
int      browser_host_tab_create(const char *url, int activate);
int      browser_host_tab_activate(uint32_t tab_id);
int      browser_host_tab_close(uint32_t tab_id);
int      browser_host_tab_navigate(uint32_t tab_id, const char *url);
int      browser_host_tab_reload(uint32_t tab_id);
int      browser_host_tab_back(uint32_t tab_id, char *out_url, int max_len);
int      browser_host_tab_forward(uint32_t tab_id, char *out_url, int max_len);
int      browser_host_tab_handle_crash(uint32_t tab_id);
int      browser_host_tab_respawn_renderer(uint32_t tab_id);
firstos_tab_t *browser_host_get_active_tab(void);
firstos_tab_t *browser_host_get_tab_by_id(uint32_t tab_id);
firstos_tab_manager_t *browser_host_get_tab_manager(void);

/* Phase 5C: Frame Tree & postMessage API */
int  browser_host_frame_create(uint32_t tab_id, uint32_t parent_frame_id, const char *url, int x, int y, int w, int h);
int  browser_host_frame_navigate(uint32_t tab_id, uint32_t frame_id, const char *url);
int  browser_host_frame_destroy(uint32_t tab_id, uint32_t frame_id);
int  browser_host_frame_handle_crash(uint32_t tab_id, uint32_t frame_id);
firstos_frame_t *browser_host_frame_get(uint32_t tab_id, uint32_t frame_id);
int  browser_host_frame_check_access(uint32_t tab_id, uint32_t caller_frame_id, uint32_t target_frame_id);
int  browser_host_post_message_send(const firstos_post_message_t *msg);
int  browser_host_post_message_receive(uint32_t tab_id, uint32_t frame_id, firstos_post_message_t *out_msg);
int  browser_host_post_message_between(uint32_t tab_id, uint32_t sender_frame_id, uint32_t target_frame_id,
                                       const char *data, const char *target_origin);
uint32_t browser_host_frame_total_count(void);

/* Phase 6A.1: User Gesture & Download Integration */
void     browser_host_record_user_gesture(uint32_t tab_id);
int      browser_host_has_valid_user_gesture(uint32_t tab_id);
int      browser_host_handle_download_request(uint32_t tab_id, uint32_t frame_id,
                                              const char *url, const char *suggested_filename,
                                              int renderer_gesture_flag);

#endif /* BROWSER_HOST_IPC_H */
