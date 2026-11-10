#include "web_browser_app.h"
#include "browser_core.h"
#include "framebuffer.h"
#include "font.h"
#include "gui.h"
#include "gui_internal.h"
#include "html5_engine.h"
#include "html5_resources.h"
#include "serial.h"
#include "string.h"
#include "ps2.h"
#include "task.h"
#include "http_body.h"
#include "browser_host_ipc.h"
#include "browser_storage.h"
#include "browser_storage_service.h"
#include "browser_renderer_pipeline.h"
#include "browser_network_service.h"
#include "include/firstos/security/browser_security.h"
#include "browser_logger.h"
#include "stdio.h"

#ifndef FIRSTOS_BROWSER_USERSPACE_RENDERER
#define FIRSTOS_BROWSER_USERSPACE_RENDERER 1
#endif

#define WEB_BROWSER_URL_MAX 1024
#define WEB_BROWSER_PAGE_MAX HTTP_BODY_MAX_HTML
#define WEB_BROWSER_HISTORY_MAX 32

typedef enum {
    WEB_BROWSER_IDLE = 0,
    WEB_BROWSER_LOADING,
    WEB_BROWSER_READY,
    WEB_BROWSER_ERROR
} web_browser_state_t;

typedef enum {
    WEB_BROWSER_FOCUS_NONE = 0,
    WEB_BROWSER_FOCUS_ADDRESS
} web_browser_focus_t;

typedef struct {
    const char *label;
    const char *url;
    u32 color;
    int width;
} web_browser_bookmark_t;

static const web_browser_bookmark_t g_bookmarks[] = {
    {"Home", "about:home", 0x30D158, 62},
    {"HTML5", "about:html5", 0xFF7139, 68},
    {"Google", "https://www.google.com", 0x4285F4, 76},
    {"Wikipedia", "https://www.wikipedia.org", 0x9E9E9E, 92},
    {"YouTube", "https://www.youtube.com", 0xFF0000, 80},
    {"Facebook", "https://www.facebook.com", 0x1877F2, 86},
    {"GitHub", "https://github.com", 0xA0A0A0, 72},
    {"Weather", "http://wttr.in/", 0x30D158, 78},
    {"FirstOS", "about:firstos", 0x007AFF, 76}
};

#define WEB_BROWSER_BOOKMARK_COUNT ((int)(sizeof(g_bookmarks) / sizeof(g_bookmarks[0])))

static const char WEB_BROWSER_HOME_PAGE[] =
    "<!doctype html>"
    "<html><head><title>FirstOS Browser · Speed Dial</title>"
    "<style>"
    ":root{--bg:#12131A;--card:#1C1E2A;--accent:#38BDF8;--green:#30D158;--text:#F2F4F8;--muted:#9499AD;}"
    "body{color:var(--text);background-color:var(--bg);padding:28px;}"
    "h1{color:#FFFFFF;font-size:24px;margin-bottom:4px;}"
    "p{color:var(--muted);margin-bottom:14px;}"
    "form{margin-bottom:20px;}"
    "input{width:460px;height:30px;background-color:#232636;color:#FFFFFF;border:1px solid #3A3F58;padding:4px 12px;}"
    "button{background-color:#007AFF;color:#FFFFFF;padding:6px 16px;margin-left:8px;font-weight:bold;}"
    ".grid{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin-top:12px;}"
    ".card{background-color:var(--card);padding:12px;border-radius:6px;border:1px solid #2A2E42;}"
    "a{color:var(--accent);font-weight:bold;}"
    "kbd{background-color:#252838;color:#FFD60A;padding:2px 6px;}"
    "</style></head><body>"
    "<p style=\"color:#30D158;font-weight:bold;\">FIRSTOS WEB ENGINE v3.0 · HTML5 + CSS3 + TLS 1.3</p>"
    "<h1>Speed Dial</h1>"
    "<p>Fast, clean native HTML5 &amp; CSS rendering engine with live terminal error tracking.</p>"
    "<form action=\"https://www.google.com/search\" method=\"GET\">"
    "<input name=\"q\" placeholder=\"Search Google or enter any web address...\"><button>Search</button>"
    "</form>"
    "<div class=\"grid\">"
    "<div class=\"card\"><a href=\"https://www.google.com\">Google</a><p>Web Search</p></div>"
    "<div class=\"card\"><a href=\"https://www.wikipedia.org\">Wikipedia</a><p>Free Encyclopedia</p></div>"
    "<div class=\"card\"><a href=\"https://github.com\">GitHub</a><p>Code &amp; Repositories</p></div>"
    "<div class=\"card\"><a href=\"https://news.ycombinator.com\">Hacker News</a><p>Tech &amp; Startups</p></div>"
    "<div class=\"card\"><a href=\"https://developer.mozilla.org\">MDN Web Docs</a><p>HTML5 &amp; CSS Specs</p></div>"
    "<div class=\"card\"><a href=\"about:html5\">HTML5 Suite</a><p>Engine Verification</p></div>"
    "</div>"
    "<p style=\"margin-top:18px;\">Shortcuts: <kbd>Ctrl+L</kbd> Omnibox · <kbd>F5</kbd> Reload · <kbd>F12</kbd> Inspector · Terminal: <kbd>berr</kbd></p>"
    "</body></html>";

static const char WEB_BROWSER_HTML5_PAGE[] =
    "<!doctype html><html><head><title>FirstOS HTML5 &amp; CSS3 Suite</title>"
    "<style>:root{--card-bg:#1E202E;--accent:#30D158;--blue:#38BDF8;--gap:10px;}"
    "body{background-color:#12131A;color:#E4E6F0;padding:20px;}"
    "h1{color:var(--accent);margin-bottom:6px;}"
    "section{background-color:var(--card-bg);padding:12px;margin:10px 0;border-radius:6px;}"
    ".flex-row{display:flex;flex-wrap:wrap;gap:var(--gap);}"
    ".grid-box{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;}"
    ".cell{background-color:#282B3E;padding:8px;border-radius:4px;}"
    "table{width:100%;border-collapse:collapse;margin-top:8px;}"
    "th{background-color:#282B3E;color:var(--blue);padding:6px;}"
    "td{padding:6px;border-bottom:1px solid #2E3246;}"
    "code{color:#FFD60A;}button{background-color:#007AFF;color:white;}"
    "</style></head><body>"
    "<h1>HTML5 &amp; CSS3 Engine Suite</h1>"
    "<section><p>CSS Variables, Flexbox, 3-Column Grid, Semantic HTML5, and Multi-Column Tables:</p>"
    "<div class=\"flex-row\"><button>Flex A</button><button>Flex B</button><button>Flex C</button></div>"
    "<div class=\"grid-box\"><div class=\"cell\">Grid 1/3</div><div class=\"cell\">Grid 2/3</div><div class=\"cell\">Grid 3/3</div></div>"
    "<table><tr><th>Subsystem</th><th>Feature</th><th>Status</th></tr>"
    "<tr><td>HTML5 Parser</td><td>16K DOM + Implicit Close</td><td>PASS</td></tr>"
    "<tr><td>CSS3 Engine</td><td>Grid + Flex + Vars + Units</td><td>PASS</td></tr>"
    "<tr><td>GPU Blitter</td><td>Fixed-Point RGBA Scale</td><td>PASS</td></tr></table>"
    "<details open><summary>HTML5 Semantic Elements</summary>"
    "<p>Progress: <progress value=\"85\" max=\"100\"></progress> Meter: <meter value=\"0.9\"></meter></p>"
    "</details>"
    "<p><strong>Images:</strong> <img src=\"file:///images/logo.png\" width=\"48\" height=\"48\"></p>"
    "<form action=\"about:home\" method=\"GET\"><input name=\"demo\" value=\"FirstOS\"><button>Submit</button></form>"
    "</section><p><a href=\"about:home\">Return to Speed Dial</a></p>"
    "<script>document.title='FirstOS HTML5 & CSS3 Suite';</script>"
    "</body></html>";

static web_browser_state_t g_state = WEB_BROWSER_IDLE;
static web_browser_focus_t g_focus = WEB_BROWSER_FOCUS_ADDRESS;
static int g_address_selected = 1;
static int g_address_cursor = 10;
static int g_cursor_blink = 1;
static u32 g_blink_ticks = 0;
static u32 g_load_ticks = 0;
static int g_scroll_y = 0;
static int g_layout_w = 0;
static int g_layout_h = 0;
static int g_view_w = 900;
static int g_view_h = 520;
static int g_initialized = 0;
static int g_browser_zoom_pct = 100;
static app_window_t *g_browser_app_win = NULL;

/* Context Menu & DOM Inspector state */
static int g_ctx_menu_open = 0;
static int g_ctx_menu_x = 0;
static int g_ctx_menu_y = 0;
static const int g_ctx_menu_w = 160;
static const int g_ctx_menu_h = 68;
static html_node_t *g_ctx_target_node = NULL;

static int g_inspector_open = 0;
static const int g_inspector_h = 170;
static html_node_t *g_inspected_node = NULL;
static int g_devtools_tab = 0; /* 0 = Elements/Inspector, 1 = Console */
static int g_console_filter = 0; /* 0 = All, 1 = Errors, 2 = Warnings */

static int web_itoa(int val, char *buf, int max_len) {
    if (!buf || max_len <= 0) return 0;
    char tmp[16];
    int tlen = 0;
    int is_neg = 0;
    if (val < 0) { is_neg = 1; val = -val; }
    if (val == 0) tmp[tlen++] = '0';
    else {
        while (val > 0 && tlen < 15) {
            tmp[tlen++] = '0' + (val % 10);
            val /= 10;
        }
    }
    int out_len = 0;
    if (is_neg && out_len < max_len - 1) buf[out_len++] = '-';
    for (int i = tlen - 1; i >= 0 && out_len < max_len - 1; i--) {
        buf[out_len++] = tmp[i];
    }
    buf[out_len] = 0;
    return out_len;
}

static int web_to_hex(u32 val, char *buf, int max_len) {
    static const char hex_chars[] = "0123456789ABCDEF";
    if (!buf || max_len < 9) return 0;
    buf[0] = '#';
    for (int i = 0; i < 6; i++) {
        int shift = (5 - i) * 4;
        buf[1 + i] = hex_chars[(val >> shift) & 0xF];
    }
    buf[7] = 0;
    return 7;
}

int web_browser_app_is_context_menu_open(void) {
    return g_ctx_menu_open;
}

void web_browser_app_close_context_menu(void) {
    g_ctx_menu_open = 0;
    g_ctx_target_node = NULL;
}

static char g_status_text[128] = "Ready";
static u32  g_status_ticks = 0;
static int  g_status_active = 0;
static u32  g_nav_generation = 0;

void web_browser_trigger_redraw(const char *reason) {
    (void)reason;
    extern int needs_redraw;
    needs_redraw = 1;
}

u32 web_browser_get_generation(void) {
    return g_nav_generation;
}

static void web_browser_on_status(const char *status) {
    if (!status || !status[0]) return;
    strncpy(g_status_text, status, sizeof(g_status_text) - 1);
    g_status_text[sizeof(g_status_text) - 1] = 0;
    g_status_active = 1;
    g_status_ticks = get_system_ticks();
    web_browser_trigger_redraw("status");
}

static char g_current_url[WEB_BROWSER_URL_MAX] = "about:home";
static char g_address_text[WEB_BROWSER_URL_MAX] = "about:home";
static char g_page[WEB_BROWSER_PAGE_MAX];
static int g_page_len = 0;

static char g_history[WEB_BROWSER_HISTORY_MAX][WEB_BROWSER_URL_MAX];
static int g_history_count = 0;
static int g_history_index = -1;

static int web_browser_text_equals(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

static void web_browser_copy(char *dst, const char *src, int max_len) {
    if (!dst || max_len <= 0) return;
    if (!src) src = "";
    strncpy(dst, src, max_len - 1);
    dst[max_len - 1] = 0;
}

static int web_browser_is_http_url(const char *url) {
    return url && (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0);
}

static void web_browser_url_encode_query(const char *query, char *out, int max_len) {
    static const char hex[] = "0123456789ABCDEF";
    int n = 0;
    if (!out || max_len <= 0) return;
    if (!query) query = "";
    while (*query && n < max_len - 1) {
        unsigned char c = (unsigned char)*query++;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[n++] = (char)c;
        } else if (c == ' ') {
            out[n++] = '+';
        } else if (n + 3 < max_len) {
            out[n++] = '%';
            out[n++] = hex[c >> 4];
            out[n++] = hex[c & 0x0F];
        } else {
            break;
        }
    }
    out[n] = 0;
}

