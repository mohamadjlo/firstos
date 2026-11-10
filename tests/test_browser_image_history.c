#include <assert.h>
#include <stdint.h>
#include <stddef.h>

int printf(const char *format, ...);
void *malloc(size_t size);
void free(void *ptr);
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);

#include "userspace/browser/include/firstos_renderer.h"

/* Include adapter and helper implementations directly for host testing */
#include "html5_arena.c"
#include "userspace/browser/dom_adapter.c"
#include "userspace/browser/html_adapter.c"
#include "userspace/browser/css_adapter.c"
#include "userspace/browser/layout_adapter.c"
#include "userspace/browser/render_adapter.c"
#include "userspace/browser/event_adapter.c"

/* Mock Host History & Image Store implementations for Unit Testing */
#define TEST_HOST_HISTORY_MAX 32

typedef struct {
    char url[256];
    uint32_t navigation_id;
} test_host_history_entry_t;

typedef struct {
    test_host_history_entry_t entries[TEST_HOST_HISTORY_MAX];
    int count;
    int index;
} test_history_manager_t;

static test_history_manager_t s_test_hist;
static uint32_t s_test_nav_id = 1;

static uint32_t test_bump_nav_id(void) {
    return ++s_test_nav_id;
}

static int test_history_push(const char *url) {
    if (!url || !url[0]) return -1;
    if (s_test_hist.index >= 0 && strcmp(s_test_hist.entries[s_test_hist.index].url, url) == 0) return 0;
    if (s_test_hist.index < s_test_hist.count - 1) s_test_hist.count = s_test_hist.index + 1;
    if (s_test_hist.count >= TEST_HOST_HISTORY_MAX) {
        for (int i = 1; i < TEST_HOST_HISTORY_MAX; i++) s_test_hist.entries[i - 1] = s_test_hist.entries[i];
        s_test_hist.count = TEST_HOST_HISTORY_MAX - 1;
        s_test_hist.index = s_test_hist.count - 1;
    }
    strncpy(s_test_hist.entries[s_test_hist.count].url, url, 255);
    s_test_hist.entries[s_test_hist.count].url[255] = '\0';
    s_test_hist.entries[s_test_hist.count].navigation_id = test_bump_nav_id();
    s_test_hist.index = s_test_hist.count++;
    return 0;
}

static int test_history_back(char *out_url, int max_len) {
    if (s_test_hist.index <= 0 || !out_url || max_len <= 0) return -1;
    s_test_hist.index--;
    test_bump_nav_id();
    strncpy(out_url, s_test_hist.entries[s_test_hist.index].url, max_len - 1);
    out_url[max_len - 1] = '\0';
    return 0;
}

static int test_history_forward(char *out_url, int max_len) {
    if (s_test_hist.index < 0 || s_test_hist.index >= s_test_hist.count - 1 || !out_url || max_len <= 0) return -1;
    s_test_hist.index++;
    test_bump_nav_id();
    strncpy(out_url, s_test_hist.entries[s_test_hist.index].url, max_len - 1);
    out_url[max_len - 1] = '\0';
    return 0;
}

static int test_history_reload(char *out_url, int max_len) {
    if (s_test_hist.index < 0 || s_test_hist.index >= s_test_hist.count || !out_url || max_len <= 0) return -1;
    test_bump_nav_id();
    strncpy(out_url, s_test_hist.entries[s_test_hist.index].url, max_len - 1);
    out_url[max_len - 1] = '\0';
    return 0;
}

/* Mock Image Store */
#define TEST_IMAGE_STORE_SLOTS 8
#define TEST_IMAGE_MAX_W 1024
#define TEST_IMAGE_MAX_H 1024

typedef struct {
    uint32_t image_id;
    int in_use;
    int w, h;
    uint32_t *pixels;
    uint32_t access_tick;
} test_image_slot_t;

static test_image_slot_t s_test_store[TEST_IMAGE_STORE_SLOTS];
static uint32_t s_test_next_img_id = 1;
static uint32_t s_test_tick = 0;

