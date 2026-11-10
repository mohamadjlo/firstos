# FirstOS GUI & VirtIO-GPU Verification & Before/After Benchmark Report (`gui_virtio_gpu_verification.md`)

**Date:** 2026-10-16  
**Regression Gate Command:** `make test-gfx-gate` (`python3 tools/gfx_regression_gate.py`)  
**Gate Result:** **PASS (Exit Code 0 across all patches)**

---

## 1. Frozen Hot Path Integrity Check

None of the 5 frozen hot-path functions were modified:
- `gui_draw` (`gui.c`) — **Untouched**
- `draw_cursor` (`gui.c`) — **Untouched**
- `fb_present` (`framebuffer.c`) — **Untouched**
- `virtio_gpu_2d_present` (`virtio_gpu_2d.c`) — **Untouched**
- `fb_mark_dirty_rect` (`framebuffer.c`) — **Untouched**

---

## 2. Before / After Benchmark Comparison (3-Round Pooled Aggregation)

All measurements were collected using `tools/test_gfx_stabilization_bench.py` (3 independent rounds, warm-up samples excluded):
- **Before**: Pre-audit snapshot (`reports/gfx_benchmark_before_arch_cleanup.json`)
- **After Patch 1**: Logging discipline (`virtio_gpu.c`, `compositor.c`), input queue telemetry & `qdepth` fix (`ps2.c`), 64-bit MMIO casts (`virtio_input.c`), and `display_server.c` counters (`reports/gfx_benchmark_after_patch1.json`)
- **After Patch 2**: `gui_weather.c` / `gui_weather.h` extraction (`-355` lines in `gui.c`), unified `theme.c`/`desktop.c`/`gui.c` ownership, and live `draw_fps_dropdown` Diagnostics Panel (`reports/gfx_benchmark_after_patch2.json`)
- **After Patch 3**: `gui_calc.c` / `gui_calc.h` & `gui_notifications.c` / `gui_notifications.h` extraction, dead media/preview removal (`-421` lines in `gui.c`, `-776` lines total), and Makefile order-only prerequisite fix (`reports/gfx_benchmark_after_patch3.json`)
- **After Patch 4**: `gui_login.c` / `gui_login.h` extraction (`-625` lines in `gui.c`, `-1,401` lines total) (`reports/gfx_benchmark_after_patch4.json`)
- **After Patch 5**: `gui_overlays.c` / `gui_overlays.h` extraction (`-702` lines in `gui.c`, `-2,103` lines total from baseline `12,694` down to `10,591`), consolidated `draw_icon_scaled()` (`reports/gfx_benchmark_after_patch5.json`)

### 2.1 Frame-Time `p50` / `p95` / `p99` & Equivalent FPS

| Scenario | N | Baseline Before `p50` / `p95` / `p99` | After Patch 3 `p50` / `p95` / `p99` | After Patch 4 `p50` / `p95` / `p99` | After Patch 5 `p50` / `p95` / `p99` | After Patch 5 Equiv. FPS | Gate (`p99 <= 50ms`) |
|---|---|---|---|---|---|---|---|
| **1. Idle Desktop** | 114 | `6.26` / `11.84` / `12.91` | `9.49` / `14.91` / `18.36` | `8.42` / `19.58` / `34.76` | `10.56` / `13.21` / `15.10` | `94.7 FPS` | **PASS** |
| **2. Mouse Movement** | 144 | `3.72` / `9.99` / `26.65` | `4.02` / `6.58` / `8.10` | `3.96` / `6.51` / `8.28` | `3.79` / `5.23` / `6.91` | `264.0 FPS` (`56.5 Hz` dispatch) | **PASS (`p99` -74.1%)** |
| **3. Window Drag** | 99 | `3.88` / `9.26` / `13.82` | `3.73` / `6.62` / `14.83` | `3.90` / `5.08` / `11.15` | `3.78` / `5.73` / `7.30` | `264.9 FPS` (`57.5 Hz` dispatch) | **PASS (`p99` -47.2%)** |
| **4. Window Resize** | 84 | `3.70` / `5.42` / `9.23` | `3.98` / `7.08` / `16.83` | `3.79` / `4.98` / `7.19` | `3.94` / `7.82` / `12.14` | `253.7 FPS` | **PASS** |
| **5. Text Input** | 99 | `4.18` / `5.12` / `6.58` | `4.23` / `5.86` / `9.68` | `4.44` / `5.26` / `5.93` | `4.27` / `5.92` / `14.65` | `234.1 FPS` | **PASS** |
| **6. App Switching** | 84 | `4.41` / `7.22` / `11.23` | `4.73` / `6.98` / `11.78` | `4.82` / `6.24` / `8.21` | `4.65` / `6.47` / `8.46` | `214.9 FPS` | **PASS (`p99` -24.7%)** |
| **7. Browser Rendering** | 84 | `13.47` / `19.86` / `20.53` | `10.71` / `17.14` / `19.23` | `11.61` / `15.06` / `16.79` | `11.97` / `13.86` / `15.53` | `83.6 FPS` | **PASS (`p99` -24.4%)** |
| **8. Wallpaper / Theme** | 84 | `10.87` / `20.06` / `28.30` | `8.93` / `12.50` / `19.30` | `9.32` / `12.42` / `15.02` | `11.39` / `13.03` / `13.38` | `87.8 FPS` | **PASS (`p99` -52.7%)** |
| **9. Cursor Fallback** | 84 | `7.87` / `15.12` / `21.03` | `9.38` / `14.09` / `19.77` | `10.84` / `12.87` / `18.95` | `11.67` / `12.84` / `17.47` | `85.7 FPS` | **PASS (`p99` -16.9%)** |

---

## 3. Kernel Telemetry & Stability Verification

| Metric | Before | After Patch 1 | After Patch 2 | After Patch 3 | After Patch 4 | After Patch 5 | Requirement |
|---|---|---|---|---|---|---|---|
| **Scanout Resolution** | `1024x768x32bpp` | `1024x768x32bpp` | `1024x768x32bpp` | `1024x768x32bpp` | `1024x768x32bpp` | `1024x768x32bpp` | `1024x768x32bpp` |
| **Non-Zero Pixel Fill** | `99.98%` | `99.98%` | `99.98%` | `99.98%` | `99.98%` | `99.98%` | `>= 99.5%` |
| **Ghost Trail / Top Bar Check** | `0` violations | `0` violations | `0` violations | `0` violations | `0` violations | `0` violations | `0` violations |
| **Unhandled VirtIO-GPU Timeouts** | `0` | `0` | `0` | `0` | `0` | `0` | `0` |
| **VirtIO Device Failures (`STATUS_FAILED`)** | `0` | `0` | `0` | `0` | `0` | `0` | `0` |
| **Stuck Input Queue Depth (`qdepth`)** | Stuck at `1` after 1st key | Returns to `0` on pop | Returns to `0` on pop | Returns to `0` on pop | Returns to `0` on pop | Returns to `0` on pop | Accurate FIFO depth |
| **`gui.c` Line Count** | `12,694` lines | `12,694` lines | `12,339` lines (`-355`) | `11,918` lines (`-776`) | `11,293` lines (`-1,401`) | `10,591` lines (`-2,103`) | Incremental modularization |
