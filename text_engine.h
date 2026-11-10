#ifndef TEXT_ENGINE_H
#define TEXT_ENGINE_H

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(__KERNEL__)
#include <stdint.h>
#include <stddef.h>
#ifndef _FIRSTOS_U_TYPES_DEFINED
#define _FIRSTOS_U_TYPES_DEFINED
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#endif
#else
#include "io.h"
#include <stddef.h>
#endif

#define MAX_FONT_PROVIDERS 8
#define MAX_CLUSTER_COMBINING_MARKS 4

/* Font Glyph representation */
typedef struct font_glyph_s {
    u32 codepoint;
    int advance_x;
    int advance_y;
    int bbox_x;
    int bbox_y;
    int bbox_w;
    int bbox_h;
    const u8 *bitmap;      /* 1-bit or 8-bit mask */
    int bitmap_pitch;
    int is_combining_mark; /* Non-spacing combining mark */
    int is_placeholder;    /* Fallback indicator / missing glyph */
} font_glyph_t;

typedef struct font_provider_s font_provider_t;

/* Font Provider interface */
struct font_provider_s {
    const char *name;
    int base_width;
    int base_height;
    int ascent;
    int descent;

    int (*has_glyph)(font_provider_t *provider, u32 codepoint);
    int (*get_glyph)(font_provider_t *provider, u32 codepoint, font_glyph_t *out_glyph);
    void (*render_glyph)(font_provider_t *provider, const font_glyph_t *glyph,
                         int x, int y, u32 color, int scale, void *target_fb);
};

/* Font Fallback Chain */
typedef struct {
    font_provider_t *providers[MAX_FONT_PROVIDERS];
    int count;
} font_fallback_chain_t;

/* Grapheme Cluster representation with source byte mapping */
typedef struct {
    u32 base_codepoint;
    u32 marks[MAX_CLUSTER_COMBINING_MARKS];
    int mark_count;
    int start_byte_offset;
    int end_byte_offset;
    int advance_w;
    int is_whitespace;
    int is_newline;
    int is_rtl;
    int is_fallback;
} grapheme_cluster_t;

/* Text Cluster Iterator */
typedef struct {
    const char *text;
    int text_len;
    const char *current_ptr;
    const char *end_ptr;
    font_fallback_chain_t *chain;
    int font_scale;
} text_cluster_iter_t;

/* Global Built-in Font Providers */
extern font_provider_t g_font_provider_latin8x16;
extern font_provider_t g_font_provider_extended;
extern font_provider_t g_font_provider_arabic;
extern font_provider_t g_font_provider_emoji;
extern font_provider_t g_font_provider_notdef;

/* Default Browser Fallback Chain */
extern font_fallback_chain_t g_browser_default_font_chain;

/* UTF-8 and Character Classification */
u32  text_utf8_decode_next(const char **ptr, const char *end);
int  text_is_combining_mark(u32 cp);
int  text_is_rtl(u32 cp);
int  text_is_whitespace(u32 cp);

/* Font Fallback Chain Operations */
void font_fallback_chain_init(font_fallback_chain_t *chain);
int  font_fallback_chain_add(font_fallback_chain_t *chain, font_provider_t *provider);
font_provider_t *font_fallback_resolve(font_fallback_chain_t *chain, u32 codepoint, font_glyph_t *out_glyph);

/* Grapheme Cluster Iterator Operations */
void text_cluster_iter_init(text_cluster_iter_t *iter, const char *text, int text_len,
                            font_fallback_chain_t *chain, int font_scale);
int  text_cluster_iter_next(text_cluster_iter_t *iter, grapheme_cluster_t *out_cluster);

/* High-level Subsystem API */
void text_engine_init(void);
int  text_engine_measure(const char *text, font_fallback_chain_t *chain, int scale, int max_w,
                         int *out_ends_with_space, int *out_has_nbsp);
void text_engine_render(int x, int y, const char *text, u32 color, int scale,
                        font_fallback_chain_t *chain, void *target_fb);
int  text_engine_hit_test(const char *text, font_fallback_chain_t *chain, int scale,
                          int click_x, int *out_byte_offset);

#endif /* TEXT_ENGINE_H */
