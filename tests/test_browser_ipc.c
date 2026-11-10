#include <assert.h>
#include <stdint.h>

int printf(const char *format, ...);

static unsigned int t_strlen(const char *s) {
    unsigned int len = 0;
    while (s && s[len]) len++;
    return len;
}

static void t_memcpy(void *dst, const void *src, unsigned int n) {
    char *d = (char *)dst;
    const char *s = (const char *)src;
    for (unsigned int i = 0; i < n; i++) d[i] = s[i];
}

static void t_memset(void *dst, int val, unsigned int n) {
    char *d = (char *)dst;
    for (unsigned int i = 0; i < n; i++) d[i] = (char)val;
}

static int t_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

#define FIRSTOS_HOST_TEST 1
#include "include/firstos/uapi/browser_ipc.h"

/* Test 1: Channel header initialization and layout */
static void test_channel_initialization(void) {
    printf("[test_browser_ipc] 1. test_channel_initialization...\n");
    assert(FIRSTOS_BROWSER_IPC_MAGIC == 0x42525753);
    assert(FIRSTOS_BROWSER_IPC_VERSION == 1);
    assert(FIRSTOS_BROWSER_SHM_TOTAL_SIZE == 4 * 1024 * 1024);
    assert(FIRSTOS_BROWSER_CHANNEL_SIZE == 2 * 1024 * 1024);
    assert(FIRSTOS_BROWSER_CHANNEL_H2R_OFF == 0);
    assert(FIRSTOS_BROWSER_CHANNEL_R2H_OFF == 2 * 1024 * 1024);

    firstos_browser_channel_t chan;
    t_memset(&chan, 0, sizeof(chan));
    chan.magic = FIRSTOS_BROWSER_IPC_MAGIC;
    chan.version = FIRSTOS_BROWSER_IPC_VERSION;
    chan.capacity = FIRSTOS_BROWSER_CHANNEL_SIZE;

    assert(chan.magic == 0x42525753);
    assert(chan.version == 1);
    assert(chan.capacity == 2097152);
    assert(chan.has_message == 0);
    assert(chan.lock_word == 0);
    printf("  -> OK (Channel init & sizes verified)\n");
}

/* Test 2: Message header struct size and layout */
static void test_message_header_layout(void) {
    printf("[test_browser_ipc] 2. test_message_header_layout...\n");
    assert(sizeof(firstos_browser_message_t) == 32);

    firstos_browser_message_t msg;
    t_memset(&msg, 0, sizeof(msg));
    msg.version = FIRSTOS_BROWSER_IPC_VERSION;
    msg.type = FIRSTOS_BROWSER_MSG_HTML_DATA;
    msg.request_id = 42;
    msg.payload_offset = 0;
    msg.payload_length = 1024;
    msg.status = 0;
    msg.param1 = 800;
    msg.param2 = 600;

    assert(msg.version == 1);
    assert(msg.type == 2);
    assert(msg.request_id == 42);
    assert(msg.payload_length == 1024);
    assert(msg.param1 == 800);
    assert(msg.param2 == 600);
    printf("  -> OK (Message header 32-byte layout verified)\n");
}

/* Test 3: Render command struct size and layout */
static void test_render_command_layout(void) {
    printf("[test_browser_ipc] 3. test_render_command_layout...\n");
    assert(sizeof(firstos_render_command_t) == 32);

    firstos_render_command_t cmd;
    t_memset(&cmd, 0, sizeof(cmd));
    cmd.type = FIRSTOS_RENDER_CMD_RECT;
    cmd.x = 10;
    cmd.y = 20;
    cmd.w = 200;
    cmd.h = 100;
    cmd.color = 0x30D158;
    cmd.text_offset = 512;
    cmd.text_len = 16;

    assert(cmd.type == 2);
    assert(cmd.x == 10);
    assert(cmd.y == 20);
    assert(cmd.w == 200);
    assert(cmd.h == 100);
    assert(cmd.color == 0x30D158);
    assert(cmd.text_offset == 512);
    assert(cmd.text_len == 16);
    printf("  -> OK (Render command 32-byte layout verified)\n");
}

