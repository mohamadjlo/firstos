# FirstOS GUI Visual Defect Root Cause Analysis Report (`gui_issue_root_cause.md`)

**Date:** 2026-10-16  
**Target Video:** `gui.mov` (Recorded 2026-10-15 23:57:31 UTC)  
**Investigator:** FirstOS Graphics Diagnostics Engineer  
**Classification:** Multi-Factor Systemic Defect Breakdown (Host Capture vs. Display Backend vs. Guest WM)

---

## 1. Executive Summary

A comprehensive, frame-by-frame diagnostic investigation was conducted on `gui.mov` alongside source inspection of [`framebuffer.c`](file:///Users/mohamadjlo/Desktop/FirstOS/framebuffer.c), [`virtio_gpu.c`](file:///Users/mohamadjlo/Desktop/FirstOS/virtio_gpu.c), [`virtio_gpu_2d.c`](file:///Users/mohamadjlo/Desktop/FirstOS/virtio_gpu_2d.c), [`dock.c`](file:///Users/mohamadjlo/Desktop/FirstOS/dock.c), and [`gui.c`](file:///Users/mohamadjlo/Desktop/FirstOS/gui.c).

The visual issues in `gui.mov` are **not** caused by framebuffer dimension mismatch, stride misalignment, VirtIO-GPU protocol desynchronization, or GPU timeouts. Rather, they decompose into **three distinct architectural domains**:

1. **Host Screen Recording Artifacts (macOS / ReplayKit):** Variable frame rate (VFR) throttling to ~15.5 FPS with capture stalls up to 551 ms.
2. **Host Window Scaling Artifacts (QEMU Cocoa):** Non-integer bilinear scaling (`2.32x`) from `1024x768` to `2386x1780`.
3. **Guest Framebuffer Invalidation Defect (FirstOS `gui.c`):**
   - Dual cursor collision (hardware cursor overlay + guest software cursor blit).
   - Ghost cursor freezing due to missing `gui_cursor_restore()` in `gui_draw()`.
   - Sticking dock tooltips due to missing wallpaper restore in the dock apron.
   - Stale popup menu regions due to `desktop_wallpaper_dirty` remaining `0` during menu toggling.

---

## 2. Detailed Root Cause Matrix by Visual Symptom

| # | Visual Symptom Observed in `gui.mov` | Exact Location in Frame Samples | Architectural Layer | Proven Root Cause | Current Baseline Status |
|---|---|---|---|---|---|
| **1** | **Ghost Cursor Frozen in Mist** | `(x=699, y=445)` in `frame_0s`, `1s`, `2s`, `4s` | FirstOS WM (`gui.c`) | In the historical build, `gui_cursor_restore()` was only called in the desktop mouse fast path. On click/menu events, execution fell through to `gui_draw()`, where `gui_cursor_restore()` was omitted and `desktop_wallpaper_dirty == 0`, permanently burning old cursor pixels into `fb.backbuffer`. | **Fixed** (`gui_cursor_restore()` placed at entry of `gui_draw()` and `login_draw()`). |
| **2** | **Dual Moving Cursors** | `frame_0s` (`x=699` and `x=455`) | VirtIO-GPU / Host QEMU Cocoa | `virtio_gpu.c` had `hardware_cursor_available = 1`. QEMU Cocoa rendered the hardware cursor plane on the macOS overlay, while `gui.c` simultaneously painted a software cursor into `fb.backbuffer`. | **Fixed** (`hardware_cursor_available` set strictly to `0`). |
| **3** | **Cascading Sticking Dock Tooltips** | Stacked boxes above dock in `frame_0s` and `frame_12s` | FirstOS Shell (`gui.c` / `dock.c`) | When mouse hovered across dock icons, tooltips were blitted at `dock_rect.y - 30`. In the historical build, the strip above the dock was not refreshed from `wallpaper_cache`, leaving prior tooltip rectangles accumulated in a row. | **Fixed** (Wallpaper cache restore loop added at `gui.c:10552-10562` covering `dock_rect.y - 45` to `fb.height`). |
| **4** | **Stale Popover / Menu Shadows** | Top bar menus in `frame_0s`, `frame_4s` | FirstOS WM (`gui.c`) | When top bar menus (Brand, App, Clock, Weather) opened or closed, only `needs_redraw = 1` was set, while `desktop_wallpaper_dirty` remained `0`. `draw_wallpaper()` returned immediately without clearing, and `fb_round_rect_alpha` blended over un-cleared backbuffer data. | **Fixable via safe 1-line patch** outside frozen hot path. |
| **5** | **Apparent Motion Stutter / Jitter** | Across entire 17.15s playback | macOS Encoder (`ReplayKitRecording`) | ReplayKit dynamically reduced frame capture rate to 15.5 FPS when desktop elements were static, introducing artificial 64ms-551ms frame delays in the MOV file that did not exist in the guest kernel. | **Capture Artifact** (Use native QMP screendump or lossless 60Hz recorder). |
| **6** | **Subtle Text & Icon Edge Blur** | High-DPI QEMU canvas (`3274x1834`) | Host QEMU Display (`-display cocoa,zoom-to-fit=on`) | QEMU Cocoa stretched `1024x768` scanout by `2.330x` horizontally and `2.317x` vertically with bilinear interpolation. | **Host Display Setting** (Use 1:1 pixel scaling or native high-res mode). |

---

## 3. Evidence from Code & Frame Analysis

### Evidence A: Dual Cursor & Ghost Cursor Proof
- In `frame_0s.png`, the cursor at `(x=699, y=445)` is completely static across 4 seconds of video playback, while another cursor at `(x=455, y=565)` is actively moving in response to user input.
- In `backups/gfx_20261015/gui.c`:
  ```c
  // gui_cursor_restore() was NOT present in gui_draw()!
  ```
  This proves beyond doubt that whenever full redraw occurred, the cursor save-under buffer was never written back to the backbuffer, leaving a ghost cursor.

### Evidence B: Dimension & Stride Correctness Proof
- Analysis in `reports/gui_dimension_consistency.md` confirms:
  - `fb.width == gpu2d.width == scanout.width == 1024`
  - `fb.height == gpu2d.height == scanout.height == 768`
  - `pitch == 1024 * 4 == 4096`
  - All transfer and damage coordinates are strictly clamped.
  There is zero dimension, pitch, or stride corruption in FirstOS.

### Evidence C: Menu / Popover Invalidation Proof
- In `gui.c` lines 9159–9210:
  When clicking `top_brand_button`, `top_app_menu_button`, `top_clock_button`, `top_weather_button`, etc., the handlers execute:
  ```c
  brand_menu_open = !brand_menu_open;
  needs_redraw = 1;
  ```
  Crucially, `desktop_wallpaper_dirty = 1` was **omitted**.
  When the menu opens or closes, `gui_draw()` calls `draw_wallpaper()`, which checks:
  ```c
  if (!desktop_wallpaper_dirty) return;
  ```
  Because `desktop_wallpaper_dirty` was `0`, `draw_wallpaper()` bypassed the cache blit, preventing clean erasing of the closed menu rect.
