#include <assert.h>
#include <stdint.h>
#include <stddef.h>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wincompatible-library-redeclaration"
#endif

int printf(const char *format, ...);
int strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, unsigned int n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, unsigned int n);
void *memcpy(void *dest, const void *src, unsigned int n);
void *memset(void *s, int c, unsigned int n);

#include "../include/firstos/uapi/browser_ipc.h"
#include "../userspace/browser/include/firstos_renderer.h"

void test_abi_media_command_struct_size(void) {
    assert(sizeof(firstos_media_command_t) == 32);
    firstos_media_command_t mcmd;
    memset(&mcmd, 0, sizeof(mcmd));
    mcmd.command = FIRSTOS_MEDIA_CMD_PLAY;
    mcmd.volume = 80;
    mcmd.seek_time_ms = 1500;
    assert(mcmd.command == FIRSTOS_MEDIA_CMD_PLAY);
    assert(mcmd.volume == 80);
    assert(mcmd.seek_time_ms == 1500);
    printf("PASS: test_abi_media_command_struct_size (32 bytes)\n");
}

void test_media_tag_layout_and_attributes(void) {
    firstos_renderer_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    html_node_t root;
    memset(&root, 0, sizeof(root));
    root.id_num = 1;
    root.tag = HTML_TAG_BODY;

    html_node_t video;
    memset(&video, 0, sizeof(video));
    video.id_num = 2;
    video.tag = HTML_TAG_VIDEO;
    video.src = "file:///movies/sample.avi";
    video.media_controls = 1;
    video.req_width = 640;
    video.req_height = 360;
    video.parent = &root;
    root.first_child = &video;

    html_node_t audio;
    memset(&audio, 0, sizeof(audio));
    audio.id_num = 3;
    audio.tag = HTML_TAG_AUDIO;
    audio.src = "file:///audio/click.mp3";
    audio.media_controls = 1;
    audio.parent = &root;
    video.next_sibling = &audio;

    ctx.root_dom = &root;
    firstos_renderer_layout(&ctx, 800, 600);

    /* Verify video box dimensions */
    assert(video.bounds.w == 640);
    assert(video.bounds.h == 360);

    /* Verify audio box dimensions */
    assert(audio.bounds.w == 320);
    assert(audio.bounds.h == 36);

    printf("PASS: test_media_tag_layout_and_attributes\n");
}

void test_media_hit_test_and_transport_toggle(void) {
    firstos_renderer_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    html_node_t root;
    memset(&root, 0, sizeof(root));
    root.id_num = 1;
    root.tag = HTML_TAG_BODY;

    html_node_t video;
    memset(&video, 0, sizeof(video));
    video.id_num = 2;
    video.tag = HTML_TAG_VIDEO;
    video.src = "file:///movies/sample.avi";
    video.media_controls = 1;
    video.media_duration_sec = 90;
    video.req_width = 480;
    video.req_height = 270;
    video.parent = &root;
    root.first_child = &video;

    ctx.root_dom = &root;
    firstos_renderer_layout(&ctx, 800, 600);

    char nav_url[256];
    char submit_action[256];
    char query[256];

    /* 1. Click inside video area -> Toggle play/pause (returns 5) */
    assert(video.media_playing == 0);
    int cres = firstos_renderer_handle_click(&ctx, 100, 100,
                                            nav_url, sizeof(nav_url),
                                            submit_action, sizeof(submit_action),
                                            query, sizeof(query));
    assert(cres == 5);
    assert(video.media_playing == 1);
    assert(strcmp(nav_url, "file:///movies/sample.avi") == 0);

    /* 2. Click scrubber bar to seek -> Returns 5 and updates media_cur_time_sec */
    /* Scrubber track is at y = sy + sh - ctrl_h + 12 = 0 + 270 - 32 + 12 = 250 */
    /* Scrubber x starts at 38, width = 480 - 120 = 360 */
    /* Click at x = 38 + 180 = 218 (50% progress -> 45 seconds) */
    cres = firstos_renderer_handle_click(&ctx, 218, 252,
                                         nav_url, sizeof(nav_url),
                                         submit_action, sizeof(submit_action),
                                         query, sizeof(query));
    assert(cres == 5);
    assert(video.media_cur_time_sec >= 44 && video.media_cur_time_sec <= 46);

    printf("PASS: test_media_hit_test_and_transport_toggle\n");
}

int main(void) {
    printf("=== Starting Phase 6B HTML5 Media & Audio Unit Tests ===\n");
    test_abi_media_command_struct_size();
    test_media_tag_layout_and_attributes();
    test_media_hit_test_and_transport_toggle();
    printf("=== All 3 Media Unit Tests PASSED ===\n");
    return 0;
}