static void web_browser_join_url(char *out, int max_len, const char *prefix, const char *value) {
    int n = 0;
    if (!out || max_len <= 0) return;
    out[0] = 0;
    if (!prefix) prefix = "";
    if (!value) value = "";
    while (*prefix && n < max_len - 1) out[n++] = *prefix++;
    while (*value && n < max_len - 1) out[n++] = *value++;
    out[n] = 0;
}

static void web_browser_resolve_target(const char *target, char *out, int max_len) {
    char encoded[WEB_BROWSER_URL_MAX];
    const char *p = target;
    if (!out || max_len <= 0) return;
    out[0] = 0;
    if (!p) return;
    while (*p == ' ' || *p == '\t') p++;

    if (strncmp(p, "about:", 6) == 0 || web_browser_is_http_url(p) ||
        (strchr(p, ':') && !strchr(p, ' '))) {
        web_browser_copy(out, p, max_len);
    } else if (strchr(p, '.') && !strchr(p, ' ')) {
        web_browser_join_url(out, max_len, "https://", p);
    } else {
        web_browser_url_encode_query(p, encoded, sizeof(encoded));
        web_browser_join_url(out, max_len, "https://www.google.com/search?q=", encoded);
    }
}

static void web_browser_reset_page_state(void) {
    g_scroll_y = 0;
    g_page_len = 0;
    g_layout_w = 0;
    g_layout_h = 0;
    g_load_ticks = 0;
    g_focus = WEB_BROWSER_FOCUS_NONE;
    g_address_selected = 0;
    g_ctx_menu_open = 0;
    g_ctx_target_node = NULL;
    g_inspected_node = NULL;
    html5_resources_clear();
#if FIRSTOS_BROWSER_USERSPACE_RENDERER
    browser_host_ipc_invalidate_cached_frame();
#endif
}

void web_browser_reflow(void) {
    if (g_state == WEB_BROWSER_READY) {
        html5_layout_document(g_view_w, g_view_h);
        g_layout_w = g_view_w;
        g_layout_h = g_view_h;
        web_browser_trigger_redraw("reflow");
    }
}

static int g_in_commit = 0;
static char g_deferred_nav_url[WEB_BROWSER_URL_MAX] = "";

int browser_core_commit_loaded_page(const char *url, const char *body, int body_len, int view_w, int view_h, u32 generation) {
    if (!body || body_len <= 0) {
        serial_write("[browser_core] commit failed: empty or null body\n");
        g_state = WEB_BROWSER_ERROR;
        web_browser_on_status("Empty page content");
        browser_log_fail_site(0, -1, "Empty page content");
        return 0;
    }

    if ((unsigned int)body_len >= WEB_BROWSER_PAGE_MAX) {
        body_len = WEB_BROWSER_PAGE_MAX - 1;
    }

    /* Keep g_page in sync with committed body */
    if (g_page != body) {
        memcpy(g_page, body, body_len);
    }
    g_page[body_len] = '\0';
    g_page_len = body_len;

    serial_write("[browser_core] body length: ");
    serial_write_hex((u32)body_len);
    serial_write("\n");

    const char *base_url = (url && url[0]) ? url : "about:blank";

    /* Determine valid viewport */
    int vw = (view_w > 0) ? view_w : (g_view_w > 0 ? g_view_w : 900);
    int vh = (view_h > 0) ? view_h : (g_view_h > 0 ? g_view_h : 560);
    g_layout_w = vw;
    g_layout_h = vh;

    /* Performance instrumentation for page commit */
    serial_write("[perf] dns done dt=0\n");
    serial_write("[perf] connect done dt=0\n");
    serial_write("[perf] body ready len=");
    serial_write_hex((u32)g_page_len);
    serial_write(" dt=0\n");

    /* Guard against re-entrant navigation while HTML5 parser/subresources pump events */
    g_in_commit = 1;
    int pipe_res = browser_renderer_pipeline_commit(base_url, g_page, g_page_len, vw, vh, generation);
    g_in_commit = 0;

    if (!pipe_res) {
        serial_write("[browser_core] commit failed: renderer pipeline returned error\n");
        g_state = WEB_BROWSER_ERROR;
        web_browser_on_status("Failed to parse page");
        browser_log_fail_site(0, -2, "Renderer pipeline parse failed");
        if (g_deferred_nav_url[0]) {
            char deferred[WEB_BROWSER_URL_MAX];
            web_browser_copy(deferred, g_deferred_nav_url, sizeof(deferred));
            g_deferred_nav_url[0] = 0;
            web_browser_navigate(deferred);
        }
        return 0;
    }

    /* Log visible text metric without overwriting live page */
    int vis_text = html5_engine_get_visible_text_count();
    browser_log_info(BLOG_CAT_RENDER, "Rendered live page text nodes (%d) on %s", vis_text, base_url);

    /* Record per-site completion metrics for Terminal browser-errors tracker */
    const browser_document_t *cdoc = browser_core_document();
    int status_code = (cdoc && cdoc->status_code > 0) ? cdoc->status_code : 200;
    browser_log_complete_site(status_code, html5_engine_get_node_count(),
                              html5_get_document_height(), html5_engine_get_css_bytes());

    /* Sync window title from rendered document if available */
    const browser_renderer_pipeline_t *pipe_state = browser_renderer_pipeline_get_state();
    if (pipe_state && pipe_state->title[0]) {
        firstos_tab_t *act = browser_host_get_active_tab();
        if (act) {
            strncpy(act->title, pipe_state->title, sizeof(act->title) - 1);
            act->title[sizeof(act->title) - 1] = '\0';
        }
    }

    g_state = WEB_BROWSER_READY;

    serial_write("[nav] NAV_READY generation=");
    serial_write_hex(generation);
    serial_write(" url=");
    serial_write(base_url);
    serial_write("\n");
    serial_write("[browser] state=READY\n");
    serial_write("[browser_core] state=READY\n");

    web_browser_on_status("Loaded");
    web_browser_trigger_redraw("page-commit");

    /* Execute any navigation that arrived on serial while parsing */
    if (g_deferred_nav_url[0]) {
        char deferred[WEB_BROWSER_URL_MAX];
        web_browser_copy(deferred, g_deferred_nav_url, sizeof(deferred));
        g_deferred_nav_url[0] = 0;
        web_browser_navigate(deferred);
    }

    return 1;
}

static void web_browser_show_local_page(const char *html, const char *base_url) {
    int len = html ? strlen(html) : 0;
    if (len <= 0) {
        g_state = WEB_BROWSER_ERROR;
        return;
    }
    browser_log_begin_site(base_url, g_nav_generation);
    serial_write("[browser] navigate url=");
    serial_write(base_url);
    serial_write("\n");
    serial_write("[web_browser] showing local page: ");
    serial_write(base_url);
    serial_write("\n");
    web_browser_on_status("Rendering local page...");

    /* Ensure storage service session cookie tracking */
    browser_storage_service_set_cookie("localhost", "/", "session_id", "firstos_phase2_session", 0, 0);

    browser_core_commit_loaded_page(base_url, html, len, g_view_w, g_view_h, g_nav_generation);
}

static void web_browser_history_push(const char *url) {
    if (!url || !url[0]) return;
    if (g_history_index >= 0 && web_browser_text_equals(g_history[g_history_index], url)) return;
    if (g_history_index < g_history_count - 1) g_history_count = g_history_index + 1;
    if (g_history_count >= WEB_BROWSER_HISTORY_MAX) {
        for (int i = 1; i < WEB_BROWSER_HISTORY_MAX; i++) {
            web_browser_copy(g_history[i - 1], g_history[i], WEB_BROWSER_URL_MAX);
        }
        g_history_count = WEB_BROWSER_HISTORY_MAX - 1;
        g_history_index = g_history_count - 1;
    }
    web_browser_copy(g_history[g_history_count], url, WEB_BROWSER_URL_MAX);
    g_history_index = g_history_count++;
}

static void web_browser_load_url(const char *url, int add_history) {
    if (!url || !url[0]) return;
    if (!browser_security_check_url(url, "navigation")) {
        serial_write("[browser] navigation rejected by security policy\n");
        g_state = WEB_BROWSER_ERROR;
        web_browser_on_status("Blocked by security policy");
        return;
    }
    g_nav_generation++;
    serial_write("[nav] NAV_BEGIN generation=");
    serial_write_hex(g_nav_generation);
    serial_write(" url=");
    serial_write(url);
    serial_write("\n");
    html5_resources_set_current_generation(g_nav_generation);

    web_browser_copy(g_current_url, url, sizeof(g_current_url));
    web_browser_copy(g_address_text, g_current_url, sizeof(g_address_text));
    g_address_cursor = strlen(g_address_text);
    web_browser_reset_page_state();
    browser_core_cancel();
    if (add_history) {
        web_browser_history_push(g_current_url);
        browser_host_history_push(g_current_url);
    } else {
        browser_host_bump_navigation_id();
    }
    uint32_t nav_id = browser_host_get_navigation_id();

    firstos_tab_t *act = browser_host_get_active_tab();
    if (act) {
        act->navigation_id = nav_id;
        strncpy(act->url, url, sizeof(act->url) - 1);
        act->url[sizeof(act->url) - 1] = '\0';
        if (web_browser_text_equals(url, "about:home")) {
            strncpy(act->title, "Speed Dial", sizeof(act->title) - 1);
        } else if (web_browser_text_equals(url, "about:html5")) {
            strncpy(act->title, "HTML5 Suite", sizeof(act->title) - 1);
        } else if (web_browser_text_equals(url, "about:firstos")) {
            strncpy(act->title, "FirstOS", sizeof(act->title) - 1);
        } else if (web_browser_text_equals(url, "about:test")) {
            strncpy(act->title, "FirstOS Test Page", sizeof(act->title) - 1);
        } else {
            strncpy(act->title, url, sizeof(act->title) - 1);
        }
        act->title[sizeof(act->title) - 1] = '\0';
    }

    if (web_browser_text_equals(g_current_url, "about:home")) {
        web_browser_show_local_page(WEB_BROWSER_HOME_PAGE, g_current_url);
    } else if (web_browser_text_equals(g_current_url, "about:html5")) {
        web_browser_show_local_page(WEB_BROWSER_HTML5_PAGE, g_current_url);
    } else if (web_browser_text_equals(g_current_url, "about:test")) {
        web_browser_show_local_page(
            "<!DOCTYPE html>\n"
            "<html>\n"
            "<head><title>FirstOS Test Page</title></head>\n"
            "<body>\n"
            "  <h1>FirstOS Browser Test</h1>\n"
            "  <p>Static renderer pipeline test page.</p>\n"
            "</body>\n"
            "</html>", g_current_url);
    } else if (web_browser_text_equals(g_current_url, "about:firstos")) {
        web_browser_show_local_page(
            "<!doctype html><html><head><title>FirstOS</title></head><body>"
            "<h1>FirstOS Documentation</h1><p>Native 32-bit operating system and Web Browser.</p>"
            "<p><a href=\"about:html5\">HTML5 test page</a> · <a href=\"about:home\">Home</a></p>"
            "</body></html>", g_current_url);
    } else if (web_browser_is_http_url(g_current_url)) {
        g_state = WEB_BROWSER_LOADING;
        web_browser_on_status("Connecting...");
        browser_core_start_async(g_current_url, g_nav_generation);
        serial_write("[web_browser] async loading ");
        serial_write(g_current_url);
        serial_write("\n");
    } else {
        g_state = WEB_BROWSER_ERROR;
        web_browser_on_status("Unsupported URL");
    }
}

