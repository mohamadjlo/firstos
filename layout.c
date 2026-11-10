#include "layout.h"

static int clamp_value(int v, int min_v, int max_v) {
    if (v < min_v) return min_v;
    if (max_v > 0 && v > max_v) return max_v;
    return v;
}

static int item_outer_main(const layout_container_t *c, const layout_item_t *it) {
    int base = c->direction == LAYOUT_DIRECTION_ROW ? it->frame.w : it->frame.h;
    int before = c->direction == LAYOUT_DIRECTION_ROW ? it->margin_left : it->margin_top;
    int after = c->direction == LAYOUT_DIRECTION_ROW ? it->margin_right : it->margin_bottom;
    return base + before + after;
}

static int item_outer_cross(const layout_container_t *c, const layout_item_t *it) {
    int base = c->direction == LAYOUT_DIRECTION_ROW ? it->frame.h : it->frame.w;
    int before = c->direction == LAYOUT_DIRECTION_ROW ? it->margin_top : it->margin_left;
    int after = c->direction == LAYOUT_DIRECTION_ROW ? it->margin_bottom : it->margin_right;
    return base + before + after;
}

void layout_init_container(layout_container_t *container, int x, int y, int w, int h) {
    container->frame.x = x;
    container->frame.y = y;
    container->frame.w = w;
    container->frame.h = h;
    container->pad_left = 0;
    container->pad_top = 0;
    container->pad_right = 0;
    container->pad_bottom = 0;
    container->gap = 0;
    container->wrap = 0;
    container->direction = LAYOUT_DIRECTION_ROW;
    container->align_items = LAYOUT_ALIGN_START;
    container->justify_content = LAYOUT_JUSTIFY_START;
}

void layout_init_item(layout_item_t *item, int basis_w, int basis_h) {
    item->basis = 0;
    item->grow = 0;
    item->shrink = 1;
    item->min_w = basis_w;
    item->min_h = basis_h;
    item->max_w = 0;
    item->max_h = 0;
    item->pad_left = item->pad_top = item->pad_right = item->pad_bottom = 0;
    item->margin_left = item->margin_top = item->margin_right = item->margin_bottom = 0;
    item->align_self = -1;
    item->visible = 1;
    item->frame.x = item->frame.y = 0;
    item->frame.w = basis_w;
    item->frame.h = basis_h;
    item->content = item->frame;
}

void layout_set_padding(layout_item_t *item, int left, int top, int right, int bottom) {
    item->pad_left = left;
    item->pad_top = top;
    item->pad_right = right;
    item->pad_bottom = bottom;
}

void layout_set_margin(layout_item_t *item, int left, int top, int right, int bottom) {
    item->margin_left = left;
    item->margin_top = top;
    item->margin_right = right;
    item->margin_bottom = bottom;
}

void layout_set_container_padding(layout_container_t *container, int left, int top, int right, int bottom) {
    container->pad_left = left;
    container->pad_top = top;
    container->pad_right = right;
    container->pad_bottom = bottom;
}

