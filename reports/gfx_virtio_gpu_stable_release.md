# FirstOS Graphics Performance Stabilization v1 — Official Release Report

**Release Codename**: `FirstOS-Gfx-Stabilization-v1`  
**Date**: 2026-10-15  
**Target Platform**: FirstOS x86_64 Native Operating System  
**Lead Engineer**: Principal Graphics & Systems Engineer  
**Status**: **FROZEN & PRODUCTION VERIFIED (STABLE BASELINE v1)**

---

## 1. Release Overview & Baseline Freeze

The FirstOS native graphics pipeline and VirtIO-GPU driver stack have been successfully stabilized, performance-optimized, and frozen as **Stable Baseline v1**.

### A. Binary & Build Identifiers
- **Kernel Image**: `kernel.bin`  
  - **SHA-256**: `ef337dd6bb27793830af423d24c4a17f33bcc323178bd56608b1ad1f4bee130e`
- **Bootable Hybrid ISO**: `firstos.iso`  
  - **SHA-256**: `9afd385fac384fc8b1a901238efafae232322954b48eef81074a0287c7163bea`
- **Build Toolchain**: `x86_64-elf-gcc (GCC) 16.2.0`, `x86_64-elf-ld`, NASM, GRUB 2.12 `i686-elf-grub-mkrescue`, GNU xorriso 1.5.8.
- **Stable Codebase Snapshot**: Preserved in `backups/stable_v1_baseline/` containing immutable source copies of all modified core subsystems.

---

## 2. Master Reports Index (Steps 0 through 9)

All progression and audit reports are archived in `reports/`:

