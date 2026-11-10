# FirstOS GUI & VirtIO-GPU Safe Improvements & v3/Virgl Readiness (`gui_virtio_gpu_safe_improvements.md`)

**Date:** 2026-10-16  
**Status:** Implemented & Verified against `make test-gfx-gate`

---

## 1. Safe High-Value Improvements Applied Outside the Frozen Hot Path

### A. Live Graphics & Input Diagnostics Panel (`draw_fps_dropdown` in `gui.c`)
- **Before**: Clicking the `FPS: XX` badge in the top bar opened a static dropdown showing hardcoded `"16.6 ms (60Hz)"` and `"Double-Buffered"`.
- **After**: Upgraded `draw_fps_dropdown()` into a live **GFX & INPUT DIAGNOSTICS** panel backed by `compositor_get_stats()`, `virtio_gpu_get_stats()`, and `ps2_get_queue_depth()`:
  1. **Framerate**: Live FPS badge (`FPS: XX`).
  2. **Presents (F/P/C)**: Live counters for `full_repaints` / `partial_repaints` / `cursor_only_presents`.
  3. **VirtIO Batch/Ntfy**: Live counters for `virtio_batched` vs total `mmio_notify_count`.
  4. **Last Damage Rect**: Live dimensions (`<w>x<h>`) of the most recent dirty region presented.
  5. **Input Queue/Drop**: Real-time PS/2 keyboard FIFO queue depth (`q=...`) and dropped scancode count (`d=...`).
  6. **Scanout / Driver**: Active scanout resolution and driver backend (`1024x768 vGPU` or `VGA`).
  7. **Footer**: Live VirtIO-GPU timeout counter (`Timeouts: 0 | SW Cursor Active`).

### B. Theme Ownership Unification (`theme.c` ↔ `desktop.c` ↔ `gui.c`)
- **Before**: `settings.c` called `desktop_apply_theme()` in `desktop.c`, which updated only `desktop.c`'s static variables (`desktop_bg`, `desktop_panel`, `desktop_bar.color`) without updating `gui.c`'s cached theme variables (`apply_theme()`), while `gui.c` read `active_theme` directly instead of `theme_get_colors()`.
- **After**:
  - `gui.c`'s `apply_theme()` now reads from `const theme_colors_t *tc = theme_get_colors();`.
  - `desktop.c`'s `desktop_apply_theme()` now calls `theme_apply()`, updates `desktop.c` via `theme_get_colors()`, and calls `apply_theme()` to keep `gui.c` synchronized.

### C. 64-Bit MMIO Pointer Safety in `virtio_input.c`
- **Before**: `virtio_input.c` stored PCI BAR MMIO addresses in `u32` variables (`mmio_base`, `notify_base`, `isr_base`, `device_cfg_base`) and cast MMIO register pointers via `(u32)(unsigned long)`, which risks truncating 64-bit PCI BAR addresses above 4 GiB.
- **After**: Replaced `u32` MMIO address types and casts with 64-bit safe `uintptr_t` and `pci_read_bar64()`.

### D. X11 Display Server Ring Buffer & Request Telemetry (`display_server.c` / `display_server.h`)
- Added `ring_overflow_drops` and `requests_processed` counters to `display_server_state_t` so X11 event ring overflows and processed protocol opcodes are observable for diagnostics.

---

## 2. P3: Prepare-for-v3 & Future Virgl / 3D Readiness Notes

### 2.1 Current VirtIO-GPU 2D Architecture (Baseline v1/v2)
- **Device ID**: `0x1AF4:0x1050` (`virtio-gpu-pci` modern transport).
- **Negotiated Features**: `VIRTIO_F_VERSION_1` (`driver_feature_select = 1`, `driver_feature = 1`).
- **Control Queue (`queue 0`)**: 8 descriptors (`VIRTQ_NUM = 8`), synchronous poll-until-used with `virtio_gpu_submit_batch2` combining `VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D` + `VIRTIO_GPU_CMD_RESOURCE_FLUSH` into a single MMIO notify when `rect_x == 0 && rect_w == fb.width`.
- **Single-Cursor Policy**: `virtio_gpu_has_hardware_cursor()` returns `0` to prevent duplicate host+guest cursors in QEMU/Cocoa; cursor is rendered via bounded save-under (`gui_cursor_save` / `gui_cursor_restore`) in `gui.c`.

### 2.2 Roadmap for Graphics Performance v3 (2D Multi-Rect & Async Ring)
When the hot path freeze is lifted in v3:
1. **Multi-Rect Dirty List Submission (`COMP_MAX_DAMAGE`)**:
   - Currently, `fb_mark_dirty_rect` merges dirty regions into a single bounding box (`fb.dirty_x1..y2`). If two small distant regions change simultaneously (e.g., top-right clock second tick + bottom-left cursor move), their bounding box covers the entire screen.
   - In v3, `framebuffer.c` can maintain up to `4` disjoint dirty rectangles (splitting wide bounding boxes when area savings exceed 50%) and submit them in a single `VIRTQ_NUM = 16` descriptor batch.
2. **Row-Strided Staging Buffer Copy for Narrow Dirty Rects**:
   - Currently, `virtio_gpu_2d_present` uses `virtio_gpu_submit_batch2` when `rect_x == 0 && rect_w == fb.width`. By packing narrow dirty rects (`rect_w < fb.width`) into `staging_fb` or using `RESOURCE_ATTACH_BACKING` scatter-gather entries, narrow rects can also use `virtio_gpu_submit_batch2` (1 MMIO notify instead of `height` row transfers).

### 2.3 Future Virgl / VirtIO-GPU 3D Readiness (`VIRTIO_GPU_F_VIRGL`)
To support hardware-accelerated 3D / Virgl contexts in a future milestone:
1. **Feature Bit Negotiation (`virtio_gpu_reset_and_negotiate`)**:
   - Check `device_feature_select = 0` for `VIRTIO_GPU_F_VIRGL (bit 0)` and `VIRTIO_GPU_F_EDID (bit 1)`.
   - Expose `int virtio_gpu_virgl_supported(void)` in `virtio_gpu.h`.
2. **3D Control Commands (`virtio_gpu_protocol.h`)**:
   - Add `VIRTIO_GPU_CMD_CTX_CREATE (0x0200)`, `VIRTIO_GPU_CMD_CTX_DESTROY (0x0201)`, `VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE (0x0202)`, `VIRTIO_GPU_CMD_SUBMIT_3D (0x0207)`, and `VIRTIO_GPU_CMD_RESOURCE_CREATE_3D (0x0204)`.
3. **Larger Control Queue & Fence Support**:
   - Increase `VIRTQ_NUM` from `8` to `64` and increase `GPU_REQ_BYTES` from `1024` to `65536` (16 pages) to accommodate `VIRTIO_GPU_CMD_SUBMIT_3D` command streams.
   - Enable `VIRTIO_GPU_FLAG_FENCE (1 << 0)` in `virtio_gpu_ctrl_hdr_t.flags` for asynchronous command retirement without blocking the CPU on `used_idx`.
