# FirstOS GUI Video Analysis & Metadata Report (`gui_video_metadata.md`)

**Target File:** `gui.mov`  
**Analysis Date:** 2026-10-16  
**Analyzed With:** `ffprobe` (version 9.0.2), `ffmpeg`, and Python `PIL`/`numpy`

---

## 1. Video Container & Stream Metadata

| Metric | Measured Value | Analysis / Significance |
|---|---|---|
| **Container Format** | QuickTime / MOV (`major_brand: qt`) | macOS Native Screen Recording container |
| **Capture Utility** | `com.apple.quicktime.author`: **`ReplayKitRecording`** | Native macOS Screen Capture framework |
| **Recording Intent** | `com.apple.quicktime.full-frame-rate-playback-intent`: `1` | Variable frame rate (VFR) capture |
| **Duration** | **17.155 seconds** (10,293 timebase units) | Duration from startup to QEMU shutdown |
| **File Size** | **16,266,408 bytes** (~15.51 MiB) | Bitrate: ~7,585 kbps |
| **Video Codec** | **H.264 / AVC** (`avc1.4d0034`, Main Profile, Level 5.2) | Standard Apple VideoToolbox hardware encoder |
| **Pixel Format / Color** | `yuv420p`, progressive, `bt709`, range `tv` | Video-range YUV420 subsampling |
| **Video Canvas Resolution** | **3274 x 1834** | High-DPI Retina screen recording |
| **Nominal Frame Rate** | **60.0 fps** (`r_frame_rate: 60/1`, `time_base: 1/600`) | Nominal display sync rate |
| **Total Frames Encoded** | **266 frames** | Over 17.155 seconds |
| **Actual Average Frame Rate** | **15.51 fps** (`avg_frame_rate: 53200/3431`) | **Variable Frame Rate (VFR)** with heavy capture throttling |

---

## 2. Keyframe & Temporal Interval Analysis

### 2.1 Keyframe Distribution
There are **5 IDR keyframes** (I-frames) in the 17.15s stream:

| Keyframe Index | PTS Time | Packet Size (Bytes) | Pict Type | Notes |
|---|---|---|---|---|
| **#0 (Frame 0)** | `0.000000s` | 428,766 | I-frame | Desktop initialized, System menu open |
| **#1 (Frame 57)** | `4.151667s` | 1,781,779 | I-frame | Interacting with top bar & desktop |
| **#2 (Frame 114)** | `9.186667s` | 1,337,627 | I-frame | File Explorer window open |
| **#3 (Frame 171)** | `12.986667s` | 1,241,850 | I-frame | Explorer window interaction & Dock hover |
| **#4 (Frame 231)** | `15.571667s` | 262,781 | I-frame | QEMU shutdown, macOS Desktop terminal visible |

### 2.2 Frame Interval & Jitter Analysis
- **Minimum Interval:** `16.67 ms` (matches 60 Hz nominal cadence).
- **Maximum Interval:** `551.67 ms` (over half a second gap between frames).
- **Mean Interval:** `64.61 ms` (~15.5 FPS).
- **Interval Distribution:**
  - `~16.6 ms` (60 FPS frames): **16 frames**
  - `~33.3 ms` (30 FPS frames): **123 frames**
  - `> 50.0 ms` (sub-20 FPS latency): **101 intervals**
  - `> 100.0 ms` (severe capture stall / dropped frame): **33 intervals**

> [!NOTE]
> `ReplayKitRecording` on macOS uses a dynamic VFR (variable frame rate) capture engine. When screen regions do not change frequently or the host GPU is under load, ReplayKit drops capture frames or throttles to 15-30 FPS, creating artificial video stutter that does not reflect guest kernel render pacing.

---

## 3. Extracted Frame Samples & Visual Chronology

