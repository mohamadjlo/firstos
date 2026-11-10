#include "browser_host_ipc.h"
#include "shm.h"
#include "process.h"
#include "framebuffer.h"
#include "font.h"
#include "serial.h"
#include "web_browser_app.h"
#include "fs.h"
#include "string.h"
#include "image.h"
#include "heap.h"
#include "download_manager.h"
#include "browser_storage.h"
#include "clock.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

static int s_browser_shmid = -1;
static char *s_shm_phys = NULL;
static int s_renderer_pid = -1;
static uint32_t s_req_counter = 0;
static uint32_t s_current_navigation_id = 1;
static char s_active_page_url[256] = "about:home";

#define HOST_RES_CACHE_SLOTS 16
#define HOST_RES_CACHE_MAX_DATA (64 * 1024)

typedef struct {
    int in_use;
    uint32_t access_tick;
    firstos_origin_t origin;
    char url[256];
    char mime[32];
    uint16_t res_type;
    uint32_t data_len;
    uint16_t meta_w;
    uint16_t meta_h;
    char data[HOST_RES_CACHE_MAX_DATA];
} host_res_cache_entry_t;

static host_res_cache_entry_t s_host_cache[HOST_RES_CACHE_SLOTS];
static uint32_t s_cache_tick_counter = 0;
static firstos_renderer_security_context_t s_renderer_security_ctx;

/* Phase 5B: Multi-Tab & Renderer Slot Managers */
static firstos_tab_manager_t s_tab_mgr;
static firstos_renderer_slot_manager_t s_slot_mgr;
static int s_tab_mgr_initialized = 0;

/* Phase 6A.1: User Gesture Tracking */
static uint32_t s_user_gesture_tab = 0;
static uint32_t s_user_gesture_tick = 0;
#define USER_GESTURE_TIMEOUT_SEC 3

void browser_host_record_user_gesture(uint32_t tab_id) {
    s_user_gesture_tab = tab_id;
    s_user_gesture_tick = clock_monotonic_get_ms() / 1000;
}

int browser_host_has_valid_user_gesture(uint32_t tab_id) {
    if (tab_id == 0 || s_user_gesture_tab != tab_id) return 0;
    uint32_t now = clock_monotonic_get_ms() / 1000;
    if (now >= s_user_gesture_tick && (now - s_user_gesture_tick) <= USER_GESTURE_TIMEOUT_SEC) {
        return 1;
    }
    return 0;
}

/* Phase 5C: frame tree hooks (implemented at end of file) */
static void frame_tree_init_for_tab(firstos_tab_t *tab);
static void frame_tree_on_tab_navigate(firstos_tab_t *tab);
static void frame_tree_on_tab_close(firstos_tab_t *tab);
static void frame_globals_reset(void);
static void browser_host_frames_sync_from_commands(firstos_render_command_t *cmds, uint32_t n,
                                                   const char *strings, uint32_t str_cap);
static void browser_host_frame_composite(uint32_t frame_id, int x, int y, int w, int h);

/* Phase 4D: Navigation History Manager */
#define HOST_HISTORY_MAX 32

typedef struct {
    char url[256];
    uint32_t navigation_id;
} host_history_entry_t;

typedef struct {
    host_history_entry_t entries[HOST_HISTORY_MAX];
    int count;
    int index;
} firstos_history_manager_t;


/* Phase 4D: Host Decoded Image Store */
#define HOST_IMAGE_STORE_SLOTS 8
#define HOST_IMAGE_MAX_W 1024
#define HOST_IMAGE_MAX_H 1024

typedef struct {
    uint32_t image_id;
    int in_use;
    int w;
    int h;
    uint32_t *pixels; /* 0xAARRGGBB */
    uint32_t access_tick;
} host_image_slot_t;

static host_image_slot_t s_image_store[HOST_IMAGE_STORE_SLOTS];
static uint32_t s_next_image_id = 1;
static uint32_t s_image_tick_counter = 0;

uint32_t browser_host_image_store_insert(const uint32_t *pixels, int w, int h) {
    if (!pixels || w <= 0 || h <= 0 || w > HOST_IMAGE_MAX_W || h > HOST_IMAGE_MAX_H) return 0;

    int slot_idx = -1;
    uint32_t oldest_tick = 0xFFFFFFFF;

    for (int i = 0; i < HOST_IMAGE_STORE_SLOTS; i++) {
        if (!s_image_store[i].in_use) {
            slot_idx = i;
            break;
        }
        if (s_image_store[i].access_tick < oldest_tick) {
            oldest_tick = s_image_store[i].access_tick;
            slot_idx = i;
        }
    }

    if (slot_idx < 0) slot_idx = 0;

    host_image_slot_t *slot = &s_image_store[slot_idx];
    if (slot->in_use && slot->pixels) {
        kfree(slot->pixels);
        slot->pixels = NULL;
    }

    uint32_t sz = (uint32_t)(w * h * 4);
    slot->pixels = (uint32_t *)kmalloc(sz);
    if (!slot->pixels) {
        slot->in_use = 0;
        return 0;
    }

    memcpy(slot->pixels, pixels, sz);
    slot->in_use = 1;
    slot->w = w;
    slot->h = h;
    slot->image_id = s_next_image_id++;
    slot->access_tick = ++s_image_tick_counter;

    serial_write("[browser_host] cached decoded image id=");
    serial_write_hex(slot->image_id);
    serial_write(" w=");
    serial_write_hex((unsigned int)w);
    serial_write(" h=");
    serial_write_hex((unsigned int)h);
    serial_write("\n");

    return slot->image_id;
}

const uint32_t *browser_host_image_store_get(uint32_t image_id, int *out_w, int *out_h) {
    if (image_id == 0) return NULL;
    for (int i = 0; i < HOST_IMAGE_STORE_SLOTS; i++) {
        if (s_image_store[i].in_use && s_image_store[i].image_id == image_id) {
            s_image_store[i].access_tick = ++s_image_tick_counter;
            if (out_w) *out_w = s_image_store[i].w;
            if (out_h) *out_h = s_image_store[i].h;
            return s_image_store[i].pixels;
        }
    }
    return NULL;
}

void browser_host_image_store_clear(void) {
    for (int i = 0; i < HOST_IMAGE_STORE_SLOTS; i++) {
        if (s_image_store[i].in_use && s_image_store[i].pixels) {
            kfree(s_image_store[i].pixels);
            s_image_store[i].pixels = NULL;
        }
        s_image_store[i].in_use = 0;
        s_image_store[i].image_id = 0;
    }
}

void browser_host_tab_manager_init(void) {
    if (s_tab_mgr_initialized) return;
    memset(&s_tab_mgr, 0, sizeof(s_tab_mgr));
    memset(&s_slot_mgr, 0, sizeof(s_slot_mgr));
    s_tab_mgr.next_tab_id = 1;
    s_tab_mgr_initialized = 1;
    frame_globals_reset();

    /* Create initial tab 1 (about:home) */
    browser_host_tab_create("about:home", 1);
}

void browser_host_tab_manager_reset(void) {
    s_tab_mgr_initialized = 0;
    browser_host_tab_manager_init();
}

firstos_tab_t *browser_host_get_active_tab(void) {
    if (!s_tab_mgr_initialized) browser_host_tab_manager_init();
    for (int i = 0; i < FIRSTOS_MAX_TABS; i++) {
        if (s_tab_mgr.tabs[i].state != FIRSTOS_TAB_FREE && s_tab_mgr.tabs[i].tab_id == s_tab_mgr.active_tab_id) {
            return &s_tab_mgr.tabs[i];
        }
    }
    return NULL;
}

firstos_tab_t *browser_host_get_tab_by_id(uint32_t tab_id) {
    if (!s_tab_mgr_initialized) browser_host_tab_manager_init();
    for (int i = 0; i < FIRSTOS_MAX_TABS; i++) {
        if (s_tab_mgr.tabs[i].state != FIRSTOS_TAB_FREE && s_tab_mgr.tabs[i].tab_id == tab_id) {
            return &s_tab_mgr.tabs[i];
        }
    }
    return NULL;
}

firstos_tab_manager_t *browser_host_get_tab_manager(void) {
    if (!s_tab_mgr_initialized) browser_host_tab_manager_init();
    return &s_tab_mgr;
}

int browser_host_tab_create(const char *url, int activate) {
    if (!s_tab_mgr_initialized) {
        memset(&s_tab_mgr, 0, sizeof(s_tab_mgr));
        memset(&s_slot_mgr, 0, sizeof(s_slot_mgr));
        s_tab_mgr.next_tab_id = 1;
        s_tab_mgr_initialized = 1;
    }

    if (s_tab_mgr.tab_count >= FIRSTOS_MAX_TABS) {
        serial_write("[tab] create rejected: max tabs reached\n");
        return -1;
    }

    int slot_idx = -1;
    for (int i = 0; i < FIRSTOS_MAX_TABS; i++) {
        if (s_tab_mgr.tabs[i].state == FIRSTOS_TAB_FREE) {
            slot_idx = i;
            break;
        }
    }
    if (slot_idx < 0) return -1;

    firstos_tab_t *tab = &s_tab_mgr.tabs[slot_idx];
    uint32_t old_gen = tab->generation;
    memset(tab, 0, sizeof(firstos_tab_t));
    tab->generation = old_gen + 1;
    tab->tab_id = s_tab_mgr.next_tab_id++;
    tab->state = FIRSTOS_TAB_READY;
    tab->renderer_slot_id = (uint32_t)slot_idx;
    tab->renderer_pid = (uint32_t)s_renderer_pid;
    tab->navigation_id = browser_host_bump_navigation_id();
    tab->scroll_x = 0;
    tab->scroll_y = 0;

    const char *target_url = (url && url[0]) ? url : "about:home";
    strncpy(tab->url, target_url, sizeof(tab->url) - 1);
    tab->url[sizeof(tab->url) - 1] = '\0';

    if (strcmp(tab->url, "about:home") == 0) {
        strncpy(tab->title, "Home", sizeof(tab->title) - 1);
    } else if (strcmp(tab->url, "about:html5") == 0) {
        strncpy(tab->title, "HTML5 Test", sizeof(tab->title) - 1);
    } else {
        strncpy(tab->title, "New Tab", sizeof(tab->title) - 1);
    }
    tab->title[sizeof(tab->title) - 1] = '\0';

    browser_security_parse_origin(tab->url, &tab->committed_origin);

    /* Initialize per-tab history */
    tab->history.count = 1;
    tab->history.index = 0;
    strncpy(tab->history.entries[0].url, tab->url, sizeof(tab->history.entries[0].url) - 1);
    tab->history.entries[0].url[sizeof(tab->history.entries[0].url) - 1] = '\0';
    tab->history.entries[0].navigation_id = tab->navigation_id;

    /* Bind renderer slot */
    s_slot_mgr.slots[slot_idx].slot_id = (uint32_t)slot_idx;
    s_slot_mgr.slots[slot_idx].tab_id = tab->tab_id;
    s_slot_mgr.slots[slot_idx].tab_generation = tab->generation;
    s_slot_mgr.slots[slot_idx].state = FIRSTOS_RENDERER_SLOT_READY;
    s_slot_mgr.slots[slot_idx].active = activate ? 1 : 0;

    s_tab_mgr.tab_count++;
    frame_tree_init_for_tab(tab);

    serial_write("[tab] created id=");
    serial_write_hex(tab->tab_id);
    serial_write(" generation=");
    serial_write_hex(tab->generation);
    serial_write("\n");

    serial_write("[tab] renderer slot bound tab=");
    serial_write_hex(tab->tab_id);
    serial_write("\n");

    if (activate) {
        browser_host_tab_activate(tab->tab_id);
    }

    return (int)tab->tab_id;
}

