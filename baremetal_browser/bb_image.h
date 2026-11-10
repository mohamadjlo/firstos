#ifndef BB_IMAGE_H
#define BB_IMAGE_H

#include "bb_types.h"
#include "bb_rasterizer.h"

/* =========================================================================
 * Resource Lifecycle State
 * ========================================================================= */
typedef enum {
    BB_IMAGE_STATE_UNLOADED = 0,
    BB_IMAGE_STATE_LOADING,
    BB_IMAGE_STATE_LOADED,
    BB_IMAGE_STATE_FAILED
} bb_image_state_t;

/* =========================================================================
 * Image Resource
 * ========================================================================= */
typedef struct bb_image {
    char             *src;              /* Image source URL or path */
    char             *alt;              /* Fallback description */
    bb_image_state_t  state;            /* UNLOADED, LOADING, LOADED, FAILED */

    int32_t           intrinsic_width;  /* Decoded pixel width */
    int32_t           intrinsic_height; /* Decoded pixel height */

    uint32_t         *pixels;           /* 32-bpp ARGB buffer */
    uint32_t          pitch;            /* Pixels per row */
} bb_image_t;

/* =========================================================================
 * Extensible Image Decoder Interface
 * ========================================================================= */
typedef struct bb_image_decoder {
    const char *name;
    bool (*probe)(const uint8_t *data, size_t len);
    bool (*decode)(const uint8_t *data, size_t len, bb_image_t *out_img);
} bb_image_decoder_t;

/* =========================================================================
 * Lifecycle & Management API
 * ========================================================================= */
bb_image_t *bb_image_create(const char *src, const char *alt);
void        bb_image_set_loaded(bb_image_t *img, int32_t width, int32_t height, uint32_t *pixels);
void        bb_image_set_failed(bb_image_t *img);
bool        bb_image_decode_buffer(bb_image_t *img, const uint8_t *data, size_t len);
void        bb_image_free(bb_image_t *img);

/* Register custom image decoders (PNG, JPEG, etc.) */
bool        bb_image_register_decoder(const bb_image_decoder_t *decoder);

/* Fallback placeholder dimensions when an image is not loaded / failed */
#define BB_IMAGE_FALLBACK_WIDTH  200
#define BB_IMAGE_FALLBACK_HEIGHT 150

#endif /* BB_IMAGE_H */