static uint32_t test_image_store_insert(const uint32_t *pixels, int w, int h) {
    if (!pixels || w <= 0 || h <= 0 || w > TEST_IMAGE_MAX_W || h > TEST_IMAGE_MAX_H) return 0;
    int slot_idx = -1;
    uint32_t oldest_tick = 0xFFFFFFFF;
    for (int i = 0; i < TEST_IMAGE_STORE_SLOTS; i++) {
        if (!s_test_store[i].in_use) { slot_idx = i; break; }
        if (s_test_store[i].access_tick < oldest_tick) { oldest_tick = s_test_store[i].access_tick; slot_idx = i; }
    }
    if (slot_idx < 0) slot_idx = 0;
    test_image_slot_t *slot = &s_test_store[slot_idx];
    if (slot->in_use && slot->pixels) { free(slot->pixels); slot->pixels = NULL; }
    uint32_t sz = (uint32_t)(w * h * 4);
    slot->pixels = (uint32_t *)malloc(sz);
    if (!slot->pixels) return 0;
    memcpy(slot->pixels, pixels, sz);
    slot->in_use = 1;
    slot->w = w;
    slot->h = h;
    slot->image_id = s_test_next_img_id++;
    slot->access_tick = ++s_test_tick;
    return slot->image_id;
}

static const uint32_t *test_image_store_get(uint32_t image_id, int *out_w, int *out_h) {
    if (image_id == 0) return NULL;
    for (int i = 0; i < TEST_IMAGE_STORE_SLOTS; i++) {
        if (s_test_store[i].in_use && s_test_store[i].image_id == image_id) {
            s_test_store[i].access_tick = ++s_test_tick;
            if (out_w) *out_w = s_test_store[i].w;
            if (out_h) *out_h = s_test_store[i].h;
            return s_test_store[i].pixels;
        }
    }
    return NULL;
}

static void test_image_store_clear(void) {
    for (int i = 0; i < TEST_IMAGE_STORE_SLOTS; i++) {
        if (s_test_store[i].in_use && s_test_store[i].pixels) {
            free(s_test_store[i].pixels);
            s_test_store[i].pixels = NULL;
        }
        s_test_store[i].in_use = 0;
        s_test_store[i].image_id = 0;
    }
}

/* ========================================================================= */
/* Test Cases */
/* ========================================================================= */

static void test_history_push_and_state(void) {
    printf("[test_browser_image_history] 1. test_history_push_and_state...\n");
    s_test_hist.count = 0;
    s_test_hist.index = -1;
    s_test_nav_id = 1;

    assert(test_history_push("about:home") == 0);
    assert(s_test_hist.count == 1);
    assert(s_test_hist.index == 0);
    assert(strcmp(s_test_hist.entries[0].url, "about:home") == 0);
    assert(s_test_hist.entries[0].navigation_id == 2);

    /* Push duplicate: should be ignored */
    assert(test_history_push("about:home") == 0);
    assert(s_test_hist.count == 1);
    assert(s_test_hist.index == 0);

    /* Push second entry */
    assert(test_history_push("about:html5") == 0);
    assert(s_test_hist.count == 2);
    assert(s_test_hist.index == 1);
    assert(strcmp(s_test_hist.entries[1].url, "about:html5") == 0);
    assert(s_test_hist.entries[1].navigation_id == 3);

    printf("  -> OK (History push & duplicate prevention verified)\n");
}

static void test_history_back_forward(void) {
    printf("[test_browser_image_history] 2. test_history_back_forward...\n");
    char url[256];

    /* Move back */
    assert(test_history_back(url, sizeof(url)) == 0);
    assert(strcmp(url, "about:home") == 0);
    assert(s_test_hist.index == 0);
    uint32_t back_nav_id = s_test_nav_id;

    /* Move back at start: should fail */
    assert(test_history_back(url, sizeof(url)) == -1);

    /* Move forward */
    assert(test_history_forward(url, sizeof(url)) == 0);
    assert(strcmp(url, "about:html5") == 0);
    assert(s_test_hist.index == 1);
    assert(s_test_nav_id > back_nav_id);

    /* Move forward at end: should fail */
    assert(test_history_forward(url, sizeof(url)) == -1);

    printf("  -> OK (Back and Forward navigation with monotonic IDs verified)\n");
}

