# FirstOS Live Browser Development: Experiment Specification

**Stage**: Stage 5 - In-System Browser Focused Experiment  
**Target Subsystem**: Browser Application UI & Navigation Header (`web_browser_app.c`)  
**Date**: 2026-10-15  

---

## 1. Understanding (العارض والمطلوب)
During live runtime inspection of the FirstOS Web Browser (`live_browser_dev_baseline.png`):
- The rightmost section of the navigation toolbar features a plain, static green button (`0x30D158`) with the text "WEB".
- This button lacks dynamic state awareness (it looks identical whether the browser is idle, navigating, loading, or encountered an error).
- The transition between page loads and page commits lacks a dedicated top-edge progressive indicator, making network latency or chunked transfer state ambiguous from the toolbar alone.

## 2. Suspected Layers (الطبقات المعنية)
- **Primary**: Browser Application Layer (`web_browser_app.c`) — Toolbar geometry, button drawing, state rendering, and `WEB_BROWSER_HOME_PAGE` template.
- **Secondary**: GUI Rendering & Compositor Subsystem (`framebuffer.c`, `font.c`) — Capsule pill drawing (`fb_round_rect`, `fb_round_rect_border`), alpha blending, and line clipping.

## 3. Evidence (الأدلة)
- Baseline frame `reports/live_browser_dev_baseline.png` reveals `brand_x = go_x + 62` drawing an 86px wide rectangle with 3-character text "WEB", resulting in ~50px of unused space on the right side and zero feedback on document readiness.
- Serial logs in `reports/live_browser_dev_serial.log` confirm discrete lifecycle events (`[nav] NAV_BEGIN`, `[nav] NAV_READY`, `[browser] state=READY`), but the visual toolbar does not reflect these state transitions.

## 4. Hypothesis (الفرضية)
By upgrading the toolbar header to:
1. A **Status Capsule Pill** (`0x24232C` base with `0x383740` stroke) featuring an active tri-state LED indicator dot:
   - `0x30D158` (Emerald Green) -> Ready / Secure
   - `0x007AFF` (Cyan Blue) -> Loading / Connecting
   - `0xFF3B30` (Coral Red) -> Error / Disconnected
2. An **Active Top-Edge Accent Line** (`toolbar_y + 36`):
   - Dynamic fill during loading transitions.
   - Crisp 1px emerald accent line when document is ready.
3. Enhanced **Home Page Engine Tag**:
   - Explicit version tag on `about:home` identifying `Antigravity Engine 2.2 · Live Dev Session`.

We achieve an immediately observable visual enhancement in live QEMU execution with zero regressions to page layout or window management.

## 5. In-System vs Host-Assisted Demarcation
- **In-System**:
  - The developer inspects source trees via FirstOS Terminal (`ls /Projects`, `cat /Projects/...`).
  - FirstOS App Studio (`ide.c`) manages project state, builds, and launches userspace Ring 3 child processes (`process_spawn`).
  - Window Manager and Compositor run fully native inside FirstOS at 1024x768.
- **Host-Assisted**:
  - Native GCC is not yet ported to run inside FirstOS Ring 3; compiling C kernel objects relies on `i686-elf-gcc` cross-compiler on the host.
  - Repackaging bootable ISOs uses host `xorriso`/`grub-mkrescue`.
  - Frame extraction is captured non-invasively through QEMU's QMP socket.

## 6. Action Plan
1. Edit `web_browser_app.c` to update `web_browser_toolbar_geometry` (`brand_w = 96`).
2. Update `web_browser_app_draw` to render the dynamic Status Capsule Pill and progress accent line.
3. Add the engine badge to `WEB_BROWSER_HOME_PAGE`.
4. Compile with host toolchain and repackage `firstos.iso`.
5. Execute live session via `tools/live_browser_dev_session.py`.
6. Capture `reports/live_browser_dev_after.png` and verify visual state.