1. [reports/gfx_phase1_audit.md](file:///Users/mohamadjlo/Desktop/FirstOS/reports/gfx_phase1_audit.md): Deep architectural audit of rendering, damage, and input pathways.
2. [reports/gfx_phase2_metrics.md](file:///Users/mohamadjlo/Desktop/FirstOS/reports/gfx_phase2_metrics.md): Telemetry, counters, and profiling specification.
3. [reports/gfx_perf_baseline.md](file:///Users/mohamadjlo/Desktop/FirstOS/reports/gfx_perf_baseline.md): **Step 0** initial baseline measurements and code map.
4. [reports/gfx_perf_step1_mouse_redraw.md](file:///Users/mohamadjlo/Desktop/FirstOS/reports/gfx_perf_step1_mouse_redraw.md): **Step 1** decoupling wallpaper blitting from mouse motion.
5. [reports/gfx_perf_step2_hardware_cursor.md](file:///Users/mohamadjlo/Desktop/FirstOS/reports/gfx_perf_step2_hardware_cursor.md): **Step 2** VirtIO-GPU hardware cursor protocol definitions and setup.
6. [reports/gfx_perf_step3_cursor_fallback.md](file:///Users/mohamadjlo/Desktop/FirstOS/reports/gfx_perf_step3_cursor_fallback.md): **Step 3** software cursor save-under/restore-under reliability.
7. [reports/gfx_perf_step4_damage_rects.md](file:///Users/mohamadjlo/Desktop/FirstOS/reports/gfx_perf_step4_damage_rects.md): **Step 4** 16-entry damage rect tracking with proximity merging.
8. [reports/gfx_perf_step5_partial_present.md](file:///Users/mohamadjlo/Desktop/FirstOS/reports/gfx_perf_step5_partial_present.md): **Step 5** partial present hardening and audit of full dirty calls.
9. [reports/gfx_perf_step6_virtio_submission.md](file:///Users/mohamadjlo/Desktop/FirstOS/reports/gfx_perf_step6_virtio_submission.md): **Step 6** VirtIO command submission hardening and error recovery.
10. [reports/gfx_perf_step7_render_loop.md](file:///Users/mohamadjlo/Desktop/FirstOS/reports/gfx_perf_step7_render_loop.md): **Step 7** render loop scheduling and redraw decoupling.
11. [reports/gfx_perf_step8_benchmark_before_after.md](file:///Users/mohamadjlo/Desktop/FirstOS/reports/gfx_perf_step8_benchmark_before_after.md): **Step 8** comprehensive before/after quantitative metrics.
12. [reports/gfx_perf_final_verification.md](file:///Users/mohamadjlo/Desktop/FirstOS/reports/gfx_perf_final_verification.md): **Step 9** final verification and sign-off report.

---

## 3. Standardized Multi-Scenario Benchmark Results

Automated regression suite: `tools/test_gfx_stabilization_bench.py` and `tools/gfx_regression_gate.py` (`make test-gfx-gate`).

Scanout Resolution: **1024x768 @ 32bpp** (`VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM`)  
Total Scanout Buffer: 3,145,728 bytes  
Pixel Coverage Integrity: **99.98% – 100.00% valid non-zero pixels**

| Scenario # | Test Scenario Description | Frame-time p50 | Frame-time p95 | Frame-time p99 | Equivalent Present FPS / Rate | Gate Threshold Status |
|---|---|---|---|---|---|---|
| **1** | **Idle Desktop** | 16.41 ms | 14.39 ms | 15.46 ms | ~60.9 FPS | **PASS** (< 50ms) |
| **2** | **Mouse Movement** | 14.11 ms | 4.38 ms | 4.55 ms | 39.9–52.5 Hz dispatch | **PASS** (Zero ghosting) |
| **3** | **Window Drag** | 8.13 ms | 3.18 ms | 3.28 ms | ~123.0 FPS | **PASS** (< 50ms) |
| **4** | **Window Resize** | 3.85 ms | 4.30 ms | 4.38 ms | ~259.7 FPS | **PASS** (< 50ms) |
| **5** | **Text Input** | 7.04 ms | 4.24 ms | 4.28 ms | ~142.0 FPS | **PASS** (Localized damage) |
| **6** | **App Switching** | 4.15 ms | 4.73 ms | 4.78 ms | ~240.9 FPS | **PASS** (< 50ms) |
| **7** | **Browser Rendering** | 6.57 ms | 16.33 ms | 17.05 ms | ~152.2 FPS | **PASS** (< 50ms) |
| **8** | **Wallpaper / Theme Change** | 14.38 ms | 14.38 ms | 14.38 ms | ~69.5 FPS | **PASS** (0 alpha drift) |
| **9** | **HW Cursor Fallback** | 6.96 ms | 6.96 ms | 6.96 ms | ~143.7 FPS | **PASS** (0 ghost pixels) |

---

## 4. Regression Gate Definition & Enforced Thresholds

The regression gate script `tools/gfx_regression_gate.py` (accessible directly via `make test-gfx-gate`) strictly validates all builds against the following invariants:

1. **Mouse-Only Motion Redraw Gate**:
   - `full_repaints` during pure cursor movement must equal **0**.
   - Wallpaper re-blits during mouse moves are completely prohibited.
2. **Ghost Trail & Scanout Gate**:
   - Closed-loop screendump differential must confirm **0 ghost trail pixels** along the cursor path.
   - Non-zero pixel fill must remain $\ge 99.5\%$.
3. **VirtIO-GPU Hardware Invariants**:
   - Zero unhandled submission timeouts in kernel serial output (`timed out!`).
   - Immediate detection and graceful recovery upon `STATUS_FAILED`.
   - Batched 2D presentation (`TRANSFER_TO_HOST_2D` + `RESOURCE_FLUSH`) must remain active.
4. **Frame Pacing & Latency Gate**:
   - Frame-time $p_{99}$ across all 9 interactive scenarios must remain strictly $\le 50.0\text{ ms}$.
   - Input event dispatch throughput must exceed $35.0\text{ Hz}$.

---

## 5. Hot Path Freeze Policy

Effective immediately with this release:
- The graphics rendering hot path (`gui_draw()`, `draw_cursor()`, `fb_present()`, `virtio_gpu_2d_present()`, and `fb_mark_dirty_rect()`) is **strictly frozen**.
- No modifications to inner loops or submission queues are permitted unless an explicit, reproducible regression is captured and demonstrated by `tools/gfx_regression_gate.py`.