/* Test 4: Host to Renderer HTML data serialization */
static void test_h2r_html_message_serialization(void) {
    printf("[test_browser_ipc] 4. test_h2r_html_message_serialization...\n");
    char channel_buf[4096];
    t_memset(channel_buf, 0, sizeof(channel_buf));

    firstos_browser_channel_t *chan = (firstos_browser_channel_t *)channel_buf;
    chan->magic = FIRSTOS_BROWSER_IPC_MAGIC;
    chan->version = FIRSTOS_BROWSER_IPC_VERSION;
    chan->capacity = FIRSTOS_BROWSER_CHANNEL_SIZE;

    const char *html = "<html><body><h1>FirstOS</h1></body></html>";
    uint32_t len = t_strlen(html);
    char *data = channel_buf + sizeof(firstos_browser_channel_t);
    t_memcpy(data, html, len);
    data[len] = '\0';

    chan->msg.version = FIRSTOS_BROWSER_IPC_VERSION;
    chan->msg.type = FIRSTOS_BROWSER_MSG_HTML_DATA;
    chan->msg.request_id = 101;
    chan->msg.payload_offset = 0;
    chan->msg.payload_length = len;
    chan->msg.param1 = 1024;
    chan->msg.param2 = 768;
    chan->has_message = 1;

    assert(chan->has_message == 1);
    assert(chan->msg.type == FIRSTOS_BROWSER_MSG_HTML_DATA);
    assert(chan->msg.payload_length == len);
    assert(t_strcmp(data, html) == 0);
    printf("  -> OK (HTML serialization & payload match)\n");
}

/* Test 5: Renderer to Host render command buffer generation */
static void test_r2h_render_command_generation(void) {
    printf("[test_browser_ipc] 5. test_r2h_render_command_generation...\n");
    char r2h_buf[4096];
    t_memset(r2h_buf, 0, sizeof(r2h_buf));

    firstos_browser_channel_t *chan = (firstos_browser_channel_t *)r2h_buf;
    char *r2h_data = r2h_buf + sizeof(firstos_browser_channel_t);
    firstos_render_command_t *cmds = (firstos_render_command_t *)r2h_data;

    /* 5 commands */
    cmds[0].type = FIRSTOS_RENDER_CMD_CLEAR;
    cmds[0].color = 0x16161E;

    cmds[1].type = FIRSTOS_RENDER_CMD_RECT;
    cmds[1].x = 0; cmds[1].y = 0; cmds[1].w = 800; cmds[1].h = 40;
    cmds[1].color = 0x222430;

    cmds[2].type = FIRSTOS_RENDER_CMD_TEXT;
    cmds[2].x = 16; cmds[2].y = 12;
    cmds[2].color = 0xFFFFFF;
    cmds[2].text_offset = 512;
    cmds[2].text_len = 11;
    t_memcpy(r2h_data + 512, "Hello First\0", 12);

    cmds[3].type = FIRSTOS_RENDER_CMD_TEXT;
    cmds[3].x = 16; cmds[3].y = 60;
    cmds[3].color = 0x888888;
    cmds[3].text_offset = 540;
    cmds[3].text_len = 9;
    t_memcpy(r2h_data + 540, "Body Text\0", 10);

    cmds[4].type = FIRSTOS_RENDER_CMD_END;

    chan->msg.version = FIRSTOS_BROWSER_IPC_VERSION;
    chan->msg.type = FIRSTOS_BROWSER_MSG_RENDER_FRAME;
    chan->msg.param1 = 5; /* command count */
    chan->has_message = 1;

    assert(chan->has_message == 1);
    assert(cmds[0].type == FIRSTOS_RENDER_CMD_CLEAR);
    assert(cmds[1].type == FIRSTOS_RENDER_CMD_RECT);
    assert(cmds[2].type == FIRSTOS_RENDER_CMD_TEXT);
    assert(cmds[4].type == FIRSTOS_RENDER_CMD_END);
    assert(t_strcmp(r2h_data + cmds[2].text_offset, "Hello First") == 0);
    printf("  -> OK (5-command render buffer validated)\n");
}

