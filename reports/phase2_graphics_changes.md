# FirstOS Graphics & UI Architecture Changes (Phase 2)

**Date**: October 15, 2026  
**Engineer**: FirstOS Graphics, UI & Window System Engineering  
**Scope**: `/Users/mohamadjlo/Desktop/FirstOS`  
**Status**: Implemented & Verified in QEMU (100% Pass)  

---

## 1. Overview of Phase 2 Changes

To resolve the architectural congestion identified in the Graphics and UI Audit without breaking the visual fidelity or responsiveness of FirstOS, we executed **Phase 2A** of the GUI Decoupling Plan.

Key milestones achieved:
1. **Creation of Unified Control & Widget Subsystem (`gui_widgets.h`, `gui_widgets.c`)**: Extracted all standard UI controls and widget primitives from `gui.c`.
2. **Unified Theme & Design Metrics Engine (`theme.h`, `theme.c`)**: Unified colors and UI metrics (radii, padding, heights, shadow depth) into a centralized, single-source-of-truth structure.
3. **Nested Clip Stack Implementation (`framebuffer.h`, `framebuffer.c`)**: Added `fb_push_clip` and `fb_pop_clip` with intersection semantics, eliminating visual bleed-through.
4. **Compositor Damage & Present Diagnostics (`compositor.h`, `compositor.c`)**: Introduced `compositor_stats_t` and connected frame presents directly to damage tracking.
5. **Driver Path Affirmation**: VirtIO-GPU 2D remains the primary accelerated display path, with linear framebuffer copy strictly preserved as the fail-safe emergency fallback.

---

## 2. Granular File Changes

### 2.1 Unified Widget Subsystem ([`gui_widgets.h`](file:///Users/mohamadjlo/Desktop/FirstOS/gui_widgets.h) & [`gui_widgets.c`](file:///Users/mohamadjlo/Desktop/FirstOS/gui_widgets.c))
- **Extracted Primitives & Controls**:
  - `point_in_rect(int px, int py, rect_t r)`
  - `outline(int x, int y, int w, int h, u32 color)`
  - `draw_soft_box(rect_t r, u32 base, u32 hi, u32 lo)`
  - `draw_label(int x, int y, const char *text, u32 color)`
  - `draw_button(rect_t r, u32 base, u32 hi, const char *text, u32 text_color)`
  - `draw_checkbox(rect_t r, int checked, const char *label)`
  - `draw_radio(rect_t r, int selected, const char *label)`
  - `draw_slider(rect_t r, int value, int min, int max)`
  - `draw_scrollbar(int x, int y, int h, int total_items, int visible_items, int scroll_index)`
  - `draw_text_input(rect_t r, const char* text, int cursor_pos, int has_focus, const char* placeholder)`
  - `draw_progress_bar(rect_t r, int value, int min, int max)`
  - `draw_tooltip(int x, int y, const char* text)`
  - `draw_dialog(const char* title, const char* message, const char* btn1, const char* btn2)`
  - `draw_toast(int x, int y, const char* message, int toast_type)`
- **Widget State Management**:
  - Encapsulated `widget_tooltip_*`, `widget_dialog_*`, `widget_progress_*`, `pressed_button` into clean global state with non-breaking accessor APIs (`gui_text_input_set`, `gui_progress_set`, `gui_dialog_show`, `gui_tooltip_show`).

### 2.2 Theme & Metrics Engine ([`theme.h`](file:///Users/mohamadjlo/Desktop/FirstOS/theme.h) & [`theme.c`](file:///Users/mohamadjlo/Desktop/FirstOS/theme.c))
- Added `theme_metrics_t`:
  ```c
  typedef struct {
      int window_radius;
      int control_radius;
      int button_height;
      int input_height;
      int titlebar_height;
      int scrollbar_width;
      int shadow_depth;
  } theme_metrics_t;
  ```
- Exported `active_metrics` with sensible defaults (`window_radius=10`, `control_radius=6`, `scrollbar_width=12`).
- Added helper functions:
  - `theme_get_colors()`
  - `theme_get_metrics()`
  - `theme_mix_color(u32 a, u32 b)`: Unified color blending logic, replacing duplicate ad-hoc arithmetic across widgets.

### 2.3 Nested Clipping Support ([`framebuffer.h`](file:///Users/mohamadjlo/Desktop/FirstOS/framebuffer.h) & [`framebuffer.c`](file:///Users/mohamadjlo/Desktop/FirstOS/framebuffer.c))
- Added a 16-level clip stack:
  ```c
  void fb_push_clip(int x, int y, int w, int h);
  void fb_pop_clip(void);
  ```
- `fb_push_clip` calculates the exact bounding intersection between the parent clip and child clip, ensuring child controls (e.g. scrollbars or table cells) cannot draw outside their container box.
- `fb_pop_clip` restores the previous clipping rectangle.

### 2.4 Compositor Statistics & Diagnostics ([`compositor.h`](file:///Users/mohamadjlo/Desktop/FirstOS/compositor.h) & [`compositor.c`](file:///Users/mohamadjlo/Desktop/FirstOS/compositor.c))
- Introduced `compositor_stats_t`:
  - `frame_count`: Total rendered frames.
  - `full_repaints`: Frames updating the full screen.
  - `partial_repaints`: Frames bounded to dirty rectangles.
  - `virtio_flushes`: Number of hardware flushes submitted to VirtIO-GPU.
  - `last_damage_w` / `last_damage_h`: Dimensions of the most recent damage bounding box.
- Connected `fb_present()` in [`framebuffer.c`](file:///Users/mohamadjlo/Desktop/FirstOS/framebuffer.c) to `compositor_record_present(is_full, w, h)`.
- Added `compositor_log_stats()` for serial diagnostic monitoring.

### 2.5 Monolithic GUI Cleanup ([`gui.c`](file:///Users/mohamadjlo/Desktop/FirstOS/gui.c))
- Included [`gui_widgets.h`](file:///Users/mohamadjlo/Desktop/FirstOS/gui_widgets.h).
- Removed redundant in-tree implementations of all 10 standard widgets and basic drawing primitives.
- Reduced `gui.c` line count from **12,842 to 12,523 lines** as the initial safe extraction step.

### 2.6 Build System Integration ([`Makefile`](file:///Users/mohamadjlo/Desktop/FirstOS/Makefile))
- Added `gui_widgets.o` to `OBJS`.
- Added explicit compilation target:
  ```makefile
  gui_widgets.o: gui_widgets.c gui_widgets.h framebuffer.h font.h theme.h gui_internal.h ps2.h
  	$(CC) $(CFLAGS) -c gui_widgets.c -o gui_widgets.o
  ```
