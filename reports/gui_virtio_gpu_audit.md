# FirstOS VirtIO-GPU & GUI File-by-File Architectural Audit

**Date:** 2026-10-16  
**Workspace:** `/Users/mohamadjlo/Desktop/FirstOS`  
**Scope:** 22 target files across VirtIO-GPU transport, 2D scanout, framebuffer, compositor, display server, GUI, desktop, dock, theme, widgets, and input layers.

---

## 1. Frozen Hot Path Verification

Per project rules, the following 5 functions are frozen and **were not modified**:
- `gui_draw` (`gui.c`)
- `draw_cursor` (`gui.c`)
- `fb_present` (`framebuffer.c`)
- `virtio_gpu_2d_present` (`virtio_gpu_2d.c`)
- `fb_mark_dirty_rect` (`framebuffer.c`)

---

## 2. File-by-File Audit Matrix (22 Files)

| # | File | LOC | Primary Responsibility | Audit Findings (Fix / Decompose / Move / Telemetry) |
|---|---|---|---|---|
| 1 | `virtio_gpu.c` | 864 | Modern VirtIO-PCI transport (`0x1AF4:0x1050`), control queue (`VIRTQ_NUM=8`), `submit2`/`submit3`/`submit_batch2`, cursor stubs | **Fix/Gate**: Verbose PCI capability MMIO addresses and queue readback hex dumps were printed unconditionally on boot (`virtio_gpu_setup_caps`, `virtio_gpu_setup_ctrl_queue`). Gated behind `VIRTIO_GPU_DEBUG_VERBOSE`. Added `virtio_gpu_transport_stats_t` (`submit2_count`, `submit3_count`, `submit_batch2_count`, `timeout_count`, `device_fail_count`, `max_spin_iterations`). |
| 2 | `virtio_gpu.h` | 45 | Public VirtIO-GPU transport structs & API | **Telemetry**: Added `virtio_gpu_transport_stats_t` and `virtio_gpu_get_stats(void)`. |
| 3 | `virtio_gpu_2d.c` | 298 | 2D scanout resource (`1024x768`, `B8G8R8X8_UNORM`), `staging_fb`, `virtio_gpu_2d_present` (frozen) | **Clean**: `virtio_gpu_2d_present` is frozen. Uses `virtio_gpu_submit_batch2` to batch `TRANSFER_TO_HOST_2D` + `RESOURCE_FLUSH` into a single MMIO notify when `rect_x == 0 && rect_w == fb.width`. |
| 4 | `virtio_gpu_2d.h` | 15 | Public 2D scanout API | **Clean**: Minimal and well-scoped. |
| 5 | `framebuffer.c` | 1094 | `fb` state, `backbuffer_storage` (`1920x1200x4`), dirty bounding box (`fb_mark_dirty_rect` — frozen), `fb_present` (frozen), 2D drawing primitives | **Clean**: Hot path functions (`fb_present`, `fb_mark_dirty_rect`) are frozen. |
| 6 | `framebuffer.h` | 107 | `framebuffer_t` struct, dirty rect API, drawing primitives | **Clean**: Cohesive header. |
| 7 | `compositor.c` | 184 | Surface tracking, damage coalescing (`COMP_MAX_DAMAGE=32`), SIMD alpha blend, 1Hz telemetry (`s_stats`) | **Fix/Gate**: `compositor_tick_1hz()` previously called `compositor_log_stats()` every second even when `frame_count` did not change (idle desktop). Updated to gate behind `COMPOSITOR_SERIAL_LOG_1HZ` and suppress duplicate idle logs when `frame_count == s_last_logged_frame_count`. |
| 8 | `compositor.h` | 105 | `compositor_stats_t`, redraw reason enums, `FIRSTOS_GFX_TELEMETRY_V2` hooks (`gfx_rdtsc`) | **Clean**: Well-structured telemetry interface. |
| 9 | `display_server.c` | 406 | Native X11 protocol server (`/tmp/.X11-unix/X0`), `944x620` client surface, `event_ring[4096]` | **Fix/Telemetry**: `ring_push()` previously dropped oldest bytes on ring overflow silently. Added `ring_overflow_drops` and `requests_processed` counters. |
| 10 | `display_server.h` | 45 | `display_server_state_t` struct & X11 event injection API | **Telemetry**: Added `ring_overflow_drops` and `requests_processed` fields to `display_server_state_t`. |
| 11 | `gui.c` | 12,339 (was 12,694) | Monolithic window manager, desktop, top bar, menus, Explorer UI, Calculator, Launchpad, Spotlight, Lock Screen, event pump | **Decompose & Upgrade**: Extracted self-contained Weather subsystem (`378 lines`) into `gui_weather.c` / `gui_weather.h`. Upgraded `draw_fps_dropdown()` into a live **GFX & Input Diagnostics Panel**. Unified `apply_theme()` with `theme_get_colors()`. |
| 12 | `gui.h` | 63 | Public GUI lifecycle & event API | **Clean**: Minimal public interface. |
| 13 | `gui_internal.h` | 197 | Shared `rect_t`, `app_window_t`, `APP_*` enums, shared globals | **Move/Organize**: Serves as internal bridge across GUI modules; paired with new `gui_weather.h`. |
| 14 | `gui_widgets.c` | 290 | Reusable UI toolkit (`ui_button`, `ui_text_input`, `ui_list_view`, `ui_tabs`, `ui_card`, `ui_modal`) | **Clean**: Properly consumes `theme_get_colors()` and `theme_get_metrics()`. |
| 15 | `gui_widgets.h` | 44 | Public widget toolkit structs & prototypes | **Clean**: Well-scoped. |
| 16 | `widgets.c` | 308 | Desktop floating widgets (Analog/Digital Clock, Weather card, Notes, Calendar) | **Clean**: Reads `current_weather` and `draw_weather_icon` (now provided by `gui_weather.c`). |
| 17 | `widgets.h` | 36 | Desktop widget manager API | **Clean**: Well-scoped. |
| 18 | `desktop.c` | 665 | Legacy desktop icon/bar module; `desktop_apply_theme()` called by `settings.c` | **Ownership Fix**: `desktop_apply_theme()` previously updated only `desktop.c`'s static variables without calling `gui.c`'s `apply_theme()`. Updated `desktop_apply_theme()` to synchronize `theme_apply()`, `desktop.c`, and `apply_theme()`. |
| 19 | `dock.c` | 485 | macOS-style bottom dock, magnification, ICO loader, running indicators | **Clean**: Self-contained module. |
| 20 | `theme.c` | 154 | `active_theme`, `active_metrics`, 3 theme presets (macOS, Ubuntu, Green), 3 custom profile slots | **Single Source of Truth**: Confirmed `theme_get_colors()` as the canonical source of theme colors across `gui.c`, `desktop.c`, and `gui_widgets.c`. |
| 21 | `ps2.c` | 207 | PS/2 keyboard & mouse polling (`ps2_poll`), 4-byte IntelliMouse packet parser, 64-byte `key_fifo` | **Fix & Telemetry**: Fixed a bug where `ps2_last_key()` popped scancodes without updating `gfx_telemetry_record_input_queue()`, leaving `qdepth=1` stuck in telemetry. Added `ps2_get_queue_depth()`, `ps2_get_dropped_events()`, `ps2_get_mouse_packets()`, and `ps2_get_mouse_sync_drops()`. |
| 22 | `virtio_input.c` | 285 | VirtIO-Input (`0x1AF4:0x1052`) PCI driver & 64-slot event queue | **64-bit Safety Fix**: Replaced 32-bit `(u32)(unsigned long)` MMIO pointer casts with 64-bit safe `uintptr_t` and `pci_read_bar64()`, and fixed `pci_device_t` member names (`dev->slot`, `dev->function`). |

