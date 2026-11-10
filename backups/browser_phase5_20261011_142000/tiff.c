#include "tiff.h"
#include "heap.h"
#include "serial.h"

extern void *memcpy(void *dest, const void *src, u32 n);
extern void *memset(void *s, int c, unsigned int n);
extern int stbi_zlib_decode_buffer(char *obuffer, int olen, char const *ibuffer, int ilen);

static inline u16 tiff_read_u16(const u8 *p, int is_le) {
    if (is_le) return (u16)(p[0] | (p[1] << 8));
    else       return (u16)((p[0] << 8) | p[1]);
}

static inline u32 tiff_read_u32(const u8 *p, int is_le) {
    if (is_le) return (u32)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
    else       return (u32)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

int tiff_check_magic(const u8 *buf, u32 size) {
    if (!buf || size < 8) return 0;
    if (buf[0] == 'I' && buf[1] == 'I' && buf[2] == 0x2A && buf[3] == 0x00) return 1; /* Little Endian */
    if (buf[0] == 'M' && buf[1] == 'M' && buf[2] == 0x00 && buf[3] == 0x2A) return 1; /* Big Endian */
    return 0;
}

/* Decompress PackBits RLE */
static int tiff_decompress_packbits(const u8 *src, u32 src_len, u8 *dst, u32 dst_len) {
    u32 s = 0, d = 0;
    while (s < src_len && d < dst_len) {
        signed char n = (signed char)src[s++];
        if (n >= 0) {
            int count = (int)n + 1;
            while (count-- > 0 && s < src_len && d < dst_len) {
                dst[d++] = src[s++];
            }
        } else if (n != -128) {
            int count = -((int)n) + 1;
            if (s < src_len) {
                u8 val = src[s++];
                while (count-- > 0 && d < dst_len) {
                    dst[d++] = val;
                }
            }
        }
    }
    return (int)d;
}

/* Decompress TIFF LZW */
typedef struct {
    int prev;
    u8  first_char;
    u8  last_char;
    int len;
} tiff_lzw_entry_t;

static int tiff_decompress_lzw(const u8 *src, u32 src_len, u8 *dst, u32 dst_len) {
    tiff_lzw_entry_t *table = (tiff_lzw_entry_t*)kmalloc(4096 * sizeof(tiff_lzw_entry_t));
    if (!table) return 0;

    for (int i = 0; i < 256; i++) {
        table[i].prev = -1;
        table[i].first_char = (u8)i;
        table[i].last_char = (u8)i;
        table[i].len = 1;
    }

    u32 bit_buf = 0;
    int bits_in_buf = 0;
    u32 src_pos = 0;
    u32 dst_pos = 0;

    int code_size = 9;
    int next_code = 258;
    int old_code = -1;

    #define GET_NEXT_CODE() ({ \
        while (bits_in_buf < code_size && src_pos < src_len) { \
            bit_buf = (bit_buf << 8) | src[src_pos++]; \
            bits_in_buf += 8; \
        } \
        int code = (bit_buf >> (bits_in_buf - code_size)) & ((1 << code_size) - 1); \
        bits_in_buf -= code_size; \
        code; \
    })

    while (src_pos < src_len || bits_in_buf >= code_size) {
        int code = GET_NEXT_CODE();
        if (code == 257) break; /* End of Information */
        if (code == 256) {     /* Clear Code */
            code_size = 9;
            next_code = 258;
            old_code = -1;
            continue;
        }

        if (old_code == -1) {
            if (code < 256 && dst_pos < dst_len) {
                dst[dst_pos++] = (u8)code;
                old_code = code;
            }
            continue;
        }

        int cur = code;
        int write_len = 0;
        u8 temp_stack[4096];

        if (code < next_code) {
            while (cur >= 0 && cur < 4096 && write_len < 4096) {
                temp_stack[write_len++] = table[cur].last_char;
                cur = table[cur].prev;
            }
        } else if (code == next_code) {
            temp_stack[write_len++] = table[old_code].first_char;
            cur = old_code;
            while (cur >= 0 && cur < 4096 && write_len < 4096) {
                temp_stack[write_len++] = table[cur].last_char;
                cur = table[cur].prev;
            }
        } else {
            break; /* Corrupt LZW stream */
        }

        while (write_len > 0 && dst_pos < dst_len) {
            dst[dst_pos++] = temp_stack[--write_len];
        }

        if (next_code < 4096) {
            table[next_code].prev = old_code;
            table[next_code].first_char = table[old_code].first_char;
            table[next_code].last_char = (code < next_code) ? table[code].first_char : table[old_code].first_char;
            table[next_code].len = table[old_code].len + 1;
            next_code++;

            if (next_code == 511)  code_size = 10;
            else if (next_code == 1023) code_size = 11;
            else if (next_code == 2047) code_size = 12;
        }

        old_code = code;
    }

    #undef GET_NEXT_CODE
    kfree(table);
    return (int)dst_pos;
}

