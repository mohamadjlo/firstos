# VirtIO-GPU & Compositor Debug / Telemetry Cleanup Report (`virtio_gpu_debug_cleanup.md`)

**Date:** 2026-10-16  
**Target Files:** `virtio_gpu.c`, `virtio_gpu.h`, `compositor.c`, `ps2.c`

---

## 1. Problem Analysis

### A. Verbose MMIO / Queue Readback Hex Dumps in `virtio_gpu.c`
During PCI capability setup (`virtio_gpu_setup_caps`) and control queue initialization (`virtio_gpu_setup_ctrl_queue`), `virtio_gpu.c` emitted 9 multi-part `serial_write` / `serial_hex64` debug lines dumping internal MMIO pointers (`common=`, `notify=`, `isr=`, `device=`, `queue_notify_off=`, `queue_desc=`, `queue_driver=`, `queue_device=`, `queue readback(before enable)`, `queue readback(after enable)`). While useful during initial VirtIO transport bring-up, they cluttered boot serial output and added unnecessary I/O port (`0x3F8`) writes.

### B. Unconditional 1Hz Idle Serial Telemetry in `compositor.c`
`compositor_tick_1hz()` previously called `compositor_log_stats()` unconditionally every second—even when the desktop was completely idle (`s_stats.frame_count` unchanged). Each `[compositor]` line writes ~195 characters over COM1 (`0x3F8`), causing periodic serial port I/O stalls during idle desktop periods.

### C. Stuck Input Queue Depth (`qdepth=0x00000001`) in `ps2.c`
In `ps2.c`, `ps2_poll()` called `gfx_telemetry_record_input_queue((u32)q_depth, 0)` when pushing a keyboard scancode into `key_fifo`, but `ps2_last_key()` did **not** update `q_depth` when popping from `key_fifo`. As a result, `[compositor] ... qdepth=0x00000001` remained permanently stuck at `1` after the first key event.

---

## 2. Changes Implemented

### 1. `virtio_gpu.c` & `virtio_gpu.h`
- Added compile-time flag `#ifndef VIRTIO_GPU_DEBUG_VERBOSE` / `#define VIRTIO_GPU_DEBUG_VERBOSE 0`.
- Gated verbose MMIO pointer and queue register readback dumps behind `#if VIRTIO_GPU_DEBUG_VERBOSE`.
- **Kept unconditional**:
  - High-level lifecycle logs:
    - `[virtio-gpu] detected vendor=... device=... bar0=... bar1=...`
    - `[virtio-gpu] modern PCI capabilities found`
    - `[virtio-gpu] display info ok w=... h=...`
    - `[virtio-gpu] hardware cursor disabled (software cursor active)`
  - All error & failure logs:
    - `[virtio-gpu] queue readback mismatch before/after enable`
    - `[virtio-gpu] device failed during submit / batch submit!`
    - `[virtio-gpu] command / batch submission timed out!`
- Added `virtio_gpu_transport_stats_t` (`submit2_count`, `submit3_count`, `submit_batch2_count`, `timeout_count`, `device_fail_count`, `max_spin_iterations`) and `virtio_gpu_get_stats(void)` for live UI diagnostics.

### 2. `compositor.c`
- Added compile-time flag `#ifndef COMPOSITOR_SERIAL_LOG_1HZ` / `#define COMPOSITOR_SERIAL_LOG_1HZ 1`.
- Added `s_last_logged_frame_count` check in `compositor_tick_1hz()` so `[compositor]` serial telemetry is emitted only when `s_stats.frame_count` has advanced since the previous second, eliminating redundant serial writes on an idle desktop while preserving full telemetry for active rendering and `make test-gfx-gate`.

### 3. `ps2.c` & `ps2.h`
- Updated `ps2_last_key()` to recompute `q_depth` and call `gfx_telemetry_record_input_queue((u32)q_depth, 0)` when popping a scancode.
- Added `ps2_get_queue_depth()`, `ps2_get_dropped_events()`, `ps2_get_mouse_packets()`, and `ps2_get_mouse_sync_drops()`.
