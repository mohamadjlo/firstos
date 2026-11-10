#include "include/firstos_renderer.h"


static int emit_node_commands_rec(firstos_renderer_context_t *ctx,
                                  html_node_t *node,
                                  firstos_render_command_t *out_cmds,
                                  uint32_t max_cmds,
                                  char *out_strings,
                                  uint32_t str_capacity,
                                  uint32_t *cmd_idx,
                                  uint32_t *str_idx) {
    if (!node || node->style.display_none || node->style.visibility_hidden) return 0;

    int sx = node->bounds.x;
    int sy = node->bounds.y - ctx->scroll_y;
    int sw = node->bounds.w;
    int sh = node->bounds.h;

    /* Viewport vertical culling */
    if (sy + sh < 0 || sy > (int)ctx->viewport_h + 100) {
        /* Node is completely off-screen vertically; skip it and children */
        return 0;
    }

    /* 1. Background rectangle */
    if (node->style.has_bg && sw > 0 && sh > 0) {
        if (*cmd_idx >= max_cmds - 2) return RENDER_OUT_OF_BUFFER;
        firstos_render_command_t *c = &out_cmds[(*cmd_idx)++];
        c->type = FIRSTOS_RENDER_CMD_RECT;
        c->x = sx;
        c->y = sy;
        c->w = sw;
        c->h = sh;
        c->color = node->style.bg_color;
        c->text_offset = 0;
        c->text_len = 0;
    }

    /* 2. Horizontal rule <hr> */
    if (node->tag == HTML_TAG_HR) {
        if (*cmd_idx >= max_cmds - 2) return RENDER_OUT_OF_BUFFER;
        firstos_render_command_t *c = &out_cmds[(*cmd_idx)++];
        c->type = FIRSTOS_RENDER_CMD_RECT;
        c->x = sx;
        c->y = sy + (sh / 2);
        c->w = sw;
        c->h = 2;
        c->color = 0x3A3A4C;
        c->text_offset = 0;
        c->text_len = 0;
    }

    /* 3. Text node */
    if (node->tag == HTML_TAG_TEXT_NODE && node->text && node->text_len > 0) {
        if (*cmd_idx >= max_cmds - 2) return RENDER_OUT_OF_BUFFER;
        if (*str_idx + node->text_len + 1 >= str_capacity) return RENDER_OUT_OF_BUFFER;

        uint32_t toff = *str_idx;
        memcpy(out_strings + toff, node->text, node->text_len);
        out_strings[toff + node->text_len] = '\0';
        *str_idx += (node->text_len + 1);

        firstos_render_command_t *c = &out_cmds[(*cmd_idx)++];
        c->type = FIRSTOS_RENDER_CMD_TEXT;
        c->x = sx;
        c->y = sy;
        c->w = sw;
        c->h = sh;
        c->color = node->style.color;
        c->text_offset = toff;
        c->text_len = (uint32_t)node->text_len;
    }

    /* 4. Image: Decoded Image or Placeholder */
    if (node->tag == HTML_TAG_IMG && sw > 0 && sh > 0) {
        if (node->image_id > 0 && !node->image_broken) {
            if (*cmd_idx >= max_cmds - 2) return RENDER_OUT_OF_BUFFER;
            firstos_render_command_t *img_cmd = &out_cmds[(*cmd_idx)++];
            img_cmd->type = FIRSTOS_RENDER_CMD_IMAGE;
            img_cmd->x = sx;
            img_cmd->y = sy;
            img_cmd->w = sw;
            img_cmd->h = sh;
            img_cmd->color = node->image_id;
            img_cmd->text_offset = 0;
            img_cmd->text_len = 0;
        } else if (node->image_broken) {
            if (*cmd_idx >= max_cmds - 3) return RENDER_OUT_OF_BUFFER;
            firstos_render_command_t *bg = &out_cmds[(*cmd_idx)++];
            bg->type = FIRSTOS_RENDER_CMD_RECT;
            bg->x = sx;
            bg->y = sy;
            bg->w = sw;
            bg->h = sh;
            bg->color = 0x2A2A38;
            bg->text_offset = 0;
            bg->text_len = 0;

            const char *brk_lbl = "[X]";
            int blen = 3;
            if (*str_idx + blen + 1 < str_capacity) {
                uint32_t toff = *str_idx;
                memcpy(out_strings + toff, brk_lbl, blen);
                out_strings[toff + blen] = '\0';
                *str_idx += (blen + 1);

                firstos_render_command_t *lbl = &out_cmds[(*cmd_idx)++];
                lbl->type = FIRSTOS_RENDER_CMD_TEXT;
                lbl->x = sx + (sw > 24 ? (sw - 24) / 2 : 4);
                lbl->y = sy + (sh > 16 ? (sh - 16) / 2 : 0);
                lbl->w = sw;
                lbl->h = 16;
                lbl->color = 0xFF453A;
                lbl->text_offset = toff;
                lbl->text_len = (uint32_t)blen;
            }
        } else {
            /* Pending placeholder */
            if (*cmd_idx >= max_cmds - 3) return RENDER_OUT_OF_BUFFER;

            firstos_render_command_t *bg = &out_cmds[(*cmd_idx)++];
            bg->type = FIRSTOS_RENDER_CMD_RECT;
            bg->x = sx;
            bg->y = sy;
            bg->w = sw;
            bg->h = sh;
            bg->color = 0x2A2A38;
            bg->text_offset = 0;
            bg->text_len = 0;

            const char *img_lbl = "[Image]";
            int lbl_len = 7;
            if (*str_idx + lbl_len + 1 < str_capacity) {
                uint32_t toff = *str_idx;
                memcpy(out_strings + toff, img_lbl, lbl_len);
                out_strings[toff + lbl_len] = '\0';
                *str_idx += (lbl_len + 1);

                firstos_render_command_t *lbl = &out_cmds[(*cmd_idx)++];
                lbl->type = FIRSTOS_RENDER_CMD_TEXT;
                lbl->x = sx + 4;
                lbl->y = sy + (sh > 16 ? (sh - 16) / 2 : 0);
                lbl->w = sw;
                lbl->h = 16;
                lbl->color = 0x8E8E93;
                lbl->text_offset = toff;
                lbl->text_len = (uint32_t)lbl_len;
            }
        }
    }

    /* 5. Form Controls: Input & Button */
    if (node->tag == HTML_TAG_INPUT && sw > 0 && sh > 0) {
        if (node->input_type && strcmp(node->input_type, "checkbox") == 0) {
            /* Checkbox box */
            if (*cmd_idx >= max_cmds - 3) return RENDER_OUT_OF_BUFFER;
            uint32_t box_col = (node->id_num == ctx->focus_state.focused_node_id) ? 0x007AFF : 0x383740;
            firstos_render_command_t *bg = &out_cmds[(*cmd_idx)++];
            bg->type = FIRSTOS_RENDER_CMD_RECT;
            bg->x = sx; bg->y = sy; bg->w = 16; bg->h = 16;
            bg->color = box_col; bg->text_offset = 0; bg->text_len = 0;

            firstos_render_command_t *in_bg = &out_cmds[(*cmd_idx)++];
            in_bg->type = FIRSTOS_RENDER_CMD_RECT;
            in_bg->x = sx + 1; in_bg->y = sy + 1; in_bg->w = 14; in_bg->h = 14;
            in_bg->color = 0x1C1B22; in_bg->text_offset = 0; in_bg->text_len = 0;

            if (node->checked) {
                firstos_render_command_t *chk = &out_cmds[(*cmd_idx)++];
                chk->type = FIRSTOS_RENDER_CMD_RECT;
                chk->x = sx + 3; chk->y = sy + 3; chk->w = 10; chk->h = 10;
                chk->color = 0x007AFF; chk->text_offset = 0; chk->text_len = 0;
            }
        } else if (node->input_type && strcmp(node->input_type, "submit") == 0) {
            /* Submit button */
            if (*cmd_idx >= max_cmds - 2) return RENDER_OUT_OF_BUFFER;
            firstos_render_command_t *btn_bg = &out_cmds[(*cmd_idx)++];
            btn_bg->type = FIRSTOS_RENDER_CMD_RECT;
            btn_bg->x = sx; btn_bg->y = sy; btn_bg->w = sw; btn_bg->h = sh;
            btn_bg->color = (node->id_num == ctx->focus_state.focused_node_id) ? 0x0A84FF : 0x007AFF;
            btn_bg->text_offset = 0; btn_bg->text_len = 0;

            const char *lbl = node->value[0] ? node->value : "Submit";
            int llen = (int)strlen(lbl);
            if (*str_idx + llen + 1 < str_capacity) {
                uint32_t toff = *str_idx;
                memcpy(out_strings + toff, lbl, llen);
                out_strings[toff + llen] = '\0';
                *str_idx += (llen + 1);

                firstos_render_command_t *tcmd = &out_cmds[(*cmd_idx)++];
                tcmd->type = FIRSTOS_RENDER_CMD_TEXT;
                tcmd->x = sx + (sw > llen * 8 ? (sw - llen * 8) / 2 : 4);
                tcmd->y = sy + (sh > 16 ? (sh - 16) / 2 : 0);
                tcmd->w = sw; tcmd->h = 16;
                tcmd->color = 0xFFFFFF;
                tcmd->text_offset = toff;
                tcmd->text_len = (uint32_t)llen;
            }
        } else {
            /* Text / Password Input */
            if (*cmd_idx >= max_cmds - 4) return RENDER_OUT_OF_BUFFER;
            int is_focused = (node->id_num == ctx->focus_state.focused_node_id);
            uint32_t border_col = is_focused ? 0x007AFF : 0x383740;

            /* Outer / Border */
            firstos_render_command_t *border = &out_cmds[(*cmd_idx)++];
            border->type = FIRSTOS_RENDER_CMD_RECT;
            border->x = sx; border->y = sy; border->w = sw; border->h = sh;
            border->color = border_col; border->text_offset = 0; border->text_len = 0;

            /* Inner background */
            firstos_render_command_t *in_bg = &out_cmds[(*cmd_idx)++];
            in_bg->type = FIRSTOS_RENDER_CMD_RECT;
            in_bg->x = sx + 1; in_bg->y = sy + 1; in_bg->w = (sw > 2 ? sw - 2 : 1); in_bg->h = (sh > 2 ? sh - 2 : 1);
            in_bg->color = 0x1C1B22; in_bg->text_offset = 0; in_bg->text_len = 0;

            /* Value or Placeholder */
            int is_pw = (node->input_type && strcmp(node->input_type, "password") == 0);
            if (node->value_len > 0) {
                char disp_val[128];
                int dlen = node->value_len;
                if (dlen > 120) dlen = 120;
                if (is_pw) {
                    for (int i = 0; i < dlen; i++) disp_val[i] = '*';
                    disp_val[dlen] = '\0';
                } else {
                    memcpy(disp_val, node->value, dlen);
                    disp_val[dlen] = '\0';
                }

                if (*str_idx + dlen + 1 < str_capacity) {
                    uint32_t toff = *str_idx;
                    memcpy(out_strings + toff, disp_val, dlen);
                    out_strings[toff + dlen] = '\0';
                    *str_idx += (dlen + 1);

                    firstos_render_command_t *tcmd = &out_cmds[(*cmd_idx)++];
                    tcmd->type = FIRSTOS_RENDER_CMD_TEXT;
                    tcmd->x = sx + 6;
                    tcmd->y = sy + (sh > 16 ? (sh - 16) / 2 : 0);
                    tcmd->w = sw - 12; tcmd->h = 16;
                    tcmd->color = 0xFFFFFF;
                    tcmd->text_offset = toff;
                    tcmd->text_len = (uint32_t)dlen;
                }
            } else if (node->placeholder && node->placeholder[0]) {
                int plen = (int)strlen(node->placeholder);
                if (plen > 120) plen = 120;
                if (*str_idx + plen + 1 < str_capacity) {
                    uint32_t toff = *str_idx;
                    memcpy(out_strings + toff, node->placeholder, plen);
                    out_strings[toff + plen] = '\0';
                    *str_idx += (plen + 1);

                    firstos_render_command_t *tcmd = &out_cmds[(*cmd_idx)++];
                    tcmd->type = FIRSTOS_RENDER_CMD_TEXT;
                    tcmd->x = sx + 6;
                    tcmd->y = sy + (sh > 16 ? (sh - 16) / 2 : 0);
                    tcmd->w = sw - 12; tcmd->h = 16;
                    tcmd->color = 0x707080;
                    tcmd->text_offset = toff;
                    tcmd->text_len = (uint32_t)plen;
                }
            }

            /* Focused cursor line */
            if (is_focused && *cmd_idx < max_cmds - 1) {
                int c_x = sx + 6 + (node->cursor_pos * 8);
                if (c_x > sx + sw - 6) c_x = sx + sw - 6;
                firstos_render_command_t *cur = &out_cmds[(*cmd_idx)++];
                cur->type = FIRSTOS_RENDER_CMD_RECT;
                cur->x = c_x; cur->y = sy + 3;
                cur->w = 2; cur->h = (sh > 6 ? sh - 6 : 14);
                cur->color = 0x007AFF;
                cur->text_offset = 0; cur->text_len = 0;
            }
        }
    } else if (node->tag == HTML_TAG_BUTTON && sw > 0 && sh > 0) {
        /* Generic button */
        if (*cmd_idx >= max_cmds - 2) return RENDER_OUT_OF_BUFFER;
        int is_focused = (node->id_num == ctx->focus_state.focused_node_id);
        firstos_render_command_t *btn_bg = &out_cmds[(*cmd_idx)++];
        btn_bg->type = FIRSTOS_RENDER_CMD_RECT;
        btn_bg->x = sx; btn_bg->y = sy; btn_bg->w = sw; btn_bg->h = sh;
        btn_bg->color = is_focused ? 0x007AFF : 0x2E2D38;
        btn_bg->text_offset = 0; btn_bg->text_len = 0;

        const char *lbl = (node->text && node->text[0]) ? node->text : (node->value[0] ? node->value : "Button");
        int llen = (int)strlen(lbl);
        if (*str_idx + llen + 1 < str_capacity) {
            uint32_t toff = *str_idx;
            memcpy(out_strings + toff, lbl, llen);
            out_strings[toff + llen] = '\0';
            *str_idx += (llen + 1);

            firstos_render_command_t *tcmd = &out_cmds[(*cmd_idx)++];
            tcmd->type = FIRSTOS_RENDER_CMD_TEXT;
            tcmd->x = sx + (sw > llen * 8 ? (sw - llen * 8) / 2 : 4);
            tcmd->y = sy + (sh > 16 ? (sh - 16) / 2 : 0);
            tcmd->w = sw; tcmd->h = 16;
            tcmd->color = 0xFFFFFF;
            tcmd->text_offset = toff;
            tcmd->text_len = (uint32_t)llen;
        }
    }

    /* Phase 5C: Iframe replaced element -> border + child viewport CLIP */
    if (node->tag == HTML_TAG_IFRAME) {
        if (sw > 0 && sh > 0) {
            if (*cmd_idx + 4 >= max_cmds) return RENDER_OUT_OF_BUFFER;
            firstos_render_command_t *bd = &out_cmds[(*cmd_idx)++];
            bd->type = FIRSTOS_RENDER_CMD_RECT;
            bd->x = sx; bd->y = sy; bd->w = sw; bd->h = sh;
            bd->color = node->style.border_color ? node->style.border_color : 0x808080;
            bd->text_offset = 0; bd->text_len = 0;

            int b = node->style.border_width > 0 ? node->style.border_width : 1;
            int iw = sw - 2 * b, ih = sh - 2 * b;
            if (iw < 0) iw = 0;
            if (ih < 0) ih = 0;

            firstos_render_command_t *in_bg = &out_cmds[(*cmd_idx)++];
            in_bg->type = FIRSTOS_RENDER_CMD_RECT;
            in_bg->x = sx + b; in_bg->y = sy + b; in_bg->w = iw; in_bg->h = ih;
            in_bg->color = 0x101018;
            in_bg->text_offset = 0; in_bg->text_len = 0;

            firstos_render_command_t *clip = &out_cmds[(*cmd_idx)++];
            clip->type = FIRSTOS_RENDER_CMD_CLIP;
            clip->x = sx + b; clip->y = sy + b; clip->w = iw; clip->h = ih;
            clip->color = node->frame_id;   /* Host-assigned child frame id (0 = unbound) */
            clip->text_offset = 0;
            clip->text_len = 0;
            /* iframe src travels through the bounded string table (no pointers) */
            if (node->src) {
                int slen = (int)strlen(node->src);
                if (slen > 511) slen = 511;
                if (slen > 0 && *str_idx + (uint32_t)slen + 1 < str_capacity) {
                    uint32_t toff = *str_idx;
                    memcpy(out_strings + toff, node->src, slen);
                    out_strings[toff + slen] = '\0';
                    *str_idx += (uint32_t)(slen + 1);
                    clip->text_offset = toff;
                    clip->text_len = (uint32_t)slen;
                }
            }
        }
        return 0; /* never paint fallback content of an iframe */
    }

    /* Phase 6B: HTML5 Video & Audio Elements */
    if ((node->tag == HTML_TAG_VIDEO || node->tag == HTML_TAG_AUDIO) && sw > 0 && sh > 0) {
        if (*cmd_idx + 8 >= max_cmds) return RENDER_OUT_OF_BUFFER;

        /* Outer Frame Container */
        firstos_render_command_t *bg = &out_cmds[(*cmd_idx)++];
        bg->type = FIRSTOS_RENDER_CMD_RECT;
        bg->x = sx; bg->y = sy; bg->w = sw; bg->h = sh;
        bg->color = (node->tag == HTML_TAG_VIDEO) ? 0x0A0A10 : 0x1E1E28;
        bg->text_offset = 0; bg->text_len = 0;

        /* Border */
        firstos_render_command_t *border = &out_cmds[(*cmd_idx)++];
        border->type = FIRSTOS_RENDER_CMD_RECT;
        border->x = sx; border->y = sy; border->w = sw; border->h = 1;
        border->color = 0x3A3A4C; border->text_offset = 0; border->text_len = 0;

        int ctrl_h = (node->media_controls || node->tag == HTML_TAG_AUDIO) ? 32 : 0;
        int canvas_h = sh - ctrl_h;

        /* Video Display Area */
        if (node->tag == HTML_TAG_VIDEO && canvas_h > 20) {
            firstos_render_command_t *vdisp = &out_cmds[(*cmd_idx)++];
            vdisp->type = FIRSTOS_RENDER_CMD_RECT;
            vdisp->x = sx + 2; vdisp->y = sy + 2; vdisp->w = sw - 4; vdisp->h = canvas_h - 4;
            vdisp->color = 0x14141E; vdisp->text_offset = 0; vdisp->text_len = 0;

            const char *label = node->media_playing ? "HTML5 VIDEO PLAYING" : "HTML5 VIDEO PLAYER";
            int llen = (int)strlen(label);
            if (*str_idx + (uint32_t)llen + 1 < str_capacity) {
                uint32_t toff = *str_idx;
                memcpy(out_strings + toff, label, llen);
                out_strings[toff + llen] = '\0';
                *str_idx += (uint32_t)(llen + 1);

                firstos_render_command_t *tcmd = &out_cmds[(*cmd_idx)++];
                tcmd->type = FIRSTOS_RENDER_CMD_TEXT;
                tcmd->x = sx + 12; tcmd->y = sy + 12; tcmd->w = sw - 24; tcmd->h = 16;
                tcmd->color = node->media_playing ? 0x30D158 : 0xFFFFFF;
                tcmd->text_offset = toff; tcmd->text_len = (uint32_t)llen;
            }
        }

        /* Bottom Transport Controls Bar */
        if (ctrl_h > 0) {
            int cy = sy + sh - ctrl_h;
            firstos_render_command_t *cbar = &out_cmds[(*cmd_idx)++];
            cbar->type = FIRSTOS_RENDER_CMD_RECT;
            cbar->x = sx; cbar->y = cy; cbar->w = sw; cbar->h = ctrl_h;
            cbar->color = 0x161622; cbar->text_offset = 0; cbar->text_len = 0;

            /* Play / Pause Button Box */
            firstos_render_command_t *pbtn = &out_cmds[(*cmd_idx)++];
            pbtn->type = FIRSTOS_RENDER_CMD_RECT;
            pbtn->x = sx + 6; pbtn->y = cy + 4; pbtn->w = 26; pbtn->h = 24;
            pbtn->color = 0x2A2A3C; pbtn->text_offset = 0; pbtn->text_len = 0;

            const char *btn_icon = node->media_playing ? "||" : ">";
            int blen = (int)strlen(btn_icon);
            if (*str_idx + (uint32_t)blen + 1 < str_capacity) {
                uint32_t toff = *str_idx;
                memcpy(out_strings + toff, btn_icon, blen);
                out_strings[toff + blen] = '\0';
                *str_idx += (uint32_t)(blen + 1);

                firstos_render_command_t *btxt = &out_cmds[(*cmd_idx)++];
                btxt->type = FIRSTOS_RENDER_CMD_TEXT;
                btxt->x = sx + 14; btxt->y = cy + 8; btxt->w = 16; btxt->h = 16;
                btxt->color = 0xFFFFFF; btxt->text_offset = toff; btxt->text_len = (uint32_t)blen;
            }

            /* Timeline Scrubber Track */
            int scrub_x = sx + 38;
            int scrub_w = sw - 120;
            if (scrub_w > 40) {
                firstos_render_command_t *track = &out_cmds[(*cmd_idx)++];
                track->type = FIRSTOS_RENDER_CMD_RECT;
                track->x = scrub_x; track->y = cy + 12; track->w = scrub_w; track->h = 8;
                track->color = 0x333348; track->text_offset = 0; track->text_len = 0;

                int dur = node->media_duration_sec > 0 ? (int)node->media_duration_sec : 90;
                int cur = (int)node->media_cur_time_sec;
                int prog_w = (dur > 0) ? (scrub_w * cur) / dur : 0;
                if (prog_w > scrub_w) prog_w = scrub_w;
                if (prog_w > 0) {
                    firstos_render_command_t *fill = &out_cmds[(*cmd_idx)++];
                    fill->type = FIRSTOS_RENDER_CMD_RECT;
                    fill->x = scrub_x; fill->y = cy + 12; fill->w = prog_w; fill->h = 8;
                    fill->color = 0x007AFF; fill->text_offset = 0; fill->text_len = 0;
                }
            }
        }
        return 0;
    }

    /* Recurse for children */
    for (html_node_t *child = node->first_child; child; child = child->next_sibling) {
        int res = emit_node_commands_rec(ctx, child, out_cmds, max_cmds, out_strings, str_capacity, cmd_idx, str_idx);
        if (res != 0) return res;
    }

    return 0;
}