int browser_host_tab_activate(uint32_t tab_id) {
    if (!s_tab_mgr_initialized) return -1;

    firstos_tab_t *target = NULL;
    for (int i = 0; i < FIRSTOS_MAX_TABS; i++) {
        if (s_tab_mgr.tabs[i].state != FIRSTOS_TAB_FREE && s_tab_mgr.tabs[i].tab_id == tab_id) {
            target = &s_tab_mgr.tabs[i];
            break;
        }
    }
    if (!target) return -1;

    uint32_t old_id = s_tab_mgr.active_tab_id;
    s_tab_mgr.active_tab_id = tab_id;

    for (int i = 0; i < FIRSTOS_MAX_TABS; i++) {
        if (s_tab_mgr.tabs[i].state != FIRSTOS_TAB_FREE) {
            s_tab_mgr.tabs[i].active = (s_tab_mgr.tabs[i].tab_id == tab_id);
            s_slot_mgr.slots[i].active = (s_tab_mgr.tabs[i].tab_id == tab_id);
        }
    }

    /* Synchronize active security context */
    s_renderer_security_ctx.committed_origin = target->committed_origin;
    s_renderer_security_ctx.navigation_id = target->navigation_id;
    strncpy(s_active_page_url, target->url, sizeof(s_active_page_url) - 1);
    s_active_page_url[sizeof(s_active_page_url) - 1] = '\0';

    serial_write("[tab] active changed old=");
    serial_write_hex(old_id);
    serial_write(" new=");
    serial_write_hex(tab_id);
    serial_write("\n");

    return 0;
}

int browser_host_tab_close(uint32_t tab_id) {
    if (!s_tab_mgr_initialized) return -1;

    int slot_idx = -1;
    for (int i = 0; i < FIRSTOS_MAX_TABS; i++) {
        if (s_tab_mgr.tabs[i].state != FIRSTOS_TAB_FREE && s_tab_mgr.tabs[i].tab_id == tab_id) {
            slot_idx = i;
            break;
        }
    }
    if (slot_idx < 0) return -1;

    firstos_tab_t *tab = &s_tab_mgr.tabs[slot_idx];
    tab->state = FIRSTOS_TAB_CLOSING;
    frame_tree_on_tab_close(tab);
    /* Phase 6A.1: Cancel active downloads belonging to this tab */
    download_cancel_for_tab(tab_id);

    serial_write("[tab] closed id=");
    serial_write_hex(tab_id);
    serial_write("\n");

    /* Release slot */
    s_slot_mgr.slots[slot_idx].state = FIRSTOS_RENDERER_SLOT_FREE;
    s_slot_mgr.slots[slot_idx].tab_id = 0;
    tab->state = FIRSTOS_TAB_FREE;
    tab->active = 0;
    s_tab_mgr.tab_count--;

    /* If closing active tab, activate neighbor or create new-tab */
    if (s_tab_mgr.active_tab_id == tab_id) {
        uint32_t next_active = 0;
        /* Look right */
        for (int i = slot_idx + 1; i < FIRSTOS_MAX_TABS; i++) {
            if (s_tab_mgr.tabs[i].state != FIRSTOS_TAB_FREE) {
                next_active = s_tab_mgr.tabs[i].tab_id;
                break;
            }
        }
        /* Look left */
        if (next_active == 0) {
            for (int i = slot_idx - 1; i >= 0; i--) {
                if (s_tab_mgr.tabs[i].state != FIRSTOS_TAB_FREE) {
                    next_active = s_tab_mgr.tabs[i].tab_id;
                    break;
                }
            }
        }

        if (next_active != 0) {
            browser_host_tab_activate(next_active);
        } else {
            /* Last tab closed: create fresh new-tab state instead of null dereference */
            browser_host_tab_create("about:home", 1);
            serial_write("[tab] new-tab state active\n");
        }
    }

    return 0;
}

int browser_host_tab_navigate(uint32_t tab_id, const char *url) {
    if (!url || !url[0]) return -1;
    firstos_tab_t *tab = browser_host_get_tab_by_id(tab_id);
    if (!tab) return -1;

    tab->navigation_id = browser_host_bump_navigation_id();
    tab->document_generation++;
    strncpy(tab->url, url, sizeof(tab->url) - 1);
    tab->url[sizeof(tab->url) - 1] = '\0';
    browser_security_parse_origin(tab->url, &tab->committed_origin);

    if (strcmp(tab->url, "about:home") == 0) {
        strncpy(tab->title, "Home", sizeof(tab->title) - 1);
    } else if (strcmp(tab->url, "about:html5") == 0) {
        strncpy(tab->title, "HTML5 Test", sizeof(tab->title) - 1);
    } else {
        strncpy(tab->title, url, sizeof(tab->title) - 1);
    }
    tab->title[sizeof(tab->title) - 1] = '\0';

    /* Update per-tab history */
    if (tab->history.index < (int32_t)tab->history.count - 1) {
        tab->history.count = (uint32_t)(tab->history.index + 1);
    }
    if (tab->history.count >= FIRSTOS_MAX_TAB_HISTORY) {
        for (int i = 1; i < FIRSTOS_MAX_TAB_HISTORY; i++) {
            tab->history.entries[i - 1] = tab->history.entries[i];
        }
        tab->history.count = FIRSTOS_MAX_TAB_HISTORY - 1;
        tab->history.index = (int32_t)(tab->history.count - 1);
    }
    strncpy(tab->history.entries[tab->history.count].url, url, sizeof(tab->history.entries[0].url) - 1);
    tab->history.entries[tab->history.count].url[sizeof(tab->history.entries[0].url) - 1] = '\0';
    tab->history.entries[tab->history.count].navigation_id = tab->navigation_id;
    tab->history.index = (int32_t)tab->history.count++;

    /* Phase 6A.2: Record to persistent browsing history */
    browser_storage_history_add(url, tab->title, 0);

    frame_tree_on_tab_navigate(tab);

    serial_write("[tab] history isolated tab=");
    serial_write_hex(tab_id);
    serial_write("\n");

    if (tab->tab_id == s_tab_mgr.active_tab_id) {
        s_renderer_security_ctx.committed_origin = tab->committed_origin;
        s_renderer_security_ctx.navigation_id = tab->navigation_id;
        strncpy(s_active_page_url, tab->url, sizeof(s_active_page_url) - 1);
        s_active_page_url[sizeof(s_active_page_url) - 1] = '\0';
    }

    return 0;
}

int browser_host_tab_back(uint32_t tab_id, char *out_url, int max_len) {
    firstos_tab_t *tab = browser_host_get_tab_by_id(tab_id);
    if (!tab || tab->history.index <= 0 || !out_url || max_len <= 0) return -1;

    tab->history.index--;
    tab->navigation_id = browser_host_bump_navigation_id();
    tab->document_generation++;
    strncpy(out_url, tab->history.entries[tab->history.index].url, max_len - 1);
    out_url[max_len - 1] = '\0';
    strncpy(tab->url, out_url, sizeof(tab->url) - 1);
    tab->url[sizeof(tab->url) - 1] = '\0';
    browser_security_parse_origin(tab->url, &tab->committed_origin);

    serial_write("[tab] history isolated tab=");
    serial_write_hex(tab_id);
    serial_write(" back\n");

    return 0;
}

int browser_host_tab_forward(uint32_t tab_id, char *out_url, int max_len) {
    firstos_tab_t *tab = browser_host_get_tab_by_id(tab_id);
    if (!tab || tab->history.index < 0 || tab->history.index >= (int32_t)tab->history.count - 1 || !out_url || max_len <= 0) return -1;

    tab->history.index++;
    tab->navigation_id = browser_host_bump_navigation_id();
    tab->document_generation++;
    strncpy(out_url, tab->history.entries[tab->history.index].url, max_len - 1);
    out_url[max_len - 1] = '\0';
    strncpy(tab->url, out_url, sizeof(tab->url) - 1);
    tab->url[sizeof(tab->url) - 1] = '\0';
    browser_security_parse_origin(tab->url, &tab->committed_origin);

    serial_write("[tab] history isolated tab=");
    serial_write_hex(tab_id);
    serial_write(" forward\n");

    return 0;
}

int browser_host_tab_respawn_renderer(uint32_t tab_id) {
    firstos_tab_t *tab = browser_host_get_tab_by_id(tab_id);
    if (!tab) return -1;

    serial_write("[tab] respawning renderer for tab=");
    serial_write_hex(tab_id);
    serial_write("\n");

    /* Reset renderer PID and force a fresh spawn */
    s_renderer_pid = -1;
    int new_pid = browser_host_ipc_spawn_renderer();
    if (new_pid < 0) {
        serial_write("[tab] ERROR: failed to respawn renderer for tab=");
        serial_write_hex(tab_id);
        serial_write("\n");
        return -1;
    }

    /* Update tab and slot state */
    tab->renderer_pid = (uint32_t)new_pid;
    tab->state = FIRSTOS_TAB_READY;
    tab->has_error = 0;
    tab->navigation_id = browser_host_bump_navigation_id();
    tab->document_generation++;

    if (tab->renderer_slot_id < FIRSTOS_MAX_RENDERER_SLOTS) {
        s_slot_mgr.slots[tab->renderer_slot_id].pid = (uint32_t)new_pid;
        s_slot_mgr.slots[tab->renderer_slot_id].state = FIRSTOS_RENDERER_SLOT_READY;
    }

    /* If this is the active tab, synchronize host security context and active URL */
    if (tab->tab_id == s_tab_mgr.active_tab_id) {
        s_renderer_security_ctx.committed_origin = tab->committed_origin;
        s_renderer_security_ctx.navigation_id = tab->navigation_id;
        s_renderer_security_ctx.renderer_pid = new_pid;
        strncpy(s_active_page_url, tab->url, sizeof(s_active_page_url) - 1);
        s_active_page_url[sizeof(s_active_page_url) - 1] = '\0';
    }

    serial_write("[tab] renderer respawned pid=");
    serial_write_hex((unsigned int)new_pid);
    serial_write(" tab=");
    serial_write_hex(tab_id);
    serial_write("\n");

    return 0;
}

int browser_host_tab_reload(uint32_t tab_id) {
    firstos_tab_t *tab = browser_host_get_tab_by_id(tab_id);
    if (!tab) return -1;

    /* If tab was crashed, respawn its renderer instance */
    if (tab->state == FIRSTOS_TAB_CRASHED) {
        return browser_host_tab_respawn_renderer(tab_id);
    }

    tab->navigation_id = browser_host_bump_navigation_id();
    tab->document_generation++;
    return 0;
}

int browser_host_tab_handle_crash(uint32_t tab_id) {
    firstos_tab_t *tab = browser_host_get_tab_by_id(tab_id);
    if (!tab) return -1;

    tab->state = FIRSTOS_TAB_CRASHED;
    tab->has_error = 1;

    if (tab->renderer_slot_id < FIRSTOS_MAX_RENDERER_SLOTS) {
        s_slot_mgr.slots[tab->renderer_slot_id].state = FIRSTOS_RENDERER_SLOT_CRASHED;
    }

    serial_write("[tab] renderer crash tab=");
    serial_write_hex(tab_id);
    serial_write("\n");

    serial_write("[tab] renderer cleanup complete tab=");
    serial_write_hex(tab_id);
    serial_write("\n");

    serial_write("[tab] error page displayed tab=");
    serial_write_hex(tab_id);
    serial_write("\n");

    return 0;
}

int browser_host_history_push(const char *url) {
    firstos_tab_t *act = browser_host_get_active_tab();
    if (act) {
        return browser_host_tab_navigate(act->tab_id, url);
    }
    return -1;
}

int browser_host_history_back(char *out_url, int max_len) {
    firstos_tab_t *act = browser_host_get_active_tab();
    if (act) {
        return browser_host_tab_back(act->tab_id, out_url, max_len);
    }
    return -1;
}

int browser_host_history_forward(char *out_url, int max_len) {
    firstos_tab_t *act = browser_host_get_active_tab();
    if (act) {
        return browser_host_tab_forward(act->tab_id, out_url, max_len);
    }
    return -1;
}

int browser_host_history_reload(char *out_url, int max_len) {
    firstos_tab_t *act = browser_host_get_active_tab();
    if (act && out_url && max_len > 0) {
        browser_host_tab_reload(act->tab_id);
        strncpy(out_url, act->url, max_len - 1);
        out_url[max_len - 1] = '\0';
        return 0;
    }
    return -1;
}

int browser_host_history_get_state(int *out_index, int *out_count) {
    firstos_tab_t *act = browser_host_get_active_tab();
    if (act) {
        if (out_index) *out_index = act->history.index;
        if (out_count) *out_count = (int)act->history.count;
        return 0;
    }
    return -1;
}

