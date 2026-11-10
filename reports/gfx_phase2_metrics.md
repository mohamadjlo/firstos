# FirstOS Graphics & UI Profiling / Instrumentation Framework (Phase 2)
**Date:** 2026-10-15  
**Author:** Principal Graphics & Systems Engineer  
**Scope:** Core Rendering Pipeline, Input Subsystem, and VirtIO-GPU Metrics

---

## 1. Overview & Architecture

To observe, profile, and eliminate rendering bottlenecks without polluting hot render loops with heavy serial I/O, a lightweight instrumentation framework was integrated directly into `compositor.h` and `compositor.c`.

All tracking functions operate in O(1) integer arithmetic without memory allocation or blocking calls. Diagnostic serial dumps can be triggered via `compositor_log_stats()` or kernel diagnostic commands on demand rather than spewing serial output per frame.

---

## 2. Metrics Structure & Counter Definitions

Defined in `compositor.h`:
```c
typedef struct {
    u32 frame_count;              /* Total presented frames */
    u32 full_repaints;            /* Full-screen (1024x768) present cycles */
    u32 partial_repaints;         /* Bounded / damaged window updates */
    u32 cursor_only_presents;     /* Isolated mouse cursor-only updates */
    u32 virtio_flushes;           /* VirtIO-GPU RESOURCE_FLUSH operations */
    u32 virtio_transfers;         /* VirtIO-GPU TRANSFER_TO_HOST_2D operations */
    u32 virtio_batched;           /* Batched transfer+flush presentations */
    u32 last_damage_w;            /* Width of latest presentation rect */
    u32 last_damage_h;            /* Height of latest presentation rect */
    u64 pixels_submitted_total;   /* Cumulative pixel volume passed to GPU */
    u32 mouse_moves_count;        /* Processed PS/2 mouse motion events */
    u32 keyboard_events_count;    /* Processed PS/2 keyboard keystrokes */
    u32 dropped_frames;           /* Coalesced / skipped frame events */
    u32 redraw_reasons[8];        /* Breakdown by trigger source */
    u32 current_fps;              /* Rolling 1-second sampled FPS rate */
    u32 current_mouse_rate;       /* Rolling 1-second sampled mouse Hz */
} compositor_stats_t;
```

### Redraw Reasons Breakdown
The trigger origin for every `needs_redraw = 1` decision is classified into:
- `REDRAW_REASON_MOUSE_MOVE` (1): Raw mouse coordinate change
- `REDRAW_REASON_MOUSE_BUTTON` (2): Left, right, or middle click / release
- `REDRAW_REASON_WINDOW_DRAG` (3): Window header movement or corner resize
- `REDRAW_REASON_TEXT_BLINK` (4): Periodic 18-tick text insertion cursor blink
- `REDRAW_REASON_FPS_TICK` (5): Rolling seconds rollover for topbar clock/FPS
- `REDRAW_REASON_APP_UPDATE` (6): Browser rendering, terminal stream, or media player
- `REDRAW_REASON_WALLPAPER` (7): Theme switch or background setting update

---

## 3. Hot-Path Safety & Anti-Spam Design

1. **Zero Serial Writes in Render Loop:** `compositor_record_present_detailed()` and `compositor_record_virtio()` increment volatile in-memory counters only.
2. **1 Hz Rate Sampling:** `compositor_tick_1hz()` evaluates `current_fps` and `current_mouse_rate` once every second during the clock update tick.
3. **On-Demand Inspection:** Kernel command handler or diagnostic test tools can read `compositor_get_stats()` or invoke `compositor_log_stats()` on demand to inspect the system state without interrupting frame delivery.
