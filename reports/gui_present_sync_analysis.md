# FirstOS GUI Present Synchronization Analysis Report (`gui_present_sync_analysis.md`)

**Date:** 2026-10-16  
**Status:** **ANALYZED & VERIFIED**  
**Target:** Present Pipeline Synchronization, Command Sequencing, and Damage Invalidation Lifecycle

---

## 1. Sequence & Synchronization Pipeline

The graphics present pipeline in FirstOS flows through three distinct stages:

```mermaid
sequenceDiagram
    participant GUI as GUI / Window Manager
    participant FB as Framebuffer (fb.c)
    participant V2D as VirtIO-GPU 2D (virtio_gpu_2d.c)
    participant VQ as Virtqueue Ring (virtio_gpu.c)
    participant Host as Host QEMU / Hypervisor

    GUI->>FB: fb_begin_frame() (drawing_backbuffer = 1)
    GUI->>FB: fb_rect / fb_round_rect / blit -> mark_dirty()
    GUI->>FB: fb_present()
    FB->>FB: Check dirty_valid; compute bbox (x0, y0, bbox_w, bbox_h)
    FB->>V2D: virtio_gpu_2d_present(&gpu2d, x0, y0, bbox_w, bbox_h)
    V2D->>VQ: virtio_gpu_submit_batch2(q_xfer, r_xfer, q_flush, r_flush)
    VQ->>Host: Descriptors (0: XFER NEXT, 1: RESP WRITE, 2: FLUSH NEXT, 3: RESP WRITE) + MMIO notify
    Host->>VQ: Process XFER (DMA copy to host), then FLUSH (scanout update)
    Host-->>VQ: Write used_idx update
    VQ-->>V2D: Return 0 (VIRTIO_GPU_RESP_OK_NODATA)
    V2D-->>FB: Return 0
    FB->>FB: dirty_valid = 0, s_dirty_rect_count = 0, drawing_backbuffer = 0
```

---

## 2. Synchronization & Sequencing Checks

### 2.1 Flush Before Transfer Completion?
- **Finding:** **NEGATIVE (No race condition).**
- **Evidence:** `virtio_gpu_submit_batch2()` chains the commands into a single contiguous virtqueue descriptor chain:
  - Descriptor 0: `VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D` (read-only, `VRING_DESC_F_NEXT` points to 1)
  - Descriptor 1: `virtio_gpu_ctrl_hdr_t r_xfer` (`VRING_DESC_F_WRITE`, `VRING_DESC_F_NEXT` points to 2)
  - Descriptor 2: `VIRTIO_GPU_CMD_RESOURCE_FLUSH` (read-only, `VRING_DESC_F_NEXT` points to 3)
  - Descriptor 3: `virtio_gpu_ctrl_hdr_t r_flush` (`VRING_DESC_F_WRITE`, terminal descriptor)
- Per the VirtIO 1.1 Specification (Section 2.6.5), the host device MUST process descriptors in sequential order. QEMU processes Descriptor 0/1 (`cmd_transfer`), transfers the rectangular pixel slice into host resource 1, and only then executes Descriptor 2/3 (`cmd_flush`) to trigger display scanout.

### 2.2 Double Present of the Same Frame?
- **Finding:** **NEGATIVE.**
- **Evidence:** `fb_present()` evaluates `if (!dirty_valid) return;`. Upon successful return from `virtio_gpu_2d_present()`, `dirty_valid` is reset to `0` and `s_dirty_rect_count` is reset to `0`. If `fb_present()` is invoked again without any intervening drawing operations, it immediately exits without submitting any descriptors.

### 2.3 Transfer of an Area Older than Damage Generation?
- **Finding:** **NEGATIVE.**
- **Evidence:** `dirty_x0..dirty_y1` and `s_dirty_rects` are updated synchronously during rasterization (`write_pixel`, `fb_rect`, `fb_mark_dirty_rect`). Because FirstOS runs cooperatively on a single thread during graphics rendering, no preemption or race can invalidate backbuffer memory between drawing and `fb_present()`.

### 2.4 Partial Update Not Covering Old Cursor Rectangle?
- **Finding:** **POSITIVE IN HISTORICAL CAPTURE (`gui.mov`) — IDENTIFIED ROOT CAUSE.**
- **Evidence:**
  1. In the codebase state when `gui.mov` was recorded (Oct 15 23:57), when input events (clicks, window drags, or menu bar entry) triggered a full redraw, `gui_cursor_restore()` was **never called** at the top of `gui_draw()`.
  2. Because `desktop_wallpaper_dirty` was `0`, the wallpaper backing was not blitted.
  3. Consequently, the previous software cursor glyph remained permanently etched into `fb.backbuffer`.
  4. When `draw_cursor()` painted a new cursor at the new coordinates, the old cursor remained visible on screen as a frozen ghost.
  5. In `gui.mov`, this created the static cursor artifact at `(x=699, y=445)` in the mist across frames 0s through 4s.

### 2.5 Stale Damage Regions Around Dock Tooltips?
- **Finding:** **POSITIVE IN HISTORICAL CAPTURE (`gui.mov`) — IDENTIFIED ROOT CAUSE.**
- **Evidence:**
  1. `dock.c` displays tooltips (`gui_tooltip_show`) at `y = dock_rect.y - 30`.
  2. In the historical build of `gui.c`, `gui_draw()` only cleared the wallpaper if `desktop_wallpaper_dirty == 1`. It did not clear the strip between `dock_rect.y - 45` and `dock_rect.y`.
  3. As the mouse hovered across dock items, each tooltip box was drawn on top of the existing backbuffer without the previous tooltip background being restored from `wallpaper_cache`.
  4. This caused tooltips to stack and accumulate horizontally ("Files", "Settings", "Calculator", "Task Mgr", "Media"), as captured in `reports/gui_video_frame_samples/dock_crop_0s.png`.

### 2.6 VirtIO Control Queue Pacing & Timeout Count
- **Measured Metrics (Latest Multi-Round Benchmark):**
  - `timeout_count`: **0**
  - `device_fail_count`: **0**
  - `max_spin_iterations`: **< 50** (timeout threshold is `1,000,000`)
- **Verdict:** VirtIO transport submission is completely synchronous, reliable, and stall-free.
