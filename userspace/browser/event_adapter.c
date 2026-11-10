#include "include/firstos_renderer.h"

int firstos_renderer_handle_scroll(firstos_renderer_context_t *ctx, int delta_y) {
    if (!ctx) return 0;
    int old_scroll = ctx->scroll_y;
    int new_scroll = old_scroll + delta_y;

    int max_scroll = ctx->doc_height - (int)ctx->viewport_h;
    if (max_scroll < 0) max_scroll = 0;

    if (new_scroll < 0) new_scroll = 0;
    if (new_scroll > max_scroll) new_scroll = max_scroll;

    if (new_scroll != old_scroll) {
        ctx->scroll_y = new_scroll;
        ctx->needs_full_repaint = 1;
        ctx->damage_rect = (rect_t){0, 0, (int)ctx->viewport_w, (int)ctx->viewport_h};
        return 1;
    }
    return 0;
}

static html_node_t *hit_test_node_rec(html_node_t *node, int doc_x, int doc_y) {
    if (!node || node->style.display_none || node->style.visibility_hidden) return NULL;

    html_node_t *hit = NULL;
    /* Check children in reverse order so topmost child matches first */
    for (html_node_t *child = node->first_child; child; child = child->next_sibling) {
        html_node_t *h = hit_test_node_rec(child, doc_x, doc_y);
        if (h) hit = h;
    }
    if (hit) return hit;

    if (doc_x >= node->bounds.x && doc_x < node->bounds.x + node->bounds.w &&
        doc_y >= node->bounds.y && doc_y < node->bounds.y + node->bounds.h) {
        return node;
    }

    return NULL;
}

static html_node_t *find_node_by_id_num_rec(html_node_t *curr, int id_num) {
    if (!curr || id_num <= 0) return NULL;
    if (curr->id_num == id_num) return curr;
    for (html_node_t *child = curr->first_child; child; child = child->next_sibling) {
        html_node_t *found = find_node_by_id_num_rec(child, id_num);
        if (found) return found;
    }
    return NULL;
}

html_node_t *firstos_renderer_get_focused(firstos_renderer_context_t *ctx) {
    if (!ctx || !ctx->root_dom || ctx->focus_state.focused_node_id <= 0) return NULL;
    return find_node_by_id_num_rec(ctx->root_dom, ctx->focus_state.focused_node_id);
}

int firstos_renderer_set_focus(firstos_renderer_context_t *ctx, html_node_t *node) {
    if (!ctx) return 0;
    int target_id = node ? node->id_num : 0;
    if (ctx->focus_state.focused_node_id == target_id) return 0;

    /* Blur old focused node */
    html_node_t *old = firstos_renderer_get_focused(ctx);
    if (old) {
        old->is_focused = 0;
        firstos_dom_event_t bl_ev;
        bl_ev.type = DOM_EVENT_BLUR;
        bl_ev.target_node_id = old->id_num;
        bl_ev.current_node_id = old->id_num;
        bl_ev.phase = 2;
        bl_ev.default_prevented = 0;
        bl_ev.propagation_stopped = 0;
        firstos_dom_dispatch_event(ctx, old, &bl_ev);
    }

    ctx->focus_state.focused_node_id = target_id;
    ctx->focus_state.focus_generation++;

    if (node) {
        node->is_focused = 1;
        firstos_dom_event_t fc_ev;
        fc_ev.type = DOM_EVENT_FOCUS;
        fc_ev.target_node_id = node->id_num;
        fc_ev.current_node_id = node->id_num;
        fc_ev.phase = 2;
        fc_ev.default_prevented = 0;
        fc_ev.propagation_stopped = 0;
        firstos_dom_dispatch_event(ctx, node, &fc_ev);
    }

    return 1;
}

static int is_focusable_node(html_node_t *n) {
    if (!n || n->style.display_none || n->style.visibility_hidden) return 0;
    if (n->tag == HTML_TAG_INPUT) return !n->disabled;
    if (n->tag == HTML_TAG_BUTTON) return !n->disabled;
    if (n->tag == HTML_TAG_A && n->href && n->href[0]) return 1;
    return 0;
}

static void collect_focusables_rec(html_node_t *curr, html_node_t **out_nodes, int *count, int max) {
    if (!curr || *count >= max) return;
    if (is_focusable_node(curr)) {
        out_nodes[(*count)++] = curr;
    }
    for (html_node_t *child = curr->first_child; child; child = child->next_sibling) {
        collect_focusables_rec(child, out_nodes, count, max);
    }
}