void browser_host_history_clear(void) {
    firstos_tab_t *act = browser_host_get_active_tab();
    if (act) {
        act->history.count = 0;
        act->history.index = -1;
    }
}

int browser_host_validate_url(const char *url, const char *base_origin) {
    if (!url || !url[0]) return -1;
    
    int len = 0;
    while (url[len]) {
        if ((unsigned char)url[len] < 32 || (unsigned char)url[len] == 127) {
            return -2; /* Control characters not allowed */
        }
        len++;
        if (len > 1024) return -3; /* Exceeds max length */
    }
    
    /* Traversal defense */
    for (int i = 0; i < len - 1; i++) {
        if (url[i] == '.' && url[i+1] == '.') {
            if (i + 2 == len || url[i+2] == '/' || url[i+2] == '\\' || (i > 0 && (url[i-1] == '/' || url[i-1] == '\\'))) {
                return -4; /* Traversal prohibited */
            }
        }
    }
    
    if (strncmp(url, "about:", 6) == 0) return 0;
    if (strncmp(url, "data:", 5) == 0) return 0;
    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) return 0;
    if (strncmp(url, "file://", 7) == 0 || url[0] == '/') {
        if (base_origin) {
            if (strncmp(base_origin, "http://", 7) == 0 || strncmp(base_origin, "https://", 8) == 0) {
                return -5; /* Cross-origin local access blocked */
            }
        }
        return 0;
    }
    
    return -6; /* Unsupported scheme */
}

uint32_t browser_host_get_navigation_id(void) {
    return s_current_navigation_id;
}

uint32_t browser_host_bump_navigation_id(void) {
    return ++s_current_navigation_id;
}

static inline char *host_strcpy(char *d, const char *s) {
    char *ret = d;
    while ((*d++ = *s++)) {}
    return ret;
}

int browser_host_set_committed_origin(const char *url) {
    if (!url || !url[0]) return -1;
    browser_security_parse_origin(url, &s_renderer_security_ctx.committed_origin);
    s_renderer_security_ctx.navigation_id = s_current_navigation_id;
    s_renderer_security_ctx.renderer_pid = s_renderer_pid;
    s_renderer_security_ctx.is_isolated = 1;

    char orig_str[128];
    browser_security_origin_to_string(&s_renderer_security_ctx.committed_origin, orig_str, sizeof(orig_str));
    serial_write("[security] committed origin: ");
    serial_write(orig_str);
    serial_write(" (nav=");
    serial_write_hex(s_current_navigation_id);
    serial_write(")\n");
    return 0;
}

const firstos_renderer_security_context_t *browser_host_get_security_context(void) {
    return &s_renderer_security_ctx;
}

int browser_host_partitioned_cache_lookup(const firstos_origin_t *origin, const char *url, char *out_buf, int max_len, char *out_mime, int *out_len, uint16_t *out_w, uint16_t *out_h) {
    if (!url || !out_buf || max_len <= 0) return -1;
    for (int i = 0; i < HOST_RES_CACHE_SLOTS; i++) {
        if (s_host_cache[i].in_use && strcmp(s_host_cache[i].url, url) == 0) {
            /* Origin partitioning: check if origin matches or either is opaque/file */
            if (origin && s_host_cache[i].origin.scheme != FIRSTOS_ORIGIN_OPAQUE) {
                if (!browser_security_origin_equals(&s_host_cache[i].origin, origin)) {
                    continue; /* Partitioned isolation: different origins cannot share cache */
                }
            }
            s_host_cache[i].access_tick = ++s_cache_tick_counter;
            int clen = (int)s_host_cache[i].data_len;
            if (clen > max_len) clen = max_len;
            memcpy(out_buf, s_host_cache[i].data, clen);
            if (out_len) *out_len = clen;
            if (out_mime) host_strcpy(out_mime, s_host_cache[i].mime);
            if (out_w) *out_w = s_host_cache[i].meta_w;
            if (out_h) *out_h = s_host_cache[i].meta_h;
            return 0;
        }
    }
    return -1;
}

int browser_host_cache_lookup(const char *url, char *out_buf, int max_len, char *out_mime, int *out_len, uint16_t *out_w, uint16_t *out_h) {
    return browser_host_partitioned_cache_lookup(&s_renderer_security_ctx.committed_origin, url, out_buf, max_len, out_mime, out_len, out_w, out_h);
}

int browser_host_partitioned_cache_insert(const firstos_origin_t *origin, const char *url, const char *mime, const char *data, int len, uint16_t meta_w, uint16_t meta_h) {
    if (!url || !data || len <= 0 || len > HOST_RES_CACHE_MAX_DATA) return -1;
    
    int target_slot = -1;
    uint32_t oldest_tick = 0xFFFFFFFF;
    
    for (int i = 0; i < HOST_RES_CACHE_SLOTS; i++) {
        if (s_host_cache[i].in_use && strcmp(s_host_cache[i].url, url) == 0) {
            if (!origin || browser_security_origin_equals(&s_host_cache[i].origin, origin)) {
                target_slot = i;
                break;
            }
        }
        if (!s_host_cache[i].in_use) {
            target_slot = i;
            break;
        }
        if (s_host_cache[i].access_tick < oldest_tick) {
            oldest_tick = s_host_cache[i].access_tick;
            target_slot = i;
        }
    }
    
    if (target_slot < 0) target_slot = 0;
    
    host_res_cache_entry_t *entry = &s_host_cache[target_slot];
    entry->in_use = 1;
    entry->access_tick = ++s_cache_tick_counter;
    if (origin) {
        entry->origin = *origin;
    } else {
        memset(&entry->origin, 0, sizeof(entry->origin));
    }
    strncpy(entry->url, url, sizeof(entry->url) - 1);
    entry->url[sizeof(entry->url) - 1] = '\0';
    if (mime) {
        strncpy(entry->mime, mime, sizeof(entry->mime) - 1);
        entry->mime[sizeof(entry->mime) - 1] = '\0';
    } else {
        host_strcpy(entry->mime, "application/octet-stream");
    }
    memcpy(entry->data, data, len);
    entry->data_len = (uint32_t)len;
    entry->meta_w = meta_w;
    entry->meta_h = meta_h;
    return 0;
}

int browser_host_cache_insert(const char *url, const char *mime, const char *data, int len, uint16_t meta_w, uint16_t meta_h) {
    return browser_host_partitioned_cache_insert(&s_renderer_security_ctx.committed_origin, url, mime, data, len, meta_w, meta_h);
}

void browser_host_cache_clear(void) {
    for (int i = 0; i < HOST_RES_CACHE_SLOTS; i++) {
        s_host_cache[i].in_use = 0;
    }
}

static inline void host_mutex_lock(volatile uint32_t *lock) {
    while (!__sync_bool_compare_and_swap(lock, 0, 1)) {
        /* spin */
    }
}

static inline void host_mutex_unlock(volatile uint32_t *lock) {
    __sync_lock_test_and_set(lock, 0);
}

int browser_host_ipc_init(void) {
    if (s_browser_shmid >= 0 && s_shm_phys) {
        return 0; /* already initialized */
    }

    s_browser_shmid = shm_open(FIRSTOS_BROWSER_SHM_NAME, 0, 0);
    if (s_browser_shmid < 0 || s_browser_shmid >= FIRSTOS_MAX_SHM) {
        serial_write("[browser_host] ERROR: failed to open browser shared memory region!\n");
        return -1;
    }

    s_shm_phys = shm_table[s_browser_shmid].phys_mem;
    if (!s_shm_phys) {
        serial_write("[browser_host] ERROR: null physical memory for browser shm!\n");
        return -1;
    }

    /* Initialize Host -> Renderer Channel at offset 0 */
    firstos_browser_channel_t *h2r = (firstos_browser_channel_t *)(s_shm_phys + FIRSTOS_BROWSER_CHANNEL_H2R_OFF);
    h2r->magic = FIRSTOS_BROWSER_IPC_MAGIC;
    h2r->version = FIRSTOS_BROWSER_IPC_VERSION;
    h2r->capacity = FIRSTOS_BROWSER_CHANNEL_SIZE;
    h2r->lock_word = 0;
    h2r->has_message = 0;
    h2r->seq = 0;
    h2r->sender_pid = 0;

    /* Initialize Renderer -> Host Channel at offset 2MB */
    firstos_browser_channel_t *r2h = (firstos_browser_channel_t *)(s_shm_phys + FIRSTOS_BROWSER_CHANNEL_R2H_OFF);
    r2h->magic = FIRSTOS_BROWSER_IPC_MAGIC;
    r2h->version = FIRSTOS_BROWSER_IPC_VERSION;
    r2h->capacity = FIRSTOS_BROWSER_CHANNEL_SIZE;
    r2h->lock_word = 0;
    r2h->has_message = 0;
    r2h->seq = 0;
    r2h->sender_pid = 0;

    serial_write("[browser_host] Browser IPC shared memory channels initialized\n");
    browser_host_tab_manager_init();
    return 0;
}

int browser_host_ipc_spawn_renderer(void) {
    if (s_renderer_pid > 0 && process_table[s_renderer_pid].state == PROC_STATE_READY) {
        return s_renderer_pid; /* already running */
    }

    if (browser_host_ipc_init() != 0) {
        return -1;
    }

    const char *args[] = { "/browser_renderer.elf", 0 };
    serial_write("[browser_host] spawning userspace renderer /browser_renderer.elf\n");
    s_renderer_pid = process_spawn(0, "/browser_renderer.elf", args);
    if (s_renderer_pid < 0) {
        serial_write("[browser_host] ERROR: failed to spawn /browser_renderer.elf\n");
        return -1;
    }

    serial_write("[browser_host] renderer spawned pid=");
    serial_write_hex((unsigned int)s_renderer_pid);
    serial_write("\n");
    return s_renderer_pid;
}

int browser_host_ipc_is_active(void) {
    if (s_renderer_pid <= 0) return 0;
    if (s_renderer_pid >= FIRSTOS_MAX_PROCESSES) return 0;
    return (process_table[s_renderer_pid].state == PROC_STATE_READY);
}

int browser_host_ipc_get_renderer_pid(void) {
    return s_renderer_pid;
}

int browser_host_ipc_send_page(const char *url, const char *html, int len, int view_w, int view_h) {
    if (url && url[0]) {
        strncpy(s_active_page_url, url, sizeof(s_active_page_url) - 1);
        s_active_page_url[sizeof(s_active_page_url) - 1] = '\0';
        browser_host_set_committed_origin(url);
    }

    if (!browser_host_ipc_is_active()) {
        if (browser_host_ipc_spawn_renderer() < 0) {
            return -1;
        }
    }

    if (!s_shm_phys) return -1;

    firstos_browser_channel_t *h2r = (firstos_browser_channel_t *)(s_shm_phys + FIRSTOS_BROWSER_CHANNEL_H2R_OFF);
    char *data = (char *)h2r + sizeof(firstos_browser_channel_t);
    unsigned int max_avail = FIRSTOS_BROWSER_CHANNEL_SIZE - sizeof(firstos_browser_channel_t);

    if (len < 0) len = 0;
    if ((unsigned int)len > max_avail) len = (int)max_avail;

    host_mutex_lock((volatile uint32_t *)(void *)&h2r->lock_word);

    for (int i = 0; i < len; i++) {
        data[i] = html[i];
    }

    h2r->msg.version = FIRSTOS_BROWSER_IPC_VERSION;
    h2r->msg.type = FIRSTOS_BROWSER_MSG_HTML_DATA;
    h2r->msg.request_id = ++s_req_counter;
    h2r->msg.payload_offset = 0;
    h2r->msg.payload_length = (uint32_t)len;
    h2r->msg.param1 = (uint32_t)view_w;
    h2r->msg.param2 = (uint32_t)view_h;
    h2r->msg.status = (int32_t)s_current_navigation_id;
    h2r->has_message = 1;

    host_mutex_unlock((volatile uint32_t *)(void *)&h2r->lock_word);

    serial_write("[browser_host] sent HTML_DATA to renderer (req=");
    serial_write_hex(h2r->msg.request_id);
    serial_write(" nav=");
    serial_write_hex(s_current_navigation_id);
    serial_write(" bytes=");
    serial_write_hex((unsigned int)len);
    serial_write(")\n");

    return 0;
}

