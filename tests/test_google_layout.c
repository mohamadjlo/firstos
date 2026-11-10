#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#define FB_W 800
#define FB_H 600
static unsigned int g_fb[FB_H][FB_W];

static int g_clip_x = 0, g_clip_y = 0, g_clip_w = FB_W, g_clip_h = FB_H;

void fb_set_clip(int x, int y, int w, int h) {
    g_clip_x = x; g_clip_y = y; g_clip_w = w; g_clip_h = h;
}
void fb_reset_clip(void) {
    g_clip_x = 0; g_clip_y = 0; g_clip_w = FB_W; g_clip_h = FB_H;
}

void fb_putpixel(int x, int y, unsigned int col) {
    if (x < g_clip_x || x >= g_clip_x + g_clip_w || y < g_clip_y || y >= g_clip_y + g_clip_h) return;
    if (x >= 0 && x < FB_W && y >= 0 && y < FB_H) {
        g_fb[y][x] = col | 0xFF000000;
    }
}

void fb_rect(int x, int y, int w, int h, unsigned int col) {
    for (int r = y; r < y + h; r++) {
        for (int c = x; c < x + w; c++) {
            fb_putpixel(c, r, col);
        }
    }
}

void fb_border(int x, int y, int w, int h, unsigned int col) {
    for (int c = x; c < x + w; c++) { fb_putpixel(c, y, col); fb_putpixel(c, y + h - 1, col); }
    for (int r = y; r < y + h; r++) { fb_putpixel(x, r, col); fb_putpixel(x + w - 1, r, col); }
}

void fb_round_rect(int x, int y, int w, int h, int rad, unsigned int col) {
    (void)rad;
    fb_rect(x, y, w, h, col);
}

void fb_round_rect_border(int x, int y, int w, int h, int rad, unsigned int col) {
    (void)rad;
    fb_border(x, y, w, h, col);
}

void font_text(int x, int y, const char *txt, unsigned int col, int size) {
    if (!txt) return;
    int len = strlen(txt);
    int cw = (size == 2) ? 10 : 8;
    int ch = (size == 2) ? 20 : 14;
    for (int i = 0; i < len; i++) {
        for (int py = 0; py < ch; py += 2) {
            for (int px = 0; px < cw - 2; px += 2) {
                fb_putpixel(x + i * cw + px, y + py, col);
            }
        }
    }
}

void font_icon(int x, int y, int icon, unsigned int col) {
    (void)icon;
    fb_rect(x, y, 16, 16, col);
}

void serial_write(const char *s) { (void)s; }
void serial_write_hex(unsigned int val) { (void)val; }
void gui_pump_events(void) {}

int http_get_binary(const char *url, unsigned char *buf, int max) {
    (void)url; (void)buf; (void)max; return 0;
}

#include "../html5_engine.c"

void html5_resource_fetch_for_dom(html_node_t *root, const char *base_url) {
    (void)root; (void)base_url;
}
void html5_script_run_dom_scripts(html_node_t *root, const char *base_url) {
    (void)root; (void)base_url;
}
html5_video_ctx_t *html5_media_create_video_ctx(const char *src, int loop, int w, int h) {
    (void)src; (void)loop; (void)w; (void)h; return NULL;
}
void html5_media_destroy_video_ctx(html5_video_ctx_t *ctx) { (void)ctx; }
void html5_media_render_video(html5_video_ctx_t *ctx, int x, int y, int w, int h) {
    (void)ctx; (void)x; (void)y; (void)w; (void)h;
}

int gui_window_dirty_only = 0;
int html5_media_handle_click(html5_video_ctx_t *ctx, int mx, int my) { (void)ctx; (void)mx; (void)my; return 0; }
void html5_media_init(void) {}
void html5_media_tick(void) {}
void html5_resources_init(void) {}
void html5_script_eval(const char *code, html_node_t *node) { (void)code; (void)node; }
void html5_script_init(void) {}
void html5_url_resolve(const char *base, const char *rel, char *out, int max) {
    if (!out || max <= 0) return;
    if (!rel) { out[0] = 0; return; }
    if (strncmp(rel, "http://", 7) == 0 || strncmp(rel, "https://", 8) == 0) {
        strncpy(out, rel, max - 1);
        out[max - 1] = 0;
    } else {
        snprintf(out, max, "%s/%s", base ? base : "", rel);
    }
}
int needs_redraw = 0;
int point_in_rect(int px, int py, rect_t r) {
    return (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h);
}