---

## 3. Summary of Architectural Issues Addressed

1. **Monolithic `gui.c` Growth**:
   - Reduced `gui.c` by **355 lines** (from `12,694` to `12,339` lines) by extracting the self-contained Weather service and weather icon renderer into `gui_weather.c` and `gui_weather.h`, establishing the pattern for the remaining 6 planned module extractions documented in `reports/gui_split_plan.md`.
2. **Serial Logging Discipline in `virtio_gpu.c` and `compositor.c`**:
   - Gated 9 verbose MMIO/register readback hex dumps in `virtio_gpu.c` behind `VIRTIO_GPU_DEBUG_VERBOSE` (default `0`).
   - Kept `[compositor]` 1Hz serial logging gated under `COMPOSITOR_SERIAL_LOG_1HZ` (default `1`) and suppressed duplicate logs when `frame_count` is unchanged during idle desktop periods.
3. **Theme Ownership Unification (`theme.c` ↔ `desktop.c` ↔ `gui.c`)**:
   - `desktop_apply_theme()` now updates `theme_apply()`, reads from `theme_get_colors()`, and invokes `apply_theme()` in `gui.c`, eliminating stale theme caches.
4. **Input Queue Visibility & Diagnostics Panel**:
   - Fixed `qdepth` tracking on FIFO pop in `ps2.c` and exposed input queue telemetry getters.
   - Upgraded `draw_fps_dropdown()` in `gui.c` from static placeholder text (`"16.6 ms (60Hz)"`) to a live **GFX & Input Diagnostics Panel** displaying real-time FPS, Full/Partial/Cursor-only frame counts, Batched VirtIO / MMIO notify counts, Last Damage Rect dimensions, PS/2 Input Queue Depth & Drops, and VirtIO-GPU timeout counts.