void layout_apply(layout_container_t *container, layout_item_t *items, int count) {
    int inner_x = container->frame.x + container->pad_left;
    int inner_y = container->frame.y + container->pad_top;
    int inner_w = container->frame.w - container->pad_left - container->pad_right;
    int inner_h = container->frame.h - container->pad_top - container->pad_bottom;
    int inner_main = container->direction == LAYOUT_DIRECTION_ROW ? inner_w : inner_h;
    int inner_cross = container->direction == LAYOUT_DIRECTION_ROW ? inner_h : inner_w;
    int i;
    int visible_count = 0;
    int total_base = 0;
    int total_grow = 0;
    int total_shrink = 0;
    int gaps = 0;
    int cursor_main;
    int extra = 0;
    int between = container->gap;

    for (i = 0; i < count; i++) {
        int main_size;
        int cross_size;
        if (!items[i].visible) continue;
        main_size = container->direction == LAYOUT_DIRECTION_ROW ? items[i].min_w : items[i].min_h;
        cross_size = container->direction == LAYOUT_DIRECTION_ROW ? items[i].min_h : items[i].min_w;
        if (container->direction == LAYOUT_DIRECTION_ROW) {
            items[i].frame.w = clamp_value(main_size, items[i].min_w, items[i].max_w);
            items[i].frame.h = clamp_value(cross_size, items[i].min_h, items[i].max_h);
        } else {
            items[i].frame.h = clamp_value(main_size, items[i].min_h, items[i].max_h);
            items[i].frame.w = clamp_value(cross_size, items[i].min_w, items[i].max_w);
        }
        total_base += item_outer_main(container, &items[i]);
        total_grow += items[i].grow;
        total_shrink += items[i].shrink;
        visible_count++;
    }

    if (visible_count <= 0) return;
    gaps = container->gap * (visible_count - 1);

    if (container->wrap) {
        int line_start = 0;
        int line_main = 0;
        int line_cross = 0;
        for (i = 0; i <= count; i++) {
            int need_break = 0;
            if (i == count) {
                need_break = 1;
            } else if (items[i].visible) {
                int outer_main = item_outer_main(container, &items[i]);
                if (line_main > 0 && line_main + container->gap + outer_main > inner_main) need_break = 1;
                if (!need_break) {
                    if (line_main > 0) line_main += container->gap;
                    line_main += outer_main;
                    if (item_outer_cross(container, &items[i]) > line_cross) line_cross = item_outer_cross(container, &items[i]);
                    continue;
                }
            }
            {
                int line_extra = inner_main - line_main;
                int line_cursor = inner_x;
                int j;
                if (container->direction == LAYOUT_DIRECTION_COLUMN) line_cursor = inner_y;
                if (container->justify_content == LAYOUT_JUSTIFY_CENTER) line_cursor += line_extra / 2;
                else if (container->justify_content == LAYOUT_JUSTIFY_END) line_cursor += line_extra;
                for (j = line_start; j < i; j++) {
                    int cross_align = items[j].align_self >= 0 ? items[j].align_self : container->align_items;
                    int cross_pos = 0;
                    if (!items[j].visible) continue;
                    if (cross_align == LAYOUT_ALIGN_CENTER) cross_pos = (line_cross - item_outer_cross(container, &items[j])) / 2;
                    else if (cross_align == LAYOUT_ALIGN_END) cross_pos = (line_cross - item_outer_cross(container, &items[j]));
                    else cross_pos = 0;
                    if (container->direction == LAYOUT_DIRECTION_ROW) {
                        items[j].frame.x = line_cursor + items[j].margin_left;
                        items[j].frame.y = inner_y + cross_pos + items[j].margin_top;
                        if (cross_align == LAYOUT_ALIGN_STRETCH) items[j].frame.h = line_cross - items[j].margin_top - items[j].margin_bottom;
                    } else {
                        items[j].frame.y = line_cursor + items[j].margin_top;
                        items[j].frame.x = inner_x + cross_pos + items[j].margin_left;
                        if (cross_align == LAYOUT_ALIGN_STRETCH) items[j].frame.w = line_cross - items[j].margin_left - items[j].margin_right;
                    }
                    items[j].content.x = items[j].frame.x + items[j].pad_left;
                    items[j].content.y = items[j].frame.y + items[j].pad_top;
                    items[j].content.w = items[j].frame.w - items[j].pad_left - items[j].pad_right;
                    items[j].content.h = items[j].frame.h - items[j].pad_top - items[j].pad_bottom;
                    line_cursor += item_outer_main(container, &items[j]) + container->gap;
                }
            }
            if (i < count && items[i].visible) {
                if (container->direction == LAYOUT_DIRECTION_ROW) inner_y += line_cross + container->gap;
                else inner_x += line_cross + container->gap;
                line_start = i;
                line_main = item_outer_main(container, &items[i]);
                line_cross = item_outer_cross(container, &items[i]);
            }
        }
        return;
    }

    extra = inner_main - total_base - gaps;
    if (extra > 0 && total_grow > 0) {
        for (i = 0; i < count; i++) {
            if (!items[i].visible || items[i].grow <= 0) continue;
            if (container->direction == LAYOUT_DIRECTION_ROW) items[i].frame.w += (extra * items[i].grow) / total_grow;
            else items[i].frame.h += (extra * items[i].grow) / total_grow;
        }
    } else if (extra < 0 && total_shrink > 0) {
        int lack = -extra;
        for (i = 0; i < count; i++) {
            int delta;
            if (!items[i].visible || items[i].shrink <= 0) continue;
            delta = (lack * items[i].shrink) / total_shrink;
            if (container->direction == LAYOUT_DIRECTION_ROW) items[i].frame.w = clamp_value(items[i].frame.w - delta, items[i].min_w, items[i].max_w);
            else items[i].frame.h = clamp_value(items[i].frame.h - delta, items[i].min_h, items[i].max_h);
        }
    }

    cursor_main = container->direction == LAYOUT_DIRECTION_ROW ? inner_x : inner_y;
    if (container->justify_content == LAYOUT_JUSTIFY_CENTER && extra > 0) cursor_main += extra / 2;
    else if (container->justify_content == LAYOUT_JUSTIFY_END && extra > 0) cursor_main += extra;
    else if (container->justify_content == LAYOUT_JUSTIFY_SPACE_BETWEEN && visible_count > 1 && extra > 0) between = container->gap + extra / (visible_count - 1);

    for (i = 0; i < count; i++) {
        int cross_align;
        int item_cross;
        int cross_pos;
        if (!items[i].visible) continue;
        cross_align = items[i].align_self >= 0 ? items[i].align_self : container->align_items;
        item_cross = container->direction == LAYOUT_DIRECTION_ROW ? items[i].frame.h + items[i].margin_top + items[i].margin_bottom : items[i].frame.w + items[i].margin_left + items[i].margin_right;
        if (cross_align == LAYOUT_ALIGN_CENTER) cross_pos = (inner_cross - item_cross) / 2;
        else if (cross_align == LAYOUT_ALIGN_END) cross_pos = inner_cross - item_cross;
        else cross_pos = 0;
        if (container->direction == LAYOUT_DIRECTION_ROW) {
            items[i].frame.x = cursor_main + items[i].margin_left;
            items[i].frame.y = inner_y + cross_pos + items[i].margin_top;
            if (cross_align == LAYOUT_ALIGN_STRETCH) items[i].frame.h = inner_cross - items[i].margin_top - items[i].margin_bottom;
            cursor_main += items[i].frame.w + items[i].margin_left + items[i].margin_right + between;
        } else {
            items[i].frame.y = cursor_main + items[i].margin_top;
            items[i].frame.x = inner_x + cross_pos + items[i].margin_left;
            if (cross_align == LAYOUT_ALIGN_STRETCH) items[i].frame.w = inner_cross - items[i].margin_left - items[i].margin_right;
            cursor_main += items[i].frame.h + items[i].margin_top + items[i].margin_bottom + between;
        }
        items[i].content.x = items[i].frame.x + items[i].pad_left;
        items[i].content.y = items[i].frame.y + items[i].pad_top;
        items[i].content.w = items[i].frame.w - items[i].pad_left - items[i].pad_right;
        items[i].content.h = items[i].frame.h - items[i].pad_top - items[i].pad_bottom;
    }
}