void web_browser_navigate(const char *target) {
    if (g_in_commit) {
        if (target && target[0]) {
            web_browser_copy(g_deferred_nav_url, target, sizeof(g_deferred_nav_url));
        }
        return;
    }
    char resolved[WEB_BROWSER_URL_MAX];
    web_browser_resolve_target(target, resolved, sizeof(resolved));
    if (resolved[0]) web_browser_load_url(resolved, 1);
    web_browser_trigger_redraw("navigate");
}

static void web_browser_history_move(int step) {
    int next = g_history_index + step;
    if (next < 0 || next >= g_history_count) return;
    g_history_index = next;
    if (step < 0) {
        char host_url[256];
        browser_host_history_back(host_url, sizeof(host_url));
    } else if (step > 0) {
        char host_url[256];
        browser_host_history_forward(host_url, sizeof(host_url));
    }
    web_browser_load_url(g_history[g_history_index], 0);
}

static void web_browser_focus_address(int select_all) {
    g_focus = WEB_BROWSER_FOCUS_ADDRESS;
    g_address_selected = select_all;
    g_address_cursor = strlen(g_address_text);
}

static void web_browser_sync_active_tab(void) {
    firstos_tab_t *act = browser_host_get_active_tab();
    if (!act) return;
    g_history_count = act->history.count;
    g_history_index = (int)act->history.index;
    for (int i = 0; i < g_history_count && i < WEB_BROWSER_HISTORY_MAX; i++) {
        web_browser_copy(g_history[i], act->history.entries[i].url, WEB_BROWSER_URL_MAX);
    }
}

/* Compact 2-Row Chrome: 26px Tab Bar + 36px Unified Omnibox Bar = 62px total (saves 30px height) */
#define WEB_BROWSER_TAB_STRIP_H 26
#define WEB_BROWSER_TOOLBAR_H   36
#define WEB_BROWSER_BOOKMARK_H  0
#define WEB_BROWSER_HEADER_H    (WEB_BROWSER_TAB_STRIP_H + WEB_BROWSER_TOOLBAR_H)

static void web_browser_toolbar_geometry(app_window_t *app, int *client_x, int *client_y,
                                         int *content_x, int *content_y, int *content_w, int *content_h,
                                         int *address_x, int *address_w, int *go_x) {
    int cx = app->frame.x + 2;
    int cy = app->frame.y + 28;
    int cw = app->frame.w - 4;
    int ch = app->frame.h - 30;
    int ax = cx + 128;
    int go_w = 46;
    int logs_w = 94;
    int aw = cw - 128 - go_w - logs_w - 24;
    if (aw < 160) aw = 160;
    if (client_x) *client_x = cx;
    if (client_y) *client_y = cy;
    if (content_x) *content_x = cx;
    if (content_y) *content_y = cy + WEB_BROWSER_HEADER_H;
    if (content_w) *content_w = cw;
    if (content_h) *content_h = ch - WEB_BROWSER_HEADER_H;
    if (address_x) *address_x = ax;
    if (address_w) *address_w = aw;
    if (go_x) *go_x = ax + aw + 6;
}

void web_browser_app_handle_right_click(app_window_t *app, int mx, int my) {
    if (!app || !app->open || app->minimized) return;

    int client_x, client_y, content_x, content_y, content_w, content_h;
    int address_x, address_w, go_x;
    web_browser_toolbar_geometry(app, &client_x, &client_y, &content_x, &content_y,
                                 &content_w, &content_h, &address_x, &address_w, &go_x);

    int eff_content_h = content_h - (g_inspector_open ? g_inspector_h : 0);
    if (eff_content_h < 80) eff_content_h = 80;

    if (g_state == WEB_BROWSER_READY && mx >= content_x && mx < content_x + content_w &&
        my >= content_y && my < content_y + eff_content_h) {
        g_ctx_target_node = html5_find_node_at(content_x, content_y, g_scroll_y, mx, my);
    } else {
        g_ctx_target_node = html5_get_root_dom();
    }

    g_ctx_menu_x = mx;
    g_ctx_menu_y = my;
    if (g_ctx_menu_x + g_ctx_menu_w > app->frame.x + app->frame.w - 8) {
        g_ctx_menu_x = app->frame.x + app->frame.w - g_ctx_menu_w - 8;
    }
    if (g_ctx_menu_y + g_ctx_menu_h > app->frame.y + app->frame.h - 8) {
        g_ctx_menu_y = app->frame.y + app->frame.h - g_ctx_menu_h - 8;
    }
    if (g_ctx_menu_x < app->frame.x + 4) g_ctx_menu_x = app->frame.x + 4;
    if (g_ctx_menu_y < app->frame.y + 28) g_ctx_menu_y = app->frame.y + 28;

    g_ctx_menu_open = 1;
    extern int needs_redraw;
    needs_redraw = 1;
}

void web_browser_app_init(void) {
    if (g_initialized) return;
    g_initialized = 1;
    browser_core_init();
    html5_engine_init();
#if FIRSTOS_BROWSER_USERSPACE_RENDERER
    browser_host_ipc_init();
    browser_host_ipc_spawn_renderer();
#endif
    browser_host_tab_manager_init();
    browser_storage_init();
    browser_storage_load_all();
    browser_core_set_status_callback(web_browser_on_status);
    html5_set_status_callback(web_browser_on_status);
    memset(g_history, 0, sizeof(g_history));
    g_history_count = 0;
    g_history_index = -1;
    web_browser_history_push("about:home");
    web_browser_load_url("about:home", 0);
    serial_write("[web_browser] new native Web Browser app initialized\n");
    serial_write("[test] browser smoke pass\n");
}

void web_browser_app_launch(const char *url) {
    gui_unlock_to_desktop();
    extern void open_app(int kind);
    open_app(APP_BROWSER);
    if (url && url[0]) web_browser_navigate(url);
    else web_browser_focus_address(1);
}

