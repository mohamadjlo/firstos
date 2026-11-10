#include "virtio_gpu_2d.h"
#include "virtio_gpu.h"
#include "serial.h"

static void mem_zero(void *ptr, u32 len) {
    u8 *p = (u8*)ptr;
    while (len--) *p++ = 0;
}

static int submit2(void *req, u32 req_len, void *resp, u32 resp_len) {
    return virtio_gpu_submit2(req, req_len, resp, resp_len);
}

static int submit3(void *a, u32 al, void *b, u32 bl, void *r, u32 rl) {
    return virtio_gpu_submit3(a, al, b, bl, r, rl);
}

static int cmd_get_display_info(virtio_gpu_resp_display_info_t *out) {
    virtio_gpu_get_display_info_t q;
    int rc;
    if (!out) return -1;
    mem_zero(&q, sizeof(q));
    mem_zero(out, sizeof(*out));
    q.hdr.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
    rc = submit2(&q, sizeof(q), out, sizeof(*out));
    if (rc < 0) return -1;
    if (out->hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) return -2;
    if (!out->pmodes[0].enabled || !out->pmodes[0].r.width || !out->pmodes[0].r.height) return -3;
    return 0;
}

static int cmd_create(virtio_gpu_2d_t *g) {
    virtio_gpu_resource_create_2d_t q;
    virtio_gpu_ctrl_hdr_t r;
    int rc;
    mem_zero(&q, sizeof(q));
    mem_zero(&r, sizeof(r));
    q.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    q.resource_id = g->resource_id;
    q.format = g->format;
    q.width = g->width;
    q.height = g->height;
    rc = submit2(&q, sizeof(q), &r, sizeof(r));
    return rc < 0 || r.type != VIRTIO_GPU_RESP_OK_NODATA ? -1 : 0;
}

static int cmd_attach(virtio_gpu_2d_t *g) {
    virtio_gpu_resource_attach_backing_t q;
    virtio_gpu_mem_entry_t m;
    virtio_gpu_ctrl_hdr_t r;
    int rc;
    mem_zero(&q, sizeof(q));
    mem_zero(&m, sizeof(m));
    mem_zero(&r, sizeof(r));
    q.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    q.resource_id = g->resource_id;
    q.nr_entries = 1;
    m.addr = (u64)(unsigned long)g->pixels;
    m.length = g->bytes;
    rc = submit3(&q, sizeof(q), &m, sizeof(m), &r, sizeof(r));
    return rc < 0 || r.type != VIRTIO_GPU_RESP_OK_NODATA ? -1 : 0;
}

static int cmd_scanout(virtio_gpu_2d_t *g) {
    virtio_gpu_set_scanout_t q;
    virtio_gpu_ctrl_hdr_t r;
    int rc;
    mem_zero(&q, sizeof(q));
    mem_zero(&r, sizeof(r));
    q.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    q.r.width = g->width;
    q.r.height = g->height;
    q.scanout_id = 0;
    q.resource_id = g->resource_id;
    rc = submit2(&q, sizeof(q), &r, sizeof(r));
    return rc < 0 || r.type != VIRTIO_GPU_RESP_OK_NODATA ? -1 : 0;
}

static int cmd_transfer(virtio_gpu_2d_t *g, u32 x, u32 y, u32 w, u32 h) {
    virtio_gpu_transfer_to_host_2d_t q;
    virtio_gpu_ctrl_hdr_t r;
    int rc;
    if (!g || !g->pixels || !w || !h || x >= g->width || y >= g->height) return -1;
    if (x + w > g->width) w = g->width - x;
    if (y + h > g->height) h = g->height - y;
    mem_zero(&q, sizeof(q));
    mem_zero(&r, sizeof(r));
    q.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    q.r.x = x;
    q.r.y = y;
    q.r.width = w;
    q.r.height = h;
    q.offset = (u64)(y * g->stride + x * 4);
    q.resource_id = g->resource_id;
    rc = submit2(&q, sizeof(q), &r, sizeof(r));
    return rc < 0 || r.type != VIRTIO_GPU_RESP_OK_NODATA ? -1 : 0;
}

static int cmd_flush(virtio_gpu_2d_t *g, u32 x, u32 y, u32 w, u32 h) {
    virtio_gpu_resource_flush_t q;
    virtio_gpu_ctrl_hdr_t r;
    int rc;
    if (!g || !w || !h || x >= g->width || y >= g->height) return -1;
    if (x + w > g->width) w = g->width - x;
    if (y + h > g->height) h = g->height - y;
    mem_zero(&q, sizeof(q));
    mem_zero(&r, sizeof(r));
    q.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    q.r.x = x;
    q.r.y = y;
    q.r.width = w;
    q.r.height = h;
    q.resource_id = g->resource_id;
    rc = submit2(&q, sizeof(q), &r, sizeof(r));
    return rc < 0 || r.type != VIRTIO_GPU_RESP_OK_NODATA ? -1 : 0;
}

