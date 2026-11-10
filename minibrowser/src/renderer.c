#define _POSIX_C_SOURCE 200809L
#include "renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool is_or_inside_anchor(const DOMNode *node) {
    const DOMNode *curr = node;
    while (curr) {
        if (curr->type == NODE_ELEMENT && curr->tag_name && strcasecmp(curr->tag_name, "a") == 0) {
            return true;
        }
        curr = curr->parent;
    }
    return false;
}

static void draw_borders(SDL_Renderer *rend, int x, int y, int w, int h, int bw, RGBA c) {
    if (bw <= 0 || c.a == 0) return;
    SDL_SetRenderDrawColor(rend, c.r, c.g, c.b, c.a);
    for (int i = 0; i < bw; i++) {
        SDL_Rect r = {x + i, y + i, w - 2 * i, h - 2 * i};
        if (r.w > 0 && r.h > 0) {
            SDL_RenderDrawRect(rend, &r);
        }
    }
}

static TTF_Font *select_font(const LayoutBox *box, WindowManager *wm) {
    if (!box || !wm) return wm ? wm->font : NULL;
    float font_size = 16.0f;
    if (box->node && box->node->style) {
        font_size = box->node->style->font_size;
    }

    if (font_size >= 24.0f && wm->font_title) {
        return wm->font_title;
    } else if (font_size >= 19.0f && wm->font_bold) {
        return wm->font_bold;
    } else if (box->node && box->node->type == NODE_ELEMENT && box->node->tag_name) {
        if (strcasecmp(box->node->tag_name, "h1") == 0 ||
            strcasecmp(box->node->tag_name, "h2") == 0 ||
            strcasecmp(box->node->tag_name, "h3") == 0 ||
            strcasecmp(box->node->tag_name, "strong") == 0 ||
            strcasecmp(box->node->tag_name, "b") == 0) {
            return wm->font_bold ? wm->font_bold : wm->font;
        }
    }
    return wm->font;
}

static void render_layout_box(SDL_Renderer *rend, const LayoutBox *box, int scroll_y, WindowManager *wm, int win_h) {
    if (!box) return;

    float bg_x = box->dimensions.x - box->padding.left;
    float bg_y = box->dimensions.y - box->padding.top - (float)scroll_y;
    float bg_w = box->dimensions.w + box->padding.left + box->padding.right;
    float bg_h = box->dimensions.h + box->padding.top + box->padding.bottom;

    /* Render Box Background & Borders for block or inline element boxes */
    if (box->box_type != BOX_TEXT && box->node && box->node->style) {
        /* Background */
        if (box->node->style->background_color.a > 0 && bg_w > 0 && bg_h > 0) {
            SDL_SetRenderDrawColor(
                rend,
                box->node->style->background_color.r,
                box->node->style->background_color.g,
                box->node->style->background_color.b,
                box->node->style->background_color.a
            );
            SDL_Rect r = {(int)bg_x, (int)bg_y, (int)bg_w, (int)bg_h};
            SDL_RenderFillRect(rend, &r);
        }

        /* Border */
        if (box->node->style->border_width > 0 && box->node->style->border_color.a > 0) {
            int bw = (int)box->node->style->border_width;
            int bx = (int)(bg_x - box->border.left);
            int by = (int)(bg_y - box->border.top);
            int bw_full = (int)(bg_w + box->border.left + box->border.right);
            int bh_full = (int)(bg_h + box->border.top + box->border.bottom);
            draw_borders(rend, bx, by, bw_full, bh_full, bw, box->node->style->border_color);
        }
    }

    /* Render Text */
    if (box->box_type == BOX_TEXT && box->text && strlen(box->text) > 0) {
        int tx = (int)box->dimensions.x;
        int ty = (int)(box->dimensions.y - scroll_y);

        /* Viewport culling */
        if (ty + (int)box->dimensions.h >= 0 && ty <= win_h) {
            TTF_Font *font = select_font(box, wm);
            if (font) {
                RGBA color = {20, 20, 25, 255};
                if (box->node && box->node->style) {
                    color = box->node->style->color;
                }

                SDL_Color sdl_col = {color.r, color.g, color.b, color.a};
                SDL_Surface *surf = TTF_RenderUTF8_Blended(font, box->text, sdl_col);
                if (surf) {
                    SDL_Texture *tex = SDL_CreateTextureFromSurface(rend, surf);
                    if (tex) {
                        SDL_Rect dst = {tx, ty, surf->w, surf->h};
                        SDL_RenderCopy(rend, tex, NULL, &dst);
                        SDL_DestroyTexture(tex);

                        /* Draw underline if link */
                        if (box->node && is_or_inside_anchor(box->node)) {
                            SDL_SetRenderDrawColor(rend, color.r, color.g, color.b, color.a);
                            int line_y = dst.y + dst.h - 1;
                            SDL_RenderDrawLine(rend, dst.x, line_y, dst.x + dst.w, line_y);
                        }
                    }
                    SDL_FreeSurface(surf);
                }
            }
        }
    }

    /* Recursively render child boxes */
    for (const LayoutBox *child = box->first_child; child; child = child->next_sibling) {
        render_layout_box(rend, child, scroll_y, wm, win_h);
    }
}