int firstos_renderer_focus_next(firstos_renderer_context_t *ctx, int reverse) {
    if (!ctx || !ctx->root_dom) return 0;
    html_node_t *focusables[64];
    int count = 0;
    collect_focusables_rec(ctx->root_dom, focusables, &count, 64);
    if (count == 0) {
        firstos_renderer_set_focus(ctx, NULL);
        return 0;
    }

    int curr_idx = -1;
    for (int i = 0; i < count; i++) {
        if (focusables[i]->id_num == ctx->focus_state.focused_node_id) {
            curr_idx = i;
            break;
        }
    }

    int next_idx;
    if (curr_idx == -1) {
        next_idx = reverse ? (count - 1) : 0;
    } else {
        if (reverse) {
            next_idx = (curr_idx - 1 + count) % count;
        } else {
            next_idx = (curr_idx + 1) % count;
        }
    }

    firstos_renderer_set_focus(ctx, focusables[next_idx]);
    return 1;
}

static const char *get_event_name_str(uint32_t type) {
    switch (type) {
        case DOM_EVENT_CLICK:   return "click";
        case DOM_EVENT_KEYDOWN: return "keydown";
        case DOM_EVENT_KEYUP:   return "keyup";
        case DOM_EVENT_INPUT:   return "input";
        case DOM_EVENT_CHANGE:  return "change";
        case DOM_EVENT_SUBMIT:  return "submit";
        case DOM_EVENT_FOCUS:   return "focus";
        case DOM_EVENT_BLUR:    return "blur";
        default:                return "event";
    }
}

int firstos_dom_dispatch_event(firstos_renderer_context_t *ctx, html_node_t *target, firstos_dom_event_t *ev) {
    if (!ctx || !target || !ev) return 0;

    /* Build ancestor path from root to target */
    html_node_t *path[32];
    int depth = 0;
    for (html_node_t *curr = target; curr && depth < 31; curr = curr->parent) {
        path[depth++] = curr;
    }

    /* Reverse path so path[0] is root, path[depth-1] is target */
    for (int i = 0; i < depth / 2; i++) {
        html_node_t *tmp = path[i];
        path[i] = path[depth - 1 - i];
        path[depth - 1 - i] = tmp;
    }

    const char *ev_name = get_event_name_str(ev->type);

    /* Phase 1: CAPTURE */
    ev->phase = 1;
    for (int i = 0; i < depth - 1; i++) {
        if (ev->propagation_stopped) break;
        ev->current_node_id = path[i]->id_num;
        firstos_js_dispatch_event(ctx, path[i], ev_name);
    }

    /* Phase 2: TARGET */
    if (!ev->propagation_stopped) {
        ev->phase = 2;
        ev->current_node_id = target->id_num;

        /* Execute inline onclick attribute if present */
        if (ev->type == DOM_EVENT_CLICK && target->onclick && target->onclick[0]) {
            firstos_js_eval(ctx, target->onclick);
        }

        firstos_js_dispatch_event(ctx, target, ev_name);
    }

    /* Phase 3: BUBBLE */
    if (!ev->propagation_stopped) {
        ev->phase = 3;
        for (int i = depth - 2; i >= 0; i--) {
            if (ev->propagation_stopped) break;
            ev->current_node_id = path[i]->id_num;
            firstos_js_dispatch_event(ctx, path[i], ev_name);
        }
    }

    return ev->default_prevented;
}