int browser_host_ipc_send_input(int input_type, int mx, int my, int key) {
    (void)key;
    firstos_tab_t *act = browser_host_get_active_tab();
    if (!act || act->state == FIRSTOS_TAB_FREE || act->state == FIRSTOS_TAB_CRASHED) {
        return -1;
    }
    if (!browser_host_ipc_is_active() || !s_shm_phys) return -1;

    serial_write("[tab] input routed tab=");
    serial_write_hex((unsigned int)act->tab_id);
    serial_write("\n");

    /* Phase 6A.1: Record user gesture on interaction */
    browser_host_record_user_gesture(act->tab_id);

    firstos_browser_channel_t *h2r = (firstos_browser_channel_t *)(s_shm_phys + FIRSTOS_BROWSER_CHANNEL_H2R_OFF);

    host_mutex_lock((volatile uint32_t *)(void *)&h2r->lock_word);

    h2r->msg.version = FIRSTOS_BROWSER_IPC_VERSION;
    h2r->msg.type = FIRSTOS_BROWSER_MSG_INPUT_EVENT;
    h2r->msg.request_id = ++s_req_counter;
    h2r->msg.payload_offset = 0;
    h2r->msg.payload_length = 0;
    h2r->msg.param1 = (uint32_t)mx;
    h2r->msg.param2 = (uint32_t)my;
    h2r->msg.status = (int32_t)input_type;
    h2r->has_message = 1;

    host_mutex_unlock((volatile uint32_t *)(void *)&h2r->lock_word);
    return 0;
}

static firstos_render_command_t s_cached_cmds[256];
static uint32_t s_cached_num_cmds = 0;
static char s_cached_strings[8192];
static int s_has_cached_frame = 0;
static firstos_damage_rect_t s_cached_damage = {0, 0, 0, 0, FIRSTOS_RENDER_FLAG_FULL_REPAINT, {0, 0, 0}};

const firstos_damage_rect_t *browser_host_get_cached_damage(void) {
    return &s_cached_damage;
}