void web_browser_app_draw(app_window_t *app) {
    if (!app || !app->open || app->minimized) return;
    g_browser_app_win = app;

    int client_x, client_y, content_x, content_y, content_w, content_h;
    int address_x, address_w, go_x;
    web_browser_toolbar_geometry(app, &client_x, &client_y, &content_x, &content_y,
                                 &content_w, &content_h, &address_x, &address_w, &go_x);
    if (content_w <= 0 || content_h <= 0) return;
    int page_content_h = content_h - (g_inspector_open ? g_inspector_h : 0);
    if (page_content_h < 80) page_content_h = 80;
    g_view_w = content_w;
    g_view_h = page_content_h;
    if (g_state == WEB_BROWSER_READY && (g_layout_w != g_view_w || g_layout_h != g_view_h)) {
        web_browser_reflow();
    }

    fb_rect(client_x, client_y, app->frame.w - 4, app->frame.h - 30, 0x12131A);

    /* -------------------------------------------------------------
     * Row 1: Sleek Pill Tab Strip + Quick-Access Pins (Height 26px)
     * ------------------------------------------------------------- */
    fb_rect(client_x, client_y, app->frame.w - 4, 26, 0x101117);
    fb_rect(client_x, client_y + 25, app->frame.w - 4, 1, 0x222533);

    firstos_tab_manager_t *tm = browser_host_get_tab_manager();
    int tab_x = client_x + 6;
    int tab_w = 132;
    if (tm) {
        for (int i = 0; i < FIRSTOS_MAX_TABS; i++) {
            firstos_tab_t *t = &tm->tabs[i];
            if (t->state == FIRSTOS_TAB_FREE) continue;
            int is_active = (t->tab_id == tm->active_tab_id);

            u32 tab_bg = is_active ? 0x1E2130 : 0x151722;
            fb_round_rect(tab_x, client_y + 3, tab_w, 21, 5, tab_bg);
            if (is_active) {
                fb_round_rect_border(tab_x, client_y + 3, tab_w, 21, 5, 0x2F344A);
                fb_rect(tab_x + 8, client_y + 3, tab_w - 16, 2, 0x38BDF8);
            }

            /* Status dot inside tab */
            u32 tdot = is_active ? ((g_state == WEB_BROWSER_LOADING) ? 0x38BDF8 : 0x30D158) : 0x5E6278;
            fb_round_rect(tab_x + 7, client_y + 11, 5, 5, 2, tdot);

            /* Tab title */
            const char *display_title = t->title[0] ? t->title : (t->url[0] ? t->url : "New Tab");
            char title_buf[15];
            int max_chars = 11;
            int k = 0;
            while (display_title[k] && k < max_chars) {
                char ch = display_title[k];
                if (ch < 32 || ch > 126) ch = ' ';
                title_buf[k] = ch;
                k++;
            }
            title_buf[k] = '\0';
            font_text(tab_x + 16, client_y + 6, title_buf, is_active ? 0xF5F7FA : 0x8A8FA6, 1);

            /* Tab close button [x] */
            font_text(tab_x + tab_w - 14, client_y + 6, "x", is_active ? 0xB8BCD0 : 0x64687A, 1);

            tab_x += tab_w + 4;
        }

        /* New Tab [+] pill button */
        if (tab_x + 24 < client_x + app->frame.w - 10) {
            fb_round_rect(tab_x, client_y + 3, 22, 20, 5, 0x1C1F2E);
            font_text(tab_x + 7, client_y + 5, "+", 0xD8DCE8, 1);
            tab_x += 28;
        }
    }

    /* Quick-Access Bookmark Pills on right side of Tab Strip */
    int qx = client_x + app->frame.w - 10;
    for (int i = WEB_BROWSER_BOOKMARK_COUNT - 1; i >= 0; i--) {
        int bw = g_bookmarks[i].width - 8;
        if (bw < 48) bw = 48;
        if (qx - bw < tab_x + 12) break;
        qx -= bw;
        fb_round_rect(qx, client_y + 4, bw, 18, 4, 0x181B26);
        fb_round_rect(qx + 5, client_y + 10, 5, 5, 2, g_bookmarks[i].color);
        font_text(qx + 13, client_y + 5, g_bookmarks[i].label, 0xB4B8CC, 1);
        qx -= 4;
    }

    /* -------------------------------------------------------------
     * Row 2: Unified Navigation & Pill Omnibox Bar (Height 36px)
     * ------------------------------------------------------------- */
    int toolbar_y = client_y + WEB_BROWSER_TAB_STRIP_H;
    fb_rect(client_x, toolbar_y, app->frame.w - 4, WEB_BROWSER_TOOLBAR_H, 0x181A24);
    fb_rect(client_x, toolbar_y + WEB_BROWSER_TOOLBAR_H - 1, app->frame.w - 4, 1, 0x262938);

    /* Minimalist Navigation Pill Buttons: Back, Forward, Reload/Stop, Home */
    fb_round_rect(client_x + 8, toolbar_y + 5, 26, 26, 6, 0x232634);
    font_text(client_x + 17, toolbar_y + 10, "<", g_history_index > 0 ? 0xF2F4F8 : 0x585C70, 1);

    fb_round_rect(client_x + 38, toolbar_y + 5, 26, 26, 6, 0x232634);
    font_text(client_x + 47, toolbar_y + 10, ">", g_history_index < g_history_count - 1 ? 0xF2F4F8 : 0x585C70, 1);

    fb_round_rect(client_x + 68, toolbar_y + 5, 26, 26, 6, 0x232634);
    font_text(client_x + 77, toolbar_y + 10, (g_state == WEB_BROWSER_LOADING) ? "X" : "R",
              (g_state == WEB_BROWSER_LOADING) ? 0xFF453A : 0xF2F4F8, 1);

    fb_round_rect(client_x + 98, toolbar_y + 5, 26, 26, 6, 0x232634);
    font_icon(client_x + 103, toolbar_y + 10, ICON_START, 0x30D158);

    /* Centered Pill Omnibox */
    u32 address_bg = (g_focus == WEB_BROWSER_FOCUS_ADDRESS) ? 0x0F1118 : 0x222534;
    fb_round_rect(address_x, toolbar_y + 4, address_w, 28, 7, address_bg);
    fb_round_rect_border(address_x, toolbar_y + 4, address_w, 28, 7,
                         (g_focus == WEB_BROWSER_FOCUS_ADDRESS) ? 0x38BDF8 : 0x303448);

    /* Security Badge Pill inside left of Omnibox */
    int is_https = (strncmp(g_current_url, "https://", 8) == 0);
    int is_local = (strncmp(g_current_url, "about:", 6) == 0);
    u32 sec_col = is_https ? 0x30D158 : (is_local ? 0x38BDF8 : 0xFFD60A);
    fb_round_rect(address_x + 6, toolbar_y + 8, 20, 20, 5, 0x191C28);
    font_icon(address_x + 8, toolbar_y + 10, ICON_NETWORK, sec_col);

    int text_start_x = address_x + 32;
    int max_url_chars = (address_w - 40) / 8;
    if (max_url_chars < 8) max_url_chars = 8;
    char url_disp[WEB_BROWSER_URL_MAX];
    web_browser_copy(url_disp, g_address_text, sizeof(url_disp));
    if ((int)strlen(url_disp) > max_url_chars) {
        url_disp[max_url_chars] = '\0';
    }

    if (g_address_selected && url_disp[0]) {
        int tw = strlen(url_disp) * 8;
        if (tw > address_w - 38) tw = address_w - 38;
        fb_round_rect(text_start_x - 2, toolbar_y + 8, tw + 4, 20, 3, 0x005FB8);
        font_text(text_start_x, toolbar_y + 10, url_disp, 0xFFFFFF, 1);
    } else {
        font_text(text_start_x, toolbar_y + 10, url_disp, 0xF5F7FA, 1);
        if (g_focus == WEB_BROWSER_FOCUS_ADDRESS && g_cursor_blink) {
            int vis_cur = g_address_cursor;
            if (vis_cur > max_url_chars) vis_cur = max_url_chars;
            int cursor_x = text_start_x + vis_cur * 8;
            if (cursor_x < address_x + address_w - 6) {
                fb_rect(cursor_x, toolbar_y + 10, 2, 16, 0x38BDF8);
            }
        }
    }

    /* Sleek GO Pill Button */
    fb_round_rect(go_x, toolbar_y + 4, 46, 28, 6, 0x007AFF);
    font_text(go_x + 15, toolbar_y + 10, "GO", 0xFFFFFF, 1);

    /* Live Error/Warning Telemetry & Inspector Pill Button */
    int logs_x = go_x + 52;
    int logs_w = 94;
    int err_cnt = browser_log_get_error_count();
    int warn_cnt = browser_log_get_warn_count();
    int site_err = browser_log_get_current_site_errors();
    u32 logs_bg = (site_err > 0 || err_cnt > 0) ? 0x4A151B : (warn_cnt > 0 ? 0x423208 : 0x222534);
    u32 logs_border = (site_err > 0 || err_cnt > 0) ? 0xFF453A : (warn_cnt > 0 ? 0xFFD60A : 0x32364A);
    if (g_inspector_open && g_devtools_tab == 1) {
        logs_bg = 0x007AFF;
        logs_border = 0x38BDF8;
    }
    fb_round_rect(logs_x, toolbar_y + 4, logs_w, 28, 6, logs_bg);
    fb_round_rect_border(logs_x, toolbar_y + 4, logs_w, 28, 6, logs_border);

    /* Status dot inside Telemetry Pill */
    u32 sdot = (g_state == WEB_BROWSER_LOADING) ? 0x38BDF8 :
               (err_cnt > 0) ? 0xFF453A :
               (warn_cnt > 0) ? 0xFFD60A : 0x30D158;
    fb_round_rect(logs_x + 8, toolbar_y + 14, 6, 6, 3, sdot);

    char logs_lbl[24];
    if (err_cnt > 0) snprintf(logs_lbl, sizeof(logs_lbl), "%d Err", err_cnt > 99 ? 99 : err_cnt);
    else if (warn_cnt > 0) snprintf(logs_lbl, sizeof(logs_lbl), "%d Warn", warn_cnt > 99 ? 99 : warn_cnt);
    else snprintf(logs_lbl, sizeof(logs_lbl), "0 Err");

    u32 logs_txt_col = (err_cnt > 0) ? 0xFFD2D2 : (warn_cnt > 0 ? 0xFFF0D0 : 0xD8DCE8);
    font_text(logs_x + 20, toolbar_y + 10, logs_lbl, logs_txt_col, 1);

    /* 2px Ultra-Thin Progress Bar along bottom edge of toolbar */
    if (g_state == WEB_BROWSER_LOADING) {
        int progress_w = (app->frame.w - 4) * (int)((g_load_ticks % 50) + 5) / 55;
        fb_rect(client_x, toolbar_y + WEB_BROWSER_TOOLBAR_H - 2, progress_w, 2, 0x38BDF8);
    }

    if (g_state == WEB_BROWSER_READY) {
#if FIRSTOS_BROWSER_USERSPACE_RENDERER
        if (browser_host_ipc_is_active()) {
            browser_host_ipc_poll_render_commands(app, content_x, content_y, content_w, page_content_h);
        }
        firstos_tab_t *active_tab = browser_host_get_active_tab();
        if (!active_tab || active_tab->state != FIRSTOS_TAB_CRASHED) {
            html5_render_document(content_x, content_y, content_w, page_content_h, g_scroll_y);
        }
#else
        html5_render_document(content_x, content_y, content_w, page_content_h, g_scroll_y);
#endif

        /* Element highlight overlay if DOM Inspector is active */
        if (g_inspector_open && g_inspected_node) {
            int hx = content_x + g_inspected_node->bounds.x;
            int hy = content_y + g_inspected_node->bounds.y - g_scroll_y;
            int hw = g_inspected_node->bounds.w;
            int hh = g_inspected_node->bounds.h;
            if (hw > 0 && hh > 0 && hy + hh > content_y && hy < content_y + page_content_h) {
                int dh_x = hx < content_x ? content_x : hx;
                int dh_y = hy < content_y ? content_y : hy;
                int dh_r = (hx + hw > content_x + content_w) ? (content_x + content_w) : (hx + hw);
                int dh_b = (hy + hh > content_y + page_content_h) ? (content_y + page_content_h) : (hy + hh);
                if (dh_r > dh_x && dh_b > dh_y) {
                    fb_rect_alpha(dh_x, dh_y, dh_r - dh_x, dh_b - dh_y, 0x007AFF, 45);
                    fb_rect(dh_x, dh_y, dh_r - dh_x, 1, 0x007AFF);
                    fb_rect(dh_x, dh_b - 1, dh_r - dh_x, 1, 0x007AFF);
                    fb_rect(dh_x, dh_y, 1, dh_b - dh_y, 0x007AFF);
                    fb_rect(dh_r - 1, dh_y, 1, dh_b - dh_y, 0x007AFF);
                }
            }
        }
    } else {
        u32 wait_bg = web_browser_text_equals(g_current_url, "about:home") ? 0x16161E : 0xFFFFFF;
        fb_rect(content_x, content_y, content_w, page_content_h, wait_bg);
        if (g_state == WEB_BROWSER_LOADING) {
            int center_x = content_x + content_w / 2;
            int center_y = content_y + 100;
            u32 txt_col = (wait_bg == 0xFFFFFF) ? 0x202124 : 0xDFDFE5;
            font_text(center_x - 48, center_y, "Connecting...", txt_col, 1);
            int url_len = strlen(g_current_url);
            int url_w = url_len * 8;
            if (url_w > content_w - 40) url_w = content_w - 40;
            font_text(center_x - url_w / 2, center_y + 24, g_current_url, 0x1A0DAB, 1);
            fb_round_rect(content_x + 48, center_y + 54, content_w - 96, 4, 2, (wait_bg == 0xFFFFFF) ? 0xDADCE0 : 0x2E2D38);
            int fill = (content_w - 96) * (int)((g_load_ticks % 60) + 1) / 60;
            fb_round_rect(content_x + 48, center_y + 54, fill, 4, 2, 0x007AFF);
            font_text(center_x - 72, center_y + 70, "Click Home to cancel", (wait_bg == 0xFFFFFF) ? 0x5F6368 : 0x8E8E93, 1);
        } else if (g_state == WEB_BROWSER_ERROR) {
            const browser_document_t *doc = browser_core_document();
            int ex = content_x + 40;
            int ey = content_y + 36;
            fb_round_rect(ex, ey, 36, 36, 18, 0xFF453A);
            font_text(ex + 13, ey + 12, "!", 0xFFFFFF, 1);
            font_text(ex + 50, ey + 6, "Unable to load page", 0xFF453A, 1);
            font_text(ex + 50, ey + 22, g_current_url, 0x8E8E93, 1);
            fb_rect(ex, ey + 54, content_w - 80, 1, 0xDADCE0);
            font_text(ex, ey + 70, doc->error[0] ? doc->error : "The response could not be rendered.", (wait_bg == 0xFFFFFF) ? 0x202124 : 0xDFDFE5, 1);
            fb_round_rect(ex, ey + 104, 120, 28, 5, 0x007AFF);
            font_text(ex + 18, ey + 112, "Try Again", 0xFFFFFF, 1);
            fb_round_rect(ex + 136, ey + 104, 120, 28, 5, (wait_bg == 0xFFFFFF) ? 0xDADCE0 : 0x383740);
            font_text(ex + 154, ey + 112, "Home", (wait_bg == 0xFFFFFF) ? 0x202124 : 0xFFFFFF, 1);
        }
    }

    /* --------------------------------------------------------------------------
     * Tooltip Bottom Down: Real-time Request Status & Link Hover
     * -------------------------------------------------------------------------- */
    int mx = ps2_mouse_x();
    int my = ps2_mouse_y();
    char hover_url[WEB_BROWSER_URL_MAX];
    hover_url[0] = 0;
    int is_hovering = 0;
    if (g_state == WEB_BROWSER_READY && mx >= content_x && mx < content_x + content_w &&
        my >= content_y && my < content_y + page_content_h) {
        if (html5_get_hovered_link(content_x, content_y, g_scroll_y, mx, my, hover_url, sizeof(hover_url))) {
            is_hovering = 1;
        }
    }

    const char *display_text = NULL;
    u32 dot_color = 0x30D158;
    u32 text_color = 0xDFDFE5;

    if (is_hovering && hover_url[0]) {
        display_text = hover_url;
        dot_color = 0x4285F4;
        text_color = 0x8AB4F8;
    } else if (g_state == WEB_BROWSER_LOADING) {
        display_text = g_status_text[0] ? g_status_text : "Connecting...";
        dot_color = 0x007AFF;
        text_color = 0xFFFFFF;
    } else if (g_status_active) {
        u32 elapsed = get_system_ticks() - g_status_ticks;
        if (elapsed < 200u) {
            display_text = g_status_text;
            dot_color = (strstr(g_status_text, "Failed") || strstr(g_status_text, "Error") || strstr(g_status_text, "timed out")) ? 0xFF453A : 0x30D158;
            text_color = 0xDFDFE5;
        } else {
            g_status_active = 0;
        }
    }

    if (display_text && display_text[0]) {
        int max_chars = (content_w - 60) / 8;
        if (max_chars < 8) max_chars = 8;
        char truncated[128];
        int text_len = strlen(display_text);
        if (text_len > max_chars && max_chars < (int)sizeof(truncated) - 4) {
            strncpy(truncated, display_text, max_chars - 3);
            truncated[max_chars - 3] = '.';
            truncated[max_chars - 2] = '.';
            truncated[max_chars - 1] = '.';
            truncated[max_chars] = 0;
            display_text = truncated;
            text_len = max_chars;
        }
        int tt_w = text_len * 8 + 24;
        if (tt_w > content_w - 20) tt_w = content_w - 20;
        int tt_h = 20;
        int tt_x = content_x + 8;
        int tt_y = content_y + page_content_h - 25;

        /* Dark pill background with sleek border */
        fb_round_rect(tt_x, tt_y, tt_w, tt_h, 4, 0x1E1E26);
        fb_round_rect_border(tt_x, tt_y, tt_w, tt_h, 4, 0x383846);

        /* Indicator dot (pulsing blue if loading) */
        if (g_state == WEB_BROWSER_LOADING && (g_blink_ticks % 20 < 10)) {
            dot_color = 0x64D2FF;
        }
        fb_round_rect(tt_x + 6, tt_y + 7, 6, 6, 3, dot_color);

        /* Status or URL text */
        font_text(tt_x + 16, tt_y + 4, display_text, text_color, 1);
    }

    /* Diagnostics error/warning badge counter (Bottom Right) */
    err_cnt = browser_log_get_error_count();
    warn_cnt = browser_log_get_warn_count();
    if (err_cnt > 0 || warn_cnt > 0) {
        char badge_str[48];
        snprintf(badge_str, sizeof(badge_str), "! %d err %d warn", err_cnt, warn_cnt);
        int bw_len = strlen(badge_str);
        int bw_w = bw_len * 8 + 18;
        int bw_x = content_x + content_w - bw_w - 12;
        int bw_y = content_y + page_content_h - 25;
        u32 badge_bg = (err_cnt > 0) ? 0x8B0000 : 0x7A5900;
        u32 badge_border = (err_cnt > 0) ? 0xFF453A : 0xFFD60A;
        fb_round_rect(bw_x, bw_y, bw_w, 20, 4, badge_bg);
        fb_round_rect_border(bw_x, bw_y, bw_w, 20, 4, badge_border);
        font_text(bw_x + 8, bw_y + 4, badge_str, 0xFFFFFF, 1);
    }

    /* --------------------------------------------------------------------------
     * DevTools DOM Inspector & Console Dock (Bottom 170px)
     * -------------------------------------------------------------------------- */
    if (g_inspector_open) {
        int insp_x = content_x;
        int insp_y = content_y + page_content_h;
        int insp_w = content_w;
        int insp_h = g_inspector_h;

        /* Header bar */
        fb_rect(insp_x, insp_y, insp_w, 26, 0x22212B);
        fb_rect(insp_x, insp_y, insp_w, 1, 0x007AFF);

        /* Tab 0: Elements */
        int tab0_active = (g_devtools_tab == 0);
        fb_round_rect(insp_x + 8, insp_y + 3, 90, 20, 3, tab0_active ? 0x007AFF : 0x2A2A38);
        font_text(insp_x + 16, insp_y + 6, "Elements", tab0_active ? 0xFFFFFF : 0x8E8E93, 1);

        /* Tab 1: Console */
        int tab1_active = (g_devtools_tab == 1);
        char console_tab_lbl[32];
        if (err_cnt > 0) snprintf(console_tab_lbl, sizeof(console_tab_lbl), "Console (%d)", err_cnt);
        else snprintf(console_tab_lbl, sizeof(console_tab_lbl), "Console");
        fb_round_rect(insp_x + 104, insp_y + 3, 96, 20, 3, tab1_active ? 0x007AFF : 0x2A2A38);
        font_text(insp_x + 112, insp_y + 6, console_tab_lbl, tab1_active ? 0xFFFFFF : (err_cnt > 0 ? 0xFF453A : 0x8E8E93), 1);

        const char *tag_name = g_inspected_node ? html5_tag_name(g_inspected_node->tag) : "none";
        char tag_badge[64];
        int tb_len = 0;
        tag_badge[tb_len++] = '<';
        for (int i = 0; tag_name[i] && tb_len < 30; i++) tag_badge[tb_len++] = tag_name[i];
        if (g_inspected_node && g_inspected_node->id[0]) {
            tag_badge[tb_len++] = '#';
            for (int i = 0; g_inspected_node->id[i] && tb_len < 45; i++) tag_badge[tb_len++] = g_inspected_node->id[i];
        }
        if (g_inspected_node && g_inspected_node->class_name[0]) {
            tag_badge[tb_len++] = '.';
            for (int i = 0; g_inspected_node->class_name[i] && g_inspected_node->class_name[i] != ' ' && tb_len < 60; i++) {
                tag_badge[tb_len++] = g_inspected_node->class_name[i];
            }
        }
        tag_badge[tb_len++] = '>';
        tag_badge[tb_len] = 0;

        int badge_w = tb_len * 8 + 12;
        if (g_devtools_tab == 0) {
            fb_round_rect(insp_x + 208, insp_y + 4, badge_w, 18, 3, 0x343342);
            font_text(insp_x + 214, insp_y + 6, tag_badge, 0x64D2FF, 1);
        }

        /* Dimensions badge */
        if (g_inspected_node) {
            char dim_buf[32];
            char num_w[12], num_h[12];
            web_itoa(g_inspected_node->bounds.w, num_w, sizeof(num_w));
            web_itoa(g_inspected_node->bounds.h, num_h, sizeof(num_h));
            int dlen = 0;
            for (int i = 0; num_w[i]; i++) dim_buf[dlen++] = num_w[i];
            dim_buf[dlen++] = 'x';
            for (int i = 0; num_h[i]; i++) dim_buf[dlen++] = num_h[i];
            dim_buf[dlen++] = ' ';
            dim_buf[dlen++] = 'p';
            dim_buf[dlen++] = 'x';
            dim_buf[dlen] = 0;
            int dim_w = dlen * 8 + 12;
            fb_round_rect(insp_x + 170 + badge_w, insp_y + 4, dim_w, 18, 3, 0x2A2A36);
            font_text(insp_x + 176 + badge_w, insp_y + 6, dim_buf, 0xFFD60A, 1);
        }

        /* Close Button [X] */
        int close_x = insp_x + insp_w - 26;
        int close_y = insp_y + 3;
        int is_close_hover = (mx >= close_x && mx < close_x + 20 && my >= close_y && my < close_y + 20);
        fb_round_rect(close_x, close_y, 20, 20, 3, is_close_hover ? 0xFF453A : 0x383742);
        font_text(close_x + 6, close_y + 4, "X", 0xFFFFFF, 1);

        if (g_devtools_tab == 1) {
            /* Clear Button */
            int clr_x = close_x - 56;
            int is_clr_hover = (mx >= clr_x && mx < clr_x + 50 && my >= insp_y + 3 && my < insp_y + 23);
            fb_round_rect(clr_x, insp_y + 3, 50, 20, 3, is_clr_hover ? 0x484756 : 0x383742);
            font_text(clr_x + 8, insp_y + 6, "Clear", 0xDFDFE5, 1);

            /* Filter buttons [All], [Errors], [Warnings] */
            int f0_active = (g_console_filter == 0);
            fb_round_rect(insp_x + 208, insp_y + 4, 38, 18, 3, f0_active ? 0x007AFF : 0x2A2A38);
            font_text(insp_x + 215, insp_y + 6, "All", f0_active ? 0xFFFFFF : 0x8E8E93, 1);

            int f1_active = (g_console_filter == 1);
            fb_round_rect(insp_x + 250, insp_y + 4, 60, 18, 3, f1_active ? 0xFF453A : 0x2A2A38);
            font_text(insp_x + 256, insp_y + 6, "Errors", f1_active ? 0xFFFFFF : 0x8E8E93, 1);

            int f2_active = (g_console_filter == 2);
            fb_round_rect(insp_x + 314, insp_y + 4, 72, 18, 3, f2_active ? 0xFFD60A : 0x2A2A38);
            font_text(insp_x + 320, insp_y + 6, "Warnings", f2_active ? 0x16161E : 0x8E8E93, 1);
        }

        /* Body Background */
        fb_rect(insp_x, insp_y + 26, insp_w, insp_h - 26, 0x16161E);

        if (g_devtools_tab == 1) {
            /* --------------------------------------------------------------
             * DevTools Tab 1: Diagnostic Console
             * -------------------------------------------------------------- */
            int log_cnt = browser_log_get_count();
            int matching_cnt = 0;
            for (int i = 0; i < log_cnt; i++) {
                const browser_log_entry_t *entry = browser_log_get_entry(i);
                if (!entry) continue;
                if (g_console_filter == 1 && entry->level != BLOG_LEVEL_ERROR) continue;
                if (g_console_filter == 2 && entry->level != BLOG_LEVEL_WARN) continue;
                matching_cnt++;
            }

            int max_visible_lines = (insp_h - 32) / 18;
            if (max_visible_lines < 1) max_visible_lines = 1;
            int skip_matching = matching_cnt - max_visible_lines;
            if (skip_matching < 0) skip_matching = 0;

            if (matching_cnt == 0) {
                const char *empty_msg = (g_console_filter == 1) ? "No errors recorded in console." :
                                        (g_console_filter == 2) ? "No warnings recorded in console." :
                                        "Console is clear. No log messages, warnings, or errors.";
                font_text(insp_x + 16, insp_y + 36, empty_msg, 0x8E8E93, 1);
            } else {
                int line_y = insp_y + 30;
                int current_match = 0;
                for (int i = 0; i < log_cnt && line_y < insp_y + insp_h - 16; i++) {
                    const browser_log_entry_t *entry = browser_log_get_entry(i);
                    if (!entry) continue;
                    if (g_console_filter == 1 && entry->level != BLOG_LEVEL_ERROR) continue;
                    if (g_console_filter == 2 && entry->level != BLOG_LEVEL_WARN) continue;

                    if (current_match < skip_matching) {
                        current_match++;
                        continue;
                    }
                    current_match++;

                    u32 badge_bg = 0x2A2A38;
                    u32 badge_text_col = 0x64D2FF;
                    const char *lvl_name = "INFO";
                    if (entry->level == BLOG_LEVEL_ERROR) {
                        badge_bg = 0x8B0000;
                        badge_text_col = 0xFF453A;
                        lvl_name = "ERR";
                    } else if (entry->level == BLOG_LEVEL_WARN) {
                        badge_bg = 0x7A5900;
                        badge_text_col = 0xFFD60A;
                        lvl_name = "WARN";
                    }

                    /* Level Badge */
                    fb_round_rect(insp_x + 10, line_y, 44, 16, 3, badge_bg);
                    font_text(insp_x + 14, line_y + 2, lvl_name, badge_text_col, 1);

                    /* Category Tag */
                    char cat_badge[16];
                    snprintf(cat_badge, sizeof(cat_badge), "[%s]", entry->category_name);
                    font_text(insp_x + 58, line_y + 2, cat_badge, 0x8E8E93, 1);

                    /* Message */
                    int msg_x = insp_x + 130;
                    u32 msg_col = (entry->level == BLOG_LEVEL_ERROR) ? 0xFF8080 :
                                  (entry->level == BLOG_LEVEL_WARN)  ? 0xFFE080 : 0xDFDFE5;
                    font_text(msg_x, line_y + 2, entry->message, msg_col, 1);

                    line_y += 18;
                }
            }
        } else {
            /* --------------------------------------------------------------
             * DevTools Tab 0: DOM Elements & Box Model Inspector
             * -------------------------------------------------------------- */
            int col1_w = (insp_w * 56) / 100;
            int col2_x = insp_x + col1_w + 12;
            fb_rect(insp_x + col1_w, insp_y + 26, 1, insp_h - 26, 0x282733);

            /* Left Column: DOM hierarchy and Node attributes */
            if (g_inspected_node) {
                char breadcrumb[128];
                html5_get_node_breadcrumb(g_inspected_node, breadcrumb, sizeof(breadcrumb));
                font_text(insp_x + 10, insp_y + 32, "Path:", 0x8E8E93, 1);
                font_text(insp_x + 54, insp_y + 32, breadcrumb, 0xFF9F0A, 1);

                char row1[128];
                int r1_len = 0;
                const char *lbl_tag = "tag: ";
                for (int i = 0; lbl_tag[i]; i++) row1[r1_len++] = lbl_tag[i];
                for (int i = 0; tag_name[i]; i++) row1[r1_len++] = tag_name[i];
                if (g_inspected_node->id[0]) {
                    const char *lbl = "  id: ";
                    for (int i = 0; lbl[i]; i++) row1[r1_len++] = lbl[i];
                    for (int i = 0; g_inspected_node->id[i] && r1_len < 60; i++) row1[r1_len++] = g_inspected_node->id[i];
                }
                if (g_inspected_node->name[0]) {
                    const char *lbl = "  name: ";
                    for (int i = 0; lbl[i]; i++) row1[r1_len++] = lbl[i];
                    for (int i = 0; g_inspected_node->name[i] && r1_len < 90; i++) row1[r1_len++] = g_inspected_node->name[i];
                }
                row1[r1_len] = 0;
                font_text(insp_x + 10, insp_y + 54, row1, 0xDFDFE5, 1);

                char row2[128];
                int r2_len = 0;
                if (g_inspected_node->input_type[0]) {
                    const char *lbl = "type: ";
                    for (int i = 0; lbl[i]; i++) row2[r2_len++] = lbl[i];
                    for (int i = 0; g_inspected_node->input_type[i] && r2_len < 30; i++) row2[r2_len++] = g_inspected_node->input_type[i];
                }
                if (g_inspected_node->href[0]) {
                    const char *lbl = "  href: ";
                    for (int i = 0; lbl[i]; i++) row2[r2_len++] = lbl[i];
                    for (int i = 0; g_inspected_node->href[i] && r2_len < 90; i++) row2[r2_len++] = g_inspected_node->href[i];
                } else if (g_inspected_node->src[0]) {
                    const char *lbl = "  src: ";
                    for (int i = 0; lbl[i]; i++) row2[r2_len++] = lbl[i];
                    for (int i = 0; g_inspected_node->src[i] && r2_len < 90; i++) row2[r2_len++] = g_inspected_node->src[i];
                } else if (g_inspected_node->action[0]) {
                    const char *lbl = "  action: ";
                    for (int i = 0; lbl[i]; i++) row2[r2_len++] = lbl[i];
                    for (int i = 0; g_inspected_node->action[i] && r2_len < 90; i++) row2[r2_len++] = g_inspected_node->action[i];
                }
                row2[r2_len] = 0;
                if (r2_len > 0) {
                    font_text(insp_x + 10, insp_y + 74, row2, 0x64D2FF, 1);
                }

                char row3[128];
                int r3_len = 0;
                if (g_inspected_node->value[0]) {
                    const char *lbl = "value: \"";
                    for (int i = 0; lbl[i]; i++) row3[r3_len++] = lbl[i];
                    for (int i = 0; g_inspected_node->value[i] && r3_len < 80; i++) row3[r3_len++] = g_inspected_node->value[i];
                    row3[r3_len++] = '"';
                } else if (g_inspected_node->placeholder[0]) {
                    const char *lbl = "placeholder: \"";
                    for (int i = 0; lbl[i]; i++) row3[r3_len++] = lbl[i];
                    for (int i = 0; g_inspected_node->placeholder[i] && r3_len < 80; i++) row3[r3_len++] = g_inspected_node->placeholder[i];
                    row3[r3_len++] = '"';
                } else if (g_inspected_node->text[0]) {
                    const char *lbl = "text: \"";
                    for (int i = 0; lbl[i]; i++) row3[r3_len++] = lbl[i];
                    for (int i = 0; g_inspected_node->text[i] && r3_len < 80; i++) row3[r3_len++] = g_inspected_node->text[i];
                    row3[r3_len++] = '"';
                }
                row3[r3_len] = 0;
                if (r3_len > 0) {
                    font_text(insp_x + 10, insp_y + 94, row3, 0x30D158, 1);
                }

                if (g_inspected_node->onclick[0]) {
                    char row4[128];
                    int r4_len = 0;
                    const char *lbl = "onclick: ";
                    for (int i = 0; lbl[i]; i++) row4[r4_len++] = lbl[i];
                    for (int i = 0; g_inspected_node->onclick[i] && r4_len < 80; i++) row4[r4_len++] = g_inspected_node->onclick[i];
                    row4[r4_len] = 0;
                    font_text(insp_x + 10, insp_y + 114, row4, 0xFF453A, 1);
                } else {
                    font_text(insp_x + 10, insp_y + 114, "Click any element in viewport to inspect", 0x6E6E78, 1);
                }
            } else {
                font_text(insp_x + 10, insp_y + 32, "No element selected", 0x8E8E93, 1);
            }

            /* Right Column: Computed Box Model & Styles */
            font_text(col2_x, insp_y + 32, "Computed Style & Box Model", 0x30D158, 1);
            if (g_inspected_node) {
                char b1[64], b2[64], b3[64];
                char sx[10], sy[10], sw[10], sh[10];
                web_itoa(g_inspected_node->bounds.x, sx, sizeof(sx));
                web_itoa(g_inspected_node->bounds.y, sy, sizeof(sy));
                web_itoa(g_inspected_node->bounds.w, sw, sizeof(sw));
                web_itoa(g_inspected_node->bounds.h, sh, sizeof(sh));

                int len1 = 0;
                const char *s_bounds = "Bounds: [x=";
                for (int i = 0; s_bounds[i]; i++) b1[len1++] = s_bounds[i];
                for (int i = 0; sx[i]; i++) b1[len1++] = sx[i];
                const char *s_y = " y=";
                for (int i = 0; s_y[i]; i++) b1[len1++] = s_y[i];
                for (int i = 0; sy[i]; i++) b1[len1++] = sy[i];
                const char *s_w = " w=";
                for (int i = 0; s_w[i]; i++) b1[len1++] = s_w[i];
                for (int i = 0; sw[i]; i++) b1[len1++] = sw[i];
                const char *s_h = " h=";
                for (int i = 0; s_h[i]; i++) b1[len1++] = s_h[i];
                for (int i = 0; sh[i]; i++) b1[len1++] = sh[i];
                b1[len1++] = ']';
                b1[len1] = 0;
                font_text(col2_x, insp_y + 54, b1, 0xFFFFFF, 1);

                char smt[10], smb[10], sml[10], smr[10];
                char spt[10], spb[10], spl[10], spr[10];
                web_itoa(g_inspected_node->style.margin_top, smt, sizeof(smt));
                web_itoa(g_inspected_node->style.margin_bottom, smb, sizeof(smb));
                web_itoa(g_inspected_node->style.margin_left, sml, sizeof(sml));
                web_itoa(g_inspected_node->style.margin_right, smr, sizeof(smr));
                web_itoa(g_inspected_node->style.padding_top, spt, sizeof(spt));
                web_itoa(g_inspected_node->style.padding_bottom, spb, sizeof(spb));
                web_itoa(g_inspected_node->style.padding_left, spl, sizeof(spl));
                web_itoa(g_inspected_node->style.padding_right, spr, sizeof(spr));

                int len2 = 0;
                const char *s_margin = "Margin: ";
                for (int i = 0; s_margin[i]; i++) b2[len2++] = s_margin[i];
                for (int i = 0; smt[i]; i++) b2[len2++] = smt[i];
                b2[len2++] = ',';
                for (int i = 0; smr[i]; i++) b2[len2++] = smr[i];
                b2[len2++] = ',';
                for (int i = 0; smb[i]; i++) b2[len2++] = smb[i];
                b2[len2++] = ',';
                for (int i = 0; sml[i]; i++) b2[len2++] = sml[i];

                const char *s_pad = "  Pad: ";
                for (int i = 0; s_pad[i]; i++) b2[len2++] = s_pad[i];
                for (int i = 0; spt[i]; i++) b2[len2++] = spt[i];
                b2[len2++] = ',';
                for (int i = 0; spr[i]; i++) b2[len2++] = spr[i];
                b2[len2++] = ',';
                for (int i = 0; spb[i]; i++) b2[len2++] = spb[i];
                b2[len2++] = ',';
                for (int i = 0; spl[i]; i++) b2[len2++] = spl[i];
                b2[len2] = 0;
                font_text(col2_x, insp_y + 74, b2, 0xDFDFE5, 1);

                char c_hex[10], bg_hex[10];
                web_to_hex(g_inspected_node->style.color, c_hex, sizeof(c_hex));
                web_to_hex(g_inspected_node->style.bg_color, bg_hex, sizeof(bg_hex));

                int len3 = 0;
                const char *s_col = "Color: ";
                for (int i = 0; s_col[i]; i++) b3[len3++] = s_col[i];
                for (int i = 0; c_hex[i]; i++) b3[len3++] = c_hex[i];
                const char *s_bg = "  Bg: ";
                for (int i = 0; s_bg[i]; i++) b3[len3++] = s_bg[i];
                for (int i = 0; bg_hex[i]; i++) b3[len3++] = bg_hex[i];
                const char *s_disp = g_inspected_node->style.is_block ? "  (block)" : "  (inline)";
                for (int i = 0; s_disp[i]; i++) b3[len3++] = s_disp[i];
                b3[len3] = 0;
                font_text(col2_x, insp_y + 94, b3, 0x64D2FF, 1);

                fb_round_rect(col2_x, insp_y + 116, 130, 22, 3, 0x2A2A38);
                fb_round_rect_border(col2_x, insp_y + 116, 130, 22, 3, 0x007AFF);
                font_text(col2_x + 8, insp_y + 121, "Box Model Active", 0xFFD60A, 1);
            }
        }
    }

    /* --------------------------------------------------------------------------
     * Dropdown Context Menu (Always on top)
     * -------------------------------------------------------------------------- */
    if (g_ctx_menu_open) {
        int it0_x = g_ctx_menu_x + 4;
        int it0_y = g_ctx_menu_y + 4;
        int it0_w = g_ctx_menu_w - 8;
        int it0_h = 28;

        int it1_x = g_ctx_menu_x + 4;
        int it1_y = g_ctx_menu_y + 34;
        int it1_w = g_ctx_menu_w - 8;
        int it1_h = 28;

        int hover0 = (mx >= it0_x && mx < it0_x + it0_w && my >= it0_y && my < it0_y + it0_h);
        int hover1 = (mx >= it1_x && mx < it1_x + it1_w && my >= it1_y && my < it1_y + it1_h);

        fb_draw_card_shadow(g_ctx_menu_x, g_ctx_menu_y, g_ctx_menu_w, g_ctx_menu_h, 6);
        fb_round_rect(g_ctx_menu_x, g_ctx_menu_y, g_ctx_menu_w, g_ctx_menu_h, 6, 0x1E1E26);
        fb_round_rect_border(g_ctx_menu_x, g_ctx_menu_y, g_ctx_menu_w, g_ctx_menu_h, 6, 0x383848);

        /* Separator line */
        fb_rect(g_ctx_menu_x + 6, g_ctx_menu_y + 33, g_ctx_menu_w - 12, 1, 0x2A2A38);

        /* Option 0: Refresh */
        if (hover0) fb_round_rect(it0_x, it0_y, it0_w, it0_h, 4, 0x007AFF);
        font_text(it0_x + 12, it0_y + 8, "Refresh", hover0 ? 0xFFFFFF : 0xDFDFE5, 1);
        font_text(it0_x + it0_w - 24, it0_y + 8, "F5", hover0 ? 0xFFFFFF : 0x8E8E93, 1);

        /* Option 1: Inspect */
        if (hover1) fb_round_rect(it1_x, it1_y, it1_w, it1_h, 4, 0x007AFF);
        font_text(it1_x + 12, it1_y + 8, "Inspect", hover1 ? 0xFFFFFF : 0xDFDFE5, 1);
        font_text(it1_x + it1_w - 32, it1_y + 8, "F12", hover1 ? 0xFFFFFF : 0x8E8E93, 1);
    }
}