static void render_modal_dialog(BrowserWindow *win, WindowManager *wm) {
    if (!win->modal.active) return;
    SDL_Renderer *rend = win->sdl_renderer;

    /* 1. Darkened Backdrop Overlay */
    SDL_SetRenderDrawColor(rend, 15, 23, 42, 180); /* Dark slate semi-transparent */
    SDL_Rect screen_rect = {0, 0, win->width, win->height};
    SDL_RenderFillRect(rend, &screen_rect);

    /* 2. Dialog Card Box */
    SDL_Rect card = {
        (int)win->modal.box.x,
        (int)win->modal.box.y,
        (int)win->modal.box.w,
        (int)win->modal.box.h
    };

    /* Card shadow */
    SDL_SetRenderDrawColor(rend, 0, 0, 0, 60);
    SDL_Rect shadow = {card.x + 4, card.y + 4, card.w, card.h};
    SDL_RenderFillRect(rend, &shadow);

    /* Card background */
    SDL_SetRenderDrawColor(rend, 255, 255, 255, 255);
    SDL_RenderFillRect(rend, &card);

    /* Card border */
    SDL_SetRenderDrawColor(rend, 203, 213, 225, 255);
    SDL_RenderDrawRect(rend, &card);

    /* 3. Header bar */
    SDL_Rect header = {card.x, card.y, card.w, 42};
    SDL_SetRenderDrawColor(rend, 248, 250, 252, 255);
    SDL_RenderFillRect(rend, &header);
    SDL_SetRenderDrawColor(rend, 226, 232, 240, 255);
    SDL_RenderDrawLine(rend, card.x, card.y + 42, card.x + card.w, card.y + 42);

    /* Title text */
    if (win->modal.title && wm->font_bold) {
        SDL_Color title_col = {30, 41, 59, 255};
        SDL_Surface *surf = TTF_RenderUTF8_Blended(wm->font_bold, win->modal.title, title_col);
        if (surf) {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(rend, surf);
            if (tex) {
                SDL_Rect dst = {card.x + 18, card.y + 11, surf->w, surf->h};
                SDL_RenderCopy(rend, tex, NULL, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
        }
    }

    /* 4. Message text */
    if (win->modal.message && wm->font) {
        SDL_Color msg_col = {51, 65, 85, 255};
        SDL_Surface *surf = TTF_RenderUTF8_Blended_Wrapped(wm->font, win->modal.message, msg_col, card.w - 36);
        if (surf) {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(rend, surf);
            if (tex) {
                SDL_Rect dst = {card.x + 18, card.y + 56, surf->w, surf->h};
                SDL_RenderCopy(rend, tex, NULL, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
        }
    }

    /* 5. "OK" Button */
    SDL_Rect btn = {
        (int)win->modal.ok_button.x,
        (int)win->modal.ok_button.y,
        (int)win->modal.ok_button.w,
        (int)win->modal.ok_button.h
    };

    SDL_SetRenderDrawColor(rend, 37, 99, 235, 255); /* Tailwind Blue-600 */
    SDL_RenderFillRect(rend, &btn);
    SDL_SetRenderDrawColor(rend, 29, 78, 216, 255); /* Tailwind Blue-700 */
    SDL_RenderDrawRect(rend, &btn);

    if (wm->font_bold) {
        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface *surf = TTF_RenderUTF8_Blended(wm->font_bold, "OK", white);
        if (surf) {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(rend, surf);
            if (tex) {
                SDL_Rect dst = {
                    btn.x + (btn.w - surf->w) / 2,
                    btn.y + (btn.h - surf->h) / 2,
                    surf->w,
                    surf->h
                };
                SDL_RenderCopy(rend, tex, NULL, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
        }
    }
}

static void render_scrollbar(BrowserWindow *win) {
    if (win->max_scroll_y <= 0) return;
    SDL_Renderer *rend = win->sdl_renderer;

    int track_w = 8;
    int track_x = win->width - track_w - 2;
    int track_y = 2;
    int track_h = win->height - 4;

    /* Background track */
    SDL_SetRenderDrawColor(rend, 240, 240, 242, 180);
    SDL_Rect track_rect = {track_x, track_y, track_w, track_h};
    SDL_RenderFillRect(rend, &track_rect);

    /* Thumb */
    int total_doc_h = win->height + win->max_scroll_y;
    int thumb_h = (track_h * win->height) / total_doc_h;
    if (thumb_h < 24) thumb_h = 24;

    int thumb_y = track_y + (int)(((float)win->scroll_y / (float)win->max_scroll_y) * (track_h - thumb_h));
    SDL_SetRenderDrawColor(rend, 160, 160, 170, 220);
    SDL_Rect thumb_rect = {track_x + 1, thumb_y, track_w - 2, thumb_h};
    SDL_RenderFillRect(rend, &thumb_rect);
}

static void renderer_draw_all_elements(BrowserWindow *win, WindowManager *wm) {
    /* 1. Clear background */
    RGBA clear_col = {250, 250, 252, 255};
    if (win->dom_root) {
        DOMNode *body = dom_node_find_by_tag(win->dom_root, "body");
        if (body && body->style && body->style->background_color.a > 0) {
            clear_col = body->style->background_color;
        }
    }
    SDL_SetRenderDrawColor(win->sdl_renderer, clear_col.r, clear_col.g, clear_col.b, clear_col.a);
    SDL_RenderClear(win->sdl_renderer);

    /* 2. Render layout tree */
    if (win->layout_root) {
        render_layout_box(win->sdl_renderer, win->layout_root, win->scroll_y, wm, win->height);
    }

    /* 3. Render scrollbar if content overflows */
    render_scrollbar(win);

    /* 4. Render modal dialog overlay if active (focus trapping modal) */
    render_modal_dialog(win, wm);
}

void renderer_paint_window(BrowserWindow *win, WindowManager *wm) {
    if (!win || !wm || !win->sdl_renderer) return;
    renderer_draw_all_elements(win, wm);
    SDL_RenderPresent(win->sdl_renderer);
    win->needs_redraw = false;
}

void renderer_save_screenshot(BrowserWindow *win, WindowManager *wm, const char *bmp_path) {
    if (!win || !wm || !win->sdl_renderer || !bmp_path) return;
    renderer_draw_all_elements(win, wm);

    int rw = 0, rh = 0;
    SDL_GetRendererOutputSize(win->sdl_renderer, &rw, &rh);
    if (rw <= 0 || rh <= 0) {
        rw = win->width;
        rh = win->height;
    }

    SDL_Surface *sshot = SDL_CreateRGBSurfaceWithFormat(0, rw, rh, 32, SDL_PIXELFORMAT_ARGB8888);
    if (sshot) {
        SDL_RenderReadPixels(win->sdl_renderer, NULL, SDL_PIXELFORMAT_ARGB8888, sshot->pixels, sshot->pitch);
        SDL_SaveBMP(sshot, bmp_path);
        SDL_FreeSurface(sshot);
    }

    SDL_RenderPresent(win->sdl_renderer);
    win->needs_redraw = false;
}