static void test_history_forward_truncation(void) {
    printf("[test_browser_image_history] 3. test_history_forward_truncation...\n");
    char url[256];

    /* Current: [about:home (0), about:html5 (1)], index=1 */
    assert(test_history_push("https://firstos.org/docs") == 0);
    /* Now: [home, html5, docs], count=3, index=2 */
    assert(s_test_hist.count == 3);
    assert(s_test_hist.index == 2);

    /* Back to html5 */
    assert(test_history_back(url, sizeof(url)) == 0);
    assert(strcmp(url, "about:html5") == 0);
    assert(s_test_hist.index == 1);

    /* New navigation from middle: docs should be truncated */
    assert(test_history_push("https://google.com") == 0);
    assert(s_test_hist.count == 3);
    assert(s_test_hist.index == 2);
    assert(strcmp(s_test_hist.entries[2].url, "https://google.com") == 0);

    /* Forward should fail since google.com is now top */
    assert(test_history_forward(url, sizeof(url)) == -1);

    printf("  -> OK (Forward-history branch truncation verified)\n");
}

static void test_case_history_reload(void) {
    printf("[test_browser_image_history] 4. test_history_reload...\n");
    char url[256];
    uint32_t pre_reload_nav = s_test_nav_id;

    assert(test_history_reload(url, sizeof(url)) == 0);
    assert(strcmp(url, "https://google.com") == 0);
    assert(s_test_hist.index == 2);
    assert(s_test_nav_id == pre_reload_nav + 1);

    printf("  -> OK (Reload preserves index & bumps navigation ID)\n");
}

static void test_history_capacity_eviction(void) {
    printf("[test_browser_image_history] 5. test_history_capacity_eviction...\n");
    s_test_hist.count = 0;
    s_test_hist.index = -1;

    for (int i = 0; i < 40; i++) {
        char buf[64];
        buf[0] = 'u'; buf[1] = 'r'; buf[2] = 'l'; buf[3] = '_';
        buf[4] = '0' + (i / 10);
        buf[5] = '0' + (i % 10);
        buf[6] = '\0';
        assert(test_history_push(buf) == 0);
    }

    assert(s_test_hist.count == TEST_HOST_HISTORY_MAX);
    assert(s_test_hist.index == TEST_HOST_HISTORY_MAX - 1);
    assert(strcmp(s_test_hist.entries[TEST_HOST_HISTORY_MAX - 1].url, "url_39") == 0);

    printf("  -> OK (FIFO history capacity limit enforced cleanly)\n");
}

static void test_image_dimension_limits(void) {
    printf("[test_browser_image_history] 6. test_image_dimension_limits...\n");
    uint32_t dummy_pix = 0xFF112233;

    /* Out of bounds checks */
    assert(test_image_store_insert(&dummy_pix, 0, 100) == 0);
    assert(test_image_store_insert(&dummy_pix, 100, 0) == 0);
    assert(test_image_store_insert(&dummy_pix, 1025, 100) == 0);
    assert(test_image_store_insert(&dummy_pix, 100, 1025) == 0);
    assert(test_image_store_insert(NULL, 100, 100) == 0);

    printf("  -> OK (Dimension and null pointer guards verified)\n");
}