int browser_host_ipc_poll_render_commands(app_window_t *app, int content_x, int content_y, int content_w, int content_h) {
    (void)app;
    if (!s_shm_phys) return 0;

    /* Phase 2/6: Check renderer health and crash containment */
    firstos_tab_t *active_tab = browser_host_get_active_tab();
    if (s_renderer_pid > 0 && (process_table[s_renderer_pid].state == PROC_STATE_ZOMBIE ||
                               process_table[s_renderer_pid].state == PROC_STATE_UNUSED ||
                               process_table[s_renderer_pid].state == PROC_STATE_REAPED)) {
        if (active_tab && active_tab->state != FIRSTOS_TAB_CRASHED) {
            serial_write("[browser_host] CRASH DETECTED: Renderer PID ");
            serial_write_hex((unsigned int)s_renderer_pid);
            serial_write(" terminated unexpectedly. Marking tab crashed.\n");
            active_tab->state = FIRSTOS_TAB_CRASHED;
            s_renderer_pid = -1;
            s_has_cached_frame = 0;
        }
    }

    if (active_tab && active_tab->state == FIRSTOS_TAB_CRASHED) {
        /* Render Chromium-style 'Sad Tab' screen */
        fb_rect(content_x, content_y, content_w, content_h, 0x1E1E28);
        int card_w = 420;
        int card_h = 160;
        int card_x = content_x + (content_w - card_w) / 2;
        int card_y = content_y + (content_h - card_h) / 2;
        if (card_x < content_x) card_x = content_x + 10;
        if (card_y < content_y) card_y = content_y + 10;
        fb_round_rect(card_x, card_y, card_w, card_h, 8, 0x282A36);
        fb_round_rect_border(card_x, card_y, card_w, card_h, 8, 0xFF5555);

        font_text(card_x + 24, card_y + 20, ":-( Aw, Snap!", 0xFF5555, 2);
        font_text(card_x + 24, card_y + 56, "Something went wrong while displaying this webpage.", 0xF8F8F2, 1);
        font_text(card_x + 24, card_y + 80, "Error code: RESULT_CODE_KILLED_BAD_MESSAGE", 0x6272A4, 1);

        /* Reload button */
        int btn_x = card_x + 24;
        int btn_y = card_y + 110;
        fb_round_rect(btn_x, btn_y, 110, 30, 4, 0xBD93F9);
        font_text(btn_x + 16, btn_y + 8, "Reload Tab", 0x282A36, 1);
        return 1;
    }

    firstos_browser_channel_t *r2h = (firstos_browser_channel_t *)(s_shm_phys + FIRSTOS_BROWSER_CHANNEL_R2H_OFF);

    if (r2h->has_message) {
        host_mutex_lock((volatile uint32_t *)(void *)&r2h->lock_word);

        firstos_browser_message_t msg = r2h->msg;

        if (msg.version == FIRSTOS_BROWSER_IPC_VERSION && msg.type == FIRSTOS_BROWSER_MSG_RENDER_FRAME) {
            /* Phase 2: Check for stale frames from old navigations */
            if (msg.status > 0 && active_tab && (uint32_t)msg.status != active_tab->navigation_id) {
                serial_write("[browser_host] dropped STALE render frame nav=");
                serial_write_hex((unsigned int)msg.status);
                serial_write(" expected=");
                serial_write_hex((unsigned int)active_tab->navigation_id);
                serial_write("\n");
                r2h->has_message = 0;
            } else {
                char *r2h_data = (char *)r2h + sizeof(firstos_browser_channel_t);
                firstos_render_command_t *cmds = (firstos_render_command_t *)r2h_data;
                uint32_t num_cmds = msg.param1;
                if (num_cmds > 256) num_cmds = 256;

                s_cached_num_cmds = num_cmds;
                for (uint32_t i = 0; i < num_cmds; i++) {
                    s_cached_cmds[i] = cmds[i];
                }
                char *cmd_strings = r2h_data + FIRSTOS_BROWSER_STRINGS_OFFSET;
                for (int s = 0; s < 8192; s++) {
                    s_cached_strings[s] = cmd_strings[s];
                }

                /* Phase 4: Retrieve compositor damage rectangle descriptor */
                firstos_damage_rect_t *dmg = (firstos_damage_rect_t *)(r2h_data + FIRSTOS_BROWSER_DAMAGE_OFFSET);
                if (dmg->flags & FIRSTOS_RENDER_FLAG_DAMAGE_VALID) {
                    s_cached_damage = *dmg;
                } else {
                    /* Fallback to full repaint if damage descriptor not populated */
                    s_cached_damage.x = 0;
                    s_cached_damage.y = 0;
                    s_cached_damage.w = 0;
                    s_cached_damage.h = 0;
                    s_cached_damage.flags = FIRSTOS_RENDER_FLAG_FULL_REPAINT;
                }

                s_has_cached_frame = 1;
                r2h->has_message = 0;
                browser_host_frames_sync_from_commands(s_cached_cmds, s_cached_num_cmds,
                                                       s_cached_strings, sizeof(s_cached_strings));
                serial_write("[browser_host] validated and executed render commands\n");
            }
        } else if (msg.type == FIRSTOS_BROWSER_MSG_NAVIGATE_REQUEST) {
            char *r2h_data = (char *)r2h + sizeof(firstos_browser_channel_t);
            const char *nav_url = r2h_data + msg.payload_offset;
            uint32_t url_len = msg.payload_length;
            char safe_url[256];
            if (url_len > 255) url_len = 255;
            for (uint32_t u = 0; u < url_len; u++) safe_url[u] = nav_url[u];
            safe_url[url_len] = '\0';
            r2h->has_message = 0;

            serial_write("[browser_host] received NAVIGATE_REQUEST: ");
            serial_write(safe_url);
            serial_write("\n");

            if (browser_host_validate_url(safe_url, s_active_page_url) == 0) {
                browser_host_bump_navigation_id();
                web_browser_navigate(safe_url);
            } else {
                serial_write("[browser_host] NAVIGATE_REQUEST rejected by policy\n");
            }
        } else if (msg.type == FIRSTOS_BROWSER_MSG_RESOURCE_REQUEST) {
            char *r2h_data = (char *)r2h + sizeof(firstos_browser_channel_t);
            firstos_resource_request_t *req = (firstos_resource_request_t *)(r2h_data + FIRSTOS_BROWSER_RESOURCE_REQ_OFFSET);
            const char *req_url = r2h_data + req->url_offset;
            uint32_t ulen = req->url_len;
            char safe_url[256];
            if (ulen > 255) ulen = 255;
            for (uint32_t u = 0; u < ulen; u++) safe_url[u] = req_url[u];
            safe_url[ulen] = '\0';

            serial_write("[browser_host] serving resource request: ");
            serial_write(safe_url);
            serial_write("\n");

            /* Build response in H2R */
            firstos_browser_channel_t *h2r = (firstos_browser_channel_t *)(s_shm_phys + FIRSTOS_BROWSER_CHANNEL_H2R_OFF);
            char *h2r_data = (char *)h2r + sizeof(firstos_browser_channel_t);
            firstos_resource_response_t *resp = (firstos_resource_response_t *)(h2r_data + FIRSTOS_BROWSER_RESOURCE_RESP_OFFSET);
            char *resp_payload = (char *)resp + sizeof(firstos_resource_response_t);

            resp->request_id = req->request_id;
            resp->navigation_id = req->navigation_id;
            resp->resource_type = req->resource_type;
            resp->meta_w = 0;
            resp->meta_h = 0;

            int vres = browser_host_validate_url(safe_url, s_active_page_url);
            int sop_allowed = browser_security_can_load_subresource(&s_renderer_security_ctx.committed_origin, safe_url, req->resource_type);
            if (vres != 0 || !sop_allowed) {
                resp->status_code = (vres != 0) ? -1 : 403;
                resp->data_len = 0;
                resp->mime_len = 0;
                resp->mime_type[0] = '\0';
                if (!sop_allowed) {
                    serial_write("[security] subresource blocked by SOP: ");
                    serial_write(safe_url);
                    serial_write("\n");
                } else {
                    serial_write("[browser_host] resource request rejected by policy\n");
                }
            } else {
                /* 1. Check Host cache first */
                int cached_len = 0;
                char cached_mime[32];
                uint16_t cw = 0, ch = 0;
                if (browser_host_cache_lookup(safe_url, resp_payload, 65536, cached_mime, &cached_len, &cw, &ch) == 0) {
                    resp->status_code = 200;
                    resp->data_len = (uint32_t)cached_len;
                    resp->meta_w = cw;
                    resp->meta_h = ch;
                    for (int m = 0; m < 31 && cached_mime[m]; m++) resp->mime_type[m] = cached_mime[m];
                    resp->mime_len = strlen(resp->mime_type);
                    serial_write("[browser_host] resource served from cache\n");
                } else {
                    /* 2. Cache miss: fetch from data URL or VFS */
                    if (strncmp(safe_url, "data:", 5) == 0) {
                        char dmime[32];
                        int dlen = 0;
                        const char *comma = safe_url + 5;
                        while (*comma && *comma != ',') comma++;
                        if (*comma == ',') {
                            comma++;
                            int raw_len = strlen(comma);
                            if (raw_len > 65000) raw_len = 65000;
                            for (int i = 0; i < raw_len; i++) resp_payload[i] = comma[i];
                            resp_payload[raw_len] = '\0';
                            dlen = raw_len;
                            host_strcpy(dmime, "text/css");
                        }
                        if (dlen > 0) {
                            resp->status_code = 200;
                            resp->data_len = (uint32_t)dlen;
                            host_strcpy(resp->mime_type, dmime);
                            resp->mime_len = strlen(dmime);
                            browser_host_cache_insert(safe_url, dmime, resp_payload, dlen, 0, 0);
                        } else {
                            resp->status_code = 404;
                            resp->data_len = 0;
                        }
                    } else if (strncmp(safe_url, "file://", 7) == 0 || safe_url[0] == '/') {
                        const char *vfs_path = (strncmp(safe_url, "file://", 7) == 0) ? (safe_url + 7) : safe_url;
                        int nread = fs_read(vfs_path, resp_payload, 65536);
                        if (nread <= 0 && vfs_path[0] == '/') {
                            nread = fs_read(vfs_path + 1, resp_payload, 65536);
                        }
                        if (nread > 0) {
                            resp->status_code = 200;
                            resp->data_len = (uint32_t)nread;
                            const char *ext = vfs_path;
                            while (*ext) ext++;
                            while (ext > vfs_path && *(ext-1) != '.') ext--;
                            if (strcmp(ext, "css") == 0) host_strcpy(resp->mime_type, "text/css");
                            else if (strcmp(ext, "png") == 0) host_strcpy(resp->mime_type, "image/png");
                            else if (strcmp(ext, "bmp") == 0) host_strcpy(resp->mime_type, "image/bmp");
                            else if (strcmp(ext, "jpg") == 0 || strcmp(ext, "jpeg") == 0) host_strcpy(resp->mime_type, "image/jpeg");
                            else host_strcpy(resp->mime_type, "application/octet-stream");
                            resp->mime_len = strlen(resp->mime_type);
                            browser_host_cache_insert(safe_url, resp->mime_type, resp_payload, nread, 0, 0);
                            serial_write("[browser_host] resource read from VFS bytes=");
                            serial_write_hex((unsigned int)nread);
                            serial_write("\n");
                        } else {
                            resp->status_code = 404;
                            resp->data_len = 0;
                            serial_write("[browser_host] resource not found in VFS\n");
                        }
                    } else {
                        resp->status_code = 404;
                        resp->data_len = 0;
                    }
                }

                /* Phase 4D: Decode images and store in Host Decoded Image Store */
                if (req->resource_type == FIRSTOS_RES_TYPE_IMAGE) {
                    if (resp->status_code == 200 && resp->data_len > 0) {
                        int iw = 0, ih = 0;
                        u8 *raw_pix = image_load_from_memory((const u8*)resp_payload, (int)resp->data_len, &iw, &ih);
                        if (raw_pix && iw > 0 && ih > 0 && iw <= HOST_IMAGE_MAX_W && ih <= HOST_IMAGE_MAX_H) {
                            uint32_t *p32 = (uint32_t *)raw_pix;
                            int npix = iw * ih;
                            for (int p = 0; p < npix; p++) {
                                uint32_t pix = p32[p];
                                p32[p] = (pix & 0xFF00FF00) | ((pix & 0x000000FF) << 16) | ((pix & 0x00FF0000) >> 16);
                            }
                            uint32_t img_id = browser_host_image_store_insert(p32, iw, ih);
                            image_free(raw_pix);
                            resp->image_id = img_id;
                            resp->meta_w = (uint16_t)iw;
                            resp->meta_h = (uint16_t)ih;
                            serial_write("[browser_host] decoded image id=");
                            serial_write_hex(img_id);
                            serial_write(" dim=");
                            serial_write_hex((unsigned int)iw);
                            serial_write("x");
                            serial_write_hex((unsigned int)ih);
                            serial_write("\n");
                        } else {
                            if (raw_pix) image_free(raw_pix);
                            resp->status_code = -2; /* Decode failed */
                            resp->image_id = 0;
                            resp->meta_w = 0;
                            resp->meta_h = 0;
                            serial_write("[browser_host] image decode failed\n");
                        }
                    } else {
                        resp->image_id = 0;
                        resp->meta_w = 0;
                        resp->meta_h = 0;
                    }
                } else {
                    resp->image_id = 0;
                }
            }

            resp->data_offset = FIRSTOS_BROWSER_RESOURCE_RESP_OFFSET + sizeof(firstos_resource_response_t);

            host_mutex_lock((volatile uint32_t *)(void *)&h2r->lock_word);
            h2r->msg.version = FIRSTOS_BROWSER_IPC_VERSION;
            h2r->msg.type = FIRSTOS_BROWSER_MSG_RESOURCE_RESPONSE;
            h2r->msg.request_id = req->request_id;
            h2r->msg.payload_offset = FIRSTOS_BROWSER_RESOURCE_RESP_OFFSET;
            h2r->msg.payload_length = sizeof(firstos_resource_response_t) + resp->data_len;
            h2r->msg.status = (int32_t)s_current_navigation_id;
            h2r->has_message = 1;
            host_mutex_unlock((volatile uint32_t *)(void *)&h2r->lock_word);

            r2h->has_message = 0;
        } else if (msg.type == FIRSTOS_BROWSER_MSG_FORM_SUBMIT) {
            char *r2h_data = (char *)r2h + sizeof(firstos_browser_channel_t);
            firstos_form_submit_request_t *freq = (firstos_form_submit_request_t *)(r2h_data + msg.payload_offset);
            const char *action_url = r2h_data + freq->action_offset;
            const char *query_str = r2h_data + freq->query_offset;

            char target_url[512];
            int pos = 0;
            for (uint32_t i = 0; i < freq->action_len && pos < 250; i++) {
                target_url[pos++] = action_url[i];
            }
            if (freq->query_len > 0 && pos < 500) {
                target_url[pos++] = '?';
                for (uint32_t i = 0; i < freq->query_len && pos < 510; i++) {
                    target_url[pos++] = query_str[i];
                }
            }
            target_url[pos] = '\0';
            r2h->has_message = 0;

            serial_write("[browser_host] received FORM_SUBMIT: ");
            serial_write(target_url);
            serial_write("\n");

            if (browser_host_validate_url(target_url, s_active_page_url) == 0 &&
                browser_security_can_navigate(&s_renderer_security_ctx.committed_origin, target_url)) {
                browser_host_bump_navigation_id();
                web_browser_navigate(target_url);
            } else {
                serial_write("[browser_host] FORM_SUBMIT rejected by security policy\n");
            }
        } else if (msg.type == FIRSTOS_BROWSER_MSG_DOWNLOAD_REQUEST) {
            char *r2h_data = (char *)r2h + sizeof(firstos_browser_channel_t);
            firstos_download_request_t *dreq = (firstos_download_request_t *)(r2h_data + msg.payload_offset);
            const char *raw_url = r2h_data + dreq->url_offset;
            const char *raw_fn = (dreq->filename_len > 0) ? (r2h_data + dreq->filename_offset) : NULL;

            char safe_url[FIRSTOS_MAX_DOWNLOAD_URL] = "";
            uint32_t ulen = dreq->url_len;
            if (ulen >= sizeof(safe_url)) ulen = sizeof(safe_url) - 1;
            for (uint32_t u = 0; u < ulen; u++) safe_url[u] = raw_url[u];
            safe_url[ulen] = '\0';

            char safe_fn[FIRSTOS_MAX_DOWNLOAD_FILENAME] = "";
            if (raw_fn && dreq->filename_len > 0) {
                uint32_t flen = dreq->filename_len;
                if (flen >= sizeof(safe_fn)) flen = sizeof(safe_fn) - 1;
                for (uint32_t f = 0; f < flen; f++) safe_fn[f] = raw_fn[f];
                safe_fn[flen] = '\0';
            }

            r2h->has_message = 0;

            serial_write("[browser_host] received DOWNLOAD_REQUEST: ");
            serial_write(safe_url);
            serial_write("\n");

            firstos_tab_t *act = browser_host_get_active_tab();
            uint32_t tab_id = act ? act->tab_id : 1;
            uint32_t tab_gen = act ? act->generation : 1;

            /* Validate user gesture strictly against host-recorded gestures */
            int has_gesture = browser_host_has_valid_user_gesture(tab_id) && (dreq->is_user_gesture != 0);
            if (!has_gesture) {
                serial_write("[download] rejected: no valid user gesture\n");
            } else if (browser_host_validate_url(safe_url, s_active_page_url) != 0) {
                serial_write("[download] rejected: invalid URL\n");
            } else {
                download_task_t *task = download_create(safe_url, safe_fn[0] ? safe_fn : NULL,
                                                        tab_id, tab_gen,
                                                        1, 1,
                                                        dreq->navigation_id, 1);
                if (task) {
                    serial_write("[download] task created id=");
                    serial_write_hex((unsigned int)task->id);
                    serial_write(" filename=");
                    serial_write(task->filename);
                    serial_write("\n");
                    /* Start async stream */
                    download_start_stream(task);
                } else {
                    serial_write("[download] task creation failed\n");
                }
            }
        } else if (msg.type == FIRSTOS_BROWSER_MSG_MEDIA_COMMAND) {
            char *r2h_data = (char *)r2h + sizeof(firstos_browser_channel_t);
            firstos_media_command_t *mcmd = (firstos_media_command_t *)(r2h_data + msg.payload_offset);
            const char *raw_src = r2h_data + mcmd->src_offset;
            char safe_src[256] = "";
            uint32_t slen = mcmd->src_len;
            if (slen >= sizeof(safe_src)) slen = sizeof(safe_src) - 1;
            for (uint32_t s = 0; s < slen; s++) safe_src[s] = raw_src[s];
            safe_src[slen] = '\0';

            r2h->has_message = 0;

            serial_write("[browser_host] received MEDIA_COMMAND cmd=");
            serial_write_hex((unsigned int)mcmd->command);
            serial_write(" src=");
            serial_write(safe_src);
            serial_write("\n");

            /* Trigger hardware sound / media playback */
            if (mcmd->command == FIRSTOS_MEDIA_CMD_PLAY) {
                extern int sound_play_file(const char *path);
                if (safe_src[0]) {
                    sound_play_file(safe_src);
                } else {
                    extern void sound_play_event(int event);
                    sound_play_event(0); /* SOUND_EVENT_CLICK */
                }
                serial_write("[media] playback started\n");
            } else if (mcmd->command == FIRSTOS_MEDIA_CMD_PAUSE) {
                extern void hda_pause_audio(void);
                hda_pause_audio();
                serial_write("[media] playback paused\n");
            }
        } else if (msg.type == FIRSTOS_BROWSER_MSG_TITLE_UPDATE) {
            r2h->has_message = 0;
        } else {
            r2h->has_message = 0;
        }

        host_mutex_unlock((volatile uint32_t *)(void *)&r2h->lock_word);
    }

    if (!s_has_cached_frame || s_cached_num_cmds <= 1) {
        return 0;
    }

    /* Phase 4: Compositor Tiled Damage Invalidation Bounds */
    int is_full_repaint = (s_cached_damage.flags & FIRSTOS_RENDER_FLAG_FULL_REPAINT) ||
                          !(s_cached_damage.flags & FIRSTOS_RENDER_FLAG_DAMAGE_VALID);
    int clip_x = content_x;
    int clip_y = content_y;
    int clip_w = content_w;
    int clip_h = content_h;

    if (!is_full_repaint && s_cached_damage.w > 0 && s_cached_damage.h > 0) {
        clip_x = content_x + s_cached_damage.x;
        clip_y = content_y + s_cached_damage.y;
        clip_w = s_cached_damage.w;
        clip_h = s_cached_damage.h;

        /* Clamp damage region to browser window content bounds */
        if (clip_x < content_x) { clip_w -= (content_x - clip_x); clip_x = content_x; }
        if (clip_y < content_y) { clip_h -= (content_y - clip_y); clip_y = content_y; }
        if (clip_x + clip_w > content_x + content_w) clip_w = (content_x + content_w) - clip_x;
        if (clip_y + clip_h > content_y + content_h) clip_h = (content_y + content_h) - clip_y;
        if (clip_w <= 0 || clip_h <= 0) {
            return 1; /* Damage region entirely outside visible window */
        }
    }

    for (uint32_t i = 0; i < s_cached_num_cmds; i++) {
        firstos_render_command_t *cmd = &s_cached_cmds[i];
        if (cmd->type == FIRSTOS_RENDER_CMD_END) break;

        if (cmd->type == FIRSTOS_RENDER_CMD_CLEAR) {
            fb_rect(clip_x, clip_y, clip_w, clip_h, cmd->color);
        } else if (cmd->type == FIRSTOS_RENDER_CMD_CLIP) {
            int rx = content_x + cmd->x;
            int ry = content_y + cmd->y;
            int rw = cmd->w;
            int rh = cmd->h;
            if (rx < clip_x) { rw -= (clip_x - rx); rx = clip_x; }
            if (ry < clip_y) { rh -= (clip_y - ry); ry = clip_y; }
            if (rx + rw > clip_x + clip_w) rw = (clip_x + clip_w) - rx;
            if (ry + rh > clip_y + clip_h) rh = (clip_y + clip_h) - ry;
            if (rw > 0 && rh > 0) {
                browser_host_frame_composite(cmd->color, rx, ry, rw, rh);
            }
        } else if (cmd->type == FIRSTOS_RENDER_CMD_RECT) {
            int rx = content_x + cmd->x;
            int ry = content_y + cmd->y;
            int rw = cmd->w;
            int rh = cmd->h;
            if (rx < clip_x) { rw -= (clip_x - rx); rx = clip_x; }
            if (ry < clip_y) { rh -= (clip_y - ry); ry = clip_y; }
            if (rx + rw > clip_x + clip_w) rw = (clip_x + clip_w) - rx;
            if (ry + rh > clip_y + clip_h) rh = (clip_y + clip_h) - ry;
            if (rw > 0 && rh > 0) {
                fb_rect(rx, ry, rw, rh, cmd->color);
            }
        } else if (cmd->type == FIRSTOS_RENDER_CMD_TEXT) {
            uint32_t str_base_off = 0;
            if (cmd->text_offset >= str_base_off && cmd->text_offset < sizeof(s_cached_strings)) {
                const char *str = s_cached_strings + (cmd->text_offset - str_base_off);
                int tx = content_x + cmd->x;
                int ty = content_y + cmd->y;
                if (tx >= clip_x && tx < clip_x + clip_w - 8 &&
                    ty >= clip_y && ty < clip_y + clip_h - 16) {
                    font_text(tx, ty, str, cmd->color, 1);
                }
            }
        } else if (cmd->type == FIRSTOS_RENDER_CMD_IMAGE) {
            int rx = content_x + cmd->x;
            int ry = content_y + cmd->y;
            int rw = cmd->w;
            int rh = cmd->h;
            if (rx < clip_x) { rw -= (clip_x - rx); rx = clip_x; }
            if (ry < clip_y) { rh -= (clip_y - ry); ry = clip_y; }
            if (rx + rw > clip_x + clip_w) rw = (clip_x + clip_w) - rx;
            if (ry + rh > clip_y + clip_h) rh = (clip_y + clip_h) - ry;
            if (rw > 0 && rh > 0) {
                uint32_t img_id = cmd->color;
                int iw = 0, ih = 0;
                const uint32_t *pixels = browser_host_image_store_get(img_id, &iw, &ih);
                if (pixels && iw > 0 && ih > 0) {
                    for (int dy = 0; dy < rh; dy++) {
                        int sy = (dy * ih) / rh;
                        for (int dx = 0; dx < rw; dx++) {
                            int sx = (dx * iw) / rw;
                            uint32_t p = pixels[sy * iw + sx];
                            uint8_t a = (p >> 24) & 0xFF;
                            if (a > 10) {
                                if (a >= 250) {
                                    fb_putpixel(rx + dx, ry + dy, p & 0x00FFFFFF);
                                } else {
                                    uint32_t bg = fb_read_pixel(rx + dx, ry + dy);
                                    uint32_t r = (((p >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * (255 - a)) / 255;
                                    uint32_t g = (((p >> 8) & 0xFF) * a + ((bg >> 8) & 0xFF) * (255 - a)) / 255;
                                    uint32_t b = ((p & 0xFF) * a + (bg & 0xFF) * (255 - a)) / 255;
                                    fb_putpixel(rx + dx, ry + dy, (r << 16) | (g << 8) | b);
                                }
                            }
                        }
                    }
                } else {
                    /* Broken image fallback */
                    fb_rect(rx, ry, rw, rh, 0x2A2A38);
                    fb_border(rx, ry, rw, rh, 0xFF453A);
                    if (rw >= 24 && rh >= 16) {
                        font_text(rx + (rw - 24) / 2, ry + (rh - 12) / 2, "[X]", 0xFF453A, 1);
                    }
                }
            }
        }
    }

    return 1;
}

void browser_host_ipc_shutdown(void) {
    if (s_renderer_pid > 0 && s_shm_phys) {
        firstos_browser_channel_t *h2r = (firstos_browser_channel_t *)(s_shm_phys + FIRSTOS_BROWSER_CHANNEL_H2R_OFF);
        host_mutex_lock((volatile uint32_t *)(void *)&h2r->lock_word);
        h2r->msg.version = FIRSTOS_BROWSER_IPC_VERSION;
        h2r->msg.type = FIRSTOS_BROWSER_MSG_SHUTDOWN;
        h2r->has_message = 1;
        host_mutex_unlock((volatile uint32_t *)(void *)&h2r->lock_word);
        serial_write("[browser_host] sent SHUTDOWN to renderer\n");
    }
}

/* ============================================================================
 * Phase 5C: Frame Tree, Iframe Lifecycle, and Secure postMessage
 *
 * Design notes:
 *  - Every tab owns a fixed-slot frame tree (FIRSTOS_MAX_FRAMES_PER_TAB slots,
 *    root frame included). No dynamic allocation.
 *  - frame_id is globally unique and monotonic; per-slot generation guards
 *    against stale references after slot reuse.
 *  - Each frame commits its own origin. Origin comparison is exact
 *    (scheme/host/effective port); opaque origins never match.
 *  - The Host is the only authority for postMessage delivery. sender_origin
 *    supplied by a renderer is ignored and overwritten from the Host's own
 *    committed origin for the sender frame.
 *  - Current architecture runs one renderer process; child frames are bound to
 *    the owning tab's renderer slot with a frame-scoped security generation.
 *    True out-of-process iframes are future work.
 * ========================================================================== */

static uint32_t s_next_frame_id = 1;
static uint32_t s_total_frames = 0;
static uint32_t s_frame_security_gen = 1;
static uint32_t s_next_message_id = 1;

typedef struct {
    int in_use;
    firstos_post_message_t msg;
} host_msg_slot_t;

static host_msg_slot_t s_msg_queue[FIRSTOS_MAX_MESSAGE_QUEUE];

static void frame_log_kv(const char *k, uint32_t v) {
    serial_write(k);
    serial_write_hex(v);
}

static void frame_strlcpy(char *dst, const char *src, int max) {
    int i = 0;
    if (!src) src = "";
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static firstos_frame_t *frame_find_in_tab(firstos_tab_t *tab, uint32_t frame_id) {
    if (!tab || frame_id == 0) return NULL;
    for (int i = 0; i < FIRSTOS_MAX_FRAMES_PER_TAB; i++) {
        firstos_frame_t *f = &tab->frame_tree.frames[i];
        if (f->state != FIRSTOS_FRAME_FREE && f->frame_id == frame_id) return f;
    }
    return NULL;
}

static void frame_purge_messages_for(uint32_t tab_id, uint32_t frame_id) {
    for (int i = 0; i < FIRSTOS_MAX_MESSAGE_QUEUE; i++) {
        if (!s_msg_queue[i].in_use) continue;
        if (s_msg_queue[i].msg.target_tab_id != tab_id) continue;
        if (frame_id == 0 || s_msg_queue[i].msg.target_frame_id == frame_id) {
            s_msg_queue[i].in_use = 0;
        }
    }
}

/* Resolve a (possibly relative) iframe URL against its parent document URL. */
static int frame_resolve_url(const char *base, const char *rel, char *out, int max) {
    if (!rel || !out || max <= 0) return -1;
    while (*rel == ' ' || *rel == '\t') rel++;
    if (!rel[0]) { frame_strlcpy(out, "about:blank", max); return 0; }

    /* Absolute URL with scheme */
    for (int i = 0; rel[i] && i < 16; i++) {
        if (rel[i] == ':') { frame_strlcpy(out, rel, max); return 0; }
        if (rel[i] == '/' || rel[i] == '?' || rel[i] == '#') break;
    }
    if (!base) base = "";

    /* Locate scheme and authority end in base */
    const char *sch = NULL;
    for (const char *q = base; *q; q++) {
        if (q[0] == ':' && q[1] == '/' && q[2] == '/') { sch = q; break; }
    }
    if (!sch) {
        if (rel[0] == '/') { frame_strlcpy(out, rel, max); return 0; }
        return -1; /* cannot resolve relative against opaque base */
    }
    const char *auth = sch + 3;
    const char *path = auth;
    while (*path && *path != '/' && *path != '?' && *path != '#') path++;

    int pos = 0;
    if (rel[0] == '/' && rel[1] == '/') {
        int sl = (int)(sch - base) + 1; /* "http:" */
        for (int i = 0; i < sl && pos < max - 1; i++) out[pos++] = base[i];
    } else if (rel[0] == '/') {
        int al = (int)(path - base);
        for (int i = 0; i < al && pos < max - 1; i++) out[pos++] = base[i];
    } else {
        /* Directory of base path */
        const char *end = path;
        const char *last = NULL;
        while (*end && *end != '?' && *end != '#') { if (*end == '/') last = end; end++; }
        int dl = last ? (int)(last - base) + 1 : (int)(path - base);
        for (int i = 0; i < dl && pos < max - 1; i++) out[pos++] = base[i];
        if (!last && pos < max - 1) out[pos++] = '/';
    }
    for (int i = 0; rel[i] && pos < max - 1; i++) out[pos++] = rel[i];
    out[pos] = '\0';
    return 0;
}

/* Initialize the root frame for a freshly created tab. */
static void frame_tree_init_for_tab(firstos_tab_t *tab) {
    if (!tab) return;
    firstos_frame_tree_t *ft = &tab->frame_tree;
    for (int i = 0; i < FIRSTOS_MAX_FRAMES_PER_TAB; i++) {
        uint32_t g = ft->frames[i].generation;
        memset(&ft->frames[i], 0, sizeof(firstos_frame_t));
        ft->frames[i].generation = g;
    }
    ft->tab_id = tab->tab_id;
    ft->tab_generation = tab->generation;
    ft->frame_count = 0;

    firstos_frame_t *root = &ft->frames[0];
    root->generation++;
    root->frame_id = s_next_frame_id++;
    root->tab_id = tab->tab_id;
    root->tab_generation = tab->generation;
    root->parent_frame_id = 0;
    root->depth = 0;
    root->state = FIRSTOS_FRAME_READY;
    root->renderer_slot_id = tab->renderer_slot_id;
    root->renderer_pid = tab->renderer_pid;
    root->navigation_id = tab->navigation_id;
    root->security_generation = s_frame_security_gen++;
    frame_strlcpy(root->url, tab->url, FIRSTOS_MAX_FRAME_URL);
    frame_strlcpy(root->title, tab->title, FIRSTOS_MAX_FRAME_TITLE);
    root->committed_origin = tab->committed_origin;
    ft->root_frame_id = root->frame_id;
    ft->frame_count = 1;
    s_total_frames++;

    serial_write("[frame] root created");
    frame_log_kv(" tab=", tab->tab_id);
    frame_log_kv(" frame=", root->frame_id);
    serial_write("\n");
}

/* Free a frame and all its descendants, bottom-up. */
static void frame_destroy_rec(firstos_tab_t *tab, firstos_frame_t *f) {
    if (!tab || !f || f->state == FIRSTOS_FRAME_FREE) return;
    uint32_t fid = f->frame_id;
    f->state = FIRSTOS_FRAME_CLOSING;
    for (int i = 0; i < FIRSTOS_MAX_FRAMES_PER_TAB; i++) {
        firstos_frame_t *c = &tab->frame_tree.frames[i];
        if (c->state != FIRSTOS_FRAME_FREE && c != f && c->parent_frame_id == fid) {
            frame_destroy_rec(tab, c);
        }
    }
    frame_purge_messages_for(tab->tab_id, fid);
    uint32_t g = f->generation;
    memset(f, 0, sizeof(firstos_frame_t));
    f->generation = g; /* preserved; bumped on reuse */
    if (tab->frame_tree.frame_count > 0) tab->frame_tree.frame_count--;
    if (s_total_frames > 0) s_total_frames--;
    serial_write("[frame] destroyed");
    frame_log_kv(" tab=", tab->tab_id);
    frame_log_kv(" frame=", fid);
    serial_write("\n");
}

static void frame_destroy_children_of(firstos_tab_t *tab, uint32_t parent_id) {
    for (int i = 0; i < FIRSTOS_MAX_FRAMES_PER_TAB; i++) {
        firstos_frame_t *c = &tab->frame_tree.frames[i];
        if (c->state != FIRSTOS_FRAME_FREE && c->parent_frame_id == parent_id && c->frame_id != parent_id) {
            frame_destroy_rec(tab, c);
        }
    }
}

/* Hook: tab navigated -> new root document, all child frames die. */
static void frame_tree_on_tab_navigate(firstos_tab_t *tab) {
    if (!tab) return;
    firstos_frame_t *root = frame_find_in_tab(tab, tab->frame_tree.root_frame_id);
    if (!root) { frame_tree_init_for_tab(tab); return; }
    frame_destroy_children_of(tab, root->frame_id);
    frame_strlcpy(root->url, tab->url, FIRSTOS_MAX_FRAME_URL);
    frame_strlcpy(root->title, tab->title, FIRSTOS_MAX_FRAME_TITLE);
    root->committed_origin = tab->committed_origin;
    root->navigation_id = tab->navigation_id;
    root->document_generation++;
    root->security_generation = s_frame_security_gen++;
    root->state = FIRSTOS_FRAME_READY;
}

/* Hook: tab closing -> tear down whole tree bottom-up. */
static void frame_tree_on_tab_close(firstos_tab_t *tab) {
    if (!tab) return;
    firstos_frame_t *root = frame_find_in_tab(tab, tab->frame_tree.root_frame_id);
    if (root) frame_destroy_rec(tab, root);
    /* Defensive sweep for orphans */
    for (int i = 0; i < FIRSTOS_MAX_FRAMES_PER_TAB; i++) {
        if (tab->frame_tree.frames[i].state != FIRSTOS_FRAME_FREE) {
            frame_destroy_rec(tab, &tab->frame_tree.frames[i]);
        }
    }
    frame_purge_messages_for(tab->tab_id, 0);
    tab->frame_tree.root_frame_id = 0;
    tab->frame_tree.frame_count = 0;
}

static int frame_load_document(firstos_frame_t *f) {
    /* Host-side child document fetch. Renderer never touches VFS/network. */
    static char s_frame_doc[4096];
    int n = 0;
    if (strncmp(f->url, "about:", 6) == 0) {
        n = 1;
    } else if (strncmp(f->url, "data:", 5) == 0) {
        const char *c = strchr(f->url, ',');
        n = c ? (int)strlen(c + 1) : 0;
        if (n == 0) n = 1;
    } else if (strncmp(f->url, "file://", 7) == 0 || f->url[0] == '/') {
        const char *p = (strncmp(f->url, "file://", 7) == 0) ? f->url + 7 : f->url;
        n = fs_read(p, s_frame_doc, sizeof(s_frame_doc) - 1);
        if (n <= 0 && p[0] == '/') n = fs_read(p + 1, s_frame_doc, sizeof(s_frame_doc) - 1);
    } else {
        /* http(s): network fetch is Host-mediated; mark loaded lazily */
        n = 1;
    }
    if (n > 0) {
        f->state = FIRSTOS_FRAME_READY;
        serial_write("[frame] document loaded");
        frame_log_kv(" frame=", f->frame_id);
        frame_log_kv(" bytes=", (uint32_t)n);
        serial_write("\n");
        return 0;
    }
    f->state = FIRSTOS_FRAME_ERROR;
    serial_write("[frame] document load failed");
    frame_log_kv(" frame=", f->frame_id);
    serial_write("\n");
    return -1;
}

static int frame_commit_url(firstos_frame_t *f, const char *resolved) {
    frame_strlcpy(f->url, resolved, FIRSTOS_MAX_FRAME_URL);
    browser_security_parse_origin(f->url, &f->committed_origin);
    f->security_generation = s_frame_security_gen++;
    f->document_generation++;
    f->navigation_id = browser_host_bump_navigation_id();
    char ostr[160];
    browser_security_origin_to_string(&f->committed_origin, ostr, sizeof(ostr));
    serial_write("[frame] origin committed");
    frame_log_kv(" frame=", f->frame_id);
    serial_write(" origin=");
    serial_write(ostr);
    serial_write("\n");
    f->state = FIRSTOS_FRAME_LOADING;
    return frame_load_document(f);
}

/* Embedding policy for a child URL under a parent origin. */
static int frame_check_embed_policy(const firstos_frame_t *parent, const char *resolved) {
    if (!browser_security_can_navigate(&parent->committed_origin, resolved)) {
        return FIRSTOS_FRAME_ACCESS_DENY_SCHEME;
    }
    /* file/http isolation: only file documents may embed file documents */
    int child_is_file = (strncmp(resolved, "file://", 7) == 0 || resolved[0] == '/');
    if (child_is_file && parent->committed_origin.scheme != FIRSTOS_ORIGIN_FILE) {
        return FIRSTOS_FRAME_ACCESS_DENY_SCHEME;
    }
    return FIRSTOS_FRAME_ACCESS_ALLOW;
}

int browser_host_frame_create(uint32_t tab_id, uint32_t parent_frame_id, const char *url,
                              int x, int y, int w, int h) {
    firstos_tab_t *tab = browser_host_get_tab_by_id(tab_id);
    if (!tab) return FIRSTOS_FRAME_ACCESS_DENY_STALE;
    if (parent_frame_id == 0) parent_frame_id = tab->frame_tree.root_frame_id;

    firstos_frame_t *parent = frame_find_in_tab(tab, parent_frame_id);
    if (!parent || parent->state == FIRSTOS_FRAME_CRASHED || parent->state == FIRSTOS_FRAME_CLOSING) {
        serial_write("[frame] create rejected: invalid parent\n");
        return FIRSTOS_FRAME_ACCESS_DENY_PARENT;
    }
    if (parent->depth + 1 > FIRSTOS_MAX_FRAME_DEPTH) {
        serial_write("[frame] create rejected: max depth\n");
        return FIRSTOS_FRAME_ACCESS_DENY_DEPTH;
    }
    if (tab->frame_tree.frame_count >= FIRSTOS_MAX_FRAMES_PER_TAB ||
        s_total_frames >= FIRSTOS_MAX_TOTAL_FRAMES) {
        serial_write("[frame] create rejected: frame limit\n");
        return FIRSTOS_FRAME_ACCESS_DENY_LIMIT;
    }

    char resolved[FIRSTOS_MAX_FRAME_URL];
    if (frame_resolve_url(parent->url, url ? url : "about:blank", resolved, sizeof(resolved)) != 0) {
        serial_write("[frame] create rejected: unresolvable url\n");
        return FIRSTOS_FRAME_ACCESS_DENY_POLICY;
    }
    int pol = frame_check_embed_policy(parent, resolved);
    if (pol != FIRSTOS_FRAME_ACCESS_ALLOW) {
        serial_write("[frame] create rejected: embed policy url=");
        serial_write(resolved);
        serial_write("\n");
        return pol;
    }

    firstos_frame_t *f = NULL;
    for (int i = 0; i < FIRSTOS_MAX_FRAMES_PER_TAB; i++) {
        if (tab->frame_tree.frames[i].state == FIRSTOS_FRAME_FREE) { f = &tab->frame_tree.frames[i]; break; }
    }
    if (!f) return FIRSTOS_FRAME_ACCESS_DENY_LIMIT;

    uint32_t g = f->generation;
    memset(f, 0, sizeof(firstos_frame_t));
    f->generation = g + 1;
    f->frame_id = s_next_frame_id++;
    f->tab_id = tab->tab_id;
    f->tab_generation = tab->generation;
    f->parent_frame_id = parent->frame_id;
    f->parent_frame_generation = parent->generation;
    f->depth = parent->depth + 1;
    f->state = FIRSTOS_FRAME_CREATED;
    f->renderer_slot_id = tab->renderer_slot_id;
    f->renderer_slot_generation = tab->generation;
    f->renderer_pid = tab->renderer_pid;
    f->x = x; f->y = y;
    f->width  = (uint32_t)((w > 0) ? (w > 4096 ? 4096 : w) : 300);
    f->height = (uint32_t)((h > 0) ? (h > 4096 ? 4096 : h) : 150);
    tab->frame_tree.frame_count++;
    s_total_frames++;

    serial_write("[frame] created");
    frame_log_kv(" tab=", tab->tab_id);
    frame_log_kv(" frame=", f->frame_id);
    frame_log_kv(" parent=", f->parent_frame_id);
    frame_log_kv(" generation=", f->generation);
    frame_log_kv(" depth=", f->depth);
    serial_write(" url=");
    serial_write(resolved);
    serial_write("\n");

    frame_commit_url(f, resolved);
    return (int)f->frame_id;
}

int browser_host_frame_navigate(uint32_t tab_id, uint32_t frame_id, const char *url) {
    firstos_tab_t *tab = browser_host_get_tab_by_id(tab_id);
    firstos_frame_t *f = frame_find_in_tab(tab, frame_id);
    if (!f) return FIRSTOS_FRAME_ACCESS_DENY_STALE;
    if (f->depth == 0) return browser_host_tab_navigate(tab_id, url);
    firstos_frame_t *parent = frame_find_in_tab(tab, f->parent_frame_id);
    if (!parent) return FIRSTOS_FRAME_ACCESS_DENY_PARENT;

    char resolved[FIRSTOS_MAX_FRAME_URL];
    if (frame_resolve_url(f->url, url, resolved, sizeof(resolved)) != 0) return FIRSTOS_FRAME_ACCESS_DENY_POLICY;
    int pol = frame_check_embed_policy(parent, resolved);
    if (pol != FIRSTOS_FRAME_ACCESS_ALLOW) return pol;

    /* New document: descendants and pending messages for the old document die. */
    frame_destroy_children_of(tab, f->frame_id);
    frame_purge_messages_for(tab_id, f->frame_id);
    serial_write("[frame] navigate");
    frame_log_kv(" frame=", f->frame_id);
    serial_write(" url=");
    serial_write(resolved);
    serial_write("\n");
    frame_commit_url(f, resolved);
    return 0;
}

int browser_host_frame_destroy(uint32_t tab_id, uint32_t frame_id) {
    firstos_tab_t *tab = browser_host_get_tab_by_id(tab_id);
    firstos_frame_t *f = frame_find_in_tab(tab, frame_id);
    if (!f) return FIRSTOS_FRAME_ACCESS_DENY_STALE;
    if (f->depth == 0) return FIRSTOS_FRAME_ACCESS_DENY_PARENT; /* root dies with its tab */
    frame_destroy_rec(tab, f);
    return 0;
}

int browser_host_frame_handle_crash(uint32_t tab_id, uint32_t frame_id) {
    firstos_tab_t *tab = browser_host_get_tab_by_id(tab_id);
    firstos_frame_t *f = frame_find_in_tab(tab, frame_id);
    if (!f) return FIRSTOS_FRAME_ACCESS_DENY_STALE;
    if (f->depth == 0) return browser_host_tab_handle_crash(tab_id);

    frame_destroy_children_of(tab, f->frame_id);
    frame_purge_messages_for(tab_id, f->frame_id);
    f->state = FIRSTOS_FRAME_CRASHED;
    f->security_generation = s_frame_security_gen++; /* revoke old capabilities */
    serial_write("[frame] crash isolated");
    frame_log_kv(" tab=", tab_id);
    frame_log_kv(" frame=", frame_id);
    frame_log_kv(" parent=", f->parent_frame_id);
    serial_write(" parent survives\n");
    return 0;
}

firstos_frame_t *browser_host_frame_get(uint32_t tab_id, uint32_t frame_id) {
    return frame_find_in_tab(browser_host_get_tab_by_id(tab_id), frame_id);
}

int browser_host_frame_check_access(uint32_t tab_id, uint32_t caller_frame_id, uint32_t target_frame_id) {
    firstos_tab_t *tab = browser_host_get_tab_by_id(tab_id);
    firstos_frame_t *a = frame_find_in_tab(tab, caller_frame_id);
    firstos_frame_t *b = frame_find_in_tab(tab, target_frame_id);
    if (!a || !b) return FIRSTOS_FRAME_ACCESS_DENY_STALE;
    if (a->state == FIRSTOS_FRAME_CRASHED || b->state == FIRSTOS_FRAME_CRASHED) return FIRSTOS_FRAME_ACCESS_DENY_STALE;
    if (!browser_security_origin_equals(&a->committed_origin, &b->committed_origin)) {
        serial_write("[frame] cross-origin access denied");
        frame_log_kv(" caller=", caller_frame_id);
        frame_log_kv(" target=", target_frame_id);
        serial_write("\n");
        return FIRSTOS_FRAME_ACCESS_DENY_ORIGIN;
    }
    return FIRSTOS_FRAME_ACCESS_ALLOW;
}

/* Returns 0 on enqueue, negative on rejection. */
int browser_host_post_message_send(const firstos_post_message_t *in) {
    if (!in) return -1;
    if (in->version != FIRSTOS_BROWSER_IPC_VERSION || in->size != sizeof(firstos_post_message_t)) {
        serial_write("[postmessage] rejected: bad envelope\n");
        return -1;
    }
    if (in->payload_length > FIRSTOS_MAX_MESSAGE_BYTES) {
        serial_write("[postmessage] rejected: payload too large\n");
        return -2;
    }

    firstos_tab_t *stab = browser_host_get_tab_by_id(in->sender_tab_id);
    if (!stab || stab->generation != in->sender_tab_generation) {
        serial_write("[postmessage] rejected: stale sender tab\n");
        return -3;
    }
    firstos_frame_t *sf = frame_find_in_tab(stab, in->sender_frame_id);
    if (!sf || sf->generation != in->sender_frame_generation ||
        sf->state == FIRSTOS_FRAME_CRASHED || sf->state == FIRSTOS_FRAME_CLOSING ||
        (in->sender_security_generation != 0 && in->sender_security_generation != sf->security_generation)) {
        serial_write("[postmessage] rejected: stale or invalid sender frame\n");
        return -3;
    }

    /* Cross-tab messaging is not supported. */
    if (in->target_tab_id != in->sender_tab_id) {
        serial_write("[postmessage] rejected: cross-tab target\n");
        return -4;
    }
    firstos_frame_t *tf = frame_find_in_tab(stab, in->target_frame_id);
    if (!tf || tf->generation != in->target_frame_generation ||
        tf->state == FIRSTOS_FRAME_CRASHED || tf->state == FIRSTOS_FRAME_CLOSING) {
        serial_write("[postmessage] rejected: stale target generation\n");
        return -5;
    }

    /* targetOrigin validation: exact match only. "*" is rejected in this
     * phase; "/" means "same origin as sender". */
    char to[128];
    int tl = 0;
    while (tl < 127 && in->target_origin[tl]) { to[tl] = in->target_origin[tl]; tl++; }
    to[tl] = '\0';
    firstos_origin_t want;
    if (tl == 0 || (to[0] == '*' && to[1] == '\0')) {
        serial_write("[postmessage] targetOrigin mismatch rejected (wildcard/empty not permitted)\n");
        return -6;
    }
    if (to[0] == '/' && to[1] == '\0') {
        want = sf->committed_origin;
    } else if (browser_security_parse_origin(to, &want) != 0) {
        serial_write("[postmessage] targetOrigin mismatch rejected (unparseable)\n");
        return -6;
    }
    if (tf->committed_origin.is_opaque || want.is_opaque ||
        !browser_security_origin_equals(&want, &tf->committed_origin)) {
        serial_write("[postmessage] targetOrigin mismatch rejected");
        frame_log_kv(" target=", tf->frame_id);
        serial_write("\n");
        return -6;
    }

    int slot = -1;
    for (int i = 0; i < FIRSTOS_MAX_MESSAGE_QUEUE; i++) {
        if (!s_msg_queue[i].in_use) { slot = i; break; }
    }
    if (slot < 0) {
        serial_write("[postmessage] rejected: queue full\n");
        return -7;
    }

    firstos_post_message_t *m = &s_msg_queue[slot].msg;
    memcpy(m, in, sizeof(*m));
    m->message_id = s_next_message_id++;
    /* Host-authoritative sender origin: never trust the renderer's claim. */
    memset(m->sender_origin, 0, sizeof(m->sender_origin));
    browser_security_origin_to_string(&sf->committed_origin, m->sender_origin, sizeof(m->sender_origin));
    m->sender_security_generation = sf->security_generation;
    if (m->payload_length < FIRSTOS_MAX_MESSAGE_BYTES) m->payload[m->payload_length] = '\0';
    s_msg_queue[slot].in_use = 1;

    serial_write("[postmessage] sent");
    frame_log_kv(" sender=", sf->frame_id);
    frame_log_kv(" target=", tf->frame_id);
    frame_log_kv(" id=", m->message_id);
    serial_write(" origin=");
    serial_write(m->sender_origin);
    serial_write("\n");
    return 0;
}

/* Returns 1 if a message was dequeued, 0 if none, negative on stale target. */
int browser_host_post_message_receive(uint32_t tab_id, uint32_t frame_id, firstos_post_message_t *out) {
    firstos_tab_t *tab = browser_host_get_tab_by_id(tab_id);
    firstos_frame_t *f = frame_find_in_tab(tab, frame_id);
    if (!f || f->state == FIRSTOS_FRAME_CRASHED) return -1;
    int best = -1;
    uint32_t best_id = 0xFFFFFFFFu;
    for (int i = 0; i < FIRSTOS_MAX_MESSAGE_QUEUE; i++) {
        if (!s_msg_queue[i].in_use) continue;
        firstos_post_message_t *m = &s_msg_queue[i].msg;
        if (m->target_tab_id == tab_id && m->target_frame_id == frame_id) {
            if (m->target_frame_generation != f->generation) { s_msg_queue[i].in_use = 0; continue; }
            if (m->message_id < best_id) { best_id = m->message_id; best = i; }
        }
    }
    if (best < 0) return 0;
    if (out) memcpy(out, &s_msg_queue[best].msg, sizeof(*out));
    s_msg_queue[best].in_use = 0;
    serial_write("[postmessage] delivered");
    frame_log_kv(" target=", frame_id);
    frame_log_kv(" id=", best_id);
    serial_write("\n");
    return 1;
}

uint32_t browser_host_frame_total_count(void) { return s_total_frames; }

/* Convenience: build and send a postMessage between two frames of a tab using
 * Host-tracked identities. Used by the renderer bridge and serial test hooks. */
int browser_host_post_message_between(uint32_t tab_id, uint32_t sender_frame_id, uint32_t target_frame_id,
                                      const char *data, const char *target_origin) {
    firstos_tab_t *tab = browser_host_get_tab_by_id(tab_id);
    firstos_frame_t *sf = frame_find_in_tab(tab, sender_frame_id);
    firstos_frame_t *tf = frame_find_in_tab(tab, target_frame_id);
    if (!sf || !tf) {
        serial_write("[postmessage] rejected: unknown frame\n");
        return -5;
    }
    static firstos_post_message_t m;
    memset(&m, 0, sizeof(m));
    m.version = FIRSTOS_BROWSER_IPC_VERSION;
    m.type = FIRSTOS_BROWSER_MSG_POST_MESSAGE;
    m.size = sizeof(m);
    m.sender_tab_id = tab_id;
    m.sender_tab_generation = tab->generation;
    m.sender_frame_id = sf->frame_id;
    m.sender_frame_generation = sf->generation;
    m.target_tab_id = tab_id;
    m.target_frame_id = tf->frame_id;
    m.target_frame_generation = tf->generation;
    frame_strlcpy(m.target_origin, target_origin, sizeof(m.target_origin));
    int dl = data ? (int)strlen(data) : 0;
    if (dl > FIRSTOS_MAX_MESSAGE_BYTES) {
        serial_write("[postmessage] rejected: payload too large\n");
        return -2;
    }
    if (dl > 0) memcpy(m.payload, data, (unsigned int)dl);
    m.payload_length = (uint32_t)dl;
    return browser_host_post_message_send(&m);
}

static void frame_globals_reset(void) {
    s_total_frames = 0;
    for (int i = 0; i < FIRSTOS_MAX_MESSAGE_QUEUE; i++) s_msg_queue[i].in_use = 0;
}

/* Bind iframe CLIP commands (in document order) to Host-owned direct children
 * of the active tab's root frame. The renderer never chooses frame ids. */
static void browser_host_frames_sync_from_commands(firstos_render_command_t *cmds, uint32_t n,
                                                   const char *strings, uint32_t str_cap) {
    firstos_tab_t *tab = browser_host_get_active_tab();
    if (!tab || !cmds) return;
    firstos_frame_t *root = frame_find_in_tab(tab, tab->frame_tree.root_frame_id);
    if (!root) return;

    int k = 0;
    for (uint32_t i = 0; i < n; i++) {
        firstos_render_command_t *c = &cmds[i];
        if (c->type == FIRSTOS_RENDER_CMD_END) break;
        if (c->type != FIRSTOS_RENDER_CMD_CLIP) continue;

        char src[FIRSTOS_MAX_FRAME_URL];
        src[0] = '\0';
        if (c->text_len > 0 && c->text_len < FIRSTOS_MAX_FRAME_URL &&
            c->text_offset < str_cap && c->text_offset + c->text_len < str_cap) {
            for (uint32_t j = 0; j < c->text_len; j++) {
                char ch = strings[c->text_offset + j];
                src[j] = ((unsigned char)ch < 32 || ch == 127) ? '_' : ch;
            }
            src[c->text_len] = '\0';
        } else {
            frame_strlcpy(src, "about:blank", sizeof(src));
        }

        /* k-th direct child of root in slot order */
        firstos_frame_t *child = NULL;
        int seen = 0;
        for (int s2 = 0; s2 < FIRSTOS_MAX_FRAMES_PER_TAB; s2++) {
            firstos_frame_t *f = &tab->frame_tree.frames[s2];
            if (f->state != FIRSTOS_FRAME_FREE && f->parent_frame_id == root->frame_id && f->depth == 1) {
                if (seen == k) { child = f; break; }
                seen++;
            }
        }

        char resolved[FIRSTOS_MAX_FRAME_URL];
        if (frame_resolve_url(root->url, src, resolved, sizeof(resolved)) != 0) {
            frame_strlcpy(resolved, src, sizeof(resolved));
        }

        if (!child) {
            int fid = browser_host_frame_create(tab->tab_id, root->frame_id, src, c->x, c->y, c->w, c->h);
            child = (fid > 0) ? frame_find_in_tab(tab, (uint32_t)fid) : NULL;
        } else if (strcmp(child->url, resolved) != 0 && child->state != FIRSTOS_FRAME_CRASHED) {
            browser_host_frame_navigate(tab->tab_id, child->frame_id, src);
        }
        if (child) {
            child->x = c->x; child->y = c->y;
            child->width = (uint32_t)(c->w > 0 ? c->w : 0);
            child->height = (uint32_t)(c->h > 0 ? c->h : 0);
            c->color = child->frame_id; /* Host-private stamp */
        } else {
            c->color = 0; /* rejected by policy: composite as blocked */
        }
        k++;
    }
}

static void browser_host_frame_composite(uint32_t frame_id, int x, int y, int w, int h) {
    firstos_tab_t *tab = browser_host_get_active_tab();
    firstos_frame_t *f = frame_find_in_tab(tab, frame_id);
    uint32_t bg = 0x101018, fg = 0x8E8E93;
    const char *label = "[iframe blocked]";
    char line[96];
    line[0] = '\0';
    if (f) {
        if (f->state == FIRSTOS_FRAME_CRASHED) { bg = 0x2A1414; fg = 0xFF453A; label = "[frame crashed]"; }
        else if (f->state == FIRSTOS_FRAME_ERROR) { bg = 0x2A2A38; fg = 0xFF9F0A; label = "[frame load error]"; }
        else { bg = 0x14141C; fg = 0x64D2FF; label = f->url; }
        browser_security_origin_to_string(&f->committed_origin, line, sizeof(line));
    }
    fb_rect(x, y, w, h, bg);
    /* Text is only drawn if it fits fully inside the clip rect. */
    int max_chars = (w - 8) / 8;
    if (max_chars > 0 && h >= 20) {
        char buf[64];
        int i = 0;
        while (label[i] && i < max_chars && i < 63) { buf[i] = label[i]; i++; }
        buf[i] = '\0';
        font_text(x + 4, y + 4, buf, fg, 1);
    }
    if (max_chars > 0 && h >= 40 && line[0]) {
        char buf[64];
        int i = 0;
        while (line[i] && i < max_chars && i < 63) { buf[i] = line[i]; i++; }
        buf[i] = '\0';
        font_text(x + 4, y + 22, buf, 0x8E8E93, 1);
    }
}
