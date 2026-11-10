# FirstOS Reusable Application Framework Plan (Phase 3)

**Date**: October 15, 2026  
**Engineer**: FirstOS Principal Systems & Developer Tooling Engineering  
**Scope**: `/Users/mohamadjlo/Desktop/FirstOS`  
**Vision**: Enable cohesive, unified, self-hosted application development from inside FirstOS.

---

## 1. Architectural Vision

FirstOS applications need to share consistent visual primitives, lifecycle management, shortcut patterns, and diagnostic consoles. Instead of having each application hand-code its own status bars, output text panes, file dialog wrappers, and shortcut parsers, the **FirstOS App Framework** provides an unopinionated, lightweight foundation.

```
+-------------------------------------------------------------------------+
|                              FirstOS Apps                               |
|       [App Studio]        [Terminal]        [Text Edit]     [Explorer]  |
+-------------------------------------------------------------------------+
                                     |
                                     v
+-------------------------------------------------------------------------+
|                         FirstOS App Framework                           |
|                      [app_framework.h / .c]                             |
|                                                                         |
|  * Status Bar Engine (app_status_bar_t, app_draw_status_bar)            |
|  * Output & Diagnostics Console (app_output_pane_t, scroll, clear, log) |
|  * Unified File Dialog Hooks (app_dialog_open_file, save_file)          |
|  * Common Shortcut Dispatch (Ctrl+S, Ctrl+O, Ctrl+W, F5, Shift+F5)     |
|  * Command Palette / Developer Tooling Bridge                          |
+-------------------------------------------------------------------------+
                                     |
                                     v
+-------------------------------------------------------------------------+
|                  GUI Control & Theme Primitives                         |
|         [gui_widgets.h]   [theme.h]   [framebuffer.h]                   |
+-------------------------------------------------------------------------+
```

---

## 2. Framework Component Specifications

### 2.1 Unified Status Bar Engine
- **Data Structure**:
  ```c
  typedef enum {
      APP_STATUS_INFO = 0,
      APP_STATUS_SUCCESS = 1,
      APP_STATUS_WARNING = 2,
      APP_STATUS_ERROR = 3
  } app_status_type_t;

  typedef struct {
      char text[64];
      char details[64];
      app_status_type_t type;
      int dirty;
  } app_status_bar_t;
  ```
- **Functionality**:
  - `app_status_bar_set(app_status_bar_t *bar, app_status_type_t type, const char *text, const char *details)`
  - `app_draw_status_bar(rect_t r, const app_status_bar_t *bar)`
  - Automatically selects harmonious theme colors (e.g. green accent for `SUCCESS`, amber for `WARNING`, red for `ERROR`, muted for `INFO`).

### 2.2 Reusable Output & Diagnostics Console (`app_output_pane_t`)
- **Problem Solved**: Replaces ad-hoc string buffers with an interactive, scrollable terminal-style pane suitable for build logs, compilation errors, and process output.
- **Data Structure**:
  ```c
  #define APP_OUTPUT_MAX_BYTES 8192
  #define APP_OUTPUT_MAX_LINES 256

  typedef struct {
      char buffer[APP_OUTPUT_MAX_BYTES];
      int length;
      int line_starts[APP_OUTPUT_MAX_LINES];
      int line_count;
      int scroll_y;
      int auto_scroll;
      char title[32];
  } app_output_pane_t;
  ```
- **Functionality**:
  - `app_output_pane_init(app_output_pane_t *pane, const char *title)`
  - `app_output_pane_append(app_output_pane_t *pane, const char *str)`
  - `app_output_pane_clear(app_output_pane_t *pane)`
  - `app_output_pane_scroll(app_output_pane_t *pane, int delta_lines)`
  - `app_output_pane_draw(rect_t r, const app_output_pane_t *pane)`

### 2.3 Common Keyboard Shortcut Dispatch
- **Centralized Action Mapping**:
  - `Ctrl + S`: `APP_ACTION_SAVE`
  - `Ctrl + O`: `APP_ACTION_OPEN`
  - `Ctrl + N`: `APP_ACTION_NEW`
  - `Ctrl + W`: `APP_ACTION_CLOSE`
  - `Ctrl + F`: `APP_ACTION_FIND`
  - `F5`: `APP_ACTION_BUILD_AND_RUN`
  - `Shift + F5`: `APP_ACTION_STOP`
  - `Ctrl + Shift + B`: `APP_ACTION_BUILD`
- Function: `int app_handle_common_shortcuts(unsigned char key, int ctrl, int shift)`

### 2.4 Terminal & App Studio Toolchain Bridge
- Add CLI commands to `terminal.c`:
  - `appstudio status`: Show active project name, template, build status, running PID.
  - `appstudio build`: Trigger in-system compilation of the active project and display output in terminal.
  - `appstudio run`: Spawn the compiled ELF executable in Ring 3 and report PID.
  - `appstudio stop`: Terminate the active process.
  - `appstudio log`: Dump recent compiler diagnostic lines to terminal.
  - `build [path]`: Shortcut to compile project at path.

---

## 3. Implementation Steps

1. **Step 1**: Write [`app_framework.h`](file:///Users/mohamadjlo/Desktop/FirstOS/app_framework.h) and [`app_framework.c`](file:///Users/mohamadjlo/Desktop/FirstOS/app_framework.c).
2. **Step 2**: Integrate `app_framework` into App Studio (`ide.c` and `gui.c`):
   - Replace the static logs rendering with `app_output_pane_t`.
   - Add status bar with `app_draw_status_bar()`.
   - Wire mouse wheel scroll to output pane.
   - Implement F5 (Build & Run), Shift+F5 (Stop), and Clear Log button.
3. **Step 3**: Integrate `appstudio` command suite into Terminal (`terminal.c`).
4. **Step 4**: Update `Makefile`, build, and test live in QEMU.