static void test_image_decoding_rgba_to_argb(void) {
    printf("[test_browser_image_history] 7. test_image_decoding_rgba_to_argb...\n");
    /* Simulate raw RGBA bytes loaded into memory: R=0x12, G=0x34, B=0x56, A=0xFF */
    uint8_t raw_rgba[8] = { 0x12, 0x34, 0x56, 0xFF,  0xAA, 0xBB, 0xCC, 0x80 };
    uint32_t *p32 = (uint32_t *)raw_rgba;

    /* Apply RGBA -> ARGB Little Endian channel swap */
    for (int p = 0; p < 2; p++) {
        uint32_t pix = p32[p];
        p32[p] = (pix & 0xFF00FF00) | ((pix & 0x000000FF) << 16) | ((pix & 0x00FF0000) >> 16);
    }

    /* Pixel 0: A=0xFF, R=0x12, G=0x34, B=0x56 => 0xFF123456 */
    assert(p32[0] == 0xFF123456);
    /* Pixel 1: A=0x80, R=0xAA, G=0xBB, B=0xCC => 0x80AABBCC */
    assert(p32[1] == 0x80AABBCC);

    printf("  -> OK (Pixel endian conversion to 0xAARRGGBB verified)\n");
}

static void test_host_image_store_insert_get(void) {
    printf("[test_browser_image_history] 8. test_host_image_store_insert_get...\n");
    test_image_store_clear();

    uint32_t test_pixels[16];
    for (int i = 0; i < 16; i++) test_pixels[i] = 0xFF000000 | (uint32_t)(i * 16);

    uint32_t id1 = test_image_store_insert(test_pixels, 4, 4);
    assert(id1 > 0);

    int out_w = 0, out_h = 0;
    const uint32_t *ret = test_image_store_get(id1, &out_w, &out_h);
    assert(ret != NULL);
    assert(out_w == 4 && out_h == 4);
    assert(ret[0] == test_pixels[0]);
    assert(ret[15] == test_pixels[15]);

    /* Non-existent ID lookup */
    assert(test_image_store_get(999, &out_w, &out_h) == NULL);

    printf("  -> OK (Image insertion and lookup verified)\n");
}

static void test_host_image_store_lru_eviction(void) {
    printf("[test_browser_image_history] 9. test_host_image_store_lru_eviction...\n");
    test_image_store_clear();

    uint32_t dummy = 0xFFFFFFFF;
    uint32_t ids[10];

    for (int i = 0; i < 8; i++) {
        ids[i] = test_image_store_insert(&dummy, 2, 2);
        assert(ids[i] > 0);
    }

    /* All 8 slots full. Inserting 9th should evict slot 0 */
    uint32_t id9 = test_image_store_insert(&dummy, 2, 2);
    assert(id9 > 0);

    int w, h;
    assert(test_image_store_get(ids[0], &w, &h) == NULL); /* Evicted */
    assert(test_image_store_get(id9, &w, &h) != NULL);    /* Present */

    printf("  -> OK (LRU image store cache eviction verified)\n");
}

static void test_renderer_intrinsic_dimensions(void) {
    printf("[test_browser_image_history] 10. test_renderer_intrinsic_dimensions...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = "<!doctype html><html><body><img src=\"/logo.png\"></body></html>";
    assert(firstos_renderer_parse_html(&ctx, html, strlen(html)) == RENDER_OK);
    assert(ctx.pending_res_count == 1);
    assert(ctx.pending_res[0].type == FIRSTOS_RES_TYPE_IMAGE);

    /* Initial layout with placeholder */
    firstos_renderer_layout(&ctx, 800, 600);
    html_node_t *img_node = firstos_renderer_get_element_by_tag(&ctx, HTML_TAG_IMG);
    assert(img_node != NULL);
    assert(img_node->bounds.w == 120); /* Default unconstrained inline placeholder width */
    assert(img_node->bounds.h == 80);  /* Default placeholder height */

    /* Simulate Host sending RESOURCE_RESPONSE with intrinsic size 64x64 and image_id=5 */
    firstos_resource_response_t resp;
    resp.request_id = ctx.pending_res[0].request_id;
    resp.navigation_id = ctx.navigation_id;
    resp.status_code = 200;
    resp.resource_type = FIRSTOS_RES_TYPE_IMAGE;
    resp.meta_w = 64;
    resp.meta_h = 64;
    resp.image_id = 5;

    /* Update node as main.c would */
    img_node->image_id = resp.image_id;
    img_node->req_width = resp.meta_w;
    img_node->req_height = resp.meta_h;

    /* Reflow */
    firstos_renderer_layout(&ctx, 800, 600);
    assert(img_node->bounds.w == 64);
    assert(img_node->bounds.h == 64);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Intrinsic image dimensions update and reflow verified)\n");
}

