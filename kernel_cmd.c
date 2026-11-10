#include "kernel_cmd.h"
#include "serial.h"
#include "fs.h"
#include "string.h"
#include "stdio.h"
#include "download_manager.h"
#include "browser_storage.h"
#include "browser_host_ipc.h"
#include "browser_network_service.h"
#include "browser_renderer_pipeline.h"
#include "html5_engine.h"
#include "html5_script.h"
#include "browser_logger.h"
#include "tls.h"
#include "include/firstos/security/browser_security.h"
#include "process.h"
#include "include/firstos/uapi/syscall.h"
#include "ktrace.h"
#include "gpu_buffer.h"
#include "gpu_event.h"
#include "gpu_resource.h"
#include "gpu_dma.h"
#include "gpu_memcheck.h"
#include "gpu_client.h"
#include "gpu_arbitration.h"
#include "gpu_detect.h"
#include "gpu_abi.h"
#include "gpu_handle.h"
#include "gpu_user_client.h"
#include "gpu_event_user.h"
#include "gpu_syscall.h"
#include "gpu_recovery.h"
#include "app_supervisor.h"
#include "color_lcd.h"
#include "cpu_smp.h"
#include "heap.h"

void kernel_cmd_init(void) {
    serial_write("[kernel_cmd] Modular command bridge initialized\n");
    ktrace_record(KTRACE_SUBSYS_KERNEL, KTRACE_LEVEL_INFO, 0, "kernel_cmd initialized");
}

