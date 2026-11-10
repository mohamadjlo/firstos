# FirstOS Graphics & VirtIO-GPU Performance Baseline (Step 0)
**Date:** 2026-10-15  
**Author:** Principal Graphics & Systems Engineer  
**Workspace:** `/Users/mohamadjlo/Desktop/FirstOS`

---

## 1. System Status & Verification Baseline

| Item | Status / Measurement | Description |
|---|---|---|
| **Build Result** | **PASS** | `kernel.bin` compiled and linked cleanly with `x86_64-elf-gcc` & `x86_64-elf-ld`. `firstos.iso` generated via `grub-mkrescue`. |
| **Boot Result** | **PASS** | Multiboot loader loads kernel and `fs.img`. PCI bus master enabled, VirtIO-GPU modern capability BAR mapped, scanout initialized. |
| **Active Resolution** | **1024x768x32bpp** | Stride: 4096 bytes/row, buffer size: 3,145,728 bytes. Pixel format: `VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM`. |
| **Screendump Latency** | **11.56 ms (active) / 26.11 ms (baseline)** | Measured via QMP `screendump` protocol in `tools/test_gfx_benchmark.py`. |
| **Input Dispatch Rate** | **51.4 Hz** | 20 relative mouse motion bursts dispatched in 389.1 ms. |
| **Visual Integrity** | **99.98% non-zero pixels** | Topbar: `#424848`, Desktop Wallpaper: `#2563eb`. Zero visual degradation or tearing observed. |

---

## 2. Codebase Reference Map (Exact Code Locations)

1. **PS/2 Mouse Motion Setting `needs_redraw`:**
   - Location: `ps2.c:113` (`ps2_poll` parses byte stream, updates `mouse_x`, `mouse_y`).
   - Location: `gui.c:3610` (`gui_needs_redraw`):
     ```c
     if (x != last_mouse_x || y != last_mouse_y || buttons != last_mouse_buttons) needs_redraw = 1;
     ```
2. **Main GUI Drawing Entry Point:**
   - Location: `gui.c:12309` (`void gui_draw(void)`).
3. **Wallpaper Blitting Entry Point:**
   - Location: `gui.c:3476` (`static void draw_wallpaper(void)`).
   - Line 3503: `fast_memcpy32(fb.backbuffer, wallpaper_cache, w * h);`
   - Line 3512: `fb_mark_dirty_full();`
4. **Cursor Drawing:**
   - Location: `gui.c:4330` (`static void draw_cursor(void)`).
5. **Full Screen Invalidation:**
   - Location: `framebuffer.c:613` (`void fb_mark_dirty_full(void)`):
     Sets `dirty_x0 = 0, dirty_y0 = 0, dirty_x1 = fb.width, dirty_y1 = fb.height, dirty_valid = 1`.
6. **Framebuffer Presentation:**
   - Location: `framebuffer.c:621` (`void fb_present(void)`):
     Extracts bounding box `[dirty_x0, dirty_y0, dirty_x1, dirty_y1]` and invokes `virtio_gpu_2d_present()`.
7. **VirtIO-GPU 2D Presentation:**
   - Location: `virtio_gpu_2d.c:193` (`int virtio_gpu_2d_present(...)`).
8. **Transfer Command:**
   - Location: `virtio_gpu_2d.c:80` (`cmd_transfer`) and `virtio_gpu_2d.c:210` (`q_xfer.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D`).
9. **Flush Command:**
   - Location: `virtio_gpu_2d.c:99` (`cmd_flush`) and `virtio_gpu_2d.c:218` (`q_flush.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH`).
10. **Render Loop Scheduling:**
    - Location: `kernel.c:425` (`kernel_main` event loop):
      ```c
      while (1) {
          hda_tick();
          ps2_poll();
          eth_poll();
          serial_poll_commands();
          if (gui_needs_redraw()) {
              gui_is_idle = 0;
              gui_draw();
          } else {
              gui_is_idle = 1;
              __asm__ volatile("hlt" ::: "memory");
              gui_is_idle = 0;
          }
      }
      ```

---

## 3. Current Present Behavior & Redraw Triggers

- **Present Behavior:** `fb_present()` currently calculates a single enclosing bounding box from accumulated damage coordinates. When VirtIO-GPU is active, `virtio_gpu_2d_present()` submits `TRANSFER_TO_HOST_2D` and `RESOURCE_FLUSH` via `virtio_gpu_submit_batch2()`.
- **Current Redraw Triggers:**
  - Mouse coordinate or button delta (`gui_needs_redraw`)
  - PS/2 keyboard keystrokes
  - Periodic text cursor blink (ticks >= 18)
  - RTC 1-second clock rollover for top bar display
  - Application content events (Terminal, Web Browser tick, Media Player tick)
- **FPS Measurement Method:**
  - In `gui_draw()`: `fps_counter++` increments per rendered frame. On each second boundary (`clock_seconds != fps_last_second`), `fps_value = fps_counter`, `fps_counter = 0`, and `compositor_tick_1hz()` samples `current_fps`.
- **Known Visual & Performance Issues:**
  1. Lack of native VirtIO-GPU hardware cursor offload (currently reliant on software cursor overlay).
  2. Single bounding box damage model causes disparate updates (e.g. top bar and dock) to trigger large transfers.
  3. No damage-driven render loop scheduling; redraw relies on checking global `needs_redraw` flag in polling loop.