int firstos_renderer_generate_commands(firstos_renderer_context_t *ctx,
                                        firstos_render_command_t *out_cmds,
                                        uint32_t max_cmds,
                                        char *out_strings,
                                        uint32_t str_capacity,
                                        uint32_t *out_num_cmds) {
    if (!ctx || !out_cmds || max_cmds < 4 || !out_strings || str_capacity < 64) {
        return RENDER_OUT_OF_BUFFER;
    }

    uint32_t cmd_idx = 0;
    uint32_t str_idx = 0;

    /* 1. Initial CLEAR command covering the viewport */
    uint32_t bg_color = 0x16161E; /* Standard FirstOS dark background */
    html_node_t *body = firstos_renderer_get_element_by_tag(ctx, HTML_TAG_BODY);
    if (body && body->style.has_bg) {
        bg_color = body->style.bg_color;
    }

    firstos_render_command_t *clear_cmd = &out_cmds[cmd_idx++];
    clear_cmd->type = FIRSTOS_RENDER_CMD_CLEAR;
    clear_cmd->x = 0;
    clear_cmd->y = 0;
    clear_cmd->w = (int)ctx->viewport_w;
    clear_cmd->h = (int)ctx->viewport_h;
    clear_cmd->color = bg_color;
    clear_cmd->text_offset = 0;
    clear_cmd->text_len = 0;

    /* 2. Traverse tree and generate commands */
    if (ctx->root_dom) {
        int res = emit_node_commands_rec(ctx, ctx->root_dom, out_cmds, max_cmds, out_strings, str_capacity, &cmd_idx, &str_idx);
        if (res != 0) return res;
    }

    /* 3. Terminal END command */
    firstos_render_command_t *end_cmd = &out_cmds[cmd_idx++];
    end_cmd->type = FIRSTOS_RENDER_CMD_END;
    end_cmd->x = 0; end_cmd->y = 0; end_cmd->w = 0; end_cmd->h = 0;
    end_cmd->color = 0; end_cmd->text_offset = 0; end_cmd->text_len = 0;

    if (out_num_cmds) *out_num_cmds = cmd_idx;
    return RENDER_OK;
}
