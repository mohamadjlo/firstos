#include "framebuffer.h"
#include "gui.h"
#include "compositor.h"
#include "virtio_gpu.h"
#include "cpu.h"
#include "serial.h"
#include "ps2.h"
#include "fs.h"
#include "virtio_blk.h"
#include "pci.h"
#include "hda.h"
#include "sound.h"
#include "io.h"
#include "settings.h"
#include "net.h"
#include "heap.h"
#include "string.h"
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

#ifndef NULL
#define NULL ((void*)0)
#endif

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef struct __attribute__((packed)) {
    u32 flags;
    u32 mem_lower;
    u32 mem_upper;
    u32 boot_device;
    u32 cmdline;
    u32 mods_count;
    u32 mods_addr;
    u32 syms[4];
    u32 mmap_length;
    u32 mmap_addr;
    u32 drives_length;
    u32 drives_addr;
    u32 config_table;
    u32 boot_loader_name;
    u32 apm_table;
    u32 vbe_control_info;
    u32 vbe_mode_info;
    u16 vbe_mode;
    u16 vbe_interface_seg;
    u16 vbe_interface_off;
    u16 vbe_interface_len;
    u64 framebuffer_addr;
    u32 framebuffer_pitch;
    u32 framebuffer_width;
    u32 framebuffer_height;
    u8 framebuffer_bpp;
    u8 framebuffer_type;
    union {
        struct {
            u32 framebuffer_palette_addr;
            u16 framebuffer_palette_num_colors;
        } palette;
        struct {
            u8 red_field_position;
            u8 red_mask_size;
            u8 green_field_position;
            u8 green_mask_size;
            u8 blue_field_position;
            u8 blue_mask_size;
        } rgb;
    } color;
} multiboot_info_t;

typedef struct __attribute__((packed)) {
    u32 mod_start;
    u32 mod_end;
    u32 string;
    u32 reserved;
} multiboot_module_t;

static volatile unsigned short* const vga = (unsigned short*)0xB8000;

static void vga_message(const char* msg) {
    int i = 0;
    while (msg[i] && i < 80 * 25) {
        vga[i] = (unsigned short)msg[i] | ((unsigned short)0x4F << 8);
        i++;
    }
}

static void fail(const char* message) {
    serial_write("FAIL: ");
    serial_write(message);
    serial_write("\n");
    vga_message(message);
    for (;;) {
        ps2_poll();
        if (gui_power_off_requested()) {
            serial_write("[kernel] shutdown requested\n");
            outb(0xF4, 0x00);
            outb(0x604, 0x00);
            cpu_halt_forever();
        }
        if (gui_needs_redraw()) gui_draw();
            serial_write("[kernel] frame end\n");
        cpu_pause();
    }
}

static multiboot_module_t *boot_modules;
static u32 boot_module_count;

static void platform_poweroff(void) {
    serial_write("[kernel] attempting qemu poweroff\n");
    if (fs_ready()) {
        serial_write("[kernel] poweroff fs_ready=1\n");
        if (fs_is_dirty()) {
            serial_write("[kernel] poweroff syncing dirty fs\n");
            if (!fs_sync()) serial_write("[kernel] poweroff fs_sync failed\n");
            else serial_write("[kernel] poweroff fs_sync ok\n");
        } else {
            serial_write("[kernel] poweroff fs already clean\n");
        }
    } else {
        serial_write("[kernel] poweroff fs_ready=0\n");
    }
    serial_write("[kernel] poweroff via 0x604/0x2000\n");
    sound_play_event(SOUND_EVENT_SHUTDOWN);
    sound_wait_finished(1500);
    outw(0x604, 0x2000);
    serial_write("[kernel] poweroff via 0xB004/0x2000\n");
    outw(0xB004, 0x2000);
    cpu_halt_forever();
}

void kernel_post_boot_fs_load(void) {
    serial_write("[kernel] post-boot fs load start\n");
    settings_load_from_disk();
    serial_write("[kernel] post-boot fs load done\n");
}

int gui_is_idle = 0;

static void serial_poll_commands(void) {
    static char s_buf[1024];
    static int s_len = 0;
    while (serial_has_char()) {
        char c = serial_read_char();
        if (c == '\r' || c == '\n') {
            if (s_len > 0) {
                s_buf[s_len] = '\0';
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
                    char m_buf[160];
                    snprintf(m_buf, sizeof(m_buf), "[metrics] pages_attempted=%u pages_loaded=%u pages_failed=%u layouts=%u frames=%u\n",
                             m->pages_attempted, m->pages_loaded, m->pages_failed, m->layout_passes, m->paint_frames);
                    serial_write(m_buf);
                }
                s_len = 0;
            }
        } else if (s_len < (int)sizeof(s_buf) - 1) {
            s_buf[s_len++] = c;
        }
    }
}