Samples were extracted at exact timestamps into [`reports/gui_video_frame_samples/`](file:///Users/mohamadjlo/Desktop/FirstOS/reports/gui_video_frame_samples/):

| Timestamp | Frame File | Visual Contents & Observed Artifacts |
|---|---|---|
| **0.0s** | [`frame_0s.png`](file:///Users/mohamadjlo/Desktop/FirstOS/reports/gui_video_frame_samples/frame_0s.png) | • Left: macOS Terminal with `make run` serial log.<br>• Right: QEMU window (2386x1780).<br>• FirstOS Desktop active (`FPS: 02`).<br>• **Artifact 1 (Ghost Cursor):** Frozen white cursor arrow visible in the mist (`x=699, y=445`).<br>• **Artifact 2 (Dual Cursor):** Moving second cursor at `x=455, y=565`.<br>• **Artifact 3 (Stale Tooltips):** 8 Dock tooltips simultaneously visible in a horizontal line.<br>• **Artifact 4 (Z-Order Underlap):** "Projects" and "picture.bmp" desktop icons render on top of the open System menu. |
| **1.0s** | [`frame_1s.png`](file:///Users/mohamadjlo/Desktop/FirstOS/reports/gui_video_frame_samples/frame_1s.png) | • Cursor moved to `x=500, y=460`.<br>• **Ghost cursor at `x=699, y=445` remains frozen in place.**<br>• Dock tooltips still displayed. |
| **2.0s** | [`frame_2s.png`](file:///Users/mohamadjlo/Desktop/FirstOS/reports/gui_video_frame_samples/frame_2s.png) | • Cursor moved to menu bar (`x=278, y=170`).<br>• **Ghost cursor at `x=699, y=445` remains frozen in place.**<br>• Audio click event in serial log (`[sound] event: CLICK`). |
| **4.0s** | [`frame_4s.png`](file:///Users/mohamadjlo/Desktop/FirstOS/reports/gui_video_frame_samples/frame_4s.png) | • Top bar menu shifted to "Settings".<br>• **Ghost cursor at `x=699, y=445` remains frozen in place.**<br>• Menu dropdown shows faded transparent artifacts. |
| **8.0s** | [`frame_8s.png`](file:///Users/mohamadjlo/Desktop/FirstOS/reports/gui_video_frame_samples/frame_8s.png) | • File Explorer window opened (`1024x768` desktop area).<br>• Full redraw cleared the ghost cursor in the mist.<br>• Explorer shows "FirstOS HD", "Desktop", "Audio", "Wallpapers".<br>• Cursor positioned over Explorer Dock icon with single tooltip. |
| **12.0s** | [`frame_12s.png`](file:///Users/mohamadjlo/Desktop/FirstOS/reports/gui_video_frame_samples/frame_12s.png) | • Explorer window remains active.<br>• Cursor moving across dock (`x=383, y=693`).<br>• **Multiple dock tooltips ("Calculator", "Task Mgr", "IDE", "Media", "Web Browser") stick and accumulate.** |
| **16.0s** | [`frame_16s.png`](file:///Users/mohamadjlo/Desktop/FirstOS/reports/gui_video_frame_samples/frame_16s.png) | • QEMU terminated with `[PWR] Shutdown`.<br>• Terminal prompt returned.<br>• macOS Screenshot toolbar visible at screen bottom. |

---

## 4. Window Geometry & Scaling Breakdown

- **Screen Recording Dimensions:** `3274 x 1834` (MacBook Pro Retina screen).
- **macOS Terminal Window:** `x = 0..887`, `y = 0..1833` (`888 x 1834`).
- **QEMU Host Window Canvas:** `x = 888..3273`, `y = 54..1833` (`2386 x 1780`).
- **FirstOS Guest Framebuffer:** **`1024 x 768`** (Aspect ratio: `1.333:1`).
- **QEMU Cocoa Scaling Factor:**
  - Horizontal scale: `2386 / 1024 = 2.330x`
  - Vertical scale: `1780 / 768 = 2.317x`
- **Result:** QEMU Cocoa is performing real-time **non-integer bilinear interpolation** (`zoom-to-fit=on`) from `1024x768` to `2386x1780`. This introduces subtle blur around 1px font lines, but is strictly a display-scaling effect, not a framebuffer corruption.
