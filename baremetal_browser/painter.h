#ifndef PAINTER_H
#define PAINTER_H

#include "bb_types.h"
#include "bb_layout.h"
#include "bb_rasterizer.h"
#include "bb_image.h"

/* Core Paint Pipeline Entry Point */
void painter_paint_tree(bb_layout_box_t *root, bb_raster_t *r, int32_t offset_x, int32_t offset_y);

/* Phase-specific painter helpers */
void painter_paint_background(const bb_layout_box_t *box, bb_raster_t *r, int32_t offset_x, int32_t offset_y);
void painter_paint_border    (const bb_layout_box_t *box, bb_raster_t *r, int32_t offset_x, int32_t offset_y);
void painter_paint_text      (const bb_layout_box_t *box, bb_raster_t *r, int32_t offset_x, int32_t offset_y);
void painter_paint_replaced  (const bb_layout_box_t *box, bb_raster_t *r, int32_t offset_x, int32_t offset_y);

/* Overflow clipping helper */
bool painter_push_overflow_clip(const bb_layout_box_t *box, bb_raster_t *r, int32_t offset_x, int32_t offset_y);
void painter_pop_overflow_clip (bb_raster_t *r);

#endif /* PAINTER_H */