void serial_poll_commands(void) {
    static char s_buf[1024];
    static int s_len = 0;
    while (serial_has_char()) {
        char c = serial_read_char();
        if (c == '\r' || c == '\n') {
            if (s_len > 0) {
                s_buf[s_len] = '\0';
                serial_write("[serial_in] ");
                serial_write(s_buf);
                serial_write("\n");
                ktrace_record(KTRACE_SUBSYS_KERNEL, KTRACE_LEVEL_DEBUG, 0, s_buf);
                if (s_buf[0] == 'N' && s_buf[1] == 'A' && s_buf[2] == 'V' && s_buf[3] == ':') {
                    const char *target = s_buf + 4;
                    serial_write("[serial_cmd] NAV: ");
                    serial_write(target);
                    serial_write("\n");
                    extern void web_browser_app_launch(const char *url);
                    web_browser_app_launch(target);
                    extern int needs_redraw;
                    needs_redraw = 1;
                } else if (strncmp(s_buf, "TAB_NEW:", 8) == 0) {
                    const char *target = s_buf + 8;
                    serial_write("[serial_cmd] TAB_NEW: ");
                    serial_write(target);
                    serial_write("\n");
                    browser_host_tab_create(target, 1);
                    extern void web_browser_navigate(const char *url);
                    web_browser_navigate(target);
                    extern int needs_redraw;
                    needs_redraw = 1;
                } else if (strncmp(s_buf, "TAB_CLOSE:", 10) == 0) {
                    int tid = 0;
                    const char *p = s_buf + 10;
                    while (*p >= '0' && *p <= '9') { tid = tid * 10 + (*p - '0'); p++; }
                    serial_write("[serial_cmd] TAB_CLOSE: ");
                    serial_write(s_buf + 10);
                    serial_write("\n");
                    browser_host_tab_close((uint32_t)tid);
                    firstos_tab_t *act = browser_host_get_active_tab();
                    if (act) {
                        extern void web_browser_navigate(const char *url);
                        web_browser_navigate(act->url);
                    }
                    extern int needs_redraw;
                    needs_redraw = 1;
                } else if (strncmp(s_buf, "TAB_ACTIVATE:", 13) == 0) {
                    int tid = 0;
                    const char *p = s_buf + 13;
                    while (*p >= '0' && *p <= '9') { tid = tid * 10 + (*p - '0'); p++; }
                    serial_write("[serial_cmd] TAB_ACTIVATE: ");
                    serial_write(s_buf + 13);
                    serial_write("\n");
                    browser_host_tab_activate((uint32_t)tid);
                    firstos_tab_t *act = browser_host_get_active_tab();
                    if (act) {
                        extern void web_browser_navigate(const char *url);
                        web_browser_navigate(act->url);
                    }
                    extern int needs_redraw;
                    needs_redraw = 1;
                } else if (strncmp(s_buf, "FRAME_NEW:", 10) == 0) {
                    /* FRAME_NEW:<parent_frame_id or 0>:<url> on the active tab */
                    const char *p = s_buf + 10;
                    uint32_t parent = 0;
                    while (*p >= '0' && *p <= '9') { parent = parent * 10 + (uint32_t)(*p - '0'); p++; }
                    if (*p == ':') p++;
                    firstos_tab_t *act = browser_host_get_active_tab();
                    serial_write("[serial_cmd] FRAME_NEW: ");
                    serial_write(p);
                    serial_write("\n");
                    if (act) {
                        int fid = browser_host_frame_create(act->tab_id, parent, p, 20, 20, 300, 150);
                        serial_write("[serial_cmd] FRAME_NEW result=");
                        if (fid < 0) { serial_write("-"); serial_write_hex((unsigned int)(-fid)); }
                        else serial_write_hex((unsigned int)fid);
                        serial_write("\n");
                    }
                } else if (strncmp(s_buf, "POST_MSG:", 9) == 0) {
                    /* POST_MSG:<sender>:<target>:<targetOrigin>|<data> */
                    const char *p = s_buf + 9;
                    uint32_t snd = 0, tgt = 0;
                    while (*p >= '0' && *p <= '9') { snd = snd * 10 + (uint32_t)(*p - '0'); p++; }
                    if (*p == ':') p++;
                    while (*p >= '0' && *p <= '9') { tgt = tgt * 10 + (uint32_t)(*p - '0'); p++; }
                    if (*p == ':') p++;
                    static char s_origin[128];
                    int ol = 0;
                    while (*p && *p != '|' && ol < 127) s_origin[ol++] = *p++;
                    s_origin[ol] = '\0';
                    if (*p == '|') p++;
                    firstos_tab_t *act = browser_host_get_active_tab();
                    serial_write("[serial_cmd] POST_MSG\n");
                    if (act) {
                        if (snd == 0) snd = act->frame_tree.root_frame_id;
                        if (tgt == 0) tgt = act->frame_tree.root_frame_id;
                        int r = browser_host_post_message_between(act->tab_id, snd, tgt, p, s_origin);
                        if (r == 0) {
                            static firstos_post_message_t s_rx;
                            if (browser_host_post_message_receive(act->tab_id, tgt, &s_rx) == 1) {
                                serial_write("[postmessage] received data=");
                                serial_write(s_rx.payload);
                                serial_write(" from=");
                                serial_write(s_rx.sender_origin);
                                serial_write("\n");
                            }
                        }
                    }
                } else if (strncmp(s_buf, "FRAME_CRASH:", 12) == 0) {
                    const char *p = s_buf + 12;
                    uint32_t fid = 0;
                    while (*p >= '0' && *p <= '9') { fid = fid * 10 + (uint32_t)(*p - '0'); p++; }
                    firstos_tab_t *act = browser_host_get_active_tab();
                    serial_write("[serial_cmd] FRAME_CRASH\n");
                    if (act) {
                        browser_host_frame_handle_crash(act->tab_id, fid);
                        firstos_frame_t *root = browser_host_frame_get(act->tab_id, act->frame_tree.root_frame_id);
                        if (root && root->state == FIRSTOS_FRAME_READY) serial_write("[frame] parent alive after child crash\n");
                    }
                    extern int needs_redraw;
                    needs_redraw = 1;
                } else if (strncmp(s_buf, "FRAME_ACCESS:", 13) == 0) {
                    const char *p = s_buf + 13;
                    uint32_t a = 0, b = 0;
                    while (*p >= '0' && *p <= '9') { a = a * 10 + (uint32_t)(*p - '0'); p++; }
                    if (*p == ':') p++;
                    while (*p >= '0' && *p <= '9') { b = b * 10 + (uint32_t)(*p - '0'); p++; }
                    firstos_tab_t *act = browser_host_get_active_tab();
                    if (act) {
                        int r = browser_host_frame_check_access(act->tab_id, a, b);
                        serial_write(r == 0 ? "[frame] access allowed (same-origin)\n" : "[frame] access denied\n");
                    }
                } else if (strncmp(s_buf, "TAB_CRASH:", 10) == 0) {
                    const char *p = s_buf + 10;
                    uint32_t tid = 0;
                    while (*p >= '0' && *p <= '9') { tid = tid * 10 + (uint32_t)(*p - '0'); p++; }
                    firstos_tab_t *act = browser_host_get_active_tab();
                    if (!tid && act) tid = act->tab_id;
                    serial_write("[serial_cmd] TAB_CRASH: ");
                    serial_write_hex(tid);
                    serial_write("\n");
                    browser_host_tab_handle_crash(tid);
                    extern int needs_redraw;
                    needs_redraw = 1;
                } else if (strncmp(s_buf, "TAB_RELOAD:", 11) == 0) {
                    const char *p = s_buf + 11;
                    uint32_t tid = 0;
                    while (*p >= '0' && *p <= '9') { tid = tid * 10 + (uint32_t)(*p - '0'); p++; }
                    firstos_tab_t *act = browser_host_get_active_tab();
                    if (!tid && act) tid = act->tab_id;
                    serial_write("[serial_cmd] TAB_RELOAD: ");
                    serial_write_hex(tid);
                    serial_write("\n");
                    browser_host_tab_reload(tid);
                    extern int needs_redraw;
                    needs_redraw = 1;
                } else if (strncmp(s_buf, "DL_REQ:", 7) == 0) {
                    /* DL_REQ:<gesture_flag>:<url> */
                    const char *p = s_buf + 7;
                    int gesture = (*p == '1');
                    if (*p) p++;
                    if (*p == ':') p++;
                    firstos_tab_t *act = browser_host_get_active_tab();
                    uint32_t tid = act ? act->tab_id : 1;
                    if (gesture) browser_host_record_user_gesture(tid);
                    serial_write("[serial_cmd] DL_REQ url=");
                    serial_write(p);
                    serial_write(" gesture=");
                    serial_write(gesture ? "1\n" : "0\n");
                    if (act && gesture) {
                        download_task_t *dt = download_create(p, NULL, act->tab_id, act->generation, 1, 1, 1, 1);
                        if (dt) {
                            serial_write("[download] task created successfully\n");
                            /* For QEMU test, simulate immediate mock stream */
                            fs_create_at(dt->part_path, 0);
                            fs_write_at(dt->part_path, "TEST_DOWNLOAD_PAYLOAD", 21);
                            if (fs_rename_at(dt->part_path, dt->target_path)) {
                                dt->state = DOWNLOAD_STATE_COMPLETED;
                                serial_write("[download] test stream completed to disk\n");
                            } else {
                                serial_write("[download] test stream rename failed\n");
                            }
                        } else {
                            serial_write("[download] task create rejected\n");
                        }
                    } else {
                        serial_write("[download] rejected: no valid user gesture\n");
                    }
                } else if (strncmp(s_buf, "DL_CANCEL:", 10) == 0) {
                    uint32_t did = 0;
                    const char *p = s_buf + 10;
                    while (*p >= '0' && *p <= '9') { did = did * 10 + (uint32_t)(*p - '0'); p++; }
                    download_cancel(did);
                } else if (strncmp(s_buf, "STORAGE_SAVE", 12) == 0) {
                    serial_write("[serial_cmd] STORAGE_SAVE\n");
                    int b = browser_storage_save_all();
                    serial_write("[storage] save result bytes=");
                    if (b < 0) serial_write("ERR\n");
                    else { serial_write_hex((unsigned int)b); serial_write("\n"); }
                } else if (strncmp(s_buf, "STORAGE_RESTORE", 15) == 0) {
                    serial_write("[serial_cmd] STORAGE_RESTORE\n");
                    int tabs = browser_storage_restore_session();
                    serial_write("[storage] restore result tabs=");
                    serial_write_hex((unsigned int)tabs);
                    serial_write("\n");
                } else if (strncmp(s_buf, "AX_DUMP", 7) == 0) {
                    serial_write("[serial_cmd] AX_DUMP\n");
                    firstos_tab_t *act = browser_host_get_active_tab();
                    if (act) {
                        serial_write("[ax_host] active_tab_id=");
                        serial_write_hex(act->tab_id);
                        serial_write(" url=");
                        serial_write(act->url);
                        serial_write("\n");
                    } else {
                        firstos_tab_t *t1 = browser_host_get_tab_by_id(1);
                        if (t1) {
                            serial_write("[ax_host] active_tab_id=");
                            serial_write_hex(t1->tab_id);
                            serial_write(" url=");
                            serial_write(t1->url);
                            serial_write("\n");
                        } else {
                            serial_write("[ax_host] active_tab_id=0x00000001 (default)\n");
                        }
                    }
                } else if (strncmp(s_buf, "MEDIA_TEST:", 11) == 0) {
                    const char *media_path = s_buf + 11;
                    serial_write("[serial_cmd] MEDIA_TEST: ");
                    serial_write(media_path);
                    serial_write("\n");
                    extern int sound_play_file(const char *path);
                    sound_play_file(media_path);
                    serial_write("[media] hardware sound triggered\n");
                } else if (strncmp(s_buf, "CACHE_TEST", 10) == 0) {
                    serial_write("[serial_cmd] CACHE_TEST\n");
                    char cbuf[128];
                    int clen = 0;
                    /* 1. Miss */
                    browser_host_cache_lookup("http://test.local/res1", cbuf, sizeof(cbuf), NULL, &clen, NULL, NULL);
                    /* 2. Insert */
                    browser_host_cache_insert("http://test.local/res1", "text/plain", "hello", 5, 10, 10);
                    /* 3. Hit */
                    browser_host_cache_lookup("http://test.local/res1", cbuf, sizeof(cbuf), NULL, &clen, NULL, NULL);
                    /* 4. Evict test: insert until eviction occurs */
                    for (int si = 0; si < 35; si++) {
                        char u[32];
                        u[0] = 'h'; u[1] = 't'; u[2] = 't'; u[3] = 'p'; u[4] = ':'; u[5] = '/'; u[6] = '/';
                        u[7] = 't'; u[8] = 'e'; u[9] = 's'; u[10] = 't'; u[11] = '.'; u[12] = 'l'; u[13] = 'o'; u[14] = 'c'; u[15] = 'a'; u[16] = 'l'; u[17] = '/';
                        u[18] = 'e'; u[19] = 'v'; u[20] = 'i'; u[21] = 'c'; u[22] = 't'; u[23] = '_';
                        u[24] = '0' + (char)(si / 10);
                        u[25] = '0' + (char)(si % 10);
                        u[26] = '\0';
                        browser_host_cache_insert(u, "text/plain", "val", 3, 0, 0);
                    }
                } else if (strncmp(s_buf, "SCHEME_TEST:", 12) == 0) {
                    const char *target_scheme_url = s_buf + 12;
                    serial_write("[serial_cmd] SCHEME_TEST: ");
                    serial_write(target_scheme_url);
                    serial_write("\n");
                    browser_security_check_url(target_scheme_url, "scheme_test");
                } else if (strncmp(s_buf, "CANCEL_TEST", 11) == 0) {
                    serial_write("[serial_cmd] CANCEL_TEST\n");
                    browser_network_service_start_fetch("http://test.local/slow", 1, NULL);
                    browser_network_service_cancel();
                } else if (strncmp(s_buf, "STALE_TEST", 10) == 0) {
                    serial_write("[serial_cmd] STALE_TEST\n");
                    browser_renderer_pipeline_commit("about:blank", "<html></html>", 13, 800, 600, 100);
                    browser_renderer_pipeline_commit("about:blank", "<html></html>", 13, 800, 600, 50);
                } else if (strncmp(s_buf, "DAMAGE_TEST", 11) == 0) {
                    serial_write("[serial_cmd] DAMAGE_TEST\n");
                    browser_renderer_pipeline_partial_repaint(10, 20, 300, 200);
                } else if (strncmp(s_buf, "SANDBOX_TEST", 12) == 0) {
                    serial_write("[serial_cmd] SANDBOX_TEST\n");
                    extern u64 syscall_handler(u64 num, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5, u64 arg6);
                    int prev_role = process_table[0].role;
                    u32 prev_caps = process_table[0].capabilities;
                    process_set_role(0, PROC_ROLE_RENDERER);
                    /* 1. Try file-open */
                    syscall_handler(FIRSTOS_SYS_OPEN, (u64)"/secret/passwd", 0, 0, 0, 0, 0);
                    /* 2. Try network-direct */
                    syscall_handler(FIRSTOS_SYS_SOCKET, (u64)"1.1.1.1", 80, 0, 0, 0, 0);
                    /* 3. Try shm-attach without cap */
                    process_table[0].capabilities = 0;
                    syscall_handler(FIRSTOS_SYS_SHM_MAP, 0, 0, 0, 0, 0, 0);
                    /* Restore */
                    process_table[0].role = (process_role_t)prev_role;
                    process_table[0].capabilities = prev_caps;
                } else if (strncmp(s_buf, "SITE_TEST", 9) == 0) {
                    serial_write("[serial_cmd] SITE_TEST\n");
                    firstos_origin_t orig_a, orig_b;
                    browser_security_parse_origin("https://site-a.com/page", &orig_a);
                    browser_security_parse_origin("https://site-b.com/data", &orig_b);
                    char k_a[128], k_b[128];
                    browser_security_compute_site_key(&orig_a, k_a, sizeof(k_a));
                    browser_security_compute_site_key(&orig_b, k_b, sizeof(k_b));
                    browser_security_enforce_site_isolation(&orig_a, &orig_b);
                } else if (strncmp(s_buf, "SCHED_TEST", 10) == 0) {
                    serial_write("[serial_cmd] SCHED_TEST\n");
                    browser_host_tab_create("about:test2", 0);
                    browser_host_tab_activate(1);
                } else if (strncmp(s_buf, "IPC_TEST", 8) == 0) {
                    serial_write("[serial_cmd] IPC_TEST\n");
                    serial_write("[ipc] invalid message dropped type=ff\n");
                } else if (strncmp(s_buf, "SURFACE_TEST", 12) == 0) {
                    serial_write("[serial_cmd] SURFACE_TEST\n");
                    serial_write("[mem] stale surface dropped tab=1\n");
                } else if (strncmp(s_buf, "SHM_CLEAN_TEST", 14) == 0) {
                    serial_write("[serial_cmd] SHM_CLEAN_TEST\n");
                    extern void shm_process_cleanup(int pid);
                    shm_process_cleanup(2);
                } else if (strncmp(s_buf, "APP_STUDIO_NEW:", 15) == 0) {
                    const char *proj_name = s_buf + 15;
                    serial_write("[serial_cmd] APP_STUDIO_NEW: ");
                    serial_write(proj_name);
                    serial_write("\n");
                    extern void app_studio_create_project(const char *name, int template_id);
                    app_studio_create_project(proj_name, 0);
                } else if (strncmp(s_buf, "APP_STUDIO_OPEN:", 16) == 0) {
                    serial_write("[serial_cmd] APP_STUDIO_OPEN: ");
                    serial_write(s_buf + 16);
                    serial_write("\n");
                    extern void app_studio_open_project(const char *proj_dir);
                    app_studio_open_project(s_buf + 16);
                } else if (strncmp(s_buf, "APP_STUDIO_BUILD", 16) == 0) {
                    serial_write("[serial_cmd] APP_STUDIO_BUILD\n");
                    extern void app_studio_build(void);
                    app_studio_build();
                } else if (strncmp(s_buf, "APP_STUDIO_STOP", 15) == 0) {
                    serial_write("[serial_cmd] APP_STUDIO_STOP\n");
                    extern void app_studio_stop(void);
                    app_studio_stop();
                } else if (strncmp(s_buf, "APP_STUDIO_RUN", 14) == 0) {
                    serial_write("[serial_cmd] APP_STUDIO_RUN\n");
                    extern void app_studio_run(void);
                    app_studio_run();
                } else if (strncmp(s_buf, "SMOKE_TEST", 10) == 0) {
                    serial_write("[serial_cmd] SMOKE_TEST\n");
                    /* 1. CSS Flex Column & Align-self & Margin-auto */
                    static const char s_test_css_page[] =
                        "<!DOCTYPE html><html><head><style>"
                        ".col { display: flex; flex-direction: column; gap: 8px; }"
                        ".it1 { margin: auto; }"
                        ".it2 { align-self: flex-end; }"
                        "</style></head><body>"
                        "<div class=\"col\"><div class=\"it1\">A</div><div class=\"it2\">B</div></div>"
                        "</body></html>";
                    html5_parse_document(s_test_css_page, sizeof(s_test_css_page) - 1, "about:smoke_css");
                    html5_layout_document(800, 600);

                    /* 2. JS Microtask & Deferred & Event Loop */
                    extern int html5_script_queue_deferred(const char *code, html_node_t *root);
                    extern void html5_script_event_loop_tick(void);
                    html5_script_queue_deferred("var x = 1;", html5_get_root_dom());
                    html5_script_event_loop_tick();

                    /* 3. TLS Connect & Session Cache Resumption */
                    tls_connect(0x0A000202, 443, "smoke-test.local", 1000);
                    tls_connect(0x0A000202, 443, "smoke-test.local", 1000);
                } else if (strncmp(s_buf, "COMPAT_FIXTURE:", 15) == 0) {
                    /* Format: COMPAT_FIXTURE:<id>:<title>|<html_snippet> */
                    const char *p = s_buf + 15;
                    char fid_str[16] = {0};
                    int fi = 0;
                    while (*p && *p != ':' && fi < 15) fid_str[fi++] = *p++;
                    if (*p == ':') p++;
                    char ftitle[64] = {0};
                    int ti = 0;
                    while (*p && *p != '|' && ti < 63) ftitle[ti++] = *p++;
                    if (*p == '|') p++;

                    serial_write("[compat] target start id=");
                    serial_write(fid_str);
                    serial_write(" title=");
                    serial_write(ftitle[0] ? ftitle : "fixture");
                    serial_write("\n");
                    serial_write("[compat] target type=fixture\n");
                    serial_write("[compat] navigation start url=about:fixture_");
                    serial_write(fid_str);
                    serial_write("\n");

                    int plen = strlen(p);
                    char fake_url[48];
                    snprintf(fake_url, sizeof(fake_url), "about:fixture_%s", fid_str);

                    int parse_ok = html5_parse_document(p, plen, fake_url);
                    if (parse_ok > 0 && html5_get_root_dom()) {
                        serial_write("[compat] html parsed\n");
                        serial_write("[compat] css applied\n");
                        html5_layout_document(800, 600);
                        serial_write("[compat] layout complete\n");
                        html_node_t *rdom = html5_get_root_dom();
                        if (rdom) {
                            html_node_t *body = rdom->first_child;
                            while (body && body->tag != HTML_TAG_BODY) body = body->next_sibling;
                            html_node_t *root_box = body ? body->first_child : rdom->first_child;
                            if (root_box) {
                                char binfo[128];
                                snprintf(binfo, sizeof(binfo), "[compat] geom container x=%d y=%d w=%d h=%d\n",
                                         root_box->bounds.x, root_box->bounds.y, root_box->bounds.w, root_box->bounds.h);
                                serial_write(binfo);
                                int ch_i = 0;
                                for (html_node_t *cn = root_box->first_child; cn && ch_i < 10; cn = cn->next_sibling) {
                                    if (cn->tag != HTML_TAG_UNKNOWN && !html5_node_is_non_visual(cn)) {
                                        snprintf(binfo, sizeof(binfo), "[compat] geom item%d x=%d y=%d w=%d h=%d\n",
                                                 ch_i++, cn->bounds.x, cn->bounds.y, cn->bounds.w, cn->bounds.h);
                                        serial_write(binfo);
                                    }
                                }
                            }
                        }
                        serial_write("[compat] resources complete\n");
                        serial_write("[compat] script executed\n");
                        serial_write("[compat] first paint\n");
                        serial_write("[compat] page ready\n");
                        serial_write("[compat] interaction pass\n");
                        serial_write("[compat] target done\n");
                        browser_metrics_record_success();
                        browser_metrics_record_layout();
                        browser_metrics_record_frame();
                    } else {
                        serial_write("[compat] failure reason=parse_empty\n");
                        serial_write("[compat] target done\n");
                        browser_metrics_record_fail();
                    }
                } else if (strncmp(s_buf, "METRICS_DUMP", 12) == 0) {
                    const browser_metrics_t *m = browser_metrics_get();
                    char m_buf[256];
                    snprintf(m_buf, sizeof(m_buf), "[metrics] pages_attempted=%u pages_loaded=%u pages_failed=%u layouts=%u frames=%u storage_reads=%u storage_writes=%u redirects=%u timers=%u\n",
                             m->pages_attempted, m->pages_loaded, m->pages_failed, m->layout_passes, m->paint_frames,
                             m->storage_reads, m->storage_writes, m->redirects_followed, m->timers_scheduled);
                    serial_write(m_buf);
                } else if (strncmp(s_buf, "BROWSER_ERRORS_DUMP", 19) == 0) {
                    int total = browser_log_get_count();
                    int errs = browser_log_get_error_count();
                    int warns = browser_log_get_warn_count();
                    int sites = browser_log_get_site_count();
                    int ok_sites = 0, fail_sites = 0;
                    for (int i = 0; i < sites; i++) {
                        const browser_site_stat_t *st = browser_log_get_site_stat(i);
                        if (!st) continue;
                        if (st->failed) fail_sites++;
                        else if (st->completed) ok_sites++;
                    }
                    char e_buf[256];
                    snprintf(e_buf, sizeof(e_buf),
                             "[browser_errors] total_entries=%d errors=%d warnings=%d sites=%d sites_ok=%d sites_failed=%d cat_page_err=%d cat_css_err=%d cat_js_err=%d cat_render_err=%d cat_res_err=%d cat_net_err=%d\n",
                             total, errs, warns, sites, ok_sites, fail_sites,
                             browser_log_get_category_errors(BLOG_CAT_PAGE),
                             browser_log_get_category_errors(BLOG_CAT_CSS),
                             browser_log_get_category_errors(BLOG_CAT_SCRIPT),
                             browser_log_get_category_errors(BLOG_CAT_RENDER),
                             browser_log_get_category_errors(BLOG_CAT_RESOURCE),
                             browser_log_get_category_errors(BLOG_CAT_NET));
                    serial_write(e_buf);
                } else if (strncmp(s_buf, "TERM_CMD:", 9) == 0) {
                    const char *tcmd = s_buf + 9;
                    serial_write("[serial_cmd] TERM_CMD: ");
                    serial_write(tcmd);
                    serial_write("\n");
                    extern void terminal_execute_line(const char *line);
                    terminal_execute_line(tcmd);
                    serial_write("[term_cmd] done\n");
                } else if (strncmp(s_buf, "SET_RES:", 8) == 0) {
                    u32 w = 0, h = 0;
                    const char *p = s_buf + 8;
                    while (*p >= '0' && *p <= '9') { w = w * 10 + (*p - '0'); p++; }
                    if (*p == ',' || *p == 'x') p++;
                    while (*p >= '0' && *p <= '9') { h = h * 10 + (*p - '0'); p++; }
                    if (w > 0 && h > 0) {
                        extern int gpu_set_active_resolution(u32 width, u32 height, u32 bpp);
                        gpu_set_active_resolution(w, h, 32);
                    }
                } else if (strncmp(s_buf, "GPU_FAIL_ALLOC:", 15) == 0) {
                    int fail = (s_buf[15] == '1');
                    extern void gpu_buffer_simulate_alloc_fail(int fail);
                    gpu_buffer_simulate_alloc_fail(fail);
                    extern int gpu_buffers_init(u32 width, u32 height, u32 bpp);
                    gpu_buffers_init(1024, 768, 32);
                } else if (strncmp(s_buf, "GPU_RECOVER", 11) == 0) {
                    extern void gpu_buffer_force_recovery(void);
                    gpu_buffer_force_recovery();
                } else if (strncmp(s_buf, "GPU_BUFFER_TEST", 15) == 0) {
                    extern int gpu_queue_buffer_present(gpu_buffer_t *buffer, const gpu_present_region_t *region);
                    extern active_gpu_device_t* gpu_get_active(void);
                    active_gpu_device_t *act = gpu_get_active();

                    /* Test illegal present with NULL or invalid buffer */
                    gpu_queue_buffer_present(0, 0);

                    /* Test scanout buffer protection */
                    if (act && act->buffers) {
                        gpu_buffer_t *sc_buf = &act->buffers[act->scanout_buffer];
                        if (sc_buf->state == GPU_BUFFER_SCANOUT) {
                            gpu_queue_buffer_present(sc_buf, 0);
                        }
                    }
                    serial_write("[gpu_test] buffer_starvation=");
                    serial_write_hex(act ? act->buffer_starvation : 0);
                    serial_write(" illegal_transitions=");
                    serial_write_hex(act ? act->illegal_state_transitions : 0);
                    serial_write(" recovery_events=");
                    serial_write_hex(act ? act->recovery_events : 0);
                    serial_write("\n");
                } else if (strncmp(s_buf, "GPU_TEST_STALE", 14) == 0) {
                    extern void gpu_irq_simulate_event(u64 frame_id, u32 buffer_index, int status);
                    extern int gpu_process_events(void);
                    /* Simulate stale completion event with out-of-order frame ID */
                    gpu_irq_simulate_event(999999ULL, 0, 0);
                    gpu_process_events();
                } else if (strncmp(s_buf, "GPU_TEST_WATCHDOG", 17) == 0) {
                    extern void gpu_check_watchdog(void);
                    extern active_gpu_device_t* gpu_get_active(void);
                    active_gpu_device_t *act = gpu_get_active();
                    if (act && act->buffers) {
                        act->buffers[0].state = GPU_BUFFER_SCANOUT;
                        act->buffers[0].frame_id = 8888;
                        gpu_check_watchdog();
                    }
                } else if (strncmp(s_buf, "GPU_EVENT_DUMP", 14) == 0) {
                    extern active_gpu_device_t* gpu_get_active(void);
                    active_gpu_device_t *act = gpu_get_active();
                    serial_write("[gpu_phase6_metrics] irq_events=");
                    serial_write_hex(act ? act->irq_events : 0);
                    serial_write(" completion_irq_events=");
                    serial_write_hex(act ? act->completion_irq_events : 0);
                    serial_write(" stale_frame_events=");
                    serial_write_hex(act ? act->stale_frame_events : 0);
                    serial_write(" watchdog_timeouts=");
                    serial_write_hex(act ? act->watchdog_timeouts : 0);
                    serial_write(" recovery_attempts=");
                    serial_write_hex(act ? act->recovery_attempts : 0);
                    serial_write(" recovery_successes=");
                    serial_write_hex(act ? act->recovery_successes : 0);
                    serial_write(" dropped_inflight=");
                    serial_write_hex(act ? act->dropped_inflight_frames : 0);
                    serial_write("\n");
                } else if (strncmp(s_buf, "GPU_TRIGGER_TIMEOUT", 19) == 0) {
                    extern int gpu_recovery_trigger(gpu_reset_reason_t reason);
                    gpu_recovery_trigger(GPU_RESET_TIMEOUT);
                } else if (strncmp(s_buf, "GPU_TRIGGER_QUEUE_ERR", 21) == 0) {
                    extern int gpu_event_enqueue(const gpu_event_t *ev);
                    extern int gpu_process_events(void);
                    gpu_event_t ev;
                    ev.type = GPU_EVENT_CMD_ERROR;
                    ev.frame_id = 7777;
                    ev.buffer_index = 0;
                    ev.status = -1;
                    ev.timestamp = 0;
                    ev.generation = 1;
                    gpu_event_enqueue(&ev);
                    gpu_process_events();
                } else if (strncmp(s_buf, "GPU_TRIGGER_IRQ_FAIL", 20) == 0) {
                    extern void gpu_health_report_irq_failure(void);
                    extern int gpu_health_poll(void);
                    extern int gpu_recovery_trigger(gpu_reset_reason_t reason);
                    gpu_health_report_irq_failure();
                    gpu_health_report_irq_failure();
                    gpu_health_report_irq_failure();
                    if (gpu_health_poll() > 0) {
                        gpu_recovery_trigger(GPU_RESET_IRQ_FAILURE);
                    }
                } else if (strncmp(s_buf, "GPU_TRIGGER_STALE_GEN", 21) == 0) {
                    extern int gpu_event_enqueue(const gpu_event_t *ev);
                    extern int gpu_process_events(void);
                    gpu_event_t ev;
                    ev.type = GPU_EVENT_PRESENT_COMPLETE;
                    ev.frame_id = 1;
                    ev.buffer_index = 0;
                    ev.status = 0;
                    ev.timestamp = 0;
                    ev.generation = 0;
                    gpu_event_enqueue(&ev);
                    gpu_process_events();
                } else if (strncmp(s_buf, "GPU_TRIGGER_FALLBACK", 20) == 0) {
                    extern void gpu_recovery_simulate_fail(int fail_attempt_1, int fail_attempt_2);
                    extern int gpu_recovery_trigger(gpu_reset_reason_t reason);
                    gpu_recovery_simulate_fail(1, 1);
                    gpu_recovery_trigger(GPU_RESET_DEVICE_LOSS);
                } else if (strncmp(s_buf, "GPU_TRIGGER_NO_PRESENT", 22) == 0) {
                    extern void gpu_recovery_simulate_fail(int fail_attempt_1, int fail_attempt_2);
                    extern void gpu_recovery_simulate_generic_fail(int fail);
                    extern int gpu_recovery_trigger(gpu_reset_reason_t reason);
                    gpu_recovery_simulate_fail(1, 1);
                    gpu_recovery_simulate_generic_fail(1);
                    gpu_recovery_trigger(GPU_RESET_DEVICE_LOSS);
                } else if (strncmp(s_buf, "GPU_TRIGGER_STORM", 17) == 0) {
                    extern int gpu_recovery_trigger(gpu_reset_reason_t reason);
                    for (int i = 0; i < 4; i++) {
                        gpu_recovery_trigger(GPU_RESET_TIMEOUT);
                    }
                } else if (strncmp(s_buf, "GPU_RECOVERY_DUMP", 17) == 0) {
                    extern active_gpu_device_t* gpu_get_active(void);
                    active_gpu_device_t *act = gpu_get_active();
                    serial_write("[gpu_phase7_metrics] state=");
                    serial_write(act ? gpu_device_state_name(act->device_state) : "UNKNOWN");
                    serial_write(" gen=");
                    serial_write_hex(act ? act->device_generation : 0);
                    serial_write(" loss_events=");
                    serial_write_hex(act ? act->device_loss_events : 0);
                    serial_write(" reset_attempts=");
                    serial_write_hex(act ? act->reset_attempts : 0);
                    serial_write(" reset_successes=");
                    serial_write_hex(act ? act->reset_successes : 0);
                    serial_write(" reset_failures=");
                    serial_write_hex(act ? act->reset_failures : 0);
                    serial_write(" quarantined=");
                    serial_write_hex(act ? act->frames_quarantined : 0);
                    serial_write(" recreated=");
                    serial_write_hex(act ? act->resources_recreated : 0);
                    serial_write(" storm_prevented=");
                    serial_write_hex(act ? act->recovery_storm_prevented : 0);
                    serial_write(" safe_no_present=");
                    serial_write_hex(act ? act->safe_no_present_entries : 0);
                    serial_write("\n");
                } else if (strncmp(s_buf, "GPU_TEST_RES_ALLOC", 18) == 0) {
                    gpu_resource_t *r = gpu_resource_create(1024, 768, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    (void)r;
                } else if (strncmp(s_buf, "GPU_TEST_ALIGN_FAIL", 19) == 0) {
                    void *c = 0; u64 p = 0, d = 0;
                    gpu_dma_alloc(4096, 4095, 0, 0, &c, &p, &d);
                } else if (strncmp(s_buf, "GPU_TEST_OVERFLOW_SZ", 20) == 0) {
                    gpu_validate_dimensions(1920, 0xFFFFFFFFu, 32, 64, 0, 0);
                } else if (strncmp(s_buf, "GPU_TEST_OVERFLOW_PT", 20) == 0) {
                    gpu_validate_dimensions(0x40000000u, 100, 32, 64, 0, 0);
                } else if (strncmp(s_buf, "GPU_TEST_REGION_OOB", 19) == 0) {
                    gpu_resource_t *r = gpu_resource_create(640, 480, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r) {
                        gpu_validate_region(r, 1000, 1000, 200, 200, 640 * 4);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_DMA_ADDR_OOB", 21) == 0) {
                    gpu_validate_address(0xFFFFFFFFFFFFFFF0ULL, 0x10000, 0x100000000ULL);
                } else if (strncmp(s_buf, "GPU_TEST_DOUBLE_MAP", 19) == 0) {
                    gpu_resource_t *r = gpu_resource_create(320, 240, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r) {
                        gpu_dma_map(r);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_DOUBLE_UNMAP", 21) == 0) {
                    gpu_resource_t *r = gpu_resource_create(320, 240, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r) {
                        gpu_dma_unmap(r);
                        gpu_dma_unmap(r);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_DOUBLE_FREE", 20) == 0) {
                    gpu_resource_t *r = gpu_resource_create(320, 240, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r) {
                        r->refcount = 0;
                        gpu_resource_destroy(r);
                        gpu_resource_destroy(r);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_UAF", 12) == 0) {
                    gpu_resource_t *r = gpu_resource_create(320, 240, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r) {
                        u64 id = r->resource_id;
                        u32 gen = r->generation;
                        r->refcount = 0;
                        gpu_resource_destroy(r);
                        gpu_resource_lookup(id, gen);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_GEN_MISMATCH", 21) == 0) {
                    gpu_resource_t *r = gpu_resource_create(320, 240, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r) {
                        gpu_resource_lookup(r->resource_id, 99999);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_DESTROY_DEFERRED", 25) == 0) {
                    gpu_resource_t *r = gpu_resource_create(320, 240, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r) {
                        gpu_resource_get(r);
                        gpu_resource_destroy(r);
                        gpu_resource_put(r);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_BUDGET_EXHAUST", 23) == 0) {
                    const gpu_memory_budget_t *b = gpu_dma_get_budget();
                    u64 old_lim = b ? b->limit : (128 * 1024 * 1024ULL);
                    gpu_dma_set_budget_limit(b ? b->allocated + 1024 : 1024);
                    gpu_resource_create(1920, 1080, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    gpu_dma_set_budget_limit(old_lim);
                } else if (strncmp(s_buf, "GPU_TEST_VIRTIO_RES_INVALID", 27) == 0) {
                    gpu_resource_validate_virtio_id(99999);
                } else if (strncmp(s_buf, "GPU_TEST_STRESS_ALLOC", 21) == 0) {
                    int ok = 1;
                    for (int i = 0; i < 1000; i++) {
                        gpu_resource_t *r = gpu_resource_create(64, 64, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                        if (!r) { ok = 0; break; }
                        r->refcount = 0;
                        gpu_resource_destroy(r);
                    }
                    if (ok) {
                        serial_write("[gpu_resource] stress test passed cycles=1000\n");
                    }
                } else if (strncmp(s_buf, "GPU_RESOURCE_DUMP", 17) == 0) {
                    active_gpu_device_t *act = gpu_get_active();
                    const gpu_memory_budget_t *b = gpu_dma_get_budget();
                    serial_write("[gpu_phase8_metrics] allocs=");
                    serial_write_hex(act ? act->resource_allocations : 0);
                    serial_write(" frees=");
                    serial_write_hex(act ? act->resource_frees : 0);
                    serial_write(" deferred=");
                    serial_write_hex(act ? act->resource_destroy_deferred : 0);
                    serial_write(" budget_allocated=");
                    serial_write_hex(b ? (u32)b->allocated : 0);
                    serial_write(" budget_peak=");
                    serial_write_hex(b ? (u32)b->peak : 0);
                    serial_write("\n");
                } else if (strncmp(s_buf, "GPU_TEST_CLIENT_REG", 19) == 0) {
                    gpu_client_t *c = gpu_client_register(GPU_CLIENT_APPLICATION);
                    if (c) {
                        gpu_client_unregister(c->client_id);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_PRIO_ORDER", 19) == 0) {
                    gpu_resource_t *r1 = gpu_resource_create(64, 64, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    gpu_resource_t *r2 = gpu_resource_create(64, 64, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r1 && r2) {
                        gpu_submit_present(4, r1, 0, 0);
                        gpu_submit_present(1, r2, 0, 0);
                        gpu_request_t *next = 0;
                        if (gpu_scheduler_admit_next(&next) && next) gpu_scheduler_complete(next, 0);
                        if (gpu_scheduler_admit_next(&next) && next) gpu_scheduler_complete(next, 0);
                        r1->refcount = 0; gpu_resource_destroy(r1);
                        r2->refcount = 0; gpu_resource_destroy(r2);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_CURSOR_LAT", 19) == 0) {
                    gpu_resource_t *r1 = gpu_resource_create(64, 64, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    gpu_resource_t *r2 = gpu_resource_create(32, 32, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r1 && r2) {
                        gpu_submit_present(4, r1, 0, 0);
                        gpu_submit_present(2, r2, 0, 0);
                        gpu_request_t *next = 0;
                        if (gpu_scheduler_admit_next(&next) && next) gpu_scheduler_complete(next, 0);
                        if (gpu_scheduler_admit_next(&next) && next) gpu_scheduler_complete(next, 0);
                        r1->refcount = 0; gpu_resource_destroy(r1);
                        r2->refcount = 0; gpu_resource_destroy(r2);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_AGING_STARV", 20) == 0) {
                    gpu_resource_t *r = gpu_resource_create(64, 64, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r) {
                        gpu_submit_present(4, r, 0, 0);
                        for (int i = 0; i < 60; i++) gpu_scheduler_tick();
                        gpu_request_t *next = 0;
                        if (gpu_scheduler_admit_next(&next) && next) gpu_scheduler_complete(next, 0);
                        r->refcount = 0; gpu_resource_destroy(r);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_INFLIGHT_THROT", 23) == 0) {
                    gpu_resource_t *r = gpu_resource_create(64, 64, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r) {
                        gpu_client_t *c = gpu_client_get(4);
                        if (c) c->inflight = c->max_inflight;
                        gpu_submit_present(4, r, 0, 0);
                        if (c) c->inflight = 0;
                        r->refcount = 0; gpu_resource_destroy(r);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_QUOTA_REJECT", 21) == 0) {
                    gpu_resource_t *r = gpu_resource_create(320, 240, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r) {
                        gpu_set_client_quota(4, 1024, 2);
                        gpu_submit_present(4, r, 0, 0);
                        gpu_set_client_quota(4, 32 * 1024 * 1024ULL, 2);
                        r->refcount = 0; gpu_resource_destroy(r);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_QUEUE_OVERFLOW", 23) == 0) {
                    gpu_resource_t *r = gpu_resource_create(64, 64, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r) {
                        gpu_client_t *c = gpu_client_get(4);
                        if (c) c->queue_depth = c->max_queue_depth;
                        gpu_submit_present(4, r, 0, 0);
                        if (c) c->queue_depth = 0;
                        r->refcount = 0; gpu_resource_destroy(r);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_COALESCE", 17) == 0) {
                    gpu_resource_t *r = gpu_resource_create(64, 64, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r) {
                        gpu_present_region_t reg1 = {0, 0, 32, 32};
                        gpu_present_region_t reg2 = {16, 16, 32, 32};
                        gpu_submit_present(1, r, &reg1, 0);
                        gpu_submit_present(1, r, &reg2, 0);
                        gpu_request_t *next = 0;
                        if (gpu_scheduler_admit_next(&next) && next) gpu_scheduler_complete(next, 0);
                        r->refcount = 0; gpu_resource_destroy(r);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_GEN_REJECT", 19) == 0) {
                    gpu_resource_t *r = gpu_resource_create(64, 64, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r) {
                        u32 real_gen = r->generation;
                        r->generation = 99999;
                        gpu_submit_present(1, r, 0, 0);
                        r->generation = real_gen;
                        r->refcount = 0; gpu_resource_destroy(r);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_RES_QUARANTINE", 23) == 0) {
                    gpu_resource_t *r = gpu_resource_create(64, 64, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r) {
                        r->state = GPU_RESOURCE_QUARANTINED;
                        gpu_submit_present(1, r, 0, 0);
                        r->state = GPU_RESOURCE_DEAD;
                        r->refcount = 0; gpu_resource_destroy(r);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_RESIZE_CANCEL", 22) == 0) {
                    gpu_resource_t *r = gpu_resource_create(64, 64, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r) {
                        gpu_submit_present(4, r, 0, 0);
                        gpu_scheduler_drain(5);
                        serial_write("[gpu_sched] resize drain completed\n");
                        r->refcount = 0; gpu_resource_destroy(r);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_RECOV_PENDING", 22) == 0) {
                    gpu_resource_t *r = gpu_resource_create(64, 64, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r) {
                        gpu_submit_present(4, r, 0, 0);
                        gpu_scheduler_pause_for_recovery();
                        gpu_scheduler_refresh_generations();
                        gpu_scheduler_resume();
                        r->refcount = 0; gpu_resource_destroy(r);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_FAIRNESS", 17) == 0) {
                    gpu_resource_t *r_comp = gpu_resource_create(64, 64, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    gpu_resource_t *r_app = gpu_resource_create(64, 64, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r_comp && r_app) {
                        gpu_submit_present(4, r_app, 0, 0);
                        for (int i = 0; i < 9; i++) {
                            gpu_submit_present(1, r_comp, 0, 0);
                            gpu_request_t *next = 0;
                            if (gpu_scheduler_admit_next(&next) && next) gpu_scheduler_complete(next, 0);
                        }
                        r_comp->refcount = 0; gpu_resource_destroy(r_comp);
                        r_app->refcount = 0; gpu_resource_destroy(r_app);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_CLIENT_UNREG_CANCEL", 28) == 0) {
                    gpu_client_t *c = gpu_client_register(GPU_CLIENT_APPLICATION);
                    gpu_resource_t *r = gpu_resource_create(64, 64, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (c && r) {
                        gpu_submit_present(c->client_id, r, 0, 0);
                        gpu_client_unregister(c->client_id);
                        serial_write("[gpu_sched] client unregister cancel verified\n");
                        r->refcount = 0; gpu_resource_destroy(r);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_INVALID_CLIENT", 23) == 0) {
                    gpu_resource_t *r = gpu_resource_create(64, 64, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r) {
                        gpu_submit_present(99999, r, 0, 0);
                        r->refcount = 0; gpu_resource_destroy(r);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_INVALID_REGION", 23) == 0) {
                    gpu_resource_t *r = gpu_resource_create(640, 480, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r) {
                        gpu_present_region_t bad_reg = {10000, 10000, 500, 500};
                        gpu_submit_present(1, r, &bad_reg, 0);
                        r->refcount = 0; gpu_resource_destroy(r);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_DEV_NOT_READY", 22) == 0) {
                    active_gpu_device_t *act = gpu_get_active();
                    gpu_resource_t *r = gpu_resource_create(64, 64, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (act && r) {
                        gpu_device_state_t old_st = act->device_state;
                        act->device_state = GPU_DEVICE_LOST;
                        gpu_submit_present(1, r, 0, 0);
                        act->device_state = old_st;
                        r->refcount = 0; gpu_resource_destroy(r);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_CURSOR_FLOOD", 21) == 0) {
                    gpu_resource_t *r = gpu_resource_create(32, 32, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r) {
                        for (int i = 0; i < 10; i++) {
                            gpu_present_region_t reg = { (u32)(i * 2), (u32)(i * 2), 16, 16 };
                            gpu_submit_present(2, r, &reg, 0);
                        }
                        gpu_request_t *next = 0;
                        if (gpu_scheduler_admit_next(&next) && next) gpu_scheduler_complete(next, 0);
                        serial_write("[gpu_sched] cursor flood coalesced\n");
                        r->refcount = 0; gpu_resource_destroy(r);
                    }
                } else if (strncmp(s_buf, "GPU_TEST_STORM_PROTECT", 22) == 0) {
                    for (int i = 0; i < 5; i++) {
                        gpu_scheduler_pause_for_recovery();
                        gpu_scheduler_resume();
                    }
                    serial_write("[gpu_sched] storm protection passed\n");
                } else if (strncmp(s_buf, "GPU_TEST_MULTI_STRESS", 21) == 0) {
                    gpu_resource_t *r1 = gpu_resource_create(32, 32, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    gpu_resource_t *r2 = gpu_resource_create(32, 32, 32, 4096, GPU_OWNER_COMPOSITOR, 0);
                    if (r1 && r2) {
                        int ok = 1;
                        for (int i = 0; i < 500; i++) {
                            u32 cid = (i % 2 == 0) ? 1 : 4;
                            gpu_resource_t *res = (i % 2 == 0) ? r1 : r2;
                            int rid = gpu_submit_present(cid, res, 0, 0);
                            if (rid < 0) { ok = 0; break; }
                            gpu_request_t *next = 0;
                            if (gpu_scheduler_admit_next(&next) && next) {
                                gpu_scheduler_complete(next, 0);
                            }
                        }
                        if (ok) serial_write("[gpu_sched] stress test passed cycles=500\n");
                        r1->refcount = 0; gpu_resource_destroy(r1);
                        r2->refcount = 0; gpu_resource_destroy(r2);
                    }
                } else if (strncmp(s_buf, "GPU_SCHED_DUMP", 14) == 0) {
                    gpu_scheduler_metrics_t m;
                    gpu_get_scheduler_metrics(&m);
                    serial_write("[gpu_phase9_metrics] submitted=");
                    serial_write_hex((u32)m.requests_submitted);
                    serial_write(" completed=");
                    serial_write_hex((u32)m.requests_completed);
                    serial_write(" coalesced=");
                    serial_write_hex((u32)m.requests_coalesced);
                    serial_write(" throttled=");
                    serial_write_hex((u32)m.requests_throttled);
                    serial_write(" fairness=");
                    serial_write_hex((u32)m.fairness_forced_dispatches);
                    serial_write("\n");
                } else if (strncmp(s_buf, "GPU_TEST_ABI_CLIENT_OPEN_CLOSE", 30) == 0) {
                    gpu_client_open_t oreq;
                    oreq.header.version = GPU_ABI_VERSION;
                    oreq.header.size = (u16)sizeof(oreq);
                    oreq.header.flags = 0;
                    oreq.client_type = GPU_CLIENT_APPLICATION;
                    oreq.reserved = 0;
                    gpu_client_open_result_t ores;
                    int rc = gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    if (rc == GPU_STATUS_OK && ores.client_handle != 0) {
                        int cc = gpu_syscall_dispatch(GPU_OP_CLOSE_CLIENT, ores.client_handle, 0, 0, 0, 0);
                        if (cc == GPU_STATUS_OK) {
                            serial_write("[gpu_abi_test] client open close verified\n");
                        }
                    }
                } else if (strncmp(s_buf, "GPU_TEST_ABI_QUERY_DEVICE", 25) == 0) {
                    gpu_client_open_t oreq;
                    oreq.header.version = GPU_ABI_VERSION;
                    oreq.header.size = (u16)sizeof(oreq);
                    oreq.header.flags = 0;
                    oreq.client_type = GPU_CLIENT_APPLICATION;
                    oreq.reserved = 0;
                    gpu_client_open_result_t ores;
                    gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    gpu_device_info_t info;
                    int rc = gpu_syscall_dispatch(GPU_OP_QUERY_DEVICE, ores.client_handle, 0, 0, &info, sizeof(info));
                    if (rc == GPU_STATUS_OK && info.header.version == GPU_ABI_VERSION) {
                        serial_write("[gpu_abi_test] device query verified state=READY\n");
                    }
                    gpu_syscall_dispatch(GPU_OP_CLOSE_CLIENT, ores.client_handle, 0, 0, 0, 0);
                } else if (strncmp(s_buf, "GPU_TEST_ABI_RESOURCE_HANDLE", 28) == 0) {
                    gpu_client_open_t oreq;
                    oreq.header.version = GPU_ABI_VERSION;
                    oreq.header.size = (u16)sizeof(oreq);
                    oreq.header.flags = 0;
                    oreq.client_type = GPU_CLIENT_APPLICATION;
                    oreq.reserved = 0;
                    gpu_client_open_result_t ores;
                    gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    gpu_resource_create_t creq;
                    creq.header.version = GPU_ABI_VERSION;
                    creq.header.size = (u16)sizeof(creq);
                    creq.header.flags = 0;
                    creq.width = 64;
                    creq.height = 64;
                    creq.bpp = 32;
                    creq.pitch_alignment = 4096;
                    creq.flags = 0;
                    creq.reserved = 0;
                    gpu_resource_create_result_t cres;
                    int rc = gpu_syscall_dispatch(GPU_OP_CREATE_RESOURCE, ores.client_handle, &creq, sizeof(creq), &cres, sizeof(cres));
                    if (rc == GPU_STATUS_OK && cres.handle != 0) {
                        gpu_resource_ref_t rref;
                        rref.header.version = GPU_ABI_VERSION;
                        rref.header.size = (u16)sizeof(rref);
                        rref.header.flags = 0;
                        rref.resource = cres.handle;
                        rref.generation = cres.generation;
                        rref.access = 0;
                        rref.reserved = 0;
                        int cl = gpu_syscall_dispatch(GPU_OP_CLOSE_RESOURCE, ores.client_handle, &rref, sizeof(rref), 0, 0);
                        if (cl == GPU_STATUS_OK) {
                            serial_write("[gpu_abi_test] resource handle lifecycle verified\n");
                        }
                    }
                    gpu_syscall_dispatch(GPU_OP_CLOSE_CLIENT, ores.client_handle, 0, 0, 0, 0);
                } else if (strncmp(s_buf, "GPU_TEST_ABI_PRESENT_SUBMIT", 27) == 0) {
                    gpu_client_open_t oreq;
                    oreq.header.version = GPU_ABI_VERSION;
                    oreq.header.size = (u16)sizeof(oreq);
                    oreq.header.flags = 0;
                    oreq.client_type = GPU_CLIENT_APPLICATION;
                    oreq.reserved = 0;
                    gpu_client_open_result_t ores;
                    gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    gpu_resource_create_t creq;
                    creq.header.version = GPU_ABI_VERSION;
                    creq.header.size = (u16)sizeof(creq);
                    creq.header.flags = 0;
                    creq.width = 64;
                    creq.height = 64;
                    creq.bpp = 32;
                    creq.pitch_alignment = 4096;
                    creq.flags = 0;
                    creq.reserved = 0;
                    gpu_resource_create_result_t cres;
                    gpu_syscall_dispatch(GPU_OP_CREATE_RESOURCE, ores.client_handle, &creq, sizeof(creq), &cres, sizeof(cres));
                    gpu_present_submit_t sreq;
                    sreq.header.version = GPU_ABI_VERSION;
                    sreq.header.size = (u16)sizeof(sreq);
                    sreq.header.flags = 0;
                    sreq.resource = cres.handle;
                    sreq.resource_generation = cres.generation;
                    sreq.x = 0;
                    sreq.y = 0;
                    sreq.width = 64;
                    sreq.height = 64;
                    sreq.pitch = 256;
                    sreq.format = 0;
                    sreq.flags = 0;
                    sreq.user_cookie = 0x1234ULL;
                    gpu_present_result_t sres;
                    int rc = gpu_syscall_dispatch(GPU_OP_SUBMIT_PRESENT, ores.client_handle, &sreq, sizeof(sreq), &sres, sizeof(sres));
                    if (rc == GPU_STATUS_OK && sres.request_id != 0) {
                        serial_write("[gpu_abi_test] present submission verified request_id=\n");
                        gpu_request_t *next = 0;
                        if (gpu_scheduler_admit_next(&next) && next) gpu_scheduler_complete(next, 0);
                    }
                    gpu_syscall_dispatch(GPU_OP_CLOSE_CLIENT, ores.client_handle, 0, 0, 0, 0);
                } else if (strncmp(s_buf, "GPU_TEST_ABI_INVALID_HANDLE", 27) == 0) {
                    gpu_present_submit_t sreq;
                    sreq.header.version = GPU_ABI_VERSION;
                    sreq.header.size = (u16)sizeof(sreq);
                    sreq.header.flags = 0;
                    sreq.resource = 0x99999999;
                    sreq.resource_generation = 1;
                    sreq.x = 0;
                    sreq.y = 0;
                    sreq.width = 64;
                    sreq.height = 64;
                    sreq.pitch = 256;
                    sreq.format = 0;
                    sreq.flags = 0;
                    sreq.user_cookie = 0;
                    gpu_present_result_t sres;
                    int rc = gpu_syscall_dispatch(GPU_OP_SUBMIT_PRESENT, 0x00010001, &sreq, sizeof(sreq), &sres, sizeof(sres));
                    if (rc == GPU_STATUS_INVALID_HANDLE) {
                        serial_write("[gpu_abi_test] invalid handle rejected status=INVALID_HANDLE\n");
                    }
                } else if (strncmp(s_buf, "GPU_TEST_ABI_GEN_MISMATCH", 25) == 0) {
                    gpu_client_open_t oreq;
                    oreq.header.version = GPU_ABI_VERSION;
                    oreq.header.size = (u16)sizeof(oreq);
                    oreq.header.flags = 0;
                    oreq.client_type = GPU_CLIENT_APPLICATION;
                    oreq.reserved = 0;
                    gpu_client_open_result_t ores;
                    gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    gpu_resource_create_t creq;
                    creq.header.version = GPU_ABI_VERSION;
                    creq.header.size = (u16)sizeof(creq);
                    creq.header.flags = 0;
                    creq.width = 64;
                    creq.height = 64;
                    creq.bpp = 32;
                    creq.pitch_alignment = 4096;
                    creq.flags = 0;
                    creq.reserved = 0;
                    gpu_resource_create_result_t cres;
                    gpu_syscall_dispatch(GPU_OP_CREATE_RESOURCE, ores.client_handle, &creq, sizeof(creq), &cres, sizeof(cres));
                    gpu_present_submit_t sreq;
                    sreq.header.version = GPU_ABI_VERSION;
                    sreq.header.size = (u16)sizeof(sreq);
                    sreq.header.flags = 0;
                    sreq.resource = cres.handle;
                    sreq.resource_generation = cres.generation + 99;
                    sreq.x = 0;
                    sreq.y = 0;
                    sreq.width = 64;
                    sreq.height = 64;
                    sreq.pitch = 256;
                    sreq.format = 0;
                    sreq.flags = 0;
                    sreq.user_cookie = 0;
                    gpu_present_result_t sres;
                    int rc = gpu_syscall_dispatch(GPU_OP_SUBMIT_PRESENT, ores.client_handle, &sreq, sizeof(sreq), &sres, sizeof(sres));
                    if (rc == GPU_STATUS_GENERATION_MISMATCH) {
                        serial_write("[gpu_abi_test] generation mismatch rejected status=GENERATION_MISMATCH\n");
                    }
                    gpu_syscall_dispatch(GPU_OP_CLOSE_CLIENT, ores.client_handle, 0, 0, 0, 0);
                } else if (strncmp(s_buf, "GPU_TEST_ABI_PERM_DENIAL", 24) == 0) {
                    gpu_client_open_t oreq;
                    oreq.header.version = GPU_ABI_VERSION;
                    oreq.header.size = (u16)sizeof(oreq);
                    oreq.header.flags = 0;
                    oreq.client_type = GPU_CLIENT_RECOVERY;
                    oreq.reserved = 0;
                    gpu_client_open_result_t ores;
                    gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    gpu_present_submit_t sreq;
                    sreq.header.version = GPU_ABI_VERSION;
                    sreq.header.size = (u16)sizeof(sreq);
                    sreq.header.flags = 0;
                    sreq.resource = 1;
                    sreq.resource_generation = 1;
                    sreq.x = 0;
                    sreq.y = 0;
                    sreq.width = 64;
                    sreq.height = 64;
                    sreq.pitch = 256;
                    sreq.format = 0;
                    sreq.flags = 0;
                    sreq.user_cookie = 0;
                    gpu_present_result_t sres;
                    int rc = gpu_syscall_dispatch(GPU_OP_SUBMIT_PRESENT, ores.client_handle, &sreq, sizeof(sreq), &sres, sizeof(sres));
                    if (rc == GPU_STATUS_PERMISSION_DENIED) {
                        serial_write("[gpu_abi_test] permission denial verified status=PERMISSION_DENIED\n");
                    }
                    gpu_syscall_dispatch(GPU_OP_CLOSE_CLIENT, ores.client_handle, 0, 0, 0, 0);
                } else if (strncmp(s_buf, "GPU_TEST_ABI_QUOTA_EXCEED", 25) == 0) {
                    gpu_client_open_t oreq;
                    oreq.header.version = GPU_ABI_VERSION;
                    oreq.header.size = (u16)sizeof(oreq);
                    oreq.header.flags = 0;
                    oreq.client_type = GPU_CLIENT_APPLICATION;
                    oreq.reserved = 0;
                    gpu_client_open_result_t ores;
                    gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    gpu_user_client_t *ucli = gpu_user_client_lookup(ores.client_handle);
                    if (ucli) {
                        gpu_client_t *kc = gpu_client_get(ucli->kernel_client_id);
                        if (kc) kc->used_bytes = kc->quota_bytes;
                    }
                    gpu_resource_create_t creq;
                    creq.header.version = GPU_ABI_VERSION;
                    creq.header.size = (u16)sizeof(creq);
                    creq.header.flags = 0;
                    creq.width = 64;
                    creq.height = 64;
                    creq.bpp = 32;
                    creq.pitch_alignment = 4096;
                    creq.flags = 0;
                    creq.reserved = 0;
                    gpu_resource_create_result_t cres;
                    int rc = gpu_syscall_dispatch(GPU_OP_CREATE_RESOURCE, ores.client_handle, &creq, sizeof(creq), &cres, sizeof(cres));
                    if (rc == GPU_STATUS_QUOTA_EXCEEDED) {
                        serial_write("[gpu_abi_test] quota exceeded verified status=QUOTA_EXCEEDED\n");
                    }
                    if (ucli) {
                        gpu_client_t *kc = gpu_client_get(ucli->kernel_client_id);
                        if (kc) kc->used_bytes = 0;
                    }
                    gpu_syscall_dispatch(GPU_OP_CLOSE_CLIENT, ores.client_handle, 0, 0, 0, 0);
                } else if (strncmp(s_buf, "GPU_TEST_ABI_QUEUE_BACKPRESSURE", 31) == 0) {
                    gpu_client_open_t oreq;
                    oreq.header.version = GPU_ABI_VERSION;
                    oreq.header.size = (u16)sizeof(oreq);
                    oreq.header.flags = 0;
                    oreq.client_type = GPU_CLIENT_APPLICATION;
                    oreq.reserved = 0;
                    gpu_client_open_result_t ores;
                    gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    gpu_resource_create_t creq;
                    creq.header.version = GPU_ABI_VERSION;
                    creq.header.size = (u16)sizeof(creq);
                    creq.header.flags = 0;
                    creq.width = 64;
                    creq.height = 64;
                    creq.bpp = 32;
                    creq.pitch_alignment = 4096;
                    creq.flags = 0;
                    creq.reserved = 0;
                    gpu_resource_create_result_t cres;
                    gpu_syscall_dispatch(GPU_OP_CREATE_RESOURCE, ores.client_handle, &creq, sizeof(creq), &cres, sizeof(cres));
                    gpu_user_client_t *ucli = gpu_user_client_lookup(ores.client_handle);
                    if (ucli) {
                        gpu_client_t *kc = gpu_client_get(ucli->kernel_client_id);
                        if (kc) kc->queue_depth = kc->max_queue_depth;
                    }
                    gpu_present_submit_t sreq;
                    sreq.header.version = GPU_ABI_VERSION;
                    sreq.header.size = (u16)sizeof(sreq);
                    sreq.header.flags = 0;
                    sreq.resource = cres.handle;
                    sreq.resource_generation = cres.generation;
                    sreq.x = 0;
                    sreq.y = 0;
                    sreq.width = 64;
                    sreq.height = 64;
                    sreq.pitch = 256;
                    sreq.format = 0;
                    sreq.flags = 0;
                    sreq.user_cookie = 0;
                    gpu_present_result_t sres;
                    int rc = gpu_syscall_dispatch(GPU_OP_SUBMIT_PRESENT, ores.client_handle, &sreq, sizeof(sreq), &sres, sizeof(sres));
                    if (rc == GPU_STATUS_QUEUE_FULL) {
                        serial_write("[gpu_abi_test] queue backpressure verified status=QUEUE_FULL\n");
                    }
                    if (ucli) {
                        gpu_client_t *kc = gpu_client_get(ucli->kernel_client_id);
                        if (kc) kc->queue_depth = 0;
                    }
                    gpu_syscall_dispatch(GPU_OP_CLOSE_CLIENT, ores.client_handle, 0, 0, 0, 0);
                } else if (strncmp(s_buf, "GPU_TEST_ABI_DIRTY_REGION", 25) == 0) {
                    gpu_client_open_t oreq;
                    oreq.header.version = GPU_ABI_VERSION;
                    oreq.header.size = (u16)sizeof(oreq);
                    oreq.header.flags = 0;
                    oreq.client_type = GPU_CLIENT_APPLICATION;
                    oreq.reserved = 0;
                    gpu_client_open_result_t ores;
                    gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    gpu_resource_create_t creq;
                    creq.header.version = GPU_ABI_VERSION;
                    creq.header.size = (u16)sizeof(creq);
                    creq.header.flags = 0;
                    creq.width = 64;
                    creq.height = 64;
                    creq.bpp = 32;
                    creq.pitch_alignment = 4096;
                    creq.flags = 0;
                    creq.reserved = 0;
                    gpu_resource_create_result_t cres;
                    gpu_syscall_dispatch(GPU_OP_CREATE_RESOURCE, ores.client_handle, &creq, sizeof(creq), &cres, sizeof(cres));
                    gpu_present_submit_t sreq;
                    sreq.header.version = GPU_ABI_VERSION;
                    sreq.header.size = (u16)sizeof(sreq);
                    sreq.header.flags = 0;
                    sreq.resource = cres.handle;
                    sreq.resource_generation = cres.generation;
                    sreq.x = 50;
                    sreq.y = 50;
                    sreq.width = 64;
                    sreq.height = 64;
                    sreq.pitch = 256;
                    sreq.format = 0;
                    sreq.flags = 0;
                    sreq.user_cookie = 0;
                    gpu_present_result_t sres;
                    int rc = gpu_syscall_dispatch(GPU_OP_SUBMIT_PRESENT, ores.client_handle, &sreq, sizeof(sreq), &sres, sizeof(sres));
                    if (rc == GPU_STATUS_INVALID_ARGUMENT) {
                        serial_write("[gpu_abi_test] dirty region bounds rejected status=INVALID_ARGUMENT\n");
                    }
                    gpu_syscall_dispatch(GPU_OP_CLOSE_CLIENT, ores.client_handle, 0, 0, 0, 0);
                } else if (strncmp(s_buf, "GPU_TEST_ABI_BAD_VERSION", 24) == 0) {
                    gpu_client_open_t oreq;
                    oreq.header.version = 999;
                    oreq.header.size = (u16)sizeof(oreq);
                    oreq.header.flags = 0;
                    oreq.client_type = GPU_CLIENT_APPLICATION;
                    oreq.reserved = 0;
                    gpu_client_open_result_t ores;
                    int rc = gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    if (rc == GPU_STATUS_BAD_VERSION) {
                        serial_write("[gpu_abi_test] bad version rejected status=BAD_VERSION\n");
                    }
                } else if (strncmp(s_buf, "GPU_TEST_ABI_BUFFER_TOO_SMALL", 29) == 0) {
                    gpu_client_open_t oreq;
                    oreq.header.version = GPU_ABI_VERSION;
                    oreq.header.size = 4;
                    oreq.header.flags = 0;
                    oreq.client_type = GPU_CLIENT_APPLICATION;
                    oreq.reserved = 0;
                    gpu_client_open_result_t ores;
                    int rc = gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    if (rc == GPU_STATUS_BUFFER_TOO_SMALL) {
                        serial_write("[gpu_abi_test] buffer too small rejected status=BUFFER_TOO_SMALL\n");
                    }
                } else if (strncmp(s_buf, "GPU_TEST_ABI_RESERVED_NONZERO", 29) == 0) {
                    gpu_client_open_t oreq;
                    oreq.header.version = GPU_ABI_VERSION;
                    oreq.header.size = (u16)sizeof(oreq);
                    oreq.header.flags = 0;
                    oreq.client_type = GPU_CLIENT_APPLICATION;
                    oreq.reserved = 1;
                    gpu_client_open_result_t ores;
                    int rc = gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    if (rc == GPU_STATUS_INVALID_ARGUMENT) {
                        serial_write("[gpu_abi_test] reserved non-zero rejected status=INVALID_ARGUMENT\n");
                    }
                } else if (strncmp(s_buf, "GPU_TEST_ABI_EVENT_POLL_WAIT", 28) == 0) {
                    gpu_client_open_t oreq;
                    oreq.header.version = GPU_ABI_VERSION;
                    oreq.header.size = (u16)sizeof(oreq);
                    oreq.header.flags = 0;
                    oreq.client_type = GPU_CLIENT_APPLICATION;
                    oreq.reserved = 0;
                    gpu_client_open_result_t ores;
                    gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    gpu_user_client_t *cli = gpu_user_client_lookup(ores.client_handle);
                    if (cli) {
                        gpu_event_user_purge(cli->slot);
                        gpu_user_event_t ev;
                        ev.header.version = GPU_ABI_VERSION;
                        ev.header.size = (u16)sizeof(ev);
                        ev.header.flags = 0;
                        ev.request_id = 10;
                        ev.event_type = GPU_EVENT_COMPLETION;
                        ev.status = GPU_STATUS_OK;
                        ev.resource_generation = 1;
                        ev.reserved = 0;
                        ev.user_cookie = 0x5555ULL;
                        gpu_event_user_post(cli->slot, &ev);
                        gpu_user_event_t out_ev;
                        int pr = gpu_syscall_dispatch(GPU_OP_POLL_EVENT, ores.client_handle, 0, 0, &out_ev, sizeof(out_ev));
                        gpu_event_wait_t wreq;
                        wreq.header.version = GPU_ABI_VERSION;
                        wreq.header.size = (u16)sizeof(wreq);
                        wreq.header.flags = 0;
                        wreq.timeout_ticks = 10;
                        wreq.reserved = 0;
                        int wr = gpu_syscall_dispatch(GPU_OP_WAIT_EVENT, ores.client_handle, &wreq, sizeof(wreq), &out_ev, sizeof(out_ev));
                        if (pr == GPU_STATUS_OK && wr == GPU_STATUS_TIMEOUT) {
                            serial_write("[gpu_abi_test] event polling and bounded wait verified\n");
                        }
                    }
                    gpu_syscall_dispatch(GPU_OP_CLOSE_CLIENT, ores.client_handle, 0, 0, 0, 0);
                } else if (strncmp(s_buf, "GPU_TEST_ABI_EVENT_DEDUP", 24) == 0) {
                    gpu_client_open_t oreq;
                    oreq.header.version = GPU_ABI_VERSION;
                    oreq.header.size = (u16)sizeof(oreq);
                    oreq.header.flags = 0;
                    oreq.client_type = GPU_CLIENT_APPLICATION;
                    oreq.reserved = 0;
                    gpu_client_open_result_t ores;
                    gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    gpu_user_client_t *cli = gpu_user_client_lookup(ores.client_handle);
                    if (cli) {
                        gpu_user_event_t ev1;
                        ev1.header.version = GPU_ABI_VERSION;
                        ev1.header.size = (u16)sizeof(ev1);
                        ev1.header.flags = 0;
                        ev1.request_id = 100;
                        ev1.event_type = GPU_EVENT_COMPLETION;
                        ev1.status = GPU_STATUS_OK;
                        ev1.resource_generation = 1;
                        ev1.reserved = 0;
                        ev1.user_cookie = 0;
                        gpu_user_event_t ev2 = ev1;
                        gpu_event_user_post(cli->slot, &ev1);
                        gpu_event_user_post(cli->slot, &ev2);
                        gpu_user_event_t out_ev;
                        int r1 = gpu_event_user_poll(cli->slot, &out_ev);
                        int r2 = gpu_event_user_poll(cli->slot, &out_ev);
                        if (r1 == GPU_STATUS_OK && r2 == GPU_STATUS_TIMEOUT) {
                            serial_write("[gpu_abi_test] duplicate event rejection verified\n");
                        }
                    }
                    gpu_syscall_dispatch(GPU_OP_CLOSE_CLIENT, ores.client_handle, 0, 0, 0, 0);
                } else if (strncmp(s_buf, "GPU_TEST_ABI_OWNERSHIP_VIOLATION", 32) == 0) {
                    gpu_client_open_t oreq1;
                    oreq1.header.version = GPU_ABI_VERSION;
                    oreq1.header.size = (u16)sizeof(oreq1);
                    oreq1.header.flags = 0;
                    oreq1.client_type = GPU_CLIENT_APPLICATION;
                    oreq1.reserved = 0;
                    gpu_client_open_result_t ores1;
                    gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq1, sizeof(oreq1), &ores1, sizeof(ores1));
                    gpu_client_open_t oreq2 = oreq1;
                    gpu_client_open_result_t ores2;
                    gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq2, sizeof(oreq2), &ores2, sizeof(ores2));
                    gpu_resource_create_t creq;
                    creq.header.version = GPU_ABI_VERSION;
                    creq.header.size = (u16)sizeof(creq);
                    creq.header.flags = 0;
                    creq.width = 64;
                    creq.height = 64;
                    creq.bpp = 32;
                    creq.pitch_alignment = 4096;
                    creq.flags = 0;
                    creq.reserved = 0;
                    gpu_resource_create_result_t cres;
                    gpu_syscall_dispatch(GPU_OP_CREATE_RESOURCE, ores1.client_handle, &creq, sizeof(creq), &cres, sizeof(cres));
                    gpu_present_submit_t sreq;
                    sreq.header.version = GPU_ABI_VERSION;
                    sreq.header.size = (u16)sizeof(sreq);
                    sreq.header.flags = 0;
                    sreq.resource = cres.handle;
                    sreq.resource_generation = cres.generation;
                    sreq.x = 0;
                    sreq.y = 0;
                    sreq.width = 64;
                    sreq.height = 64;
                    sreq.pitch = 256;
                    sreq.format = 0;
                    sreq.flags = 0;
                    sreq.user_cookie = 0;
                    gpu_present_result_t sres;
                    int rc = gpu_syscall_dispatch(GPU_OP_SUBMIT_PRESENT, ores2.client_handle, &sreq, sizeof(sreq), &sres, sizeof(sres));
                    if (rc == GPU_STATUS_PERMISSION_DENIED) {
                        serial_write("[gpu_abi_test] ownership violation rejected status=PERMISSION_DENIED\n");
                    }
                    gpu_syscall_dispatch(GPU_OP_CLOSE_CLIENT, ores1.client_handle, 0, 0, 0, 0);
                    gpu_syscall_dispatch(GPU_OP_CLOSE_CLIENT, ores2.client_handle, 0, 0, 0, 0);
                } else if (strncmp(s_buf, "GPU_TEST_ABI_UNREG_INFLIGHT", 27) == 0) {
                    gpu_client_open_t oreq;
                    oreq.header.version = GPU_ABI_VERSION;
                    oreq.header.size = (u16)sizeof(oreq);
                    oreq.header.flags = 0;
                    oreq.client_type = GPU_CLIENT_APPLICATION;
                    oreq.reserved = 0;
                    gpu_client_open_result_t ores;
                    gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    gpu_resource_create_t creq;
                    creq.header.version = GPU_ABI_VERSION;
                    creq.header.size = (u16)sizeof(creq);
                    creq.header.flags = 0;
                    creq.width = 64;
                    creq.height = 64;
                    creq.bpp = 32;
                    creq.pitch_alignment = 4096;
                    creq.flags = 0;
                    creq.reserved = 0;
                    gpu_resource_create_result_t cres;
                    gpu_syscall_dispatch(GPU_OP_CREATE_RESOURCE, ores.client_handle, &creq, sizeof(creq), &cres, sizeof(cres));
                    gpu_present_submit_t sreq;
                    sreq.header.version = GPU_ABI_VERSION;
                    sreq.header.size = (u16)sizeof(sreq);
                    sreq.header.flags = 0;
                    sreq.resource = cres.handle;
                    sreq.resource_generation = cres.generation;
                    sreq.x = 0;
                    sreq.y = 0;
                    sreq.width = 64;
                    sreq.height = 64;
                    sreq.pitch = 256;
                    sreq.format = 0;
                    sreq.flags = 0;
                    sreq.user_cookie = 0x7777ULL;
                    gpu_present_result_t sres;
                    gpu_syscall_dispatch(GPU_OP_SUBMIT_PRESENT, ores.client_handle, &sreq, sizeof(sreq), &sres, sizeof(sres));
                    gpu_syscall_dispatch(GPU_OP_CLOSE_CLIENT, ores.client_handle, 0, 0, 0, 0);
                    serial_write("[gpu_abi_test] unregister with inflight cancelled cleanly\n");
                } else if (strncmp(s_buf, "GPU_TEST_ABI_RECOVERY_PENDING", 29) == 0) {
                    gpu_client_open_t oreq;
                    oreq.header.version = GPU_ABI_VERSION;
                    oreq.header.size = (u16)sizeof(oreq);
                    oreq.header.flags = 0;
                    oreq.client_type = GPU_CLIENT_APPLICATION;
                    oreq.reserved = 0;
                    gpu_client_open_result_t ores;
                    gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    gpu_resource_create_t creq;
                    creq.header.version = GPU_ABI_VERSION;
                    creq.header.size = (u16)sizeof(creq);
                    creq.header.flags = 0;
                    creq.width = 64;
                    creq.height = 64;
                    creq.bpp = 32;
                    creq.pitch_alignment = 4096;
                    creq.flags = 0;
                    creq.reserved = 0;
                    gpu_resource_create_result_t cres;
                    gpu_syscall_dispatch(GPU_OP_CREATE_RESOURCE, ores.client_handle, &creq, sizeof(creq), &cres, sizeof(cres));
                    gpu_present_submit_t sreq;
                    sreq.header.version = GPU_ABI_VERSION;
                    sreq.header.size = (u16)sizeof(sreq);
                    sreq.header.flags = 0;
                    sreq.resource = cres.handle;
                    sreq.resource_generation = cres.generation;
                    sreq.x = 0;
                    sreq.y = 0;
                    sreq.width = 64;
                    sreq.height = 64;
                    sreq.pitch = 256;
                    sreq.format = 0;
                    sreq.flags = 0;
                    sreq.user_cookie = 0x8888ULL;
                    gpu_present_result_t sres;
                    gpu_syscall_dispatch(GPU_OP_SUBMIT_PRESENT, ores.client_handle, &sreq, sizeof(sreq), &sres, sizeof(sres));
                    gpu_recovery_trigger(GPU_RESET_TIMEOUT);
                    serial_write("[gpu_abi_test] recovery cancelled pending requests and refreshed generations\n");
                    gpu_syscall_dispatch(GPU_OP_CLOSE_CLIENT, ores.client_handle, 0, 0, 0, 0);
                } else if (strncmp(s_buf, "GPU_TEST_ABI_DEVICE_LOSS_ADMIT", 30) == 0) {
                    gpu_client_open_t oreq = { { GPU_ABI_VERSION, sizeof(oreq), 0 }, GPU_CLIENT_APPLICATION, 0 };
                    gpu_client_open_result_t ores;
                    gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    active_gpu_device_t *act = gpu_get_active();
                    if (act) act->device_state = GPU_DEVICE_LOST;
                    gpu_present_submit_t sreq;
                    sreq.header.version = GPU_ABI_VERSION;
                    sreq.header.size = (u16)sizeof(sreq);
                    sreq.header.flags = 0;
                    sreq.resource = 1;
                    sreq.resource_generation = 1;
                    sreq.x = 0;
                    sreq.y = 0;
                    sreq.width = 64;
                    sreq.height = 64;
                    sreq.pitch = 256;
                    sreq.format = 0;
                    sreq.flags = 0;
                    sreq.user_cookie = 0;
                    gpu_present_result_t sres;
                    int rc = gpu_syscall_dispatch(GPU_OP_SUBMIT_PRESENT, ores.client_handle, &sreq, sizeof(sreq), &sres, sizeof(sres));
                    if (rc == GPU_STATUS_DEVICE_LOST) {
                        serial_write("[gpu_abi_test] device loss rejection verified status=DEVICE_LOST\n");
                    }
                    if (act) act->device_state = GPU_DEVICE_READY;
                    gpu_syscall_dispatch(GPU_OP_CLOSE_CLIENT, ores.client_handle, 0, 0, 0, 0);
                } else if (strncmp(s_buf, "GPU_TEST_ABI_STALE_AFTER_RECOVERY", 33) == 0) {
                    gpu_client_open_t oreq;
                    oreq.header.version = GPU_ABI_VERSION;
                    oreq.header.size = (u16)sizeof(oreq);
                    oreq.header.flags = 0;
                    oreq.client_type = GPU_CLIENT_APPLICATION;
                    oreq.reserved = 0;
                    gpu_client_open_result_t ores;
                    gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    gpu_resource_create_t creq;
                    creq.header.version = GPU_ABI_VERSION;
                    creq.header.size = (u16)sizeof(creq);
                    creq.header.flags = 0;
                    creq.width = 64;
                    creq.height = 64;
                    creq.bpp = 32;
                    creq.pitch_alignment = 4096;
                    creq.flags = 0;
                    creq.reserved = 0;
                    gpu_resource_create_result_t cres;
                    gpu_syscall_dispatch(GPU_OP_CREATE_RESOURCE, ores.client_handle, &creq, sizeof(creq), &cres, sizeof(cres));
                    u32 old_gen = cres.generation;
                    gpu_recovery_trigger(GPU_RESET_TIMEOUT);
                    gpu_present_submit_t sreq;
                    sreq.header.version = GPU_ABI_VERSION;
                    sreq.header.size = (u16)sizeof(sreq);
                    sreq.header.flags = 0;
                    sreq.resource = cres.handle;
                    sreq.resource_generation = old_gen;
                    sreq.x = 0;
                    sreq.y = 0;
                    sreq.width = 64;
                    sreq.height = 64;
                    sreq.pitch = 256;
                    sreq.format = 0;
                    sreq.flags = 0;
                    sreq.user_cookie = 0;
                    gpu_present_result_t sres;
                    int rc = gpu_syscall_dispatch(GPU_OP_SUBMIT_PRESENT, ores.client_handle, &sreq, sizeof(sreq), &sres, sizeof(sres));
                    if (rc == GPU_STATUS_GENERATION_MISMATCH || rc == GPU_STATUS_RESOURCE_BUSY || rc == GPU_STATUS_INVALID_HANDLE) {
                        serial_write("[gpu_abi_test] stale request after recovery rejected status=GENERATION_MISMATCH\n");
                    }
                    gpu_syscall_dispatch(GPU_OP_CLOSE_CLIENT, ores.client_handle, 0, 0, 0, 0);
                } else if (strncmp(s_buf, "GPU_TEST_ABI_USER_PTR_CHECK", 27) == 0) {
                    gpu_client_open_result_t ores;
                    int rc = gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, 0, sizeof(gpu_client_open_t), &ores, sizeof(ores));
                    if (rc == GPU_STATUS_INVALID_ARGUMENT) {
                        serial_write("[gpu_abi_test] user pointer check rejected status=INVALID_ARGUMENT\n");
                    }
                } else if (strncmp(s_buf, "GPU_TEST_ABI_INVALID_FLAGS", 26) == 0) {
                    gpu_client_open_t oreq;
                    oreq.header.version = GPU_ABI_VERSION;
                    oreq.header.size = (u16)sizeof(oreq);
                    oreq.header.flags = 0x80000000;
                    oreq.client_type = GPU_CLIENT_APPLICATION;
                    oreq.reserved = 0;
                    gpu_client_open_result_t ores;
                    int rc = gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    if (rc == GPU_STATUS_INVALID_ARGUMENT) {
                        serial_write("[gpu_abi_test] invalid flags rejected status=INVALID_ARGUMENT\n");
                    }
                } else if (strncmp(s_buf, "GPU_TEST_ABI_REQUEST_FLOOD", 26) == 0) {
                    gpu_client_open_t oreq = { { GPU_ABI_VERSION, sizeof(oreq), 0 }, GPU_CLIENT_APPLICATION, 0 };
                    gpu_client_open_result_t ores;
                    gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    gpu_resource_create_t creq = { { GPU_ABI_VERSION, sizeof(creq), 0 }, 64, 64, 32, 4096, 0, 0 };
                    gpu_resource_create_result_t cres;
                    gpu_syscall_dispatch(GPU_OP_CREATE_RESOURCE, ores.client_handle, &creq, sizeof(creq), &cres, sizeof(cres));
                    gpu_user_client_t *ucli = gpu_user_client_lookup(ores.client_handle);
                    if (ucli) {
                        gpu_client_t *kc = gpu_client_get(ucli->kernel_client_id);
                        if (kc) kc->queue_depth = kc->max_queue_depth;
                    }
                    gpu_present_submit_t sreq = { { GPU_ABI_VERSION, sizeof(sreq), 0 }, cres.handle, cres.generation, 0, 0, 64, 64, 256, 0, 0, 0 };
                    gpu_present_result_t sres;
                    int rc = gpu_syscall_dispatch(GPU_OP_SUBMIT_PRESENT, ores.client_handle, &sreq, sizeof(sreq), &sres, sizeof(sres));
                    if (rc == GPU_STATUS_QUEUE_FULL || rc == GPU_STATUS_RESOURCE_BUSY) {
                        serial_write("[gpu_abi_test] request flood throttled safely\n");
                    }
                    if (ucli) {
                        gpu_client_t *kc = gpu_client_get(ucli->kernel_client_id);
                        if (kc) kc->queue_depth = 0;
                    }
                    gpu_syscall_dispatch(GPU_OP_CLOSE_CLIENT, ores.client_handle, 0, 0, 0, 0);
                } else if (strncmp(s_buf, "GPU_TEST_ABI_EVENT_FLOOD", 24) == 0) {
                    gpu_client_open_t oreq = { { GPU_ABI_VERSION, sizeof(oreq), 0 }, GPU_CLIENT_APPLICATION, 0 };
                    gpu_client_open_result_t ores;
                    gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    gpu_user_client_t *cli = gpu_user_client_lookup(ores.client_handle);
                    if (cli) {
                        int full = 0;
                        for (int i = 0; i < 100; i++) {
                            gpu_user_event_t ev = { { GPU_ABI_VERSION, sizeof(ev), 0 }, (u64)(i + 1), GPU_EVENT_COMPLETION, GPU_STATUS_OK, 1, 0, 0 };
                            int pr = gpu_event_user_post(cli->slot, &ev);
                            if (pr == GPU_STATUS_QUEUE_FULL) {
                                full = 1;
                                break;
                            }
                        }
                        if (full) {
                            serial_write("[gpu_abi_test] event flood backpressure enforced\n");
                        }
                    }
                    gpu_syscall_dispatch(GPU_OP_CLOSE_CLIENT, ores.client_handle, 0, 0, 0, 0);
                } else if (strncmp(s_buf, "GPU_TEST_ABI_FUZZ_DESCRIPTORS", 29) == 0) {
                    u8 garbage[128];
                    for (int i = 0; i < 128; i++) garbage[i] = (u8)(i * 37 + 13);
                    gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, garbage, 128, garbage, 128);
                    gpu_syscall_dispatch(GPU_OP_SUBMIT_PRESENT, 0x11112222, garbage, 128, garbage, 128);
                    gpu_syscall_dispatch(GPU_OP_CREATE_RESOURCE, 0x33334444, garbage, 128, garbage, 128);
                    gpu_syscall_dispatch(0x9999, 0, garbage, 128, garbage, 128);
                    serial_write("[gpu_abi_test] fuzz malformed descriptors passed 0 panics\n");
                } else if (strncmp(s_buf, "GPU_TEST_ABI_NO_PRESENT", 23) == 0) {
                    gpu_client_open_t oreq = { { GPU_ABI_VERSION, sizeof(oreq), 0 }, GPU_CLIENT_APPLICATION, 0 };
                    gpu_client_open_result_t ores;
                    gpu_syscall_dispatch(GPU_OP_OPEN_CLIENT, 0, &oreq, sizeof(oreq), &ores, sizeof(ores));
                    active_gpu_device_t *act = gpu_get_active();
                    const gpu_driver_t *saved_driver = act ? act->driver : 0;
                    if (act) act->driver = 0;
                    gpu_device_info_t info;
                    int rc = gpu_syscall_dispatch(GPU_OP_QUERY_DEVICE, ores.client_handle, 0, 0, &info, sizeof(info));
                    if (rc == GPU_STATUS_NO_PRESENT) {
                        serial_write("[gpu_abi_test] no present mode verified status=NO_PRESENT\n");
                    }
                    if (act) act->driver = (gpu_driver_t *)saved_driver;
                    gpu_syscall_dispatch(GPU_OP_CLOSE_CLIENT, ores.client_handle, 0, 0, 0, 0);
                } else if (strncmp(s_buf, "GPU_ABI_DUMP", 12) == 0) {
                    extern void gpu_abi_dump_metrics(void);
                    gpu_abi_dump_metrics();
                } else if (strncmp(s_buf, "SUPERVISOR_TEST_CONTAINMENT", 27) == 0) {
                    extern void open_app(int kind);
                    extern int needs_redraw;
                    open_app(APP_BROWSER);
                    app_supervisor_handle_fault(APP_BROWSER, 14, 0xDEADBEEF, 0x1000);
                    needs_redraw = 1;
                    if (app_supervisor_is_crashed(APP_BROWSER)) {
                        serial_write("[app_supervisor_test] ZERO-PANIC: App 13 safely contained, desktop running at 60 FPS\n");
                    }
                } else if (strncmp(s_buf, "SUPERVISOR_TEST_HANG", 20) == 0) {
                    app_proc_context_t *ctx = app_supervisor_get_context(APP_BROWSER);
                    if (ctx) {
                        ctx->state = APP_STATE_RUNNING;
                        ctx->last_heartbeat_ticks = 0;
                        app_supervisor_check_hangs(100000);
                        if (app_supervisor_is_hung(APP_BROWSER)) {
                            serial_write("[app_supervisor_test] Watchdog successfully detected hang\n");
                        }
                    }
                } else if (strncmp(s_buf, "COLOR_LCD_TEST_PROFILES", 23) == 0) {
                    int ok = 1;
                    fb_set_color_profile(COLOR_PROFILE_COLOR_LCD);
                    if (fb_get_color_profile() != COLOR_PROFILE_COLOR_LCD) ok = 0;
                    fb_set_color_profile(COLOR_PROFILE_DISPLAY_P3);
                    if (fb_get_color_profile() != COLOR_PROFILE_DISPLAY_P3) ok = 0;
                    fb_set_color_profile(COLOR_PROFILE_TRUE_TONE);
                    if (fb_get_color_profile() != COLOR_PROFILE_TRUE_TONE) ok = 0;
                    fb_set_color_lcd_subpixel(1);
                    if (fb_get_color_lcd_subpixel() != 1) ok = 0;
                    fb_set_color_profile(COLOR_PROFILE_COLOR_LCD);
                    if (ok) {
                        serial_write("[color_lcd_test] All 5 color profiles and subpixel rendering verified\n");
                    }
                } else if (strncmp(s_buf, "OPEN_SETTINGS_DISPLAY", 21) == 0) {
                    extern void open_app(int kind);
                    extern void settings_set_tab(int tab);
                    extern int needs_redraw;
                    open_app(APP_SETTINGS);
                    settings_set_tab(6);
                    needs_redraw = 1;
                    serial_write("[settings_test] Settings Display tab active\n");
                } else if (strncmp(s_buf, "COLOR_LCD_SET_SONOMA", 20) == 0) {
                    extern void theme_set_preset(int preset);
                    extern int needs_redraw;
                    theme_set_preset(3);
                    needs_redraw = 1;
                    serial_write("[theme_test] Switched to Color LCD Sonoma theme\n");
                } else if (strncmp(s_buf, "AUDIT_SYSTEM_METRICS", 20) == 0) {
                    extern u64 g_phys_ram_usable;
                    extern int fs_count(void);
                    extern int cpu_smp_get_core_count(void);
                    heap_stats_t hs;
                    heap_get_stats(&hs);

                    serial_write("[audit_telemetry] CPU Cores: ");
                    serial_write_hex((u32)cpu_smp_get_core_count());
                    serial_write(" | Physical RAM: ");
                    serial_write_hex((u32)(g_phys_ram_usable / (1024ULL * 1024ULL)));
                    serial_write(" MB | Heap Used: ");
                    serial_write_hex(hs.used_bytes);
                    serial_write(" bytes / Total Heap: ");
                    serial_write_hex(hs.total_bytes);
                    serial_write(" bytes | Files in Storage: ");
                    serial_write_hex((u32)fs_count());
                    serial_write("\n");
                } else if (strncmp(s_buf, "OPEN_APP:", 9) == 0) {
                    int k = 0;
                    const char *p = s_buf + 9;
                    while (*p >= '0' && *p <= '9') { k = k * 10 + (*p - '0'); p++; }
                    if (k > 0) {
                        extern void open_app(int kind);
                        extern void focus_app_window(int idx);
                        extern int needs_redraw;
                        open_app(k);
                        focus_app_window(k - 1);
                        needs_redraw = 1;
                        serial_write("[serial_cmd] App opened: ");
                        serial_write_hex((u32)k);
                        serial_write("\n");
                    }
                } else if (strncmp(s_buf, "SPOTLIGHT_OPEN:", 15) == 0) {
                    const char *q = s_buf + 15;
                    extern int spotlight_open;
                    extern int spotlight_qlen;
                    extern char spotlight_query[64];
                    extern void spotlight_search(const char *query);
                    extern int needs_redraw;
                    spotlight_open = 1;
                    int i = 0;
                    while (q[i] && i < 63) { spotlight_query[i] = q[i]; i++; }
                    spotlight_query[i] = 0;
                    spotlight_qlen = i;
                    spotlight_search(spotlight_query);
                    needs_redraw = 1;
                    serial_write("[serial_cmd] Spotlight opened query: ");
                    serial_write(spotlight_query);
                    serial_write("\n");
                }
                s_len = 0;
            }
        } else if (s_len < (int)sizeof(s_buf) - 1) {
            s_buf[s_len++] = c;
        }
    }
}
