# FirstOS Browser Reverification Plan (Phase 2 Baseline Documentation)

**Date**: 2026-10-11  
**Project**: `/Users/mohamadjlo/Desktop/FirstOS`  
**Purpose**: Rigorous visual, empirical, and automated reverification of the 5 defects previously observed in screen recording `Screen Recording 2026-10-11 at 18.43.42.mov`.

---

## 1. Targeted Defects and Applied Fixes

1. **Dark Canvas & Inverted Contrast Bug**
   - *Previous Defect*: Fallback color `0x242432` in `html5_script.c` produced dark backgrounds under dark text (`0x202124`), rendering pages black-on-black and illegible.
   - *Applied Fix*: Replaced `0x242432` fallback with existing element background or `0xFFFFFF`. Added `find_effective_bg()` with ITU-R BT.601 luminance calculation to boost dark text automatically if rendered over dark backgrounds.

2. **Arabic Text Mojibake & Letter Fragmentation**
   - *Previous Defect*: Arabic words appeared as detached punctuation dots and placeholder marks.
   - *Applied Fix*: Upgraded `arabic_render_glyph` in `text_engine.c` to draw continuous baseline connectivity and render distinctive letter families (Alif, Ba/Ta/Tha, Jim/Ha/Kha, Dal/Dhal, Ra/Zay, Sin/Shin, Meem, Lam, Waw, Ya).

3. **HTTP 429 Errors on Images**
   - *Previous Defect*: Subresources failed with HTTP 429 under outdated User-Agent string.
   - *Applied Fix*: Set truthful, standard User-Agent header: `Mozilla/5.0 (X11; FirstOS x86_64) FirstOS-WebBrowser/2.1` across `browser_network_service.c`, `http_client.c`, and `http_v11.c`. Rate-limited responses will be faithfully categorized as `HTTP_RATE_LIMITED`.

4. **Stuck Progress Bar on Single-Page Apps (SPA) / Chunked Responses**
   - *Previous Defect*: Indefinite loading loop on streaming responses without explicit Content-Length.
   - *Applied Fix*: Implemented RFC 7230 terminal chunk termination detection (`0\r\n\r\n`) and tightened consecutive empty poll threshold to 30 ticks in `browser_network_service.c`.

5. **Network Packet Bursts Starving Input & Compositor**
   - *Previous Defect*: High-rate packet reception starved PS/2 mouse cursor and UI redraw.
   - *Applied Fix*: Enforced 8-packet budget per `eth_poll()` tick in `eth.c` and ensured `ps2_poll()` runs before each polling iteration.

---

## 2. Test Fixtures and Live Scenarios

1. **Fixture 1 (`about:home` & Color Contrast)**: White-on-dark, dark-on-white, hyperlinks, and dynamic background mutations.
2. **Fixture 2 (Arabic & RTL Typography)**: Text containing mixed Arabic phrases, Alif, Dal, Ba, Sin, Waw, and numbers with right-to-left layout.
3. **Fixture 3 (Image Burst & HTTP Header Verification)**: Multiple image requests via proxy bridge verifying User-Agent and status codes.
4. **Fixture 4 (Chunked Transfer Stream)**: HTTP response with `Transfer-Encoding: chunked` and terminal `0\r\n\r\n`.
5. **Fixture 5 (Interactive Input Stress)**: Continuous PS/2 mouse motion and clicking during packet transfers.

---

## 3. Recording & Logging Strategy

- **Video File**: `/Users/mohamadjlo/Desktop/FirstOS/Screen Recording 2026-10-11 browser fixes reverification.mov`
- **Video Tools**: Captured via QMP continuous frame screendump sequence compiled into `.mov` via `ffmpeg` (H.264 / AAC).
- **Serial Log**: `/Users/mohamadjlo/Desktop/FirstOS/reports/browser_video_reverification_serial.log`
- **Build Log**: `/Users/mohamadjlo/Desktop/FirstOS/reports/browser_video_reverification_build.log`
- **Extracted Frames**: `/Users/mohamadjlo/Desktop/FirstOS/reports/browser_video_reverification_frames/`

---

## 4. Pass / Fail Criteria

| Item | PASS Criterion | PARTIAL Criterion | FAIL Criterion |
|---|---|---|---|
| **Contrast** | Text contrast ratio legible, no black-on-black, no accidental dark canvas | Text readable but minor contrast anomaly | Text invisible or dark-on-dark |
| **Arabic** | Continuous cursive baseline with identifiable letter shapes | Letters readable but missing contextual shaping | Broken scattered dots/bars |
| **Images/429** | Legitimate images loaded or graceful fallback icon; zero spurious 429 client bursts | Fallback icon displayed for edge rate limits | Crash, hang, or infinite retry loop |
| **Chunked SPA** | Terminal chunk detected, progress bar finishes, page marks ready | Content rendered but delayed transition | Infinite loading bar |
| **Input Fairness** | Cursor moves smoothly with no freezes or compositor starvation | Cursor responsive with slight visual jitter | Mouse freezes or clicks lost |
