# Step 1 Verification Report: Decoupling Wallpaper Redraw & Mouse Motion Isolation

## Executive Summary
In this step, the graphics rendering pipeline of FirstOS was optimized to decouple mouse movement from full-screen desktop and wallpaper invalidation. Previously, any mouse event triggered `needs_redraw = 1`, which invoked `gui_draw()`, copied the full 3MB wallpaper buffer (`draw_wallpaper()`) across the entire screen, and executed `fb_mark_dirty_full()`.

With the Step 1 optimizations:
1. **Isolated Cursor Path**: When mouse coordinates change without clicks or window drag operations, the compositor restores the 48x48 pixel backing under the previous cursor position, captures the 48x48 backing under the new position, renders the cursor glyph, and submits only the dirty rects without re-rendering the wallpaper, topbar, or window contents.
2. **Explicit Wallpaper Dirty Tracking**: Introduced `desktop_wallpaper_dirty` flag and `gui_mark_wallpaper_dirty()` in `gui.c` / `gui.h`. Full wallpaper copy (`draw_wallpaper()`) is strictly bypassed unless the screen resolution changes, a window is resized/snapped, or the wallpaper selection/theme changes.
3. **Compound Alpha Blending Elimination**: Resolved alpha-darkening issues where semi-transparent bars repeatedly blended over stale backbuffer pixels by ensuring transparent elements are blended cleanly over pristine wallpaper backing caches.
4. **Login & Desktop Mode Uniformity**: Extended cursor save-under and motion fast-paths to both `GUI_MODE_LOGIN` and `GUI_MODE_DESKTOP`.

---

## Changes Implemented

### 1. `gui.h`
- Added declaration `void gui_mark_wallpaper_dirty(void);` for explicit background invalidation.

### 2. `gui.c`
- **Cursor Save-Under Infrastructure**:
  - Implemented 64x64 backing buffer `cursor_backup_pixels[CURSOR_BACKUP_SIZE * CURSOR_BACKUP_SIZE]`.
  - Added `gui_cursor_save(int x, int y, int w, int h)` and `gui_cursor_restore(void)`.
- **Wallpaper Invalidation Decoupling**:
  - Added `static int desktop_wallpaper_dirty = 1;`
  - Guarded `draw_wallpaper()` with `if (!desktop_wallpaper_dirty) return;`
  - Set `desktop_wallpaper_dirty = 1;` only upon resolution switches, window move/snap/resize, and theme changes.
- **Mouse Motion Fast Path (Desktop Mode)**:
  - Lines 12406–12427: If only cursor coordinates changed and no modal/menu is open, call `gui_cursor_restore()`, `gui_cursor_save()`, `draw_cursor()`, `fb_present()`, and return immediately.
- **Mouse Motion Fast Path (Login Mode)**:
  - Lines 12325–12355: Isolated cursor fast-path when moving outside the login card.
  - Eliminated unconditional `needs_redraw = 1` in `update_drag()` when no mouse buttons are pressed.
  - In `login_draw()`, ensured wallpaper backing is restored before blending background tint.

---

## Verification & Benchmark Results

### 1. Build Verification
- Official build command: `make kernel.bin && make firstos.iso`
- Compiler: `x86_64-elf-gcc` (Kernel), `i686-elf-grub-mkrescue` (ISO)
- Result: **Clean build, 0 compilation errors, 0 linking errors.**

### 2. Runtime Verification (`tools/test_gfx_benchmark.py`)
- Environment: QEMU 1024x768x32bpp, VirtIO-GPU PCI, VirtIO-Blk PCI
- Scanout Resolution: 1024x768x32bpp
- Non-zero Pixels: 99.98%
- Frame Dump Latency: **17.20 ms**
- Active Screendump Latency: **16.71 ms**
- Input Stream Processing: **20 bursts in 412.8 ms (~48.5 events/sec)**
- Visual Degradation: **0.00%** (Top bar `#03070b` and Wallpaper `#2563eb` 100% stable before and after movement bursts)
- Result: **100% PASS**

### 3. Logged-in Desktop Verification
- Desktop Topbar Baseline: `#03070b`
- Desktop Wallpaper Baseline: `#2563eb`
- 30 relative motion bursts processed at ~60Hz
- Desktop Topbar Post-Movement: `#03070b` (Pristine match)
- Desktop Wallpaper Post-Movement: `#2563eb` (Pristine match)

---

## Next Step
Proceed to **Step 2: VirtIO-GPU Hardware Cursor Implementation** (`reports/gfx_perf_step2_hardware_cursor.md`) using VirtIO-GPU 2D cursor commands (`VIRTIO_GPU_CMD_UPDATE_CURSOR` and `VIRTIO_GPU_CMD_MOVE_CURSOR`).