int firstos_renderer_handle_click(firstos_renderer_context_t *ctx, int click_x, int click_y,
                                   char *out_nav_url, int max_url,
                                   char *out_submit_action, int max_action,
                                   char *out_query, int max_query) {
    if (!ctx || !ctx->root_dom) return 0;

    int doc_x = click_x;
    int doc_y = click_y + ctx->scroll_y;

    html_node_t *clicked = hit_test_node_rec(ctx->root_dom, doc_x, doc_y);
    if (!clicked) {
        firstos_renderer_set_focus(ctx, NULL);
        return 0;
    }

    /* Update focus */
    if (is_focusable_node(clicked)) {
        firstos_renderer_set_focus(ctx, clicked);
    } else {
        firstos_renderer_set_focus(ctx, NULL);
    }

    /* Dispatch DOM Click Event */
    firstos_dom_event_t ev;
    ev.type = DOM_EVENT_CLICK;
    ev.target_node_id = clicked->id_num;
    ev.current_node_id = clicked->id_num;
    ev.phase = 2;
    ev.default_prevented = 0;
    ev.propagation_stopped = 0;
    firstos_dom_dispatch_event(ctx, clicked, &ev);

    /* 1. Checkbox toggle */
    if (clicked->tag == HTML_TAG_INPUT && clicked->input_type && strcmp(clicked->input_type, "checkbox") == 0) {
        firstos_form_toggle_checkbox(clicked);
        firstos_dom_event_t chg_ev;
        chg_ev.type = DOM_EVENT_CHANGE;
        chg_ev.target_node_id = clicked->id_num;
        chg_ev.current_node_id = clicked->id_num;
        chg_ev.phase = 2;
        chg_ev.default_prevented = 0;
        chg_ev.propagation_stopped = 0;
        firstos_dom_dispatch_event(ctx, clicked, &chg_ev);
        return 3; /* Redraw frame */
    }

    /* 2. Submit Button */
    int is_submit = 0;
    if (clicked->tag == HTML_TAG_INPUT && clicked->input_type && strcmp(clicked->input_type, "submit") == 0) {
        is_submit = 1;
    } else if (clicked->tag == HTML_TAG_BUTTON) {
        if (!clicked->input_type || strcmp(clicked->input_type, "submit") == 0) is_submit = 1;
    }

    if (is_submit) {
        html_node_t *form = firstos_form_find_parent_form(clicked);
        if (form) {
            firstos_dom_event_t sub_ev;
            sub_ev.type = DOM_EVENT_SUBMIT;
            sub_ev.target_node_id = form->id_num;
            sub_ev.current_node_id = form->id_num;
            sub_ev.phase = 2;
            sub_ev.default_prevented = 0;
            sub_ev.propagation_stopped = 0;
            firstos_dom_dispatch_event(ctx, form, &sub_ev);

            if (!sub_ev.default_prevented) {
                if (out_submit_action && max_action > 0) {
                    const char *act = form->action ? form->action : "";
                    int i = 0;
                    while (act[i] && i < max_action - 1) {
                        out_submit_action[i] = act[i];
                        i++;
                    }
                    out_submit_action[i] = '\0';
                }
                if (out_query && max_query > 0) {
                    firstos_form_collect_query(ctx, form, out_query, max_query);
                }
                return 2; /* Form Submit Triggered */
            }
        }
    }

    /* 3. Anchor Link */
    for (html_node_t *curr = clicked; curr; curr = curr->parent) {
        if (curr->tag == HTML_TAG_A && curr->href && curr->href[0]) {
            if (!ev.default_prevented) {
                if (out_nav_url && max_url > 1) {
                    int i = 0;
                    while (curr->href[i] && i < max_url - 1) {
                        out_nav_url[i] = curr->href[i];
                        i++;
                    }
                    out_nav_url[i] = '\0';
                }
                /* Phase 6A.1: If link has download attribute, return 4 (Download Triggered) */
                if (curr->download) {
                    if (out_submit_action && max_action > 0) {
                        int j = 0;
                        while (curr->download[j] && j < max_action - 1) {
                            out_submit_action[j] = curr->download[j];
                            j++;
                        }
                        out_submit_action[j] = '\0';
                    }
                    return 4; /* Download Triggered */
                }
                return 1; /* Navigate */
            }
        }
    }

    /* 4. Phase 6B: HTML5 Audio & Video Media Controls */
    for (html_node_t *curr = clicked; curr; curr = curr->parent) {
        if (curr->tag == HTML_TAG_AUDIO || curr->tag == HTML_TAG_VIDEO) {
            int ctrl_h = (curr->media_controls || curr->tag == HTML_TAG_AUDIO) ? 32 : 0;
            int rel_y = doc_y - curr->bounds.y;
            int rel_x = doc_x - curr->bounds.x;

            /* Check scrubber bar click (timeline seek) */
            int scrub_x = 38;
            int scrub_w = curr->bounds.w - 120;
            if (ctrl_h > 0 && rel_y >= curr->bounds.h - ctrl_h + 8 && rel_y <= curr->bounds.h - ctrl_h + 24 &&
                rel_x >= scrub_x && rel_x <= scrub_x + scrub_w && scrub_w > 0) {
                int dur = curr->media_duration_sec > 0 ? (int)curr->media_duration_sec : 90;
                int seek_sec = ((rel_x - scrub_x) * dur) / scrub_w;
                if (seek_sec < 0) seek_sec = 0;
                if (seek_sec > dur) seek_sec = dur;
                curr->media_cur_time_sec = (uint32_t)seek_sec;

                if (out_nav_url && max_url > 0) {
                    const char *s = curr->src ? curr->src : "";
                    int i = 0; while (s[i] && i < max_url - 1) { out_nav_url[i] = s[i]; i++; }
                    out_nav_url[i] = '\0';
                }
                return 5; /* Media Command Triggered */
            }

            /* Main canvas click or play/pause button toggle */
            curr->media_playing = !curr->media_playing;
            if (out_nav_url && max_url > 0) {
                const char *s = curr->src ? curr->src : "";
                int i = 0; while (s[i] && i < max_url - 1) { out_nav_url[i] = s[i]; i++; }
                out_nav_url[i] = '\0';
            }
            return 5; /* Media Command Triggered */
        }
    }

    return 3; /* Interactive event processed, redraw frame */
}