static void save_bmp(const char *filename) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    unsigned char header[54] = {
        'B', 'M',
        0, 0, 0, 0,
        0, 0, 0, 0,
        54, 0, 0, 0,
        40, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        1, 0, 32, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    int w = FB_W, h = FB_H;
    int fsize = 54 + w * h * 4;
    header[2] = fsize & 0xFF; header[3] = (fsize >> 8) & 0xFF; header[4] = (fsize >> 16) & 0xFF; header[5] = (fsize >> 24) & 0xFF;
    header[18] = w & 0xFF; header[19] = (w >> 8) & 0xFF; header[20] = (w >> 16) & 0xFF; header[21] = (w >> 24) & 0xFF;
    header[22] = h & 0xFF; header[23] = (h >> 8) & 0xFF; header[24] = (h >> 16) & 0xFF; header[25] = (h >> 24) & 0xFF;
    write(fd, header, 54);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            unsigned int c = g_fb[y][x];
            unsigned char px[4] = { (unsigned char)(c & 0xFF), (unsigned char)((c >> 8) & 0xFF), (unsigned char)((c >> 16) & 0xFF), 0xFF };
            write(fd, px, 4);
        }
    }
    close(fd);
}

int main(void) {
    write(1, "[test_google_layout] Starting Google Homepage Layout & Visual Tests...\n", 71);

    const char *html =
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "<title>Google</title>\n"
        "<style>\n"
        "body { margin: 0; padding: 0; color: #202124; text-align: center; }\n"
        ".header { height: 40px; text-align: right; padding-right: 30px; padding-top: 15px; }\n"
        ".header a { color: #5f6368; text-decoration: none; margin-left: 15px; }\n"
        ".main { margin-top: 60px; }\n"
        ".search-form { margin: 0 auto; width: 460px; }\n"
        ".search-input { width: 460px; height: 34px; border-radius: 4px; border: 1px solid #dfe1e5; margin-bottom: 20px; }\n"
        ".btn { margin-top: 12px; margin-left: 6px; margin-right: 6px; background-color: #f8f9fa; border: 1px solid #dadce0; color: #3c4043; border-radius: 4px; padding: 8px 16px; display: inline-block; }\n"
        ".footer { margin-top: 160px; background-color: #f2f2f2; border-top: 1px solid #dadce0; padding: 15px; text-align: center; color: #70757a; font-size: 14px; }\n"
        ".footer a { color: #70757a; text-decoration: underline; margin-right: 12px; }\n"
        "</style>\n"
        "</head>\n"
        "<body>\n"
        "<div class=\"header\">\n"
        "  <a href=\"https://mail.google.com\">Gmail</a>\n"
        "  <a href=\"https://images.google.com\">Bilder</a>\n"
        "</div>\n"
        "<div class=\"main\">\n"
        "  <center>\n"
        "    <img id=\"logo\" src=\"google_logo.png\" width=\"272\" height=\"92\" alt=\"Google\">\n"
        "    <br><br>\n"
        "    <form class=\"search-form\" action=\"/search\" method=\"GET\">\n"
        "      <input type=\"text\" class=\"search-input\" name=\"q\" placeholder=\"Google Suche oder URL eingeben\">\n"
        "      <br>\n"
        "      <input type=\"submit\" name=\"btnG\" class=\"btn\" value=\"Google Suche\">\n"
        "      <input type=\"submit\" name=\"btnI\" class=\"btn\" value=\"Auf gut Glueck!\">\n"
        "    </form>\n"
        "  </center>\n"
        "</div>\n"
        "<div class=\"footer\">\n"
        "  <a href=\"/privacy\">Datenschutzerklaerung</a>\n"
        "  <a href=\"/terms\">Nutzungsbedingungen</a>\n"
        "  <a href=\"/settings\">Einstellungen</a>\n"
        "</div>\n"
        "</body>\n"
        "</html>\n";

    int parsed = html5_parse_document(html, strlen(html), "http://www.google.de");
    assert(parsed > 10);

    /* Test 1: Layout Computation */
    html5_layout_document(FB_W, FB_H);

    /* Verify Root & Default Text Color */
    assert(g_root_dom != NULL);
    assert(g_root_dom->style.color == 0x202124);

    /* Test 2: Logo Aspect Ratio and Centering */
    html_node_t *logo = html5_get_element_by_id("logo");
    assert(logo != NULL);
    assert(logo->bounds.w == 272);
    assert(logo->bounds.h == 92);
    int expected_logo_center_x = (FB_W - 272) / 2;
    assert(abs(logo->bounds.x - expected_logo_center_x) <= 20);

    /* Test 3: Search Box Centering */
    html_node_t *search_input = NULL;
    for (int i = 0; i < g_node_count; i++) {
        if (g_nodes[i].tag == HTML_TAG_INPUT && strcasecmp(g_nodes[i].input_type, "submit") != 0 && strcasecmp(g_nodes[i].input_type, "button") != 0) {
            search_input = &g_nodes[i];
            break;
        }
    }
    assert(search_input != NULL);
    assert(search_input->bounds.w == 460);
    int expected_search_x = (FB_W - 460) / 2;
    assert(abs(search_input->bounds.x - expected_search_x) <= 20);

    /* Test 4: Action Buttons Side-by-Side and Centered */
    html_node_t *btn1 = NULL, *btn2 = NULL;
    for (int i = 0; i < g_node_count; i++) {
        if (g_nodes[i].tag == HTML_TAG_INPUT && strcasecmp(g_nodes[i].input_type, "submit") == 0) {
            if (!btn1) btn1 = &g_nodes[i];
            else if (!btn2) btn2 = &g_nodes[i];
        }
    }
    assert(btn1 != NULL);
    assert(btn2 != NULL);
    char dbg[256];
    snprintf(dbg, sizeof(dbg), "  Btn1 bounds: x=%d, y=%d, w=%d, h=%d\n  Btn2 bounds: x=%d, y=%d, w=%d, h=%d\n",
             btn1->bounds.x, btn1->bounds.y, btn1->bounds.w, btn1->bounds.h,
             btn2->bounds.x, btn2->bounds.y, btn2->bounds.w, btn2->bounds.h);
    write(1, dbg, strlen(dbg));
    /* Buttons must be side-by-side on same vertical row */
    assert(btn1->bounds.y == btn2->bounds.y);
    /* Button 2 must be positioned after Button 1 without overlapping */
    assert(btn2->bounds.x >= btn1->bounds.x + btn1->bounds.w);
    /* Together they must be centered horizontally */
    int buttons_span = (btn2->bounds.x + btn2->bounds.w) - btn1->bounds.x;
    int buttons_mid = btn1->bounds.x + buttons_span / 2;
    assert(abs(buttons_mid - FB_W / 2) <= 30);

    /* Test 5: Footer Links Spacing */
    html_node_t *priv_link = NULL, *terms_link = NULL;
    for (int i = 0; i < g_node_count; i++) {
        if (g_nodes[i].tag == HTML_TAG_A && strstr(g_nodes[i].href, "privacy")) priv_link = &g_nodes[i];
        if (g_nodes[i].tag == HTML_TAG_A && strstr(g_nodes[i].href, "terms")) terms_link = &g_nodes[i];
    }
    assert(priv_link != NULL);
    assert(terms_link != NULL);
    int gap = terms_link->bounds.x - (priv_link->bounds.x + priv_link->bounds.w);
    assert(gap >= 6);

    /* Test 6: Render to Framebuffer */
    html5_render_document(0, 0, FB_W, FB_H, 0);

    /* Verify top-left pixel is opaque white (canvas background) */
    assert((g_fb[0][0] & 0x00FFFFFF) == 0x00FFFFFF);

    save_bmp("test_google_rendered.bmp");
    write(1, "[test_google_layout] Saved test_google_rendered.bmp successfully.\n", 67);
    write(1, "[test_google_layout] ALL 5 LAYOUT & VISUAL TESTS PASSED!\n", 57);

    return 0;
}