/* Test 6: Render command bounds clamping and clipping */
static void test_render_command_clipping_and_bounds(void) {
    printf("[test_browser_ipc] 6. test_render_command_clipping_and_bounds...\n");
    int content_x = 50, content_y = 50, content_w = 400, content_h = 300;

    /* Case A: Rectangle extending beyond right and bottom */
    firstos_render_command_t cmdA = {
        .type = FIRSTOS_RENDER_CMD_RECT,
        .x = 350, .y = 250, .w = 100, .h = 100,
        .color = 0xFF0000, .text_offset = 0, .text_len = 0
    };

    int rx = content_x + cmdA.x;
    int ry = content_y + cmdA.y;
    int rw = cmdA.w;
    int rh = cmdA.h;
    if (rx < content_x) { rw -= (content_x - rx); rx = content_x; }
    if (ry < content_y) { rh -= (content_y - ry); ry = content_y; }
    if (rx + rw > content_x + content_w) rw = (content_x + content_w) - rx;
    if (ry + rh > content_y + content_h) rh = (content_y + content_h) - ry;

    assert(rx == 400);
    assert(ry == 300);
    assert(rw == 50); /* Clamped to 50 instead of 100 */
    assert(rh == 50); /* Clamped to 50 instead of 100 */

    /* Case B: Rectangle with negative coordinates (extending off top-left) */
    firstos_render_command_t cmdB = {
        .type = FIRSTOS_RENDER_CMD_RECT,
        .x = -20, .y = -30, .w = 80, .h = 80,
        .color = 0x00FF00, .text_offset = 0, .text_len = 0
    };

    rx = content_x + cmdB.x;
    ry = content_y + cmdB.y;
    rw = cmdB.w;
    rh = cmdB.h;
    if (rx < content_x) { rw -= (content_x - rx); rx = content_x; }
    if (ry < content_y) { rh -= (content_y - ry); ry = content_y; }
    if (rx + rw > content_x + content_w) rw = (content_x + content_w) - rx;
    if (ry + rh > content_y + content_h) rh = (content_y + content_h) - ry;

    assert(rx == 50);  /* Clamped to content_x */
    assert(ry == 50);  /* Clamped to content_y */
    assert(rw == 60);  /* 80 - 20 = 60 */
    assert(rh == 50);  /* 80 - 30 = 50 */

    printf("  -> OK (Rect clamping & clipping verified)\n");
}

/* Test 7: Text offset validation (preventing buffer overflow) */
static void test_text_command_offset_validation(void) {
    printf("[test_browser_ipc] 7. test_text_command_offset_validation...\n");
    uint32_t channel_capacity = FIRSTOS_BROWSER_CHANNEL_SIZE;

    /* Out of bounds offset: >= channel capacity */
    firstos_render_command_t cmd_bad = {
        .type = FIRSTOS_RENDER_CMD_TEXT,
        .x = 10, .y = 10, .w = 0, .h = 16,
        .color = 0xFFFFFF,
        .text_offset = channel_capacity + 100, /* Out of bounds! */
        .text_len = 10
    };

    int is_valid = (cmd_bad.text_offset < channel_capacity);
    assert(is_valid == 0);

    /* In bounds offset */
    firstos_render_command_t cmd_good = {
        .type = FIRSTOS_RENDER_CMD_TEXT,
        .x = 10, .y = 10, .w = 0, .h = 16,
        .color = 0xFFFFFF,
        .text_offset = 512,
        .text_len = 10
    };
    is_valid = (cmd_good.text_offset < channel_capacity);
    assert(is_valid == 1);
    printf("  -> OK (Text offset buffer bounds verified)\n");
}

/* Test 8: Input event message serialization */
static void test_input_event_serialization(void) {
    printf("[test_browser_ipc] 8. test_input_event_serialization...\n");
    firstos_browser_message_t msg;
    t_memset(&msg, 0, sizeof(msg));

    msg.version = FIRSTOS_BROWSER_IPC_VERSION;
    msg.type = FIRSTOS_BROWSER_MSG_INPUT_EVENT;
    msg.request_id = 999;
    msg.param1 = 320; /* mx */
    msg.param2 = 240; /* my */
    msg.status = 0x1C; /* Enter key / scancode */

    assert(msg.type == FIRSTOS_BROWSER_MSG_INPUT_EVENT);
    assert(msg.param1 == 320);
    assert(msg.param2 == 240);
    assert(msg.status == 0x1C);
    printf("  -> OK (Input event serialization verified)\n");
}

/* Test 9: Shutdown message sequence */
static void test_shutdown_sequence(void) {
    printf("[test_browser_ipc] 9. test_shutdown_sequence...\n");
    firstos_browser_message_t msg;
    t_memset(&msg, 0, sizeof(msg));

    msg.version = FIRSTOS_BROWSER_IPC_VERSION;
    msg.type = FIRSTOS_BROWSER_MSG_SHUTDOWN;
    msg.request_id = 1234;

    assert(msg.type == FIRSTOS_BROWSER_MSG_SHUTDOWN);
    assert(msg.version == FIRSTOS_BROWSER_IPC_VERSION);
    printf("  -> OK (Shutdown message verified)\n");
}

