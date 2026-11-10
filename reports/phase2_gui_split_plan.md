# FirstOS GUI Modular Split Architecture Plan (Phase 2)

**Date**: October 15, 2026  
**Engineer**: FirstOS Graphics, UI & Window System Engineering  
**Scope**: `/Users/mohamadjlo/Desktop/FirstOS`  
**Target Goal**: Incrementally and safely decouple [`gui.c`](file:///Users/mohamadjlo/Desktop/FirstOS/gui.c) (12,842 lines) into clean, testable subsystems without visual or functional regressions.

---

## 1. Modular Target Architecture Overview

Instead of one monolithic `gui.c`, FirstOS UI will be organized into distinct layers with strict ownership:

```
+--------------------------------------------------------------------------+
|                            Applications Layer                            |
|     (Browser, Terminal, App Studio, Calculator, Media Player, Explorer)   |
+--------------------------------------------------------------------------+
                                     |
                                     v
+--------------------------------------------------------------------------+
|                          Window Manager Subsystem                        |
|   (app_windows[], z-order, focus, snap zones, resize, tab management)    |
|               [window_manager.h / window_manager.c]                      |
+--------------------------------------------------------------------------+
          |                                              |
          v                                              v
+----------------------------------+   +-----------------------------------+
|         Desktop Shell            |   |       Menu & Modal Subsystem      |
| (wallpaper, desktop icons, dock, |   | (top bar menus, start menu,       |
|  marquee selection, widgets)     |   |  popovers, notifications, dialogs)|
|      [desktop.h / desktop.c]     |   |       [gui_menu.h / gui_menu.c]   |
+----------------------------------+   +-----------------------------------+
                   \                                    /
                    \                                  /
                     v                                v
+--------------------------------------------------------------------------+
|                   Unified Control & Widget Subsystem                     |
| (buttons, checkboxes, radios, sliders, text inputs, scrollbars, toasts)  |
|                     [gui_widgets.h / gui_widgets.c]                      |
+--------------------------------------------------------------------------+
                                     |
                                     v
+--------------------------------------------------------------------------+
|                   Theme & Design Metrics Engine                          |
|         (unified colors, typography, border radii, padding metrics)       |
|                           [theme.h / theme.c]                            |
+--------------------------------------------------------------------------+
                                     |
                                     v
+--------------------------------------------------------------------------+
|                    Compositor & Damage Pipeline                          |
|    (damage list, dirty rects, clip stack, present diagnostics, blend)     |
|                   [compositor.h / compositor.c]                          |
+--------------------------------------------------------------------------+
                                     |
                                     v
+--------------------------------------------------------------------------+
|                Hardware Layer (VirtIO-GPU 2D / Linear FB)                |
+--------------------------------------------------------------------------+
```

---

## 2. Decoupling Phases & Milestones

### Phase 2A: The Immediate Safe Foundation (Current Milestone)
1. **Unified Theme & Design Metrics**:
   - Extend [`theme.h`](file:///Users/mohamadjlo/Desktop/FirstOS/theme.h) to define `theme_metrics_t` (standard corner radii, titlebar height, control dimensions, scrollbar width, shadow depths).
   - Ensure `active_theme` and `active_metrics` are the authoritative source of truth.
2. **Unified Control & Widget Library (`gui_widgets.h`, `gui_widgets.c`)**:
   - Extract all 10 core controls from `gui.c`:
     - `draw_button`
     - `draw_checkbox`
     - `draw_radio`
     - `draw_slider`
     - `draw_scrollbar`
     - `draw_text_input`
     - `draw_progress_bar`
     - `draw_tooltip`
     - `draw_dialog`
     - `draw_toast`
     - Auxiliary drawing primitives (`draw_soft_box`, `outline`, `point_in_rect`)
   - Migrate widget state management (`gui_text_input_*`, `gui_progress_*`, `gui_dialog_*`, `gui_tooltip_*`) into `gui_widgets.c`.
3. **Compositor Damage & Clip Stack Hardening**:
   - Implement `fb_push_clip` and `fb_pop_clip` in `framebuffer.c` to prevent nested container clipping corruption.
   - Introduce `compositor_stats_t` in `compositor.c` to track damage area, full vs partial repaints, and VirtIO-GPU flush operations.

### Phase 2B: Desktop Shell Consolidation
- Reconcile `desktop.c` and `gui.c`:
  - Move wallpaper caching, background painting, and wallpaper format parsing into `desktop.c`.
  - Move `desktop_items[]`, `sync_desktop_icons_from_fs()`, icon sorting, and marquee selection into `desktop.c`.
  - Wire `desktop_init()`, `desktop_draw()`, and `desktop_handle_*` directly into `gui_draw()` and input dispatch.

### Phase 2C: Window Manager Extraction (`window_manager.c`)
- Extract window data structures (`app_windows[32]`, `z_order[32]`, active window pointer).
- Move snap engine, resize edge detection, window tab switching, and window titlebar dragging into `window_manager.c`.
- Provide clean API: `wm_init()`, `wm_draw_windows()`, `wm_handle_mouse()`, `wm_handle_key()`.

### Phase 2D: Menu & Popover Extraction (`gui_menu.c`)
- Extract the 1,500 lines of topbar menus (brand, app, file, edit, view), popovers (wifi, volume, clock, weather, fps), and notification center into a dedicated menu manager.

---

## 3. Safety Guarantees & Non-Breaking Boundaries

1. **Pixel-Perfect Visual Continuity**:
   - All extracted widgets and controls must retain their exact current dimensions, color calculations, and hover states.
2. **ABI & Global Symbols**:
   - Existing external entry points (`gui_init()`, `gui_draw()`, `gui_needs_redraw()`, `gui_mark_redraw()`, `point_in_rect()`, `outline()`, `draw_button()`) must remain available with identical signatures for other components (`settings.c`, `terminal.c`, `web_browser_app.c`).
3. **VirtIO-GPU Integrity**:
   - Zero modifications to the underlying VirtIO-GPU driver packet protocol or scanout bindings.
