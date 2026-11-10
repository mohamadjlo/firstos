#ifndef TIFF_H
#define TIFF_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

/* Returns 1 if buffer starts with a valid TIFF magic header (II* or MM*), 0 otherwise */
int tiff_check_magic(const u8 *buf, u32 size);

/* Decodes a TIFF file from memory into 32-bit RGBA pixel array.
   Allocates memory using kmalloc. Returns NULL on failure.
   *out_w and *out_h receive image dimensions. */
u8 *decode_tiff_format(const u8 *buf, u32 size, int *out_w, int *out_h);

#endif /* TIFF_H */