int firstos_renderer_handle_key(firstos_renderer_context_t *ctx, int key, char ascii,
                                 char *out_submit_action, int max_action,
                                 char *out_query, int max_query) {
    if (!ctx) return 0;
    html_node_t *focused = firstos_renderer_get_focused(ctx);

    /* 1. Tab Sequential Focus */
    if (key == 9 || key == 0x0F || ascii == '\t') {
        int reverse = (key == 0x8F || key == 0x10); /* Shift indicator */
        return firstos_renderer_focus_next(ctx, reverse);
    }

    if (!focused) return 0;

    /* Dispatch DOM KeyDown Event */
    firstos_dom_event_t kd_ev;
    kd_ev.type = DOM_EVENT_KEYDOWN;
    kd_ev.target_node_id = focused->id_num;
    kd_ev.current_node_id = focused->id_num;
    kd_ev.phase = 2;
    kd_ev.default_prevented = 0;
    kd_ev.propagation_stopped = 0;
    firstos_dom_dispatch_event(ctx, focused, &kd_ev);

    /* 2. Enter -> Form Submit */
    if (key == 13 || key == 0x1C || ascii == '\r' || ascii == '\n') {
        html_node_t *form = firstos_form_find_parent_form(focused);
        if (form) {
            firstos_dom_event_t sub_ev;
            sub_ev.type = DOM_EVENT_SUBMIT;
            sub_ev.target_node_id = form->id_num;
            sub_ev.current_node_id = form->id_num;
            sub_ev.phase = 2;
            sub_ev.default_prevented = 0;
            sub_ev.propagation_stopped = 0;
            firstos_dom_dispatch_event(ctx, form, &sub_ev);

            if (!sub_ev.default_prevented) {
                if (out_submit_action && max_action > 0) {
                    const char *act = form->action ? form->action : "";
                    int i = 0;
                    while (act[i] && i < max_action - 1) {
                        out_submit_action[i] = act[i];
                        i++;
                    }
                    out_submit_action[i] = '\0';
                }
                if (out_query && max_query > 0) {
                    firstos_form_collect_query(ctx, form, out_query, max_query);
                }
                return 2; /* Form Submit */
            }
        }
        return 1;
    }

    /* 3. Space on Checkbox */
    if (ascii == ' ' && focused->tag == HTML_TAG_INPUT && focused->input_type && strcmp(focused->input_type, "checkbox") == 0) {
        firstos_form_toggle_checkbox(focused);
        firstos_dom_event_t chg_ev;
        chg_ev.type = DOM_EVENT_CHANGE;
        chg_ev.target_node_id = focused->id_num;
        chg_ev.current_node_id = focused->id_num;
        chg_ev.phase = 2;
        chg_ev.default_prevented = 0;
        chg_ev.propagation_stopped = 0;
        firstos_dom_dispatch_event(ctx, focused, &chg_ev);
        return 1;
    }

    /* 4. Backspace */
    if (key == 8 || key == 0x0E || ascii == 8) {
        if (firstos_form_handle_backspace(ctx)) {
            firstos_dom_event_t inp_ev;
            inp_ev.type = DOM_EVENT_INPUT;
            inp_ev.target_node_id = focused->id_num;
            inp_ev.current_node_id = focused->id_num;
            inp_ev.phase = 2;
            inp_ev.default_prevented = 0;
            inp_ev.propagation_stopped = 0;
            firstos_dom_dispatch_event(ctx, focused, &inp_ev);
            return 1;
        }
        return 0;
    }

    /* 5. Printable Character */
    if (ascii >= 32 && ascii <= 126) {
        if (firstos_form_handle_char(ctx, ascii)) {
            firstos_dom_event_t inp_ev;
            inp_ev.type = DOM_EVENT_INPUT;
            inp_ev.target_node_id = focused->id_num;
            inp_ev.current_node_id = focused->id_num;
            inp_ev.phase = 2;
            inp_ev.default_prevented = 0;
            inp_ev.propagation_stopped = 0;
            firstos_dom_dispatch_event(ctx, focused, &inp_ev);
            return 1;
        }
    }

    return 0;
}

void firstos_renderer_handle_resize(firstos_renderer_context_t *ctx, uint32_t new_w, uint32_t new_h) {
    if (!ctx || new_w == 0 || new_h == 0) return;
    ctx->needs_full_repaint = 1;
    ctx->damage_rect = (rect_t){0, 0, (int)new_w, (int)new_h};
    firstos_renderer_layout(ctx, new_w, new_h);
}