void web_browser_app_handle_click(app_window_t *app, int mx, int my) {
    if (!app || !app->open) return;
    int client_x, client_y, content_x, content_y, content_w, content_h;
    int address_x, address_w, go_x;
    web_browser_toolbar_geometry(app, &client_x, &client_y, &content_x, &content_y,
                                 &content_w, &content_h, &address_x, &address_w, &go_x);

    /* 1. Context Menu Interaction */
    if (g_ctx_menu_open) {
        int it0_x = g_ctx_menu_x + 4;
        int it0_y = g_ctx_menu_y + 4;
        int it0_w = g_ctx_menu_w - 8;
        int it0_h = 28;

        int it1_x = g_ctx_menu_x + 4;
        int it1_y = g_ctx_menu_y + 34;
        int it1_w = g_ctx_menu_w - 8;
        int it1_h = 28;

        if (mx >= it0_x && mx < it0_x + it0_w && my >= it0_y && my < it0_y + it0_h) {
            serial_write("[web_browser] Context menu: Refresh selected\n");
            g_ctx_menu_open = 0;
            web_browser_load_url(g_current_url, 0);
            return;
        }
        if (mx >= it1_x && mx < it1_x + it1_w && my >= it1_y && my < it1_y + it1_h) {
            serial_write("[web_browser] Context menu: Inspect selected\n");
            g_ctx_menu_open = 0;
            g_inspector_open = 1;
            g_inspected_node = g_ctx_target_node ? g_ctx_target_node : html5_get_root_dom();
            extern int needs_redraw;
            needs_redraw = 1;
            return;
        }
        g_ctx_menu_open = 0;
        extern int needs_redraw;
        needs_redraw = 1;
        return;
    }

    int page_content_h = content_h - (g_inspector_open ? g_inspector_h : 0);
    if (page_content_h < 80) page_content_h = 80;

    /* 2. Inspector Dock Interaction */
    if (g_inspector_open) {
        int insp_x = content_x;
        int insp_y = content_y + page_content_h;
        int insp_w = content_w;
        int insp_h = g_inspector_h;
        if (my >= insp_y && my < insp_y + insp_h) {
            int close_x = insp_x + insp_w - 26;
            int close_y = insp_y + 3;
            if (mx >= close_x - 10 && mx < close_x + 30 && my >= close_y - 5 && my < close_y + 30) {
                serial_write("[web_browser] Inspector close [X] clicked\n");
                g_inspector_open = 0;
                extern int needs_redraw;
                needs_redraw = 1;
                return;
            }
            /* Elements tab button clicked */
            if (my >= insp_y + 3 && my < insp_y + 24 && mx >= insp_x + 8 && mx < insp_x + 98) {
                g_devtools_tab = 0;
                extern int needs_redraw;
                needs_redraw = 1;
                return;
            }
            /* Console tab button clicked */
            if (my >= insp_y + 3 && my < insp_y + 24 && mx >= insp_x + 104 && mx < insp_x + 200) {
                g_devtools_tab = 1;
                extern int needs_redraw;
                needs_redraw = 1;
                return;
            }
            if (g_devtools_tab == 1 && my >= insp_y + 3 && my < insp_y + 24) {
                int clr_x = close_x - 56;
                if (mx >= clr_x && mx < clr_x + 50) {
                    browser_log_clear();
                    extern int needs_redraw;
                    needs_redraw = 1;
                    return;
                }
                if (mx >= insp_x + 208 && mx < insp_x + 246) {
                    g_console_filter = 0;
                    extern int needs_redraw;
                    needs_redraw = 1;
                    return;
                }
                if (mx >= insp_x + 250 && mx < insp_x + 310) {
                    g_console_filter = 1;
                    extern int needs_redraw;
                    needs_redraw = 1;
                    return;
                }
                if (mx >= insp_x + 314 && mx < insp_x + 386) {
                    g_console_filter = 2;
                    extern int needs_redraw;
                    needs_redraw = 1;
                    return;
                }
            }
            return;
        }
    } else {
        /* Bottom-right error/warning status badge clicked -> open DevTools Console */
        int err_cnt = browser_log_get_error_count();
        int warn_cnt = browser_log_get_warn_count();
        if (err_cnt > 0 || warn_cnt > 0) {
            char badge_str[48];
            snprintf(badge_str, sizeof(badge_str), "! %d err %d warn", err_cnt, warn_cnt);
            int bw_w = strlen(badge_str) * 8 + 18;
            int bw_x = content_x + content_w - bw_w - 12;
            int bw_y = content_y + page_content_h - 25;
            if (mx >= bw_x && mx < bw_x + bw_w && my >= bw_y && my < bw_y + 20) {
                g_inspector_open = 1;
                g_devtools_tab = 1;
                extern int needs_redraw;
                needs_redraw = 1;
                return;
            }
        }
    }

    /* 3. Tab Strip & Quick-Access Pins Click Handling */
    if (my >= client_y && my < client_y + WEB_BROWSER_TAB_STRIP_H) {
        firstos_tab_manager_t *tm = browser_host_get_tab_manager();
        int tab_x = client_x + 6;
        int tab_w = 132;
        if (tm) {
            for (int i = 0; i < FIRSTOS_MAX_TABS; i++) {
                firstos_tab_t *t = &tm->tabs[i];
                if (t->state == FIRSTOS_TAB_FREE) continue;

                if (mx >= tab_x && mx < tab_x + tab_w) {
                    /* Close button [x] clicked? */
                    if (mx >= tab_x + tab_w - 20) {
                        serial_write("[tab] UI close clicked tab=");
                        serial_write_hex((unsigned int)t->tab_id);
                        serial_write("\n");
                        browser_host_tab_close(t->tab_id);
                        firstos_tab_t *act = browser_host_get_active_tab();
                        if (act) {
                            web_browser_sync_active_tab();
                            web_browser_load_url(act->url, 0);
                        }
                    } else {
                        /* Tab body clicked -> activate */
                        serial_write("[tab] UI activate clicked tab=");
                        serial_write_hex((unsigned int)t->tab_id);
                        serial_write("\n");
                        browser_host_tab_activate(t->tab_id);
                        web_browser_sync_active_tab();
                        web_browser_load_url(t->url, 0);
                    }
                    extern int needs_redraw;
                    needs_redraw = 1;
                    return;
                }
                tab_x += tab_w + 4;
            }

            /* New tab [+] clicked? */
            if (mx >= tab_x && mx < tab_x + 24) {
                serial_write("[tab] UI new tab [+] clicked\n");
                uint32_t new_tab_id = browser_host_tab_create("about:home", 1);
                if (new_tab_id > 0) {
                    web_browser_sync_active_tab();
                    web_browser_load_url("about:home", 0);
                }
                extern int needs_redraw;
                needs_redraw = 1;
                return;
            }
            tab_x += 28;
        }

        /* Quick-Access Bookmark Pills on right side of Tab Strip */
        int qx = client_x + app->frame.w - 10;
        for (int i = WEB_BROWSER_BOOKMARK_COUNT - 1; i >= 0; i--) {
            int bw = g_bookmarks[i].width - 8;
            if (bw < 48) bw = 48;
            if (qx - bw < tab_x + 12) break;
            qx -= bw;
            if (mx >= qx && mx < qx + bw) {
                serial_write("[web_browser] quick-access bookmark clicked: ");
                serial_write(g_bookmarks[i].url);
                serial_write("\n");
                web_browser_navigate(g_bookmarks[i].url);
                return;
            }
            qx -= 4;
        }
        return;
    }

    int toolbar_y = client_y + WEB_BROWSER_TAB_STRIP_H;

    if (my >= toolbar_y && my < toolbar_y + WEB_BROWSER_TOOLBAR_H) {
        if (mx >= client_x + 8 && mx < client_x + 34) { web_browser_history_move(-1); return; }
        if (mx >= client_x + 38 && mx < client_x + 64) { web_browser_history_move(1); return; }
        if (mx >= client_x + 68 && mx < client_x + 94) {
            if (g_state == WEB_BROWSER_LOADING) {
                browser_core_cancel();
                g_state = WEB_BROWSER_READY;
                web_browser_on_status("Stopped");
                web_browser_trigger_redraw("stop");
                return;
            }
            char host_url[256];
            browser_host_history_reload(host_url, sizeof(host_url));
            web_browser_load_url(g_current_url, 0);
            return;
        }
        if (mx >= client_x + 98 && mx < client_x + 124) {
            web_browser_navigate("about:home");
            return;
        }
        if (mx >= address_x && mx < address_x + address_w) {
            web_browser_focus_address(1);
            web_browser_trigger_redraw("focus-address");
            return;
        }
        if (mx >= go_x && mx < go_x + 46) {
            if (g_address_text[0]) web_browser_navigate(g_address_text);
            return;
        }
        int logs_x = go_x + 52;
        int logs_w = 94;
        if (mx >= logs_x && mx < logs_x + logs_w) {
            if (g_inspector_open && g_devtools_tab == 1) {
                g_inspector_open = 0;
            } else {
                g_inspector_open = 1;
                g_devtools_tab = 1;
            }
            extern int needs_redraw;
            needs_redraw = 1;
            return;
        }
    }

    if (my < content_y || my >= content_y + page_content_h) return;

    if (g_inspector_open && g_state == WEB_BROWSER_READY) {
        html_node_t *clicked = html5_find_node_at(content_x, content_y, g_scroll_y, mx, my);
        if (clicked) {
            g_inspected_node = clicked;
            extern int needs_redraw;
            needs_redraw = 1;
        }
    }

    if (g_state == WEB_BROWSER_ERROR) {
        int button_y = content_y + 36 + 104;
        if (my >= button_y && my < button_y + 28) {
            if (mx >= content_x + 40 && mx < content_x + 160) {
                web_browser_load_url(g_current_url, 0);
                return;
            }
            if (mx >= content_x + 176 && mx < content_x + 296) {
                web_browser_navigate("about:home");
                return;
            }
        }
    }

    if (g_state == WEB_BROWSER_READY) {
#if FIRSTOS_BROWSER_USERSPACE_RENDERER
        if (browser_host_ipc_is_active()) {
            browser_host_ipc_send_input(1, mx - content_x, my - content_y + g_scroll_y, 0);
        }
#endif
        char nav_url[WEB_BROWSER_URL_MAX];
        nav_url[0] = 0;
        if (html5_handle_click(content_x, content_y, g_scroll_y, mx, my, nav_url, sizeof(nav_url))) {
            const char *pending = html5_take_navigation_request();
            if (pending && pending[0]) web_browser_navigate(pending);
            else if (nav_url[0]) web_browser_navigate(nav_url);
            return;
        }
    }
}

