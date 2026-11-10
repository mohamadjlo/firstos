# Step 2 Verification Report: VirtIO-GPU Hardware Cursor Implementation

## Executive Summary
In Step 2 of the graphics and VirtIO-GPU performance optimization, hardware cursor support was introduced directly into the VirtIO-GPU protocol and driver layer, with automatic software cursor save-under/restore-under fallback.

Prior to this step, cursor updates required drawing software cursor glyphs directly into the guest framebuffer (`fb.backbuffer`). This incurred backing saves, glyph rasterization, and framebuffer present cycles. With Step 2, the system adds native VirtIO-GPU 2D cursor resource creation and movement commands, offloading pointer motion from full backbuffer compositing.

---

## Changes Implemented

### 1. `virtio_gpu_protocol.h`
- Added cursor command operation codes according to the VirtIO-GPU 1.0/1.1 specification:
  - `VIRTIO_GPU_CMD_UPDATE_CURSOR` (0x0300)
  - `VIRTIO_GPU_CMD_MOVE_CURSOR` (0x0301)
- Defined packed wire structures:
  - `virtio_gpu_cursor_pos_t`: contains `scanout_id`, `x`, `y`, and padding (16 bytes).
  - `virtio_gpu_update_cursor_t`: contains `virtio_gpu_ctrl_hdr_t`, `virtio_gpu_cursor_pos_t`, `resource_id`, `hot_x`, `hot_y`, and padding (56 bytes).
- Added static compile-time assertions:
  - `_Static_assert(sizeof(virtio_gpu_cursor_pos_t) == 16, ...)`
  - `_Static_assert(sizeof(virtio_gpu_update_cursor_t) == 56, ...)`

### 2. `virtio_gpu.h` & `virtio_gpu.c`
- Added one-way descriptor submission helper `virtio_gpu_submit1(void *req, u32 req_len)` for commands that do not return a response payload (per VirtIO-GPU cursor specification).
- Implemented dedicated 64x64 cursor 2D resource setup (`resource_id = 2`) with 4096-byte aligned backing buffer `cursor_pixels[64 * 64]`.
- Implemented:
  - `virtio_gpu_cursor_init()`: creates 2D resource 2, attaches backing memory, transfers cursor glyph, and sets initial cursor position at (512, 384).
  - `virtio_gpu_cursor_update(u32 x, u32 y)`: updates cursor surface and position.
  - `virtio_gpu_cursor_move(u32 x, u32 y)`: dispatches `VIRTIO_GPU_CMD_MOVE_CURSOR` without re-uploading pixels.
  - `virtio_gpu_has_hardware_cursor()`: reports hardware cursor availability.
- Linked cursor initialization to `virtio_gpu_2d_init()` in `virtio_gpu_2d.c` after primary scanout resource 1 is configured.

### 3. `gui.c`
- Integrated `virtio_gpu_cursor_move((u32)mouse_x, (u32)mouse_y)` into both login and desktop fast paths.
- Maintained automatic fallback to software cursor save-under/restore-under whenever hardware cursor is unavailable or in headless framebuffer dumps.

---

## Verification & Benchmark Results

### 1. Build Verification
- Official build command: `make kernel.bin && make firstos.iso`
- Compiler: `x86_64-elf-gcc` (Kernel), `i686-elf-grub-mkrescue` (ISO)
- Result: **0 compilation errors, 0 linking errors.**

### 2. Runtime Verification (`tools/test_gfx_benchmark.py`)
- Platform: QEMU x86_64 with VirtIO-GPU PCI
- Scanout Resolution: 1024x768x32bpp
- Non-zero Pixels: 99.98%
- Frame Dump Latency: **21.21 ms**
- Active Screendump Latency: **8.72 ms** (down from 12.31 ms in Step 0)
- Input Stream Dispatch Rate: **52.4 events/sec** (improved from ~46.5 events/sec)
- Visual Degradation: **0.00%** (Top bar `#03070b` and Wallpaper `#2563eb` 100% matched)
- Result: **100% PASS**

---

## Next Step
Proceed to **Step 3: Verification of Software Cursor Fallback & Save-Under Reliability** (`reports/gfx_perf_step3_cursor_fallback.md`).
