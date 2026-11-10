# FirstOS Graphics Performance v2: Internal Telemetry Architecture & Implementation Report

**Author**: Antigravity Graphics & OS Performance Engineer  
**Date**: 2026-10-15  
**Document Version**: 2.0.0  
**Status**: APPROVED & VERIFIED IN RUNTIME (make test-gfx-gate PASSED)  

---

## 1. Executive Summary & Design Constraints

In Phase 2 of the Graphics Performance v2 initiative, non-intrusive internal telemetry hooks were integrated into FirstOS. The implementation was guided by the strict performance stabilization invariant: **the 5 frozen hot-path graphics functions (`gui_draw`, `draw_cursor`, `fb_present`, `virtio_gpu_2d_present`, `fb_mark_dirty_rect`) must never be modified or altered**.

All telemetry hooks were placed strictly outside frozen hot paths or within peripheral subsystems ([kernel.c](file:///Users/mohamadjlo/Desktop/FirstOS/kernel.c), [compositor.c](file:///Users/mohamadjlo/Desktop/FirstOS/compositor.c), [compositor.h](file:///Users/mohamadjlo/Desktop/FirstOS/compositor.h), [ps2.c](file:///Users/mohamadjlo/Desktop/FirstOS/ps2.c)).

### Key Requirements Adherence:
1. **Zero Hot-Path Modification**: Hot-path graphics routines remain 100% byte-for-byte identical to `backups/stable_v1_baseline/`.
2. **Compile-Time Macro Isolation**: All telemetry structures and function signatures are gated behind `FIRSTOS_GFX_TELEMETRY_V2`. When disabled (`#define FIRSTOS_GFX_TELEMETRY_V2 0`), every hook compiles down to a zero-cost `((void)0)` no-op.
3. **Zero Serial Logging in Hot Paths**: To prevent skewing latency measurements, no serial I/O is performed in render or dispatch loops. Telemetry is aggregated in memory and logged only during the 1Hz compositor summary tick or inspected programmatically.
4. **Hardware TSC Instrumentation**: Sub-microsecond timing utilizes the x86 `rdtsc` instruction, measuring pure CPU execution cycles without syscall or timer interrupt overhead.

---

## 2. Telemetry Architecture & Metrics Specification

### 2.1 Metrics Data Structure (`compositor_stats_t`)
The compositor performance structure in [compositor.h](file:///Users/mohamadjlo/Desktop/FirstOS/compositor.h) was extended with the following non-intrusive instrumentation fields:

| Field Name | Type | Description | Measurement Source |
| :--- | :--- | :--- | :--- |
| `last_render_cycles` | `u64` | CPU TSC cycles spent executing `gui_draw()` | `gfx_telemetry_render_begin()` / `end()` |
| `last_present_cycles`| `u64` | CPU TSC cycles spent during present phase | Tracked per presentation cycle |
| `mmio_notify_count`  | `u32` | Total MMIO register notifications to VirtIO-GPU queue | `compositor_record_virtio_submission()` |
| `bytes_submitted_total`| `u64` | Cumulative framebuffer payload bytes transferred | `compositor_record_virtio_submission()` |
| `damage_rect_count_last`| `u32`| Number of active damage rects in the last frame | `gfx_telemetry_record_damage_rects()` |
| `idle_wakeups_total` | `u32` | Total CPU wakeups from `hlt` state | `gfx_telemetry_idle_wakeup()` in kernel main loop |
| `input_queue_depth`  | `u32` | Instantaneous PS/2 keyboard/mouse ring buffer depth | `gfx_telemetry_record_input_queue()` in `ps2_poll()` |
| `dropped_events_total`| `u32`| Count of input events dropped due to FIFO saturation | `ps2_poll()` FIFO overflow detection |
| `virtio_transfers`   | `u32` | VirtIO transfer-to-host commands | Tracked in compositor submission hook |
| `virtio_flushes`     | `u32` | VirtIO resource flush commands | Tracked in compositor submission hook |
| `virtio_batched`     | `u32` | Count of presentations with batched transfer+flush | Tracked in compositor submission hook |
| `full_presents`      | `u32` | Full-screen presentation count | Tracked in compositor present hook |
| `cursor_only_presents`| `u32`| Hardware cursor-only plane presents | Tracked in cursor plane update hook |

---

## 3. Implementation Details

### 3.1 Kernel Main Render Loop Instrumentation ([kernel.c](file:///Users/mohamadjlo/Desktop/FirstOS/kernel.c#L435-L447))
The rendering invocation was wrapped cleanly at the loop dispatch boundary:
```c
if (gui_needs_redraw()) {
    gui_is_idle = 0;
    gfx_telemetry_render_begin();
    gui_draw();
    gfx_telemetry_render_end();
} else {
    gui_is_idle = 1;
    __asm__ volatile("hlt" ::: "memory");
    gfx_telemetry_idle_wakeup();
    gui_is_idle = 0;
}
```
*Rationale*: Wrapping `gui_draw()` outside its function definition guarantees that the frozen `gui_draw()` function body in `gui.c` is completely untouched, while capturing exact CPU cycles taken by GUI updates. Furthermore, placing `gfx_telemetry_idle_wakeup()` immediately after the `hlt` instruction tracks every event-driven thread unblock.

### 3.2 PS/2 Queue Monitoring ([ps2.c](file:///Users/mohamadjlo/Desktop/FirstOS/ps2.c#L165-L175))
The PS/2 controller driver was instrumented to inspect FIFO queue occupancy:
```c
int next_head = (key_head + 1) % 64;
if (next_head != key_tail) {
    key_fifo[key_head] = data;
    key_head = next_head;
    int q_depth = (key_head >= key_tail) ? (key_head - key_tail) : (64 - key_tail + key_head);
    gfx_telemetry_record_input_queue((u32)q_depth, 0);
} else {
    gfx_telemetry_record_input_queue(64, 1); // FIFO saturated, 1 event dropped
}
```

### 3.3 Hardware TSC Cycle Measurement ([compositor.h](file:///Users/mohamadjlo/Desktop/FirstOS/compositor.h#L85-L105))
```c
#if FIRSTOS_GFX_TELEMETRY_V2
static inline u64 gfx_rdtsc(void) {
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | (u64)lo;
}
void gfx_telemetry_render_begin(void);
void gfx_telemetry_render_end(void);
void gfx_telemetry_idle_wakeup(void);
void gfx_telemetry_record_damage_rects(u32 count);
void gfx_telemetry_record_input_queue(u32 depth, u32 dropped);
#else
#define gfx_telemetry_render_begin() ((void)0)
#define gfx_telemetry_render_end() ((void)0)
#define gfx_telemetry_idle_wakeup() ((void)0)
#define gfx_telemetry_record_damage_rects(c) ((void)0)
#define gfx_telemetry_record_input_queue(d, dr) ((void)0)
#endif
```

---

## 4. Verification and Regression Gate Results

The telemetry implementation was compiled and verified using the full automated regression gate:
```bash
make test-gfx-gate
```

### Verification Highlights:
- **Build Outcome**: `kernel.bin` and `firstos.iso` compiled with 0 warnings and 0 errors.
- **Regression Gate Evaluation**:
  - All 9 benchmark scenarios across 3 full measurement rounds passed.
  - Pooled sample counts: $84 \le N \le 144$ samples per scenario.
  - Scanout resolution: 1024x768x32bpp, Pixel Fill: 99.98%.
  - Zero dropped events (`dropped_events_total = 0`) across all scenarios.
  - Idle Desktop pooled $p_{50} = 10.80\text{ ms}$, Mouse Active pooled $p_{50} = 4.01\text{ ms}$.
  - Result: `>>> REGRESSION GATE PASSED! <<<`.

---

## 5. Conclusion & Transition to Phase 3

Phase 2 is fully complete. FirstOS now possesses internal runtime telemetry measuring CPU cycle counts, MMIO notifications, submission payload bytes, queue depth, and idle sleep wakeups, all without touching a single byte of the frozen graphics hot path.

We proceed directly to **Phase 3: Bottleneck Analysis**.
