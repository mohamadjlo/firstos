#include "style.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

ComputedStyle *computed_style_create(void) {
    ComputedStyle *style = (ComputedStyle *)calloc(1, sizeof(ComputedStyle));
    if (!style) return NULL;

    style->display = DISPLAY_INLINE;
    style->width = 0.0f;
    style->height = 0.0f;
    style->width_auto = true;
    style->height_auto = true;

    style->margin = (Spacing){0, 0, 0, 0};
    style->padding = (Spacing){0, 0, 0, 0};

    style->color = (RGBA){0, 0, 0, 255};              /* Default text: black */
    style->background_color = (RGBA){0, 0, 0, 0};     /* Default bg: transparent */

    style->border_width = 0.0f;
    style->border_color = (RGBA){0, 0, 0, 0};

    style->font_size = 16.0f;
    return style;
}

void computed_style_free(ComputedStyle *style) {
    free(style);
}

void computed_style_apply_defaults(ComputedStyle *style, const char *tag_name) {
    if (!style || !tag_name) return;

    /* Hidden / metadata elements */
    if (strcasecmp(tag_name, "head") == 0 ||
        strcasecmp(tag_name, "style") == 0 ||
        strcasecmp(tag_name, "script") == 0 ||
        strcasecmp(tag_name, "meta") == 0 ||
        strcasecmp(tag_name, "link") == 0 ||
        strcasecmp(tag_name, "title") == 0) {
        style->display = DISPLAY_NONE;
        return;
    }

    /* Block elements */
    if (strcasecmp(tag_name, "html") == 0 ||
        strcasecmp(tag_name, "body") == 0 ||
        strcasecmp(tag_name, "div") == 0 ||
        strcasecmp(tag_name, "p") == 0 ||
        strcasecmp(tag_name, "h1") == 0 ||
        strcasecmp(tag_name, "h2") == 0 ||
        strcasecmp(tag_name, "h3") == 0 ||
        strcasecmp(tag_name, "h4") == 0 ||
        strcasecmp(tag_name, "h5") == 0 ||
        strcasecmp(tag_name, "h6") == 0 ||
        strcasecmp(tag_name, "ul") == 0 ||
        strcasecmp(tag_name, "ol") == 0 ||
        strcasecmp(tag_name, "li") == 0 ||
        strcasecmp(tag_name, "section") == 0 ||
        strcasecmp(tag_name, "header") == 0 ||
        strcasecmp(tag_name, "footer") == 0 ||
        strcasecmp(tag_name, "main") == 0 ||
        strcasecmp(tag_name, "article") == 0) {
        style->display = DISPLAY_BLOCK;
    }

    /* Tag-specific baseline styling */
    if (strcasecmp(tag_name, "body") == 0) {
        style->background_color = (RGBA){250, 250, 252, 255};
        style->color = (RGBA){30, 30, 35, 255};
        style->margin = (Spacing){12, 12, 12, 12};
    } else if (strcasecmp(tag_name, "h1") == 0) {
        style->font_size = 28.0f;
        style->margin = (Spacing){14, 0, 14, 0};
        style->color = (RGBA){20, 20, 25, 255};
    } else if (strcasecmp(tag_name, "h2") == 0) {
        style->font_size = 22.0f;
        style->margin = (Spacing){12, 0, 12, 0};
        style->color = (RGBA){30, 30, 35, 255};
    } else if (strcasecmp(tag_name, "h3") == 0) {
        style->font_size = 18.0f;
        style->margin = (Spacing){10, 0, 10, 0};
    } else if (strcasecmp(tag_name, "p") == 0) {
        style->font_size = 16.0f;
        style->margin = (Spacing){10, 0, 10, 0};
    } else if (strcasecmp(tag_name, "a") == 0) {
        style->color = (RGBA){0, 110, 230, 255}; /* Primary blue link color */
    } else if (strcasecmp(tag_name, "button") == 0) {
        style->display = DISPLAY_INLINE;
        style->background_color = (RGBA){0, 122, 255, 255};
        style->color = (RGBA){255, 255, 255, 255};
        style->padding = (Spacing){6, 14, 6, 14};
        style->margin = (Spacing){4, 4, 4, 4};
        style->border_width = 1.0f;
        style->border_color = (RGBA){0, 90, 200, 255};
    }
}

void computed_style_inherit_from_parent(ComputedStyle *child_style, const ComputedStyle *parent_style) {
    if (!child_style || !parent_style) return;

    /* Inherit font-size if child has default */
    if (child_style->font_size == 16.0f && parent_style->font_size != 16.0f) {
        child_style->font_size = parent_style->font_size;
    }

    /* Inherit text color if child is unstyled black and parent has color */
    if (child_style->color.r == 0 && child_style->color.g == 0 && child_style->color.b == 0 &&
        child_style->color.a == 255) {
        child_style->color = parent_style->color;
    }
}