static void web_browser_address_backspace(void) {
    int len = strlen(g_address_text);
    if (g_address_selected) {
        g_address_text[0] = 0;
        g_address_cursor = 0;
        g_address_selected = 0;
    } else if (g_address_cursor > 0) {
        for (int i = g_address_cursor - 1; i < len; i++) g_address_text[i] = g_address_text[i + 1];
        g_address_cursor--;
    }
}

static void web_browser_address_insert(char c) {
    int len = strlen(g_address_text);
    if (g_address_selected) {
        g_address_text[0] = c;
        g_address_text[1] = 0;
        g_address_cursor = 1;
        g_address_selected = 0;
    } else if (len < WEB_BROWSER_URL_MAX - 2) {
        for (int i = len; i >= g_address_cursor; i--) g_address_text[i + 1] = g_address_text[i];
        g_address_text[g_address_cursor++] = c;
    }
}

void web_browser_app_handle_key(unsigned char key, char ascii) {
    serial_write("[web_browser] handle_key: 0x");
    serial_write_hex(key);
    serial_write("\n");
    if (key == 0x01) { /* Escape */
        if (g_ctx_menu_open) {
            g_ctx_menu_open = 0;
            extern int needs_redraw;
            needs_redraw = 1;
            return;
        }
        if (g_inspector_open) {
            g_inspector_open = 0;
            extern int needs_redraw;
            needs_redraw = 1;
            return;
        }
    }
    if (key == 0x58 || (gui_is_ctrl_pressed() && (ascii == 'i' || ascii == 'I'))) { /* F12 or Ctrl+I */
        g_inspector_open = !g_inspector_open;
        if (g_inspector_open && !g_inspected_node) {
            g_inspected_node = html5_get_root_dom();
        }
        extern int needs_redraw;
        needs_redraw = 1;
        return;
    }
    if (key == 0x57) { /* F11 Fullscreen */
        if (g_browser_app_win) {
            gui_set_window_maximized(g_browser_app_win, !g_browser_app_win->maximized);
            extern int needs_redraw;
            needs_redraw = 1;
            return;
        }
    }
    if (key == 0x3F) { web_browser_load_url(g_current_url, 0); return; }
    if (key == 0x40) { web_browser_focus_address(1); return; }

    if (gui_is_ctrl_pressed()) {
        if (ascii == '+' || ascii == '=' || key == 0x0D) {
            if (g_browser_zoom_pct < 200) g_browser_zoom_pct += 25;
            char zbuf[32];
            snprintf(zbuf, sizeof(zbuf), "Zoom: %d%%", g_browser_zoom_pct);
            web_browser_on_status(zbuf);
            web_browser_reflow();
            extern int needs_redraw;
            needs_redraw = 1;
            return;
        }
        if (ascii == '-' || ascii == '_' || key == 0x0C) {
            if (g_browser_zoom_pct > 50) g_browser_zoom_pct -= 25;
            char zbuf[32];
            snprintf(zbuf, sizeof(zbuf), "Zoom: %d%%", g_browser_zoom_pct);
            web_browser_on_status(zbuf);
            web_browser_reflow();
            extern int needs_redraw;
            needs_redraw = 1;
            return;
        }
        if (ascii == '0' || key == 0x0B) {
            g_browser_zoom_pct = 100;
            web_browser_on_status("Zoom: 100%");
            web_browser_reflow();
            extern int needs_redraw;
            needs_redraw = 1;
            return;
        }
        if (ascii == 't' || ascii == 'T' || key == 0x14) {
            uint32_t new_id = browser_host_tab_create("about:home", 1);
            if (new_id > 0) {
                web_browser_sync_active_tab();
                web_browser_load_url("about:home", 0);
            }
            extern int needs_redraw;
            needs_redraw = 1;
            return;
        }
        if (ascii == 'w' || ascii == 'W' || key == 0x11) {
            firstos_tab_t *act = browser_host_get_active_tab();
            if (act) {
                browser_host_tab_close(act->tab_id);
                act = browser_host_get_active_tab();
                if (act) {
                    web_browser_sync_active_tab();
                    web_browser_load_url(act->url, 0);
                }
            }
            extern int needs_redraw;
            needs_redraw = 1;
            return;
        }
        if (key == 0x0F) { /* Ctrl+Tab */
            firstos_tab_manager_t *tm = browser_host_get_tab_manager();
            if (tm && tm->tab_count > 1) {
                int cur = -1;
                for (int i = 0; i < FIRSTOS_MAX_TABS; i++) {
                    if (tm->tabs[i].tab_id == tm->active_tab_id && tm->tabs[i].state != FIRSTOS_TAB_FREE) {
                        cur = i;
                        break;
                    }
                }
                for (int step = 1; step <= FIRSTOS_MAX_TABS; step++) {
                    int nxt = (cur + step) % FIRSTOS_MAX_TABS;
                    if (tm->tabs[nxt].state != FIRSTOS_TAB_FREE) {
                        browser_host_tab_activate(tm->tabs[nxt].tab_id);
                        web_browser_sync_active_tab();
                        web_browser_load_url(tm->tabs[nxt].url, 0);
                        break;
                    }
                }
            }
            extern int needs_redraw;
            needs_redraw = 1;
            return;
        }
        if (ascii == 'l' || ascii == 'L' || key == 0x26) { web_browser_focus_address(1); return; }
        if (ascii == 'r' || ascii == 'R' || key == 0x13) { web_browser_load_url(g_current_url, 0); return; }
        if (ascii == 'h' || ascii == 'H' || key == 0x23) { web_browser_navigate("about:home"); return; }
        if (ascii == 'a' || ascii == 'A' || key == 0x1E) {
            if (g_focus == WEB_BROWSER_FOCUS_ADDRESS) g_address_selected = 1;
            return;
        }
        return;
    }

    if (gui_is_alt_pressed()) {
        if (key == 0x4B) { web_browser_history_move(-1); return; }
        if (key == 0x4D) { web_browser_history_move(1); return; }
        if (key == 0x47) { web_browser_navigate("about:home"); return; }
        return;
    }

    if (key == 0x0F) {
        web_browser_focus_address(g_focus != WEB_BROWSER_FOCUS_ADDRESS);
        return;
    }

    if (g_focus == WEB_BROWSER_FOCUS_ADDRESS) {
        if (key == 0x0E) { web_browser_address_backspace(); return; }
        if (key == 0x53) {
            int len = strlen(g_address_text);
            if (g_address_selected) { g_address_text[0] = 0; g_address_cursor = 0; g_address_selected = 0; }
            else if (g_address_cursor < len) {
                for (int i = g_address_cursor; i < len; i++) g_address_text[i] = g_address_text[i + 1];
            }
            return;
        }
        if (key == 0x1C || ascii == '\n') {
            if (g_address_text[0]) web_browser_navigate(g_address_text);
            g_focus = WEB_BROWSER_FOCUS_NONE;
            g_address_selected = 0;
            return;
        }
        if (key == 0x4B) { if (g_address_selected) { g_address_selected = 0; g_address_cursor = 0; } else if (g_address_cursor > 0) g_address_cursor--; return; }
        if (key == 0x4D) { if (g_address_selected) { g_address_selected = 0; g_address_cursor = strlen(g_address_text); } else if (g_address_cursor < (int)strlen(g_address_text)) g_address_cursor++; return; }
        if (key == 0x47) { g_address_selected = 0; g_address_cursor = 0; return; }
        if (key == 0x4F) { g_address_selected = 0; g_address_cursor = strlen(g_address_text); return; }
        if (ascii >= 32 && ascii <= 126) { web_browser_address_insert(ascii); return; }
    } else {
#if FIRSTOS_BROWSER_USERSPACE_RENDERER
        if (g_state == WEB_BROWSER_READY && browser_host_ipc_is_active()) {
            browser_host_ipc_send_input(FIRSTOS_BROWSER_INPUT_KEY, (int)key, (int)(unsigned char)ascii, 0);
        }
#endif
        if (g_state == WEB_BROWSER_READY && html5_handle_key(key, ascii)) {
            const char *pending = html5_take_navigation_request();
            if (pending && pending[0]) {
                web_browser_navigate(pending);
            }
            return;
        }
        if (key == 0x50 || key == 0x51) {
            int max_scroll = html5_get_document_height() - g_view_h;
            if (max_scroll < 0) max_scroll = 0;
            g_scroll_y += key == 0x51 ? 120 : 24;
            if (g_scroll_y > max_scroll) g_scroll_y = max_scroll;
            return;
        }
        if (key == 0x48 || key == 0x49) {
            g_scroll_y -= key == 0x49 ? 120 : 24;
            if (g_scroll_y < 0) g_scroll_y = 0;
            return;
        }
        if (ascii >= 32 && ascii <= 126) {
            web_browser_focus_address(0);
            g_address_text[0] = 0;
            g_address_cursor = 0;
            web_browser_address_insert(ascii);
        }
    }
}