/* Horizontal Differencing Predictor */
static void tiff_apply_predictor(u8 *data, int width, int height, int spp) {
    if (spp <= 0 || width <= 0 || height <= 0) return;
    int row_bytes = width * spp;
    for (int y = 0; y < height; y++) {
        u8 *row = data + y * row_bytes;
        for (int x = spp; x < row_bytes; x++) {
            row[x] = (u8)(row[x] + row[x - spp]);
        }
    }
}

u8 *decode_tiff_format(const u8 *buf, u32 size, int *out_w, int *out_h) {
    if (!tiff_check_magic(buf, size)) return 0;

    int is_le = (buf[0] == 'I');
    u32 ifd_offset = tiff_read_u32(buf + 4, is_le);
    if (ifd_offset + 2 >= size) return 0;

    u16 num_tags = tiff_read_u16(buf + ifd_offset, is_le);
    u32 tag_base = ifd_offset + 2;

    u32 width = 0, height = 0;
    u32 bits_per_sample = 8;
    u32 compression = 1;       /* 1=none, 5=LZW, 8/32946=Deflate, 32773=PackBits */
    u32 photometric = 2;       /* 0=white0, 1=black0, 2=RGB, 3=palette */
    u32 samples_per_pixel = 1; /* default 1 for mono/palette, 3 for RGB */
    u32 rows_per_strip = 0xFFFFFFFFu;
    u32 predictor = 1;         /* 1=none, 2=horizontal */

    /* Strip arrays */
    u32 num_strips = 0;
    u32 *strip_offsets = 0;
    u32 *strip_byte_counts = 0;

    /* Color map for palette */
    u16 *colormap = 0;

    for (u16 t = 0; t < num_tags; t++) {
        u32 entry = tag_base + t * 12;
        if (entry + 12 > size) break;

        u16 tag   = tiff_read_u16(buf + entry, is_le);
        u16 type  = tiff_read_u16(buf + entry + 2, is_le);
        u32 count = tiff_read_u32(buf + entry + 4, is_le);
        u32 val_offset = entry + 8;
        if ((type == 3 && count > 2) || (type == 4 && count > 1) || (type == 1 && count > 4)) {
            val_offset = tiff_read_u32(buf + entry + 8, is_le);
        }

        #define READ_VAL() (type == 3 ? (u32)tiff_read_u16(buf + val_offset, is_le) : tiff_read_u32(buf + val_offset, is_le))

        if (tag == 256) width = READ_VAL();
        else if (tag == 257) height = READ_VAL();
        else if (tag == 258) {
            bits_per_sample = (type == 3) ? tiff_read_u16(buf + val_offset, is_le) : 8;
        }
        else if (tag == 259) compression = READ_VAL();
        else if (tag == 262) photometric = READ_VAL();
        else if (tag == 273) { /* StripOffsets */
            num_strips = count;
            if (num_strips > 0 && num_strips < 65536) {
                strip_offsets = (u32*)kmalloc(num_strips * sizeof(u32));
                if (strip_offsets) {
                    for (u32 s = 0; s < num_strips; s++) {
                        u32 off_ptr = (count == 1 && type == 4) ? (entry + 8) : (val_offset + s * (type == 3 ? 2 : 4));
                        strip_offsets[s] = (type == 3) ? tiff_read_u16(buf + off_ptr, is_le) : tiff_read_u32(buf + off_ptr, is_le);
                    }
                }
            }
        }
        else if (tag == 277) samples_per_pixel = READ_VAL();
        else if (tag == 278) rows_per_strip = READ_VAL();
        else if (tag == 279) { /* StripByteCounts */
            if (count > 0 && count < 65536) {
                strip_byte_counts = (u32*)kmalloc(count * sizeof(u32));
                if (strip_byte_counts) {
                    for (u32 s = 0; s < count; s++) {
                        u32 off_ptr = (count == 1 && type == 4) ? (entry + 8) : (val_offset + s * (type == 3 ? 2 : 4));
                        strip_byte_counts[s] = (type == 3) ? tiff_read_u16(buf + off_ptr, is_le) : tiff_read_u32(buf + off_ptr, is_le);
                    }
                }
            }
        }
        else if (tag == 317) predictor = READ_VAL();
        else if (tag == 320) { /* ColorMap */
            if (val_offset + count * 2 <= size) {
                colormap = (u16*)(buf + val_offset);
            }
        }
        #undef READ_VAL
    }

    if (width == 0 || height == 0 || !strip_offsets || !strip_byte_counts) {
        if (strip_offsets) kfree(strip_offsets);
        if (strip_byte_counts) kfree(strip_byte_counts);
        return 0;
    }

    if (rows_per_strip == 0 || rows_per_strip > height) rows_per_strip = height;

    /* Allocate intermediate decoded raster buffer */
    u32 bytes_per_pixel = samples_per_pixel;
    if (bytes_per_pixel == 0) bytes_per_pixel = 1;
    u32 row_bytes = width * bytes_per_pixel;
    u32 raw_total = row_bytes * height;
    u8 *raw_data = (u8*)kmalloc(raw_total + 1024);
    if (!raw_data) {
        kfree(strip_offsets);
        kfree(strip_byte_counts);
        return 0;
    }

    /* Decompress each strip into raw_data */
    u32 current_row = 0;
    for (u32 s = 0; s < num_strips && current_row < height; s++) {
        u32 strip_off = strip_offsets[s];
        u32 strip_len = strip_byte_counts[s];
        if (strip_off + strip_len > size) strip_len = size > strip_off ? (size - strip_off) : 0;

        u32 strip_rows = (current_row + rows_per_strip <= height) ? rows_per_strip : (height - current_row);
        u32 expected_dst = strip_rows * row_bytes;
        u8 *dst_ptr = raw_data + current_row * row_bytes;

        if (compression == 1) { /* Uncompressed */
            u32 copy_len = strip_len < expected_dst ? strip_len : expected_dst;
            if (strip_off < size) memcpy(dst_ptr, buf + strip_off, copy_len);
        } else if (compression == 32773) { /* PackBits */
            tiff_decompress_packbits(buf + strip_off, strip_len, dst_ptr, expected_dst);
        } else if (compression == 8 || compression == 32946) { /* Deflate / zlib */
            stbi_zlib_decode_buffer((char*)dst_ptr, expected_dst, (const char*)(buf + strip_off), strip_len);
        } else if (compression == 5) { /* LZW */
            tiff_decompress_lzw(buf + strip_off, strip_len, dst_ptr, expected_dst);
        } else {
            /* Fallback uncompressed */
            u32 copy_len = strip_len < expected_dst ? strip_len : expected_dst;
            if (strip_off < size) memcpy(dst_ptr, buf + strip_off, copy_len);
        }

        current_row += strip_rows;
    }

    kfree(strip_offsets);
    kfree(strip_byte_counts);

    /* Apply horizontal differencing predictor if specified */
    if (predictor == 2) {
        tiff_apply_predictor(raw_data, (int)width, (int)height, (int)samples_per_pixel);
    }

    /* Convert raw decoded raster into standard 32-bit RGBA pixels */
    u8 *out_pixels = (u8*)kmalloc(width * height * 4);
    if (!out_pixels) {
        kfree(raw_data);
        return 0;
    }

    u32 total_px = width * height;
    if (samples_per_pixel >= 3) {
        /* RGB or RGBA */
        for (u32 i = 0; i < total_px; i++) {
            u8 *src_p = raw_data + i * samples_per_pixel;
            u8 *dst_p = out_pixels + i * 4;
            dst_p[0] = src_p[0]; /* R */
            dst_p[1] = src_p[1]; /* G */
            dst_p[2] = src_p[2]; /* B */
            dst_p[3] = (samples_per_pixel >= 4) ? src_p[3] : 0xFF; /* A */
        }
    } else if (photometric == 3 && colormap) {
        /* Palette / Indexed Color */
        for (u32 i = 0; i < total_px; i++) {
            u8 idx = raw_data[i];
            u8 *dst_p = out_pixels + i * 4;
            u16 r16 = tiff_read_u16((u8*)&colormap[idx], is_le);
            u16 g16 = tiff_read_u16((u8*)&colormap[256 + idx], is_le);
            u16 b16 = tiff_read_u16((u8*)&colormap[512 + idx], is_le);
            dst_p[0] = (u8)(r16 >> 8);
            dst_p[1] = (u8)(g16 >> 8);
            dst_p[2] = (u8)(b16 >> 8);
            dst_p[3] = 0xFF;
        }
    } else {
        /* Grayscale */
        for (u32 i = 0; i < total_px; i++) {
            u8 val = raw_data[i];
            if (photometric == 0) val = 255 - val; /* WhiteIsZero */
            u8 *dst_p = out_pixels + i * 4;
            dst_p[0] = val;
            dst_p[1] = val;
            dst_p[2] = val;
            dst_p[3] = 0xFF;
        }
    }

    kfree(raw_data);

    *out_w = (int)width;
    *out_h = (int)height;
    return out_pixels;
}
