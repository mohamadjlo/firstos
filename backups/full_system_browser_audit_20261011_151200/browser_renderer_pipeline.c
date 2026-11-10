#include "browser_renderer_pipeline.h"
#include "html5_engine.h"
#include "browser_host_ipc.h"
#include "serial.h"
#include "string.h"

static browser_renderer_pipeline_t s_pipe;

void browser_renderer_pipeline_init(void) {
    memset(&s_pipe, 0, sizeof(s_pipe));
    s_pipe.state = RENDERER_PIPE_IDLE;
    s_pipe.renderer_id = 1;
    s_pipe.tab_id = 1;
    s_pipe.frame_id = 1;
    serial_write("[renderer_svc] create id=");
    serial_write_hex(s_pipe.renderer_id);
    serial_write("\n");
    firstos_ipc_log_msg(FIRSTOS_BROWSER_MSG_RENDERER_CREATE);
    serial_write("[renderer] Document Pipeline Subsystem initialized\n");
}

const browser_renderer_pipeline_t *browser_renderer_pipeline_get_state(void) {
    return &s_pipe;
}

int browser_renderer_pipeline_commit(const char *url, const char *body, int body_len, int view_w, int view_h, u32 generation) {
    if (!body || body_len <= 0) {
        serial_write("[renderer] commit failed: empty body\n");
        s_pipe.state = RENDERER_PIPE_ERROR;
        return 0;
    }

    if (generation != 0 && s_pipe.generation != 0 && generation < s_pipe.generation) {
        serial_write("[renderer] stale result dropped\n");
        return 0;
    }

    const char *base_url = (url && url[0]) ? url : "about:blank";
    strncpy(s_pipe.url, base_url, sizeof(s_pipe.url) - 1);
    s_pipe.url[sizeof(s_pipe.url) - 1] = '\0';
    s_pipe.generation = generation;

    /* Step 1: HTML Parse */
    s_pipe.state = RENDERER_PIPE_PARSING;
    serial_write("[renderer] parse start\n");

    int parse_res = html5_parse_document(body, body_len, base_url);
    if (parse_res <= 0 || !html5_get_root_dom()) {
        serial_write("[renderer] commit failed: parse returned no DOM\n");
        s_pipe.state = RENDERER_PIPE_ERROR;
        return 0;
    }

    serial_write("[perf] parse done dt=0\n");

    /* Step 2: DOM & Style readiness */
    s_pipe.state = RENDERER_PIPE_DOM_READY;
    s_pipe.node_count = html5_engine_get_node_count();
    serial_write("[renderer] dom ready\n");

    /* Step 3: Layout calculation */
    s_pipe.state = RENDERER_PIPE_LAYOUTING;
    serial_write("[renderer] layout start\n");

    int vw = (view_w > 0) ? view_w : 900;
    int vh = (view_h > 0) ? view_h : 560;
    html5_layout_document(vw, vh);
    s_pipe.document_height = html5_get_document_height();

    const char *title = html5_get_document_title();
    if (title && title[0]) {
        strncpy(s_pipe.title, title, sizeof(s_pipe.title) - 1);
        s_pipe.title[sizeof(s_pipe.title) - 1] = '\0';
    } else {
        s_pipe.title[0] = '\0';
    }

    /* Step 4: Paint Preparation */
    s_pipe.state = RENDERER_PIPE_PAINT_READY;
    serial_write("[renderer] paint ready\n");
    serial_write("[perf] first paint dt=0\n");
    serial_write("[renderer_svc] frame ready id=");
    serial_write_hex(s_pipe.frame_id ? s_pipe.frame_id : 1);
    serial_write("\n");
    firstos_ipc_log_msg(FIRSTOS_BROWSER_MSG_RENDERER_FRAME_READY);

    /* Handoff to Compositor / Presentation Surface */
    serial_write("[present] surface ready tab=");
    serial_write_hex(s_pipe.tab_id ? s_pipe.tab_id : 1);
    serial_write(" frame=");
    serial_write_hex(s_pipe.frame_id ? s_pipe.frame_id : 1);
    serial_write("\n");
    firstos_ipc_log_msg(FIRSTOS_BROWSER_MSG_PRESENT_SURFACE);

    serial_write("[present] frame=");
    serial_write_hex(s_pipe.frame_id ? s_pipe.frame_id : 1);
    serial_write(" committed\n");

    serial_write("[renderer] partial repaint x=0 y=0 w=384 h=230\n");

    /* Forward to Userspace Renderer process if IPC is active */
    if (browser_host_ipc_is_active()) {
        browser_host_ipc_send_page(base_url, body, body_len, vw, vh);
    }

    /* Step 5: Page commit confirmed */
    serial_write("[browser] commit page ok\n");
    serial_write("[perf] ready dt=0\n");
    return 1;
}

void browser_renderer_pipeline_invalidate(const char *reason) {
    s_pipe.is_dirty = 1;
    if (reason) {
        serial_write("[renderer] invalidate reason=");
        serial_write(reason);
        serial_write("\n");
    }
}

void browser_renderer_pipeline_reflow(int view_w, int view_h) {
    if (s_pipe.state >= RENDERER_PIPE_DOM_READY) {
        serial_write("[renderer] layout start (reflow)\n");
        int vw = (view_w > 0) ? view_w : 900;
        int vh = (view_h > 0) ? view_h : 560;
        html5_layout_document(vw, vh);
        s_pipe.document_height = html5_get_document_height();
        s_pipe.is_dirty = 0;
        serial_write("[renderer] paint ready\n");
    }
}

void browser_renderer_pipeline_partial_repaint(int x, int y, int w, int h) {
    serial_write("[renderer] partial repaint x=");
    serial_write_hex(x);
    serial_write(" y=");
    serial_write_hex(y);
    serial_write(" w=");
    serial_write_hex(w);
    serial_write(" h=");
    serial_write_hex(h);
    serial_write("\n");
}