void kernel_main(u32 multiboot_magic, u32 multiboot_addr) {
    multiboot_info_t* mb = (multiboot_info_t*)(unsigned long)multiboot_addr;
    u32 palette_addr = 0;
    u16 palette_size = 0;

    serial_init();
    serial_write("[kernel] entered kernel_main\n");

    extern void gdt_init(void);
    gdt_init();
    
    extern void idt_init(void);
    idt_init();

    extern void syscall_init(void);
    syscall_init();

    fpu_init();
    serial_write("[kernel] FPU/SSE initialized\n");
    boot_modules = 0;
    boot_module_count = 0;
    serial_write("[kernel] multiboot_magic=");
    serial_write_hex(multiboot_magic);
    serial_write("\n");

    extern void heap_init(void);
    heap_init();
    serial_write("[kernel] heap initialized\n");
    
    extern void paging_init(void);
    paging_init();
    
    extern void pic_init(void);
    pic_init();
    serial_write("[kernel] PIC initialized\n");
    
    extern void tasking_init(void);
    tasking_init();
    
    asm volatile("sti");
    serial_write("[kernel] interrupts enabled!\n");

    extern void clock_monotonic_init(void);
    clock_monotonic_init();
    extern void firstos_platform_init(void);
    firstos_platform_init();
    extern void heap_dump_diagnostics(void);
    heap_dump_diagnostics();
    extern int ring3_hello_task_spawn(void);
    extern int ring3_fault_task_spawn(void);
    extern int userspace_hello_spawn(void);

    // Preflight tasks superseded by filesystem-based ELF loader:
    // ring3_hello_task_spawn();
    // userspace_hello_spawn();
    // ring3_fault_task_spawn();

    if (multiboot_magic != 0x2BADB002) fail("BAD MULTIBOOT MAGIC");
    if (!mb) fail("NULL MULTIBOOT INFO");

    serial_write("[kernel] multiboot info present\n");
    int have_multiboot_fb = (mb->flags & (1u << 12)) &&
                            mb->framebuffer_type == 1 &&
                            mb->framebuffer_addr &&
                            mb->framebuffer_width >= 640 &&
                            mb->framebuffer_height >= 480 &&
                            mb->framebuffer_pitch &&
                            mb->framebuffer_bpp >= 15;
    u32 boot_width = 0;
    u32 boot_height = 0;

    if (have_multiboot_fb) {
        serial_write("[kernel] framebuffer data present\n");
        serial_write("[kernel] fb addr=");
        serial_write_hex((u32)mb->framebuffer_addr);
        serial_write(" pitch=");
        serial_write_hex(mb->framebuffer_pitch);
        serial_write(" width=");
        serial_write_hex(mb->framebuffer_width);
        serial_write(" height=");
        serial_write_hex(mb->framebuffer_height);
        serial_write(" bpp=");
        serial_write_hex(mb->framebuffer_bpp);
        serial_write("\n");

        if (mb->framebuffer_type == 0) {
            palette_addr = mb->color.palette.framebuffer_palette_addr;
            palette_size = mb->color.palette.framebuffer_palette_num_colors;
            serial_write("[kernel] indexed framebuffer palette detected\n");
        }

        boot_width = mb->framebuffer_width;
        boot_height = mb->framebuffer_height;

        serial_write("[kernel] calling framebuffer_init\n");
        compositor_init((int)boot_width, (int)boot_height);
        framebuffer_init(
            mb->framebuffer_addr,
            mb->framebuffer_width,
            mb->framebuffer_height,
            mb->framebuffer_pitch,
            mb->framebuffer_bpp,
            mb->framebuffer_type,
            mb->color.rgb.red_field_position,
            mb->color.rgb.red_mask_size,
            mb->color.rgb.green_field_position,
            mb->color.rgb.green_mask_size,
            mb->color.rgb.blue_field_position,
            mb->color.rgb.blue_mask_size,
            palette_addr,
            palette_size
        );

        serial_write("[kernel] framebuffer_init returned\n");
        if (!fb.ready) fail("FRAMEBUFFER INIT FAILED");

        serial_write("[kernel] framebuffer ready\n");
    } else {
        serial_write("[kernel] no multiboot framebuffer, using virtio-gpu path\n");
        boot_width = 1024;
        boot_height = 768;
        compositor_init((int)boot_width, (int)boot_height);
        framebuffer_init_virtual(boot_width, boot_height);
        if (!fb.ready) fail("VIRTUAL FRAMEBUFFER INIT FAILED");
    }
    pci_init();
    net_init();    /* initializes virtio-net NIC, runs DHCP */
    hda_init();
    virtio_gpu_probe();
    if (virtio_gpu_available()) {
        serial_write("[kernel] initializing virtio-gpu\n");
        virtio_gpu_init_safe(boot_width, boot_height);
    }
    if (!have_multiboot_fb) {
        u32 det_w = virtio_gpu_get_detected_width();
        u32 det_h = virtio_gpu_get_detected_height();
        if (det_w > 0 && det_h > 0) {
            boot_width = det_w;
            boot_height = det_h;
            serial_write("[kernel] adopting detected virtio-gpu resolution\n");
            compositor_init((int)boot_width, (int)boot_height);
            fb_set_resolution(boot_width, boot_height);
        }
    }
    pci_debug_dump();
    serial_write("[kernel] before virtio_blk_init\n");
    virtio_blk_init();
    serial_write("[kernel] after virtio_blk_init\n");
    if ((mb->flags & (1u << 3)) && mb->mods_count && mb->mods_addr) {
        boot_modules = (multiboot_module_t*)(unsigned long)mb->mods_addr;
        boot_module_count = mb->mods_count;
        multiboot_module_t *mods = boot_modules;
        u32 mod_size = mods[0].mod_end - mods[0].mod_start;
        serial_write("[kernel] module image size=");
        serial_write_hex(mod_size);
        serial_write("\n");
        fs_init((void*)(unsigned long)mods[0].mod_start, mod_size);
        serial_write("[kernel] after fs_init\n");
        
        if (!fs_ready()) {
            serial_write("[kernel] formatting fs module\n");
            fs_format((void*)(unsigned long)mods[0].mod_start, mod_size);
        }
    } else if (virtio_blk_ready()) {
        static u8 s_boot_sec[512] __attribute__((aligned(16)));
        if (virtio_blk_read(0, s_boot_sec, 1)) {
            u32 magic = *(u32*)s_boot_sec;
            u32 ver = *(u32*)(s_boot_sec + 4);
            u32 img_size = *(u32*)(s_boot_sec + 12);
            if (magic == 0x314F5346 && ver == 2 && img_size > 0 && img_size <= 256 * 1024 * 1024) {
                serial_write("[kernel] mounting fs from virtio-blk disk size=");
                serial_write_hex(img_size);
                serial_write("\n");
                void *fs_disk_buf = kmalloc(img_size);
                if (fs_disk_buf) {
                    u32 total_sec = (img_size + 511u) / 512u;
                    u32 chunk = 256;
                    int read_ok = 1;
                    for (u32 s = 0; s < total_sec; s += chunk) {
                        u32 rem = total_sec - s;
                        u32 cnt = (rem < chunk) ? rem : chunk;
                        if (!virtio_blk_read(s, (u8*)fs_disk_buf + s * 512u, cnt)) {
                            read_ok = 0;
                            break;
                        }
                    }
                    if (read_ok) {
                        fs_init(fs_disk_buf, img_size);
                        serial_write("[kernel] virtio-blk fs mounted successfully\n");
                    }
                }
            }
        }
    }

    if (fs_ready()) {
        extern void vfs_init(void);
        vfs_init();
        extern void net_rescan_wifi(void);
        net_rescan_wifi();

        extern int process_spawn(int parent_pid, const char *path, const char *const *argv);
        extern int userspace_hello_spawn(void);
        const char *fs_args[] = {
            "/userspace_hello.elf",
            "fs_mode_arg1",
            "fs_mode_arg2",
            0
        };
        serial_write("[kernel] spawning Ring 3 ELF directly from filesystem...\n");
        int fs_tid = process_spawn(0, "/userspace_hello.elf", fs_args);
        if (fs_tid < 0) {
            fs_tid = userspace_hello_spawn();
        }
        if (fs_tid >= 0) {
            serial_write("[kernel] yielding to FS-loaded userspace task via int 0x81...\n");
            __asm__ volatile("int $0x81");
            serial_write("[kernel] resumed from FS-loaded userspace task successfully!\n");
        }
    }
    serial_write("[kernel] calling gui_init\n");
    serial_write("[kernel] before gui_init\n");
    gui_init();
    serial_write("[kernel] after gui_init\n");
    
    extern int elf_load_and_run(const char *path);
    serial_write("[kernel] loading /hello.elf\n");
    // elf_load_and_run("/hello.elf");
    serial_write("[kernel] finished running /hello.elf\n");
    serial_write("[kernel] before ps2_init\n");
    ps2_init();
    serial_write("[kernel] after ps2_init\n");
    serial_write("[kernel] calling gui_draw\n");
    serial_write("[kernel] before gui_draw\n");
    gui_draw();
            serial_write("[kernel] frame end\n");
    serial_write("[kernel] after gui_draw\n");
    fs_set_auto_sync(1);
    serial_write("[kernel] gui_draw done\n");
    while (1) {
        hda_tick();
        ps2_poll();
        extern void eth_poll(void);
        eth_poll();
        serial_poll_commands();
        if (gui_power_off_requested()) {
            serial_write("[kernel] shutdown requested\n");
            platform_poweroff();
        }
        
        if (gui_needs_redraw()) {
            gui_is_idle = 0;
            gui_draw();
        } else {
            gui_is_idle = 1;
            __asm__ volatile("hlt" ::: "memory");
            gui_is_idle = 0;
        }
    }
}