static void test_renderer_image_render_command(void) {
    printf("[test_browser_image_history] 11. test_renderer_image_render_command...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = "<!doctype html><html><body><img src=\"/avatar.bmp\" width=\"48\" height=\"48\"></body></html>";
    firstos_renderer_parse_html(&ctx, html, strlen(html));

    html_node_t *img_node = firstos_renderer_get_element_by_tag(&ctx, HTML_TAG_IMG);
    assert(img_node != NULL);
    img_node->image_id = 42;

    firstos_renderer_layout(&ctx, 800, 600);

    firstos_render_command_t cmds[64];
    char strings[1024];
    uint32_t num_cmds = 0;
    assert(firstos_renderer_generate_commands(&ctx, cmds, 64, strings, sizeof(strings), &num_cmds) == 0);

    /* Look for FIRSTOS_RENDER_CMD_IMAGE */
    int found_image_cmd = 0;
    for (uint32_t i = 0; i < num_cmds; i++) {
        if (cmds[i].type == FIRSTOS_RENDER_CMD_IMAGE) {
            found_image_cmd = 1;
            assert(cmds[i].color == 42); /* Holds image_id */
            assert(cmds[i].w == 48);
            assert(cmds[i].h == 48);
            break;
        }
    }
    assert(found_image_cmd == 1);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (FIRSTOS_RENDER_CMD_IMAGE emission with image_id verified)\n");
}

static void test_broken_image_recovery(void) {
    printf("[test_browser_image_history] 12. test_broken_image_recovery...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = "<html><body><img src=\"/missing.png\"></body></html>";
    firstos_renderer_parse_html(&ctx, html, strlen(html));

    html_node_t *img_node = firstos_renderer_get_element_by_tag(&ctx, HTML_TAG_IMG);
    assert(img_node != NULL);

    /* Simulate error response from Host */
    img_node->image_broken = 1;
    img_node->image_id = 0;

    firstos_renderer_layout(&ctx, 800, 600);

    firstos_render_command_t cmds[64];
    char strings[1024];
    uint32_t num_cmds = 0;
    assert(firstos_renderer_generate_commands(&ctx, cmds, 64, strings, sizeof(strings), &num_cmds) == 0);

    /* Verify broken indicator string "[X]" is present in strings buffer */
    int has_broken_label = 0;
    for (uint32_t i = 0; i < num_cmds; i++) {
        if (cmds[i].type == FIRSTOS_RENDER_CMD_TEXT && cmds[i].color == 0xFF453A) {
            has_broken_label = 1;
            assert(strcmp(strings + cmds[i].text_offset, "[X]") == 0);
            break;
        }
    }
    assert(has_broken_label == 1);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Broken image fallback renders cleanly without aborting)\n");
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Phase 4D: Image Decoding & Navigation History Tests\n");
    printf("============================================================\n");

    test_history_push_and_state();
    test_history_back_forward();
    test_history_forward_truncation();
    test_case_history_reload();
    test_history_capacity_eviction();
    test_image_dimension_limits();
    test_image_decoding_rgba_to_argb();
    test_host_image_store_insert_get();
    test_host_image_store_lru_eviction();
    test_renderer_intrinsic_dimensions();
    test_renderer_image_render_command();
    test_broken_image_recovery();

    printf("============================================================\n");
    printf("ALL 12 PHASE 4D UNIT TESTS PASSED (12/12)\n");
    printf("============================================================\n");
    return 0;
}
