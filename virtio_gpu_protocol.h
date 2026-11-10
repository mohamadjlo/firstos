#ifndef VIRTIO_GPU_PROTOCOL_H
#define VIRTIO_GPU_PROTOCOL_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO 0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF 0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT 0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH 0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
#define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING 0x0107
#define VIRTIO_GPU_RESP_OK_NODATA 0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO 0x1101
#define VIRTIO_GPU_RESP_ERR_UNSPEC 0x1200
#define VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY 0x1201
#define VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID 0x1202
#define VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID 0x1203
#define VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER 0x1205
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM 2
#define VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM 67
#define VIRTIO_GPU_MAX_SCANOUTS 16

typedef struct __attribute__((packed)) { u32 x,y,width,height; } virtio_gpu_rect_t;
typedef struct __attribute__((packed)) { u32 type,flags; u64 fence_id; u32 ctx_id; u8 ring_idx; u8 padding[3]; } virtio_gpu_ctrl_hdr_t;
typedef struct __attribute__((packed)) { virtio_gpu_ctrl_hdr_t hdr; } virtio_gpu_get_display_info_t;
typedef struct __attribute__((packed)) { virtio_gpu_rect_t r; u32 enabled; u32 flags; } virtio_gpu_display_one_t;
typedef struct __attribute__((packed)) { virtio_gpu_ctrl_hdr_t hdr; virtio_gpu_display_one_t pmodes[VIRTIO_GPU_MAX_SCANOUTS]; } virtio_gpu_resp_display_info_t;
typedef struct __attribute__((packed)) { virtio_gpu_ctrl_hdr_t hdr; u32 resource_id,format,width,height; } virtio_gpu_resource_create_2d_t;
typedef struct __attribute__((packed)) { virtio_gpu_ctrl_hdr_t hdr; u32 resource_id,nr_entries; } virtio_gpu_resource_attach_backing_t;
typedef struct __attribute__((packed)) { u64 addr; u32 length,padding; } virtio_gpu_mem_entry_t;
typedef struct __attribute__((packed)) { virtio_gpu_ctrl_hdr_t hdr; virtio_gpu_rect_t r; u32 scanout_id,resource_id; } virtio_gpu_set_scanout_t;
typedef struct __attribute__((packed)) { virtio_gpu_ctrl_hdr_t hdr; virtio_gpu_rect_t r; u64 offset; u32 resource_id,padding; } virtio_gpu_transfer_to_host_2d_t;
typedef struct __attribute__((packed)) { virtio_gpu_ctrl_hdr_t hdr; virtio_gpu_rect_t r; u32 resource_id,padding; } virtio_gpu_resource_flush_t;
typedef struct __attribute__((packed)) { virtio_gpu_ctrl_hdr_t hdr; u32 resource_id; u32 padding; } virtio_gpu_resource_unref_t;
typedef struct __attribute__((packed)) { virtio_gpu_ctrl_hdr_t hdr; u32 resource_id; u32 padding; } virtio_gpu_resource_detach_backing_t;
#define VIRTIO_GPU_CMD_UPDATE_CURSOR 0x0300
#define VIRTIO_GPU_CMD_MOVE_CURSOR 0x0301

typedef struct __attribute__((packed)) { u32 scanout_id, x, y, padding; } virtio_gpu_cursor_pos_t;
typedef struct __attribute__((packed)) { virtio_gpu_ctrl_hdr_t hdr; virtio_gpu_cursor_pos_t pos; u32 resource_id, hot_x, hot_y, padding; } virtio_gpu_update_cursor_t;

#if defined(__GNUC__)
_Static_assert(sizeof(virtio_gpu_rect_t) == 16, "virtio_gpu_rect_t size mismatch");
_Static_assert(sizeof(virtio_gpu_ctrl_hdr_t) == 24, "virtio_gpu_ctrl_hdr_t size mismatch");
_Static_assert(__builtin_offsetof(virtio_gpu_ctrl_hdr_t, type) == 0, "virtio_gpu_ctrl_hdr_t.type offset mismatch");
_Static_assert(__builtin_offsetof(virtio_gpu_ctrl_hdr_t, flags) == 4, "virtio_gpu_ctrl_hdr_t.flags offset mismatch");
_Static_assert(__builtin_offsetof(virtio_gpu_ctrl_hdr_t, fence_id) == 8, "virtio_gpu_ctrl_hdr_t.fence_id offset mismatch");
_Static_assert(__builtin_offsetof(virtio_gpu_ctrl_hdr_t, ctx_id) == 16, "virtio_gpu_ctrl_hdr_t.ctx_id offset mismatch");
_Static_assert(__builtin_offsetof(virtio_gpu_ctrl_hdr_t, ring_idx) == 20, "virtio_gpu_ctrl_hdr_t.ring_idx offset mismatch");
_Static_assert(__builtin_offsetof(virtio_gpu_ctrl_hdr_t, padding) == 21, "virtio_gpu_ctrl_hdr_t.padding offset mismatch");
_Static_assert(sizeof(virtio_gpu_display_one_t) == 24, "virtio_gpu_display_one_t size mismatch");
_Static_assert(__builtin_offsetof(virtio_gpu_display_one_t, r) == 0, "virtio_gpu_display_one_t.r offset mismatch");
_Static_assert(__builtin_offsetof(virtio_gpu_display_one_t, enabled) == 16, "virtio_gpu_display_one_t.enabled offset mismatch");
_Static_assert(__builtin_offsetof(virtio_gpu_display_one_t, flags) == 20, "virtio_gpu_display_one_t.flags offset mismatch");
_Static_assert(sizeof(virtio_gpu_resp_display_info_t) == 408, "virtio_gpu_resp_display_info_t size mismatch");
_Static_assert(__builtin_offsetof(virtio_gpu_resp_display_info_t, hdr) == 0, "virtio_gpu_resp_display_info_t.hdr offset mismatch");
_Static_assert(__builtin_offsetof(virtio_gpu_resp_display_info_t, pmodes) == 24, "virtio_gpu_resp_display_info_t.pmodes offset mismatch");
_Static_assert(sizeof(virtio_gpu_cursor_pos_t) == 16, "virtio_gpu_cursor_pos_t size mismatch");
_Static_assert(sizeof(virtio_gpu_update_cursor_t) == 56, "virtio_gpu_update_cursor_t size mismatch");
#endif
#endif
