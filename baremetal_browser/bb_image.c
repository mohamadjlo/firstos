#include "bb_image.h"
#include "bb_libc.h"

#define MAX_DECODERS 8

static const bb_image_decoder_t *s_decoders[MAX_DECODERS];
static int s_decoder_count = 0;

/* =========================================================================
 * Built-in BMP Decoder (24-bit and 32-bit uncompressed)
 * ========================================================================= */

static bool bmp_probe(const uint8_t *data, size_t len) {
    if (!data || len < 54) return false;
    return (data[0] == 'B' && data[1] == 'M');
}

static bool bmp_decode(const uint8_t *data, size_t len, bb_image_t *out_img) {
    if (!bmp_probe(data, len) || !out_img) return false;

    /* Little-endian 16/32 bit read helpers */
    uint32_t data_offset = (uint32_t)data[10] | ((uint32_t)data[11] << 8) |
                           ((uint32_t)data[12] << 16) | ((uint32_t)data[13] << 24);
    int32_t width        = (int32_t)((uint32_t)data[18] | ((uint32_t)data[19] << 8) |
                           ((uint32_t)data[20] << 16) | ((uint32_t)data[21] << 24));
    int32_t height       = (int32_t)((uint32_t)data[22] | ((uint32_t)data[23] << 8) |
                           ((uint32_t)data[24] << 16) | ((uint32_t)data[25] << 24));
    uint16_t bpp         = (uint16_t)((uint32_t)data[28] | ((uint32_t)data[29] << 8));
    uint32_t compression = (uint32_t)data[30] | ((uint32_t)data[31] << 8) |
                           ((uint32_t)data[32] << 16) | ((uint32_t)data[33] << 24);

    if (width <= 0 || width > 4096 || height == 0 || (height > 4096 || height < -4096)) {
        return false;
    }
    if (compression != 0) return false; /* Only uncompressed RGB supported */
    if (bpp != 24 && bpp != 32) return false;

    bool top_down = (height < 0);
    int32_t abs_h = top_down ? -height : height;

    uint32_t *pixels = (uint32_t *)bb_malloc((size_t)width * (size_t)abs_h * sizeof(uint32_t));
    if (!pixels) return false;

    size_t row_stride = (size_t)(((bpp * width + 31) / 32) * 4);
    const uint8_t *pixel_data = data + data_offset;

    int32_t y;
    for (y = 0; y < abs_h; y++) {
        int32_t src_y = top_down ? y : (abs_h - 1 - y);
        if (data_offset + (size_t)src_y * row_stride > len) {
            bb_free(pixels);
            return false;
        }

        const uint8_t *src_row = pixel_data + src_y * row_stride;
        uint32_t *dst_row = &pixels[y * width];

        int32_t x;
        if (bpp == 24) {
            for (x = 0; x < width; x++) {
                uint8_t b = src_row[x * 3 + 0];
                uint8_t g = src_row[x * 3 + 1];
                uint8_t r = src_row[x * 3 + 2];
                dst_row[x] = BB_ARGB(255, r, g, b);
            }
        } else if (bpp == 32) {
            for (x = 0; x < width; x++) {
                uint8_t b = src_row[x * 4 + 0];
                uint8_t g = src_row[x * 4 + 1];
                uint8_t r = src_row[x * 4 + 2];
                uint8_t a = src_row[x * 4 + 3];
                if (a == 0) a = 255; /* Handle standard 32bpp RGB without alpha channel */
                dst_row[x] = BB_ARGB(a, r, g, b);
            }
        }
    }

    bb_image_set_loaded(out_img, width, abs_h, pixels);
    return true;
}

static const bb_image_decoder_t s_bmp_decoder = {
    .name = "bmp",
    .probe = bmp_probe,
    .decode = bmp_decode
};

/* =========================================================================
 * Decoder Registry
 * ========================================================================= */

static void init_default_decoders_if_needed(void) {
    if (s_decoder_count == 0) {
        s_decoders[s_decoder_count++] = &s_bmp_decoder;
    }
}

bool bb_image_register_decoder(const bb_image_decoder_t *decoder) {
    if (!decoder || s_decoder_count >= MAX_DECODERS) return false;
    init_default_decoders_if_needed();
    s_decoders[s_decoder_count++] = decoder;
    return true;
}

/* =========================================================================
 * Resource Lifecycle & Management
 * ========================================================================= */

bb_image_t *bb_image_create(const char *src, const char *alt) {
    bb_image_t *img = (bb_image_t *)bb_malloc(sizeof(bb_image_t));
    if (!img) return NULL;
    bb_memset(img, 0, sizeof(bb_image_t));

    img->src   = src ? bb_strdup(src) : NULL;
    img->alt   = alt ? bb_strdup(alt) : NULL;
    img->state = BB_IMAGE_STATE_UNLOADED;
    return img;
}

void bb_image_set_loaded(bb_image_t *img, int32_t width, int32_t height, uint32_t *pixels) {
    if (!img) return;
    if (img->pixels && img->pixels != pixels) {
        bb_free(img->pixels);
    }
    img->intrinsic_width  = width;
    img->intrinsic_height = height;
    img->pixels           = pixels;
    img->pitch            = (uint32_t)width;
    img->state            = BB_IMAGE_STATE_LOADED;
}

void bb_image_set_failed(bb_image_t *img) {
    if (!img) return;
    if (img->pixels) {
        bb_free(img->pixels);
        img->pixels = NULL;
    }
    img->intrinsic_width  = 0;
    img->intrinsic_height = 0;
    img->state            = BB_IMAGE_STATE_FAILED;
}

bool bb_image_decode_buffer(bb_image_t *img, const uint8_t *data, size_t len) {
    if (!img || !data || len == 0) {
        if (img) bb_image_set_failed(img);
        return false;
    }

    init_default_decoders_if_needed();
    img->state = BB_IMAGE_STATE_LOADING;

    for (int i = 0; i < s_decoder_count; i++) {
        const bb_image_decoder_t *dec = s_decoders[i];
        if (dec && dec->probe && dec->probe(data, len)) {
            if (dec->decode && dec->decode(data, len, img)) {
                return true;
            }
        }
    }

    bb_image_set_failed(img);
    return false;
}

void bb_image_free(bb_image_t *img) {
    if (!img) return;
    if (img->src) bb_free(img->src);
    if (img->alt) bb_free(img->alt);
    if (img->pixels) bb_free(img->pixels);
    bb_free(img);
}