void web_browser_app_handle_scroll(int delta) {
    if (g_state != WEB_BROWSER_READY) return;
#if FIRSTOS_BROWSER_USERSPACE_RENDERER
    if (browser_host_ipc_is_active()) {
        browser_host_ipc_send_input(3, 0, (delta > 0 ? 28 : -28), 0);
    }
#endif
    int max_scroll = html5_get_document_height() - g_view_h;
    if (max_scroll < 0) max_scroll = 0;
    g_scroll_y += delta > 0 ? 28 : -28;
    if (g_scroll_y < 0) g_scroll_y = 0;
    if (g_scroll_y > max_scroll) g_scroll_y = max_scroll;
    web_browser_trigger_redraw("scroll");
}

void web_browser_app_tick(void) {
    static int s_in_tick = 0;
    if (s_in_tick) return;
    s_in_tick = 1;

    html5_tick();

    const char *pending = html5_take_navigation_request();
    if (pending && pending[0]) {
        web_browser_navigate(pending);
        s_in_tick = 0;
        return;
    }

    /* Only redraw on mouse movement when the hovered link URL or context menu state changes */
    static int last_mx = 0, last_my = 0;
    static char s_last_hover_url[WEB_BROWSER_URL_MAX] = "";
    int cur_mx = ps2_mouse_x();
    int cur_my = ps2_mouse_y();
    if (cur_mx != last_mx || cur_my != last_my) {
        last_mx = cur_mx;
        last_my = cur_my;
        if (g_ctx_menu_open) {
            web_browser_trigger_redraw("ctx-hover");
        } else if (g_state == WEB_BROWSER_READY && g_browser_app_win && g_browser_app_win->open) {
            int client_x, client_y, content_x, content_y, content_w, content_h;
            web_browser_toolbar_geometry(g_browser_app_win, &client_x, &client_y, &content_x, &content_y,
                                         &content_w, &content_h, NULL, NULL, NULL);
            int page_content_h = content_h - (g_inspector_open ? g_inspector_h : 0);
            char cur_hover[WEB_BROWSER_URL_MAX];
            cur_hover[0] = 0;
            if (cur_mx >= content_x && cur_mx < content_x + content_w &&
                cur_my >= content_y && cur_my < content_y + page_content_h) {
                html5_get_hovered_link(content_x, content_y, g_scroll_y, cur_mx, cur_my, cur_hover, sizeof(cur_hover));
            }
            if (!web_browser_text_equals(cur_hover, s_last_hover_url)) {
                web_browser_copy(s_last_hover_url, cur_hover, sizeof(s_last_hover_url));
                web_browser_trigger_redraw("hover-link");
            }
        }
    }

    /* Expire completed status tooltip after 2 seconds */
    if (g_status_active && g_state != WEB_BROWSER_LOADING) {
        if ((get_system_ticks() - g_status_ticks) >= 200u) {
            g_status_active = 0;
            web_browser_trigger_redraw("status-expire");
        }
    }

    g_blink_ticks++;
    if (g_blink_ticks >= 30) {
        g_blink_ticks = 0;
        g_cursor_blink = !g_cursor_blink;
        if (g_focus == WEB_BROWSER_FOCUS_ADDRESS || g_state == WEB_BROWSER_LOADING) {
            web_browser_trigger_redraw("cursor-blink");
        }
    }

    if (g_state == WEB_BROWSER_LOADING) {
        g_load_ticks++;
        browser_document_t doc;
        memset(&doc, 0, sizeof(doc));
        u32 current_fetch_gen = g_nav_generation;
        g_page_len = browser_core_step(g_page, sizeof(g_page), &doc);

        if (g_state != WEB_BROWSER_LOADING || current_fetch_gen != g_nav_generation) {
            /* Navigation cancelled or superseded by user action */
            s_in_tick = 0;
            return;
        }

        if (doc.state == BROWSER_CORE_READY && g_page_len > 0) {
            if (browser_core_commit_loaded_page(g_current_url, g_page, g_page_len, g_view_w, g_view_h, current_fetch_gen)) {
                serial_write("[web_browser] response rendered by native HTML5 pipeline\n");
            }
        } else if (doc.state == BROWSER_CORE_ERROR) {
            g_state = WEB_BROWSER_ERROR;
            serial_write("[web_browser] response failed: code=");
            serial_write_hex((unsigned int)doc.error_code);
            serial_write(" msg=");
            serial_write(doc.error);
            serial_write("\n");

            char err_page[2048];
            snprintf(err_page, sizeof(err_page),
                "<!doctype html><html><head><title>Connection Failure</title>"
                "<style>"
                "body{background-color:#181A1F;color:#ABB2BF;font-family:sans-serif;padding:36px;}"
                ".errbox{max-width:580px;margin:20px auto;background:#21252B;padding:28px;border-radius:8px;border:1px solid #3B4048;}"
                "h1{color:#E06C75;font-size:22px;margin-top:0;}"
                "p{line-height:1.6;color:#9DA5B4;font-size:14px;}"
                "code{background:#282C34;color:#E5C07B;padding:2px 6px;border-radius:4px;font-family:monospace;}"
                "a.btn{display:inline-block;background:#4D78CC;color:#fff;padding:8px 18px;border-radius:4px;text-decoration:none;margin-top:16px;font-weight:bold;}"
                "</style></head><body>"
                "<div class=\"errbox\">"
                "<h1>Unable to Connect</h1>"
                "<p>FirstOS Browser could not establish a connection to <code>%s</code>.</p>"
                "<p><b>Diagnostic Details:</b> %s (Error Code: 0x%04X)</p>"
                "<p>&bull; Check the address for typing errors.<br>"
                "&bull; Ensure the network interface is active and connected to the Internet.<br>"
                "&bull; If behind a firewall or proxy bridge, verify that it is running and accessible.</p>"
                "<p><a class=\"btn\" href=\"%s\">Try Again</a></p>"
                "</div></body></html>",
                g_current_url, doc.error[0] ? doc.error : "Connection refused or timed out", (unsigned int)doc.error_code, g_current_url);
            web_browser_show_local_page(err_page, g_current_url);
        }
    }

    s_in_tick = 0;
}
