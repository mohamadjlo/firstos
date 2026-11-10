#include "include/firstos_renderer.h"


extern html_node_t *firstos_dom_alloc_node(firstos_renderer_context_t *ctx, html_tag_t tag);

static char html_to_lower_char(char c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

static int html_is_space_char(char c) {
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

static html_tag_t tag_from_name(const char *name, int len) {
    if (!name || len <= 0) return HTML_TAG_UNKNOWN;
    char buf[16];
    int l = (len < 15) ? len : 15;
    for (int i = 0; i < l; i++) buf[i] = html_to_lower_char(name[i]);
    buf[l] = '\0';

    if (l == 4 && buf[0] == 'h' && buf[1] == 't' && buf[2] == 'm' && buf[3] == 'l') return HTML_TAG_HTML;
    if (l == 4 && buf[0] == 'h' && buf[1] == 'e' && buf[2] == 'a' && buf[3] == 'd') return HTML_TAG_HEAD;
    if (l == 4 && buf[0] == 'b' && buf[1] == 'o' && buf[2] == 'd' && buf[3] == 'y') return HTML_TAG_BODY;
    if (l == 5 && buf[0] == 't' && buf[1] == 'i' && buf[2] == 't' && buf[3] == 'l' && buf[4] == 'e') return HTML_TAG_TITLE;
    if (l == 3 && buf[0] == 'd' && buf[1] == 'i' && buf[2] == 'v') return HTML_TAG_DIV;
    if (l == 7 && strncmp(buf, "section", 7) == 0) return HTML_TAG_SECTION;
    if (l == 7 && strncmp(buf, "article", 7) == 0) return HTML_TAG_ARTICLE;
    if (l == 6 && strncmp(buf, "header", 6) == 0) return HTML_TAG_HEADER;
    if (l == 6 && strncmp(buf, "footer", 6) == 0) return HTML_TAG_FOOTER;
    if (l == 3 && buf[0] == 'n' && buf[1] == 'a' && buf[2] == 'v') return HTML_TAG_NAV;
    if (l == 2 && buf[0] == 'h' && buf[1] == '1') return HTML_TAG_H1;
    if (l == 2 && buf[0] == 'h' && buf[1] == '2') return HTML_TAG_H2;
    if (l == 2 && buf[0] == 'h' && buf[1] == '3') return HTML_TAG_H3;
    if (l == 1 && buf[0] == 'p') return HTML_TAG_P;
    if (l == 2 && buf[0] == 'h' && buf[1] == 'r') return HTML_TAG_HR;
    if (l == 2 && buf[0] == 'b' && buf[1] == 'r') return HTML_TAG_BR;
    if (l == 1 && buf[0] == 'a') return HTML_TAG_A;
    if (l == 4 && buf[0] == 's' && buf[1] == 'p' && buf[2] == 'a' && buf[3] == 'n') return HTML_TAG_SPAN;
    if (l == 1 && buf[0] == 'b') return HTML_TAG_B;
    if (l == 6 && strncmp(buf, "strong", 6) == 0) return HTML_TAG_STRONG;
    if (l == 1 && buf[0] == 'i') return HTML_TAG_I;
    if (l == 2 && buf[0] == 'e' && buf[1] == 'm') return HTML_TAG_EM;
    if (l == 1 && buf[0] == 'u') return HTML_TAG_U;
    if (l == 5 && strncmp(buf, "style", 5) == 0) return HTML_TAG_STYLE;
    if (l == 6 && strncmp(buf, "script", 6) == 0) return HTML_TAG_SCRIPT;
    if (l == 4 && strncmp(buf, "link", 4) == 0) return HTML_TAG_LINK;
    if (l == 3 && strncmp(buf, "img", 3) == 0) return HTML_TAG_IMG;
    if (l == 4 && strncmp(buf, "form", 4) == 0) return HTML_TAG_FORM;
    if (l == 5 && strncmp(buf, "input", 5) == 0) return HTML_TAG_INPUT;
    if (l == 6 && strncmp(buf, "button", 6) == 0) return HTML_TAG_BUTTON;
    if (l == 8 && strncmp(buf, "textarea", 8) == 0) return HTML_TAG_TEXTAREA;
    if (l == 6 && strncmp(buf, "select", 6) == 0) return HTML_TAG_SELECT;
    if (l == 6 && strncmp(buf, "option", 6) == 0) return HTML_TAG_OPTION;
    if (l == 5 && strncmp(buf, "label", 5) == 0) return HTML_TAG_LABEL;
    if (l == 6 && strncmp(buf, "iframe", 6) == 0) return HTML_TAG_IFRAME;
    if (l == 5 && strncmp(buf, "audio", 5) == 0) return HTML_TAG_AUDIO;
    if (l == 5 && strncmp(buf, "video", 5) == 0) return HTML_TAG_VIDEO;

    return HTML_TAG_UNKNOWN;
}

static int is_self_closing_tag(html_tag_t tag) {
    return (tag == HTML_TAG_BR || tag == HTML_TAG_HR || tag == HTML_TAG_LINK || tag == HTML_TAG_IMG || tag == HTML_TAG_INPUT);
}

static void decode_entities(const char *src, int src_len, char *dst, int dst_max) {
    if (!src || !dst || dst_max <= 0) return;
    int di = 0;
    for (int i = 0; i < src_len && di < dst_max - 1; ) {
        if (src[i] == '&') {
            if (i + 4 <= src_len && strncmp(&src[i], "&amp;", 5) == 0) {
                dst[di++] = '&'; i += 5; continue;
            }
            if (i + 3 <= src_len && strncmp(&src[i], "&lt;", 4) == 0) {
                dst[di++] = '<'; i += 4; continue;
            }
            if (i + 3 <= src_len && strncmp(&src[i], "&gt;", 4) == 0) {
                dst[di++] = '>'; i += 4; continue;
            }
            if (i + 5 <= src_len && strncmp(&src[i], "&quot;", 6) == 0) {
                dst[di++] = '"'; i += 6; continue;
            }
            if (i + 5 <= src_len && strncmp(&src[i], "&nbsp;", 6) == 0) {
                dst[di++] = ' '; i += 6; continue;
            }
        }
        dst[di++] = src[i++];
    }
    dst[di] = '\0';
}

static int parse_int_attr(const char *str, int len) {
    int val = 0;
    for (int i = 0; i < len; i++) {
        if (str[i] >= '0' && str[i] <= '9') {
            val = val * 10 + (str[i] - '0');
        } else break;
    }
    return val;
}

static void parse_attributes(firstos_renderer_context_t *ctx, html_node_t *node, const char *str, int len) {
    if (!ctx || !node || !str || len <= 0) return;
    int p = 0;
    while (p < len) {
        while (p < len && html_is_space_char(str[p])) p++;
        if (p >= len) break;

        int kstart = p;
        while (p < len && !html_is_space_char(str[p]) && str[p] != '=' && str[p] != '>') p++;
        int klen = p - kstart;

        while (p < len && html_is_space_char(str[p])) p++;

        /* Check boolean attributes */
        if (klen == 7 && strncmp(str + kstart, "checked", 7) == 0) {
            node->checked = 1;
        } else if (klen == 8 && strncmp(str + kstart, "disabled", 8) == 0) {
            node->disabled = 1;
        }

        if (p < len && str[p] == '=') {
            p++; /* skip '=' */
            while (p < len && html_is_space_char(str[p])) p++;
            if (p >= len) break;

            char quote = 0;
            if (str[p] == '"' || str[p] == '\'') {
                quote = str[p++];
            }
            int vstart = p;
            while (p < len) {
                if (quote) {
                    if (str[p] == quote) break;
                } else {
                    if (html_is_space_char(str[p]) || str[p] == '>') break;
                }
                p++;
            }
            int vlen = p - vstart;
            if (quote && p < len && str[p] == quote) p++;

            if (klen == 2 && str[kstart] == 'i' && str[kstart+1] == 'd') {
                node->id = html5_arena_strndup(&ctx->arena, str + vstart, vlen);
            } else if (klen == 5 && strncmp(str + kstart, "class", 5) == 0) {
                node->class_name = html5_arena_strndup(&ctx->arena, str + vstart, vlen);
            } else if (klen == 4 && strncmp(str + kstart, "href", 4) == 0) {
                node->href = html5_arena_strndup(&ctx->arena, str + vstart, vlen);
            } else if (klen == 8 && strncmp(str + kstart, "download", 8) == 0) {
                node->download = html5_arena_strndup(&ctx->arena, str + vstart, vlen);
            } else if (klen == 3 && strncmp(str + kstart, "src", 3) == 0) {
                node->src = html5_arena_strndup(&ctx->arena, str + vstart, vlen);
            } else if (klen == 3 && strncmp(str + kstart, "rel", 3) == 0) {
                node->rel = html5_arena_strndup(&ctx->arena, str + vstart, vlen);
            } else if (klen == 5 && strncmp(str + kstart, "width", 5) == 0) {
                node->req_width = parse_int_attr(str + vstart, vlen);
            } else if (klen == 6 && strncmp(str + kstart, "height", 6) == 0) {
                node->req_height = parse_int_attr(str + vstart, vlen);
            } else if (klen == 4 && strncmp(str + kstart, "name", 4) == 0) {
                node->name = html5_arena_strndup(&ctx->arena, str + vstart, vlen);
            } else if (klen == 5 && strncmp(str + kstart, "value", 5) == 0) {
                int cplen = (vlen < 127) ? vlen : 127;
                memcpy(node->value, str + vstart, cplen);
                node->value[cplen] = '\0';
                node->value_len = cplen;
                node->cursor_pos = cplen;
            } else if (klen == 11 && strncmp(str + kstart, "placeholder", 11) == 0) {
                node->placeholder = html5_arena_strndup(&ctx->arena, str + vstart, vlen);
            } else if (klen == 4 && strncmp(str + kstart, "type", 4) == 0) {
                node->input_type = html5_arena_strndup(&ctx->arena, str + vstart, vlen);
            } else if (klen == 6 && strncmp(str + kstart, "action", 6) == 0) {
                node->action = html5_arena_strndup(&ctx->arena, str + vstart, vlen);
            } else if (klen == 6 && strncmp(str + kstart, "method", 6) == 0) {
                node->method = html5_arena_strndup(&ctx->arena, str + vstart, vlen);
            } else if (klen == 7 && strncmp(str + kstart, "checked", 7) == 0) {
                node->checked = 1;
            } else if (klen == 8 && strncmp(str + kstart, "disabled", 8) == 0) {
                node->disabled = 1;
            } else if (klen == 8 && strncmp(str + kstart, "controls", 8) == 0) {
                node->media_controls = 1;
            } else if (klen == 8 && strncmp(str + kstart, "autoplay", 8) == 0) {
                node->media_autoplay = 1;
            } else if (klen == 4 && strncmp(str + kstart, "loop", 4) == 0) {
                node->media_loop = 1;
            } else if (klen == 5 && strncmp(str + kstart, "muted", 5) == 0) {
                node->media_muted = 1;
            } else if (klen == 6 && strncmp(str + kstart, "poster", 6) == 0) {
                node->media_poster = html5_arena_strndup(&ctx->arena, str + vstart, vlen);
            } else if (klen == 5 && strncmp(str + kstart, "title", 5) == 0) {
                node->frame_title = html5_arena_strndup(&ctx->arena, str + vstart, vlen);
            }
        } else {
            /* Boolean attributes without value e.g. <audio controls autoplay> */
            if (klen == 8 && strncmp(str + kstart, "controls", 8) == 0) {
                node->media_controls = 1;
            } else if (klen == 8 && strncmp(str + kstart, "autoplay", 8) == 0) {
                node->media_autoplay = 1;
            } else if (klen == 4 && strncmp(str + kstart, "loop", 4) == 0) {
                node->media_loop = 1;
            } else if (klen == 5 && strncmp(str + kstart, "muted", 5) == 0) {
                node->media_muted = 1;
            }
        }
    }
}

int firstos_renderer_parse_html(firstos_renderer_context_t *ctx, const char *html, int len) {
    if (!ctx || !html || len <= 0) return RENDER_BAD_HTML;
    if (len > (int)FIRSTOS_BROWSER_MAX_PAYLOAD) return RENDER_TOO_LARGE;

    firstos_renderer_reset(ctx);

    /* Allocate virtual document root */
    ctx->root_dom = firstos_dom_alloc_node(ctx, HTML_TAG_HTML);
    if (!ctx->root_dom) return RENDER_OUT_OF_MEMORY;

    html_node_t *stack[32];
    int stack_top = 0;
    stack[stack_top] = ctx->root_dom;

    int p = 0;
    while (p < len) {
        if (html[p] == '<') {
            /* Check for comment <!-- ... --> */
            if (p + 3 < len && html[p+1] == '!' && html[p+2] == '-' && html[p+3] == '-') {
                p += 4;
                while (p + 2 < len && !(html[p] == '-' && html[p+1] == '-' && html[p+2] == '>')) p++;
                if (p + 2 < len) p += 3;
                continue;
            }

            /* End tag </tag> */
            if (p + 1 < len && html[p+1] == '/') {
                p += 2;
                int tstart = p;
                while (p < len && html[p] != '>') p++;
                html_tag_t end_tag = tag_from_name(html + tstart, p - tstart);
                if (p < len) p++; /* skip '>' */

                /* Pop until matching tag or stack bottom */
                while (stack_top > 0) {
                    if (stack[stack_top]->tag == end_tag) {
                        stack_top--;
                        break;
                    }
                    stack_top--;
                }
                continue;
            }

            /* Start tag <tag ...> */
            p++; /* skip '<' */
            int tstart = p;
            while (p < len && !html_is_space_char(html[p]) && html[p] != '>' && html[p] != '/') p++;
            int tlen = p - tstart;
            html_tag_t tag = tag_from_name(html + tstart, tlen);

            int astart = p;
            while (p < len && html[p] != '>') p++;
            int is_self_close = 0;
            if (p > astart && html[p-1] == '/') is_self_close = 1;
            int alen = p - astart;
            if (p < len) p++; /* skip '>' */

            /* Handle <style> and <script> contents directly */
            if (tag == HTML_TAG_STYLE || tag == HTML_TAG_SCRIPT) {
                const char *end_str = (tag == HTML_TAG_STYLE) ? "</style" : "</script";
                int elen = (tag == HTML_TAG_STYLE) ? 7 : 8;
                int cstart = p;
                while (p + elen <= len) {
                    if (html[p] == '<' && strncmp(html + p, end_str, elen) == 0) break;
                    p++;
                }
                int clen = p - cstart;
                while (p < len && html[p] != '>') p++;
                if (p < len) p++;

                if (clen > 0) {
                    html_node_t *snode = firstos_dom_alloc_node(ctx, tag);
                    if (snode) {
                        snode->text = html5_arena_strndup(&ctx->arena, html + cstart, clen);
                        snode->text_len = clen;
                        snode->parent = stack[stack_top];
                        if (!stack[stack_top]->first_child) {
                            stack[stack_top]->first_child = snode;
                        } else {
                            html_node_t *tail = stack[stack_top]->first_child;
                            while (tail->next_sibling) tail = tail->next_sibling;
                            tail->next_sibling = snode;
                        }
                    }
                }
                continue;
            }

            html_node_t *node = firstos_dom_alloc_node(ctx, tag);
            if (!node) break; /* Reached MAX_DOM_NODES safely */

            parse_attributes(ctx, node, html + astart, alen);

            /* Attach to parent */
            html_node_t *parent = stack[stack_top];
            node->parent = parent;
            if (!parent->first_child) {
                parent->first_child = node;
            } else {
                html_node_t *tail = parent->first_child;
                while (tail->next_sibling) tail = tail->next_sibling;
                tail->next_sibling = node;
            }

            /* Discover subresources for LINK and IMG */
            if (tag == HTML_TAG_LINK && node->rel && node->href) {
                if (strncmp(node->rel, "stylesheet", 10) == 0) {
                    if (ctx->pending_res_count < 16) {
                        ctx->pending_res[ctx->pending_res_count].request_id = ctx->pending_res_count + 1;
                        ctx->pending_res[ctx->pending_res_count].type = FIRSTOS_RES_TYPE_STYLESHEET;
                        ctx->pending_res[ctx->pending_res_count].url = node->href;
                        ctx->pending_res[ctx->pending_res_count].status = 0;
                        ctx->pending_res[ctx->pending_res_count].node = node;
                        ctx->pending_res_count++;
                    }
                }
            } else if (tag == HTML_TAG_IMG && node->src) {
                if (ctx->pending_res_count < 16) {
                    ctx->pending_res[ctx->pending_res_count].request_id = ctx->pending_res_count + 1;
                    ctx->pending_res[ctx->pending_res_count].type = FIRSTOS_RES_TYPE_IMAGE;
                    ctx->pending_res[ctx->pending_res_count].url = node->src;
                    ctx->pending_res[ctx->pending_res_count].status = 0;
                    ctx->pending_res[ctx->pending_res_count].node = node;
                    ctx->pending_res_count++;
                }
            }

            /* Push if not self-closing */
            if (!is_self_closing_tag(tag) && !is_self_close) {
                if (stack_top < 31) {
                    stack[++stack_top] = node;
                }
            }
        } else {
            /* Text content between tags */
            int tstart = p;
            while (p < len && html[p] != '<') p++;
            int tlen = p - tstart;

            /* Check if not purely whitespace */
            int has_content = 0;
            for (int i = 0; i < tlen; i++) {
                if (!html_is_space_char(html[tstart + i])) { has_content = 1; break; }
            }

            if (has_content) {
                char dec_buf[1024];
                decode_entities(html + tstart, tlen, dec_buf, sizeof(dec_buf));
                int dlen = strlen(dec_buf);

                /* If parent is <title>, store title */
                if (stack[stack_top]->tag == HTML_TAG_TITLE) {
                    int cplen = (dlen < 127) ? dlen : 127;
                    for (int i = 0; i < cplen; i++) ctx->doc_title[i] = dec_buf[i];
                    ctx->doc_title[cplen] = '\0';
                }

                html_node_t *tnode = firstos_dom_alloc_node(ctx, HTML_TAG_TEXT_NODE);
                if (tnode) {
                    tnode->text = html5_arena_strdup(&ctx->arena, dec_buf);
                    tnode->text_len = dlen;
                    tnode->parent = stack[stack_top];
                    if (!stack[stack_top]->text) {
                        stack[stack_top]->text = tnode->text;
                        stack[stack_top]->text_len = dlen;
                    }
                    if (!stack[stack_top]->first_child) {
                        stack[stack_top]->first_child = tnode;
                    } else {
                        html_node_t *tail = stack[stack_top]->first_child;
                        while (tail->next_sibling) tail = tail->next_sibling;
                        tail->next_sibling = tnode;
                    }
                }
            }
        }
    }

    return RENDER_OK;
}