static int cmd_detach(virtio_gpu_2d_t *g) {
    virtio_gpu_resource_detach_backing_t q;
    virtio_gpu_ctrl_hdr_t r;
    int rc;
    mem_zero(&q, sizeof(q));
    mem_zero(&r, sizeof(r));
    q.hdr.type = VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING;
    q.resource_id = g->resource_id;
    rc = submit2(&q, sizeof(q), &r, sizeof(r));
    return rc < 0 || r.type != VIRTIO_GPU_RESP_OK_NODATA ? -1 : 0;
}

static int cmd_unref(virtio_gpu_2d_t *g) {
    virtio_gpu_resource_unref_t q;
    virtio_gpu_ctrl_hdr_t r;
    int rc;
    mem_zero(&q, sizeof(q));
    mem_zero(&r, sizeof(r));
    q.hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
    q.resource_id = g->resource_id;
    rc = submit2(&q, sizeof(q), &r, sizeof(r));
    return rc < 0 || r.type != VIRTIO_GPU_RESP_OK_NODATA ? -1 : 0;
}

int virtio_gpu_2d_init(virtio_gpu_2d_t *g, u32 w, u32 h) {
    virtio_gpu_resp_display_info_t info;
    const virtio_gpu_info_t *gpu;
    int rc;
    if (!g || !w || !h || !g->pixels) return -1;
    gpu = virtio_gpu_info();
    if (!gpu || !gpu->initialized) return -2;
    g->ready = 0;
    g->width = w;
    g->height = h;
    g->stride = w * 4;
    g->bytes = g->stride * h;
    g->format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
    g->resource_id = 1;
    rc = cmd_get_display_info(&info);
    if (rc < 0) return -3;
    // We intentionally ignore info.pmodes to force the requested w/h
    // QEMU will automatically resize the window to match our scanout resource.
    if (cmd_create(g) < 0) return -4;
    if (cmd_attach(g) < 0) return -5;
    if (cmd_scanout(g) < 0) return -6;
    g->ready = 1;
    serial_write("[virtio-gpu] 2D init ok\n");
    virtio_gpu_cursor_init();
    return 0;
}

int virtio_gpu_2d_set_resolution(virtio_gpu_2d_t *g, u32 w, u32 h) {
    if (!g || !w || !h || !g->pixels) return -1;
    if (g->width == w && g->height == h && g->ready) return 0;
    
    g->ready = 0;
    cmd_detach(g);
    cmd_unref(g);

    g->width = w;
    g->height = h;
    g->stride = w * 4;
    g->bytes = g->stride * h;
    g->format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
    g->resource_id = 1;

    if (cmd_create(g) < 0) return -4;
    if (cmd_attach(g) < 0) return -5;
    if (cmd_scanout(g) < 0) return -6;
    g->ready = 1;
    cmd_transfer(g, 0, 0, w, h);
    cmd_flush(g, 0, 0, w, h);
    serial_write("[virtio-gpu] 2D resolution updated ok\n");
    return 0;
}

int virtio_gpu_2d_present(virtio_gpu_2d_t *g, u32 x, u32 y, u32 w, u32 h) {
    virtio_gpu_transfer_to_host_2d_t q_xfer;
    virtio_gpu_resource_flush_t q_flush;
    virtio_gpu_ctrl_hdr_t r_xfer, r_flush;
    int rc;

    if (!g || !g->ready || !g->pixels || !w || !h || x >= g->width || y >= g->height) return -1;
    if (x + w > g->width) w = g->width - x;
    if (y + h > g->height) h = g->height - y;

    mem_zero(&q_xfer, sizeof(q_xfer));
    mem_zero(&r_xfer, sizeof(r_xfer));
    q_xfer.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    q_xfer.r.x = x;
    q_xfer.r.y = y;
    q_xfer.r.width = w;
    q_xfer.r.height = h;
    q_xfer.offset = (u64)(y * g->stride + x * 4);
    q_xfer.resource_id = g->resource_id;

    mem_zero(&q_flush, sizeof(q_flush));
    mem_zero(&r_flush, sizeof(r_flush));
    q_flush.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    q_flush.r.x = x;
    q_flush.r.y = y;
    q_flush.r.width = w;
    q_flush.r.height = h;
    q_flush.resource_id = g->resource_id;

    rc = virtio_gpu_submit_batch2(&q_xfer, sizeof(q_xfer), &r_xfer, sizeof(r_xfer),
                                  &q_flush, sizeof(q_flush), &r_flush, sizeof(r_flush));
    if (rc < 0 || r_xfer.type != VIRTIO_GPU_RESP_OK_NODATA || r_flush.type != VIRTIO_GPU_RESP_OK_NODATA) {
        return -2;
    }
    return 0;
}

int virtio_gpu_2d_transfer_rect(virtio_gpu_2d_t *g, u32 x, u32 y, u32 width, u32 height) {
    return cmd_transfer(g, x, y, width, height);
}

int virtio_gpu_2d_flush_rect(virtio_gpu_2d_t *g, u32 x, u32 y, u32 width, u32 height) {
    return cmd_flush(g, x, y, width, height);
}

void virtio_gpu_2d_shutdown(virtio_gpu_2d_t *g) {
    if (g) mem_zero(g, sizeof(*g));
}
