#include "../../include/firstos/syscall.h"
#include "../../../include/firstos/uapi/browser_ipc.h"
#include "../../browser/include/firstos_renderer.h"

static void print_str(const char *s) {
    if (s) {
        firstos_write(1, s, (unsigned int)u_strlen(s));
    }
}

static void print_dec(int val) {
    char buf[16];
    int idx = 0;
    if (val == 0) {
        firstos_write(1, "0", 1);
        return;
    }
    if (val < 0) {
        firstos_write(1, "-", 1);
        val = -val;
    }
    char tmp[16];
    int t = 0;
    while (val > 0) {
        tmp[t++] = '0' + (val % 10);
        val /= 10;
    }
    while (t > 0) {
        buf[idx++] = tmp[--t];
    }
    firstos_write(1, buf, idx);
}

static firstos_renderer_context_t s_ctx;

static void wait_r2h_empty(firstos_browser_channel_t *chan) {
    int spins = 0;
    while (chan->has_message && spins < 200000) {
        firstos_yield();
        spins++;
    }
}

static void renderer_emit_frame(firstos_browser_channel_t *r2h_chan,
                                char *r2h_data,
                                uint32_t req_id,
                                uint32_t num_cmds) {
    wait_r2h_empty(r2h_chan);
    /* Phase 4: Populate damage rectangle descriptor in R2H data buffer */
    firstos_damage_rect_t *dmg = (firstos_damage_rect_t *)(r2h_data + FIRSTOS_BROWSER_DAMAGE_OFFSET);
    dmg->x = s_ctx.damage_rect.x;
    dmg->y = s_ctx.damage_rect.y;
    dmg->w = s_ctx.damage_rect.w;
    dmg->h = s_ctx.damage_rect.h;
    dmg->flags = FIRSTOS_RENDER_FLAG_DAMAGE_VALID;
    if (s_ctx.needs_full_repaint) {
        dmg->flags |= FIRSTOS_RENDER_FLAG_FULL_REPAINT;
    }
    dmg->reserved[0] = 0;
    dmg->reserved[1] = 0;
    dmg->reserved[2] = 0;

    firstos_mutex_lock((int *)(void *)&r2h_chan->lock_word);
    r2h_chan->msg.version = FIRSTOS_BROWSER_IPC_VERSION;
    r2h_chan->msg.type = FIRSTOS_BROWSER_MSG_RENDER_FRAME;
    r2h_chan->msg.request_id = req_id;
    r2h_chan->msg.payload_offset = 0;
    r2h_chan->msg.payload_length = num_cmds * sizeof(firstos_render_command_t);
    r2h_chan->msg.param1 = num_cmds;
    r2h_chan->msg.param2 = (uint32_t)s_ctx.doc_height;
    r2h_chan->msg.status = (int32_t)s_ctx.navigation_id;
    r2h_chan->has_message = 1;
    firstos_mutex_unlock((int *)(void *)&r2h_chan->lock_word);

    /* Reset full repaint flag now that frame has been submitted */
    s_ctx.needs_full_repaint = 0;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    int pid = firstos_getpid();
    print_str("[browser_renderer] started in Ring 3 pid=");
    print_dec(pid);
    print_str("\n");

    /* Step 1: Open and map the shared browser memory region */
    int shmid = firstos_shm_open(FIRSTOS_BROWSER_SHM_NAME, 0, 0);
    if (shmid < 0) {
        print_str("[browser_renderer] ERROR: Failed to open browser shared memory channel!\n");
        return 1;
    }

    void *shm_base = firstos_shm_map(shmid);
    if (!shm_base) {
        print_str("[browser_renderer] ERROR: Failed to map browser shared memory region!\n");
        return 1;
    }

    print_str("[browser_renderer] mapped shared memory at 0x04400000\n");

    firstos_browser_channel_t *h2r_chan = (firstos_browser_channel_t *)((char *)shm_base + FIRSTOS_BROWSER_CHANNEL_H2R_OFF);
    firstos_browser_channel_t *r2h_chan = (firstos_browser_channel_t *)((char *)shm_base + FIRSTOS_BROWSER_CHANNEL_R2H_OFF);

    char *h2r_data = (char *)h2r_chan + sizeof(firstos_browser_channel_t);
    char *r2h_data = (char *)r2h_chan + sizeof(firstos_browser_channel_t);

    firstos_render_command_t *cmds = (firstos_render_command_t *)r2h_data;
    char *cmd_strings = r2h_data + FIRSTOS_BROWSER_STRINGS_OFFSET;

    firstos_renderer_init(&s_ctx);

    print_str("[browser_renderer] channels configured. Entering message dispatch loop...\n");

    int running = 1;
    int frame_count = 0;
    int cur_view_w = 800;
    int cur_view_h = 600;

    while (running) {
        /* Check if Host has posted a message */
        if (h2r_chan->has_message) {
            firstos_mutex_lock((int *)(void *)&h2r_chan->lock_word);
            
            firstos_browser_message_t msg = h2r_chan->msg;
            h2r_chan->has_message = 0;
            
            firstos_mutex_unlock((int *)(void *)&h2r_chan->lock_word);

            if (msg.version != FIRSTOS_BROWSER_IPC_VERSION) {
                print_str("[browser_renderer] ERROR: unsupported IPC protocol version\n");
                continue;
            }

            if (msg.type == FIRSTOS_BROWSER_MSG_SHUTDOWN) {
                print_str("[browser_renderer] received SHUTDOWN request\n");
                running = 0;
                break;
            }

            if (msg.type == FIRSTOS_BROWSER_MSG_VIEWPORT_CHANGED) {
                int vw = (int)msg.param1;
                int vh = (int)msg.param2;
                if (vw > 0 && vh > 0 && (vw != cur_view_w || vh != cur_view_h)) {
                    cur_view_w = vw;
                    cur_view_h = vh;
                    firstos_renderer_handle_resize(&s_ctx, (uint32_t)vw, (uint32_t)vh);
                    uint32_t num_cmds = 0;
                    firstos_renderer_generate_commands(&s_ctx, cmds, 256, cmd_strings, 8192, &num_cmds);
                    renderer_emit_frame(r2h_chan, r2h_data, ++s_ctx.generation, num_cmds);

                    frame_count++;
                    print_str("[renderer] re-emitted RENDER_FRAME post-resize\n");
                }
                continue;
            }

            if (msg.type == FIRSTOS_BROWSER_MSG_INPUT_EVENT) {
                int input_type = (int)msg.status;
                if (input_type == FIRSTOS_BROWSER_INPUT_SCROLL) {
                    int delta_y = (int)msg.param2;
                    if (firstos_renderer_handle_scroll(&s_ctx, delta_y)) {
                        uint32_t num_cmds = 0;
                        firstos_renderer_generate_commands(&s_ctx, cmds, 256, cmd_strings, 8192, &num_cmds);
                        renderer_emit_frame(r2h_chan, r2h_data, msg.request_id, num_cmds);

                        frame_count++;
                        print_str("[renderer] scroll state updated y=");
                        print_dec(s_ctx.scroll_y);
                        print_str("\n");
                    }
                } else if (input_type == FIRSTOS_BROWSER_INPUT_CLICK) {
                    int click_x = (int)msg.param1;
                    int click_y = (int)msg.param2;
                    char nav_url[256];
                    char submit_action[256];
                    char query[512];
                    nav_url[0] = '\0';
                    submit_action[0] = '\0';
                    query[0] = '\0';
                    int cres = firstos_renderer_handle_click(&s_ctx, click_x, click_y,
                                                            nav_url, sizeof(nav_url),
                                                            submit_action, sizeof(submit_action),
                                                            query, sizeof(query));
                    if (cres == 1) {
                        int ulen = (int)u_strlen(nav_url);
                        u_memcpy(r2h_data, nav_url, ulen + 1);

                        firstos_mutex_lock((int *)(void *)&r2h_chan->lock_word);
                        r2h_chan->msg.version = FIRSTOS_BROWSER_IPC_VERSION;
                        r2h_chan->msg.type = FIRSTOS_BROWSER_MSG_NAVIGATE_REQUEST;
                        r2h_chan->msg.request_id = msg.request_id;
                        r2h_chan->msg.payload_offset = 0;
                        r2h_chan->msg.payload_length = (uint32_t)ulen;
                        r2h_chan->msg.status = 0;
                        r2h_chan->has_message = 1;
                        firstos_mutex_unlock((int *)(void *)&r2h_chan->lock_word);
                    } else if (cres == 2) {
                        firstos_form_submit_request_t *freq = (firstos_form_submit_request_t *)(r2h_data + FIRSTOS_BROWSER_FORM_SUBMIT_OFFSET);
                        freq->request_id = ++s_ctx.generation;
                        freq->navigation_id = s_ctx.navigation_id;
                        freq->method = 0; /* GET */

                        uint32_t a_off = FIRSTOS_BROWSER_FORM_SUBMIT_OFFSET + sizeof(firstos_form_submit_request_t);
                        freq->action_offset = a_off;
                        freq->action_len = (uint32_t)u_strlen(submit_action);
                        u_memcpy(r2h_data + a_off, submit_action, freq->action_len + 1);

                        uint32_t q_off = a_off + freq->action_len + 1;
                        freq->query_offset = q_off;
                        freq->query_len = (uint32_t)u_strlen(query);
                        u_memcpy(r2h_data + q_off, query, freq->query_len + 1);

                        firstos_mutex_lock((int *)(void *)&r2h_chan->lock_word);
                        r2h_chan->msg.version = FIRSTOS_BROWSER_IPC_VERSION;
                        r2h_chan->msg.type = FIRSTOS_BROWSER_MSG_FORM_SUBMIT;
                        r2h_chan->msg.request_id = freq->request_id;
                        r2h_chan->msg.payload_offset = FIRSTOS_BROWSER_FORM_SUBMIT_OFFSET;
                        r2h_chan->msg.payload_length = sizeof(firstos_form_submit_request_t) + freq->action_len + freq->query_len + 2;
                        r2h_chan->msg.status = 0;
                        r2h_chan->has_message = 1;
                        firstos_mutex_unlock((int *)(void *)&r2h_chan->lock_word);
                    } else if (cres == 3) {
                        uint32_t num_cmds = 0;
                        firstos_renderer_generate_commands(&s_ctx, cmds, 256, cmd_strings, 8192, &num_cmds);
                        renderer_emit_frame(r2h_chan, r2h_data, ++s_ctx.generation, num_cmds);
                    } else if (cres == 4) {
                        /* Phase 6A.1: Anchor download triggered */
                        firstos_download_request_t *dreq = (firstos_download_request_t *)(r2h_data + FIRSTOS_BROWSER_DOWNLOAD_REQ_OFFSET);
                        dreq->request_id = ++s_ctx.generation;
                        dreq->navigation_id = s_ctx.navigation_id;
                        dreq->is_user_gesture = 1;

                        uint32_t u_off = FIRSTOS_BROWSER_DOWNLOAD_REQ_OFFSET + sizeof(firstos_download_request_t);
                        dreq->url_offset = u_off;
                        dreq->url_len = (uint32_t)u_strlen(nav_url);
                        u_memcpy(r2h_data + u_off, nav_url, dreq->url_len + 1);

                        uint32_t f_off = u_off + dreq->url_len + 1;
                        dreq->filename_offset = f_off;
                        dreq->filename_len = (uint32_t)u_strlen(submit_action);
                        u_memcpy(r2h_data + f_off, submit_action, dreq->filename_len + 1);

                        firstos_mutex_lock((int *)(void *)&r2h_chan->lock_word);
                        r2h_chan->msg.version = FIRSTOS_BROWSER_IPC_VERSION;
                        r2h_chan->msg.type = FIRSTOS_BROWSER_MSG_DOWNLOAD_REQUEST;
                        r2h_chan->msg.request_id = dreq->request_id;
                        r2h_chan->msg.payload_offset = FIRSTOS_BROWSER_DOWNLOAD_REQ_OFFSET;
                        r2h_chan->msg.payload_length = sizeof(firstos_download_request_t) + dreq->url_len + dreq->filename_len + 2;
                        r2h_chan->msg.status = 0;
                        r2h_chan->has_message = 1;
                        firstos_mutex_unlock((int *)(void *)&r2h_chan->lock_word);
                    } else if (cres == 5) {
                        /* Phase 6B: Media playback command triggered */
                        firstos_media_command_t *mcmd = (firstos_media_command_t *)(r2h_data + FIRSTOS_BROWSER_MEDIA_CMD_OFFSET);
                        mcmd->request_id = ++s_ctx.generation;
                        mcmd->media_id = 1;
                        mcmd->command = FIRSTOS_MEDIA_CMD_PLAY;
                        mcmd->volume = 80;
                        mcmd->seek_time_ms = 0;

                        uint32_t s_off = FIRSTOS_BROWSER_MEDIA_CMD_OFFSET + sizeof(firstos_media_command_t);
                        mcmd->src_offset = s_off;
                        mcmd->src_len = (uint32_t)u_strlen(nav_url);
                        u_memcpy(r2h_data + s_off, nav_url, mcmd->src_len + 1);

                        /* Emit updated render commands for play state */
                        uint32_t num_cmds = 0;
                        firstos_renderer_generate_commands(&s_ctx, cmds, 256, cmd_strings, 8192, &num_cmds);

                        firstos_mutex_lock((int *)(void *)&r2h_chan->lock_word);
                        r2h_chan->msg.version = FIRSTOS_BROWSER_IPC_VERSION;
                        r2h_chan->msg.type = FIRSTOS_BROWSER_MSG_MEDIA_COMMAND;
                        r2h_chan->msg.request_id = mcmd->request_id;
                        r2h_chan->msg.payload_offset = FIRSTOS_BROWSER_MEDIA_CMD_OFFSET;
                        r2h_chan->msg.payload_length = sizeof(firstos_media_command_t) + mcmd->src_len + 1;
                        r2h_chan->msg.param1 = num_cmds;
                        r2h_chan->msg.status = 0;
                        r2h_chan->has_message = 1;
                        firstos_mutex_unlock((int *)(void *)&r2h_chan->lock_word);

                        print_str("[renderer] dispatched MEDIA_COMMAND for ");
                        print_str(nav_url);
                        print_str("\n");
                    }
                } else if (input_type == FIRSTOS_BROWSER_INPUT_KEY) {
                    int key = (int)msg.param1;
                    char ascii = (char)msg.param2;
                    char submit_action[256];
                    char query[512];
                    submit_action[0] = '\0';
                    query[0] = '\0';
                    int kres = firstos_renderer_handle_key(&s_ctx, key, ascii,
                                                          submit_action, sizeof(submit_action),
                                                          query, sizeof(query));
                    if (kres == 2) {
                        firstos_form_submit_request_t *freq = (firstos_form_submit_request_t *)(r2h_data + FIRSTOS_BROWSER_FORM_SUBMIT_OFFSET);
                        freq->request_id = ++s_ctx.generation;
                        freq->navigation_id = s_ctx.navigation_id;
                        freq->method = 0; /* GET */

                        uint32_t a_off = FIRSTOS_BROWSER_FORM_SUBMIT_OFFSET + sizeof(firstos_form_submit_request_t);
                        freq->action_offset = a_off;
                        freq->action_len = (uint32_t)u_strlen(submit_action);
                        u_memcpy(r2h_data + a_off, submit_action, freq->action_len + 1);

                        uint32_t q_off = a_off + freq->action_len + 1;
                        freq->query_offset = q_off;
                        freq->query_len = (uint32_t)u_strlen(query);
                        u_memcpy(r2h_data + q_off, query, freq->query_len + 1);

                        firstos_mutex_lock((int *)(void *)&r2h_chan->lock_word);
                        r2h_chan->msg.version = FIRSTOS_BROWSER_IPC_VERSION;
                        r2h_chan->msg.type = FIRSTOS_BROWSER_MSG_FORM_SUBMIT;
                        r2h_chan->msg.request_id = freq->request_id;
                        r2h_chan->msg.payload_offset = FIRSTOS_BROWSER_FORM_SUBMIT_OFFSET;
                        r2h_chan->msg.payload_length = sizeof(firstos_form_submit_request_t) + freq->action_len + freq->query_len + 2;
                        r2h_chan->msg.status = 0;
                        r2h_chan->has_message = 1;
                        firstos_mutex_unlock((int *)(void *)&r2h_chan->lock_word);
                    } else if (kres == 1) {
                        uint32_t num_cmds = 0;
                        firstos_renderer_generate_commands(&s_ctx, cmds, 256, cmd_strings, 8192, &num_cmds);
                        renderer_emit_frame(r2h_chan, r2h_data, ++s_ctx.generation, num_cmds);
                    }
                }

                /* Acknowledge event */
                firstos_mutex_lock((int *)(void *)&r2h_chan->lock_word);
                r2h_chan->msg.version = FIRSTOS_BROWSER_IPC_VERSION;
                r2h_chan->msg.type = FIRSTOS_BROWSER_MSG_ACK;
                r2h_chan->msg.request_id = msg.request_id;
                r2h_chan->msg.status = 0;
                r2h_chan->has_message = 1;
                firstos_mutex_unlock((int *)(void *)&r2h_chan->lock_word);
                continue;
            }

            if (msg.type == FIRSTOS_BROWSER_MSG_RESOURCE_RESPONSE) {
                firstos_resource_response_t *resp = (firstos_resource_response_t *)(h2r_data + FIRSTOS_BROWSER_RESOURCE_RESP_OFFSET);

                print_str("[renderer] received RESOURCE_RESPONSE status=");
                print_dec(resp->status_code);
                print_str(" nav=");
                print_dec((int)resp->navigation_id);
                print_str("\n");

                /* Stale response cancellation check! */
                if (resp->navigation_id != s_ctx.navigation_id) {
                    print_str("[renderer] dropped STALE resource response (current nav=");
                    print_dec((int)s_ctx.navigation_id);
                    print_str(")\n");
                    continue;
                }

                if (resp->resource_type == FIRSTOS_RES_TYPE_STYLESHEET) {
                    if (resp->status_code == 200 && resp->data_len > 0) {
                        const char *res_data = h2r_data + resp->data_offset;
                        print_str("[renderer] applying external stylesheet bytes=");
                        print_dec((int)resp->data_len);
                        print_str("\n");
                        firstos_renderer_add_stylesheet(&s_ctx, res_data, (int)resp->data_len);
                        firstos_renderer_layout(&s_ctx, (uint32_t)cur_view_w, (uint32_t)cur_view_h);

                        /* Re-emit RENDER_FRAME */
                        uint32_t num_cmds = 0;
                        firstos_renderer_generate_commands(&s_ctx, cmds, 256, cmd_strings, 8192, &num_cmds);
                        renderer_emit_frame(r2h_chan, r2h_data, ++s_ctx.generation, num_cmds);

                        frame_count++;
                        print_str("[renderer] re-emitted RENDER_FRAME post-CSS doc_h=");
                        print_dec(s_ctx.doc_height);
                        print_str("\n");
                    }
                    /* Mark pending resource loaded */
                    for (int i = 0; i < s_ctx.pending_res_count; i++) {
                        if (s_ctx.pending_res[i].request_id == resp->request_id) {
                            s_ctx.pending_res[i].status = (resp->status_code == 200) ? 1 : -1;
                            break;
                        }
                    }
                } else if (resp->resource_type == FIRSTOS_RES_TYPE_IMAGE) {
                    print_str("[renderer] image response id=");
                    print_dec((int)resp->image_id);
                    print_str(" w=");
                    print_dec((int)resp->meta_w);
                    print_str(" h=");
                    print_dec((int)resp->meta_h);
                    print_str("\n");

                    /* Match node from pending resources */
                    html_node_t *target_node = NULL;
                    for (int i = 0; i < s_ctx.pending_res_count; i++) {
                        if (s_ctx.pending_res[i].request_id == resp->request_id) {
                            target_node = s_ctx.pending_res[i].node;
                            s_ctx.pending_res[i].status = (resp->status_code == 200) ? 1 : -1;
                            break;
                        }
                    }

                    if (!target_node) {
                        target_node = firstos_renderer_get_element_by_tag(&s_ctx, HTML_TAG_IMG);
                    }

                    if (target_node) {
                        if (resp->status_code == 200 && resp->image_id > 0) {
                            target_node->image_id = resp->image_id;
                            target_node->image_broken = 0;
                            if (resp->meta_w > 0 && target_node->req_width <= 0) {
                                target_node->req_width = (int)resp->meta_w;
                            }
                            if (resp->meta_h > 0 && target_node->req_height <= 0) {
                                target_node->req_height = (int)resp->meta_h;
                            }
                        } else {
                            target_node->image_broken = 1;
                        }

                        /* Re-compute layout with intrinsic/resolved dimensions */
                        firstos_renderer_layout(&s_ctx, (uint32_t)cur_view_w, (uint32_t)cur_view_h);

                        /* Re-emit RENDER_FRAME */
                        uint32_t num_cmds = 0;
                        firstos_renderer_generate_commands(&s_ctx, cmds, 256, cmd_strings, 8192, &num_cmds);
                        renderer_emit_frame(r2h_chan, r2h_data, ++s_ctx.generation, num_cmds);

                        frame_count++;
                        print_str("[renderer] re-emitted RENDER_FRAME post-image doc_h=");
                        print_dec(s_ctx.doc_height);
                        print_str("\n");
                    }
                }

                /* Dispatch next pending resource if any */
                for (int i = 0; i < s_ctx.pending_res_count; i++) {
                    if (s_ctx.pending_res[i].status == 0) {
                        firstos_pending_resource_t *next_res = &s_ctx.pending_res[i];
                        next_res->status = 2; /* In-flight */

                        firstos_resource_request_t *nreq = (firstos_resource_request_t *)(r2h_data + FIRSTOS_BROWSER_RESOURCE_REQ_OFFSET);
                        nreq->request_id = next_res->request_id;
                        nreq->navigation_id = s_ctx.navigation_id;
                        nreq->resource_type = (uint16_t)next_res->type;
                        nreq->flags = 0;

                        uint32_t u_off = FIRSTOS_BROWSER_RESOURCE_REQ_OFFSET + sizeof(firstos_resource_request_t);
                        nreq->url_offset = u_off;
                        nreq->url_len = (uint32_t)u_strlen(next_res->url);
                        u_memcpy(r2h_data + u_off, next_res->url, nreq->url_len + 1);

                        wait_r2h_empty(r2h_chan);
                        firstos_mutex_lock((int *)(void *)&r2h_chan->lock_word);
                        r2h_chan->msg.version = FIRSTOS_BROWSER_IPC_VERSION;
                        r2h_chan->msg.type = FIRSTOS_BROWSER_MSG_RESOURCE_REQUEST;
                        r2h_chan->msg.request_id = nreq->request_id;
                        r2h_chan->msg.payload_offset = FIRSTOS_BROWSER_RESOURCE_REQ_OFFSET;
                        r2h_chan->msg.payload_length = sizeof(firstos_resource_request_t) + nreq->url_len + 1;
                        r2h_chan->msg.status = 0;
                        r2h_chan->has_message = 1;
                        firstos_mutex_unlock((int *)(void *)&r2h_chan->lock_word);

                        print_str("[renderer] chained next RESOURCE_REQUEST for ");
                        print_str(next_res->url);
                        print_str("\n");
                        break;
                    }
                }
                continue;
            }

            if (msg.type == FIRSTOS_BROWSER_MSG_LOAD_URL || msg.type == FIRSTOS_BROWSER_MSG_HTML_DATA) {
                if (msg.type == FIRSTOS_BROWSER_MSG_LOAD_URL) {
                    print_str("[browser_renderer] received LOAD_URL\n");
                } else {
                    print_str("[browser_renderer] received HTML_DATA\n");
                }

                int view_w = (int)msg.param1;
                int view_h = (int)msg.param2;
                if (view_w <= 0) view_w = 800;
                if (view_h <= 0) view_h = 600;
                cur_view_w = view_w;
                cur_view_h = view_h;

                if (msg.status > 0) {
                    s_ctx.navigation_id = (uint32_t)msg.status;
                }

                const char *html_payload = h2r_data + msg.payload_offset;
                int html_len = (int)msg.payload_length;

                print_str("[renderer] HTML received bytes=");
                print_dec(html_len);
                print_str(" nav=");
                print_dec((int)s_ctx.navigation_id);
                print_str("\n");

                /* 1. Parse HTML into DOM */
                firstos_renderer_parse_html(&s_ctx, html_payload, html_len);
                print_str("[renderer] DOM built nodes=");
                print_dec(s_ctx.node_count);
                print_str("\n");

                /* 2. Apply CSS stylesheets and cascade */
                firstos_renderer_apply_styles(&s_ctx);
                print_str("[renderer] CSS parsed & styles computed\n");
                int var_count = 0;
                if (s_ctx.root_dom) {
                    var_count += s_ctx.root_dom->var_table.count;
                    if (s_ctx.root_dom->first_child) {
                        var_count += s_ctx.root_dom->first_child->var_table.count;
                    }
                }
                if (var_count > 0) {
                    print_str("[renderer] CSS variables evaluated count=");
                    print_dec(var_count);
                    print_str("\n");
                }

                /* 3. Compute layout */
                firstos_renderer_layout(&s_ctx, (uint32_t)cur_view_w, (uint32_t)cur_view_h);
                print_str("[renderer] layout complete doc_h=");
                print_dec(s_ctx.doc_height);
                print_str("\n");

                /* 3.5 Execute DOM scripts & reflow */
                firstos_js_run_dom_scripts(&s_ctx);
                firstos_renderer_layout(&s_ctx, (uint32_t)cur_view_w, (uint32_t)cur_view_h);
                print_str("[renderer] layout & script evaluation complete doc_h=");
                print_dec(s_ctx.doc_height);
                print_str("\n");

                /* 3.6 Phase 6A.3: Accessibility Tree Synthesis */
                firstos_ax_tree_t *p_ax_tree = (firstos_ax_tree_t *)firstos_malloc(sizeof(firstos_ax_tree_t));
                if (p_ax_tree) {
                    firstos_ax_tree_init(p_ax_tree, 1, 1);
                    if (firstos_ax_tree_build(&s_ctx, p_ax_tree) > 0) {
                        print_str("[renderer] AX tree built nodes=");
                        print_dec((int)p_ax_tree->node_count);
                        print_str("\n");
                        static char s_ax_dump[2048];
                        firstos_ax_tree_dump(p_ax_tree, s_ax_dump, sizeof(s_ax_dump));
                        print_str(s_ax_dump);
                    }
                    firstos_free(p_ax_tree);
                }

                /* 4. Generate Render Command Buffer into r2h_data */
                uint32_t num_cmds = 0;
                firstos_renderer_generate_commands(&s_ctx, cmds, 256, cmd_strings, 8192, &num_cmds);

                print_str("[renderer] render commands emitted count=");
                print_dec((int)num_cmds);
                print_str("\n");

                /* 5. Post RENDER_FRAME message to Host */
                renderer_emit_frame(r2h_chan, r2h_data, msg.request_id, num_cmds);

                frame_count++;
                print_str("[browser_renderer] emitted RENDER_FRAME (frame ");
                print_dec(frame_count);
                print_str(")\n");

                /* 6. Check for pending subresources to fetch */
                if (s_ctx.pending_res_count > 0) {
                    firstos_pending_resource_t *res = &s_ctx.pending_res[0];
                    res->status = 2; /* In-flight */
                    firstos_resource_request_t *req = (firstos_resource_request_t *)(r2h_data + FIRSTOS_BROWSER_RESOURCE_REQ_OFFSET);
                    req->request_id = res->request_id;
                    req->navigation_id = s_ctx.navigation_id;
                    req->resource_type = (uint16_t)res->type;
                    req->flags = 0;

                    uint32_t u_off = FIRSTOS_BROWSER_RESOURCE_REQ_OFFSET + sizeof(firstos_resource_request_t);
                    req->url_offset = u_off;
                    req->url_len = (uint32_t)u_strlen(res->url);

                    u_memcpy(r2h_data + u_off, res->url, req->url_len + 1);

                    wait_r2h_empty(r2h_chan);
                    firstos_mutex_lock((int *)(void *)&r2h_chan->lock_word);
                    r2h_chan->msg.version = FIRSTOS_BROWSER_IPC_VERSION;
                    r2h_chan->msg.type = FIRSTOS_BROWSER_MSG_RESOURCE_REQUEST;
                    r2h_chan->msg.request_id = req->request_id;
                    r2h_chan->msg.payload_offset = FIRSTOS_BROWSER_RESOURCE_REQ_OFFSET;
                    r2h_chan->msg.payload_length = sizeof(firstos_resource_request_t) + req->url_len + 1;
                    r2h_chan->msg.status = 0;
                    r2h_chan->has_message = 1;
                    firstos_mutex_unlock((int *)(void *)&r2h_chan->lock_word);

                    print_str("[renderer] dispatched RESOURCE_REQUEST for ");
                    print_str(res->url);
                    print_str("\n");
                }
            }
        }

        /* Yield CPU to allow host to process */
        firstos_yield();
    }

    firstos_renderer_destroy(&s_ctx);
    firstos_shm_unmap(shmid);
    print_str("[browser_renderer] exited cleanly with status 0\n");
    return 0;
}
