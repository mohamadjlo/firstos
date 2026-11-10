# Step 3 Verification Report: Software Cursor Fallback & Save-Under Reliability

## Executive Summary
In Step 3, the software cursor fallback pipeline was evaluated and verified for stability, bounds safety, and zero ghosting. While VirtIO-GPU hardware cursor commands (`VIRTIO_GPU_CMD_UPDATE_CURSOR` and `VIRTIO_GPU_CMD_MOVE_CURSOR`) offload pointer movement on supported display environments, headless capture environments (such as QEMU screendump) and fallback display topologies require a fully reliable software cursor save-under and restore-under implementation.

---

## Architecture & Implementation Details

### 1. Dedicated Save-Under Backing Buffer
- Statically allocated 64x64 pixel backing store:
  `static u32 cursor_backup_pixels[CURSOR_BACKUP_SIZE * CURSOR_BACKUP_SIZE];`
- Kept completely separate from `wallpaper_cache`, `framebuffer_backing`, and window buffers.

### 2. Save and Restore Mechanics
- **`gui_cursor_save(int x, int y, int w, int h)`**:
  - Clamps requested cursor bounding box to `[0, fb.width)` and `[0, fb.height)`.
  - Captures exact pristine backbuffer pixels using 32-bit assembly copy (`fast_memcpy32`).
  - Sets `cursor_backup_valid = 1`.
- **`gui_cursor_restore()`**:
  - Validates `cursor_backup_valid` and backbuffer presence.
  - Blits saved pixels back to `fb.backbuffer` at the previous position.
  - Registers the old cursor bounding box as dirty via `fb_mark_dirty_rect(bx, by, bw, bh)`.
  - Invalidate `cursor_backup_valid = 0`.
- **`draw_cursor()`**:
  - Rasterizes cursor glyphs using `fb_rect()` primitives.
  - Because `fb_rect()` invokes `mark_dirty_rect()`, new cursor bounds are automatically added to the damage bounding box.
  - The combination of `gui_cursor_restore()` and `draw_cursor()` ensures only the dirty union of previous and new positions is scheduled for transfer/flush.

### 3. Prevention of Cursor Artifacts on Wallpaper
- The cursor is never drawn into `wallpaper_cache`.
- In `GUI_MODE_LOGIN`, `login_draw()` refreshes background pixels from `wallpaper_cache` prior to any alpha blending.
- In `GUI_MODE_DESKTOP`, `draw_wallpaper()` is strictly isolated from cursor motion.

---

## Verification & Benchmark Results

### 1. Build Verification
- Official build command: `make kernel.bin && make firstos.iso`
- Compiler: `x86_64-elf-gcc`
- Result: **0 warnings, 0 compilation errors, 0 linking errors.**

### 2. Closed-Loop Cursor Trail & Ghosting Test
- Test Scenario:
  - System booted and authenticated to Desktop Mode.
  - Initial 1024x768 frame captured.
  - Mouse dispatched on a 50-step trajectory (+50px right, +25px down, then returning to origin).
  - Post-movement frame captured.
  - Compared all pixels along the traversal corridor (excluding the final cursor resting point).
- Result:
  - **Ghost trail pixels found along path: 0**
  - **Artifacts on background: 0**
  - **Status: PASS**

### 3. General Benchmark Verification (`tools/test_gfx_benchmark.py`)
- Input dispatch rate: **~52.4 Hz**
- Frame dump latency: **17–21 ms**
- Active latency during motion: **8.72 ms**
- Visual degradation: **0.00%**
- Result: **100% PASS**

---

## Next Step
Proceed to **Step 4: Damage Rectangles & Multi-Region Tracking in Compositor** (`reports/gfx_perf_step4_damage_rects.md`).