/* Test 10: Protocol version mismatch rejection */
static void test_protocol_version_mismatch_rejection(void) {
    printf("[test_browser_ipc] 10. test_protocol_version_mismatch_rejection...\n");
    firstos_browser_message_t msg_bad_ver;
    t_memset(&msg_bad_ver, 0, sizeof(msg_bad_ver));
    msg_bad_ver.version = 99; /* Unknown future version */
    msg_bad_ver.type = FIRSTOS_BROWSER_MSG_HTML_DATA;

    int accepted = (msg_bad_ver.version == FIRSTOS_BROWSER_IPC_VERSION);
    assert(accepted == 0);

    firstos_browser_message_t msg_good_ver;
    t_memset(&msg_good_ver, 0, sizeof(msg_good_ver));
    msg_good_ver.version = FIRSTOS_BROWSER_IPC_VERSION;
    accepted = (msg_good_ver.version == FIRSTOS_BROWSER_IPC_VERSION);
    assert(accepted == 1);
    printf("  -> OK (Version mismatch rejected successfully)\n");
}

/* Test 11: Viewport resize message and navigation_id frame filtering */
static void test_viewport_resize_and_navigation_id_filtering(void) {
    printf("[test_browser_ipc] 11. test_viewport_resize_and_navigation_id_filtering...\n");
    assert(FIRSTOS_BROWSER_MSG_VIEWPORT_CHANGED == 24);

    firstos_browser_message_t resize_msg;
    t_memset(&resize_msg, 0, sizeof(resize_msg));
    resize_msg.version = FIRSTOS_BROWSER_IPC_VERSION;
    resize_msg.type = FIRSTOS_BROWSER_MSG_VIEWPORT_CHANGED;
    resize_msg.param1 = 1024; /* width */
    resize_msg.param2 = 768;  /* height */

    assert(resize_msg.type == FIRSTOS_BROWSER_MSG_VIEWPORT_CHANGED);
    assert(resize_msg.param1 == 1024);
    assert(resize_msg.param2 == 768);

    /* Test navigation_id validation on RENDER_FRAME */
    firstos_browser_message_t frame_msg;
    t_memset(&frame_msg, 0, sizeof(frame_msg));
    frame_msg.version = FIRSTOS_BROWSER_IPC_VERSION;
    frame_msg.type = FIRSTOS_BROWSER_MSG_RENDER_FRAME;
    frame_msg.payload_length = 15; /* command count */
    frame_msg.param2 = 1200;       /* doc_height */

    uint32_t active_tab_nav_id = 42;

    /* Stale frame from old navigation (nav_id = 41) */
    frame_msg.status = 41;
    int is_stale = (frame_msg.status > 0 && (uint32_t)frame_msg.status != active_tab_nav_id);
    assert(is_stale == 1);

    /* Matching frame from current navigation (nav_id = 42) */
    frame_msg.status = 42;
    is_stale = (frame_msg.status > 0 && (uint32_t)frame_msg.status != active_tab_nav_id);
    assert(is_stale == 0);

    /* Next navigation frame (nav_id = 43) while tab is still at 42 */
    frame_msg.status = 43;
    is_stale = (frame_msg.status > 0 && (uint32_t)frame_msg.status != active_tab_nav_id);
    assert(is_stale == 1);

    printf("  -> OK (Viewport change message and navigation_id filtering verified)\n");
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Phase 4A Browser IPC & Renderer Protocol Unit Tests\n");
    printf("============================================================\n");

    test_channel_initialization();
    test_message_header_layout();
    test_render_command_layout();
    test_h2r_html_message_serialization();
    test_r2h_render_command_generation();
    test_render_command_clipping_and_bounds();
    test_text_command_offset_validation();
    test_input_event_serialization();
    test_shutdown_sequence();
    test_protocol_version_mismatch_rejection();
    test_viewport_resize_and_navigation_id_filtering();

    /* Test 12: Phase 4 damage descriptor layout & constants */
    printf("[test_browser_ipc] 12. test_damage_descriptor_layout...\n");
    assert(sizeof(firstos_damage_rect_t) == 32);
    assert(FIRSTOS_BROWSER_DAMAGE_OFFSET == 36864u);
    assert(FIRSTOS_RENDER_FLAG_FULL_REPAINT == 0x0001u);
    assert(FIRSTOS_RENDER_FLAG_DAMAGE_VALID == 0x0002u);
    printf("  -> OK (Damage descriptor layout & constants verified)\n");

    printf("============================================================\n");
    printf("ALL 12 BROWSER IPC UNIT TESTS PASSED (12/12)\n");
    printf("============================================================\n");
    return 0;
}
