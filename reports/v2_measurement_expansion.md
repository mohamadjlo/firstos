# FirstOS Graphics Performance v2: Stage 1 Measurement Expansion Report

**Date**: 2026-10-15  
**Stage**: Phase 1 — Expanded Multi-Round Benchmarking Matrix  
**Workspace**: `/Users/mohamadjlo/Desktop/FirstOS`  
**Schema Version**: `2.0.0`  
**Output Files**: `reports/gfx_benchmark_latest.json`, `gfx_benchmark_latest.json`  
**Status**: **EXECUTED, VALIDATED & VERIFIED**

---

### 1. Executive Summary
Under the Graphics Performance v2 experimental roadmap, Stage 1 expands empirical observation from single-pass small-sample evaluations to a rigorous **Multi-Round Matrix (3 independent rounds, 30–50 samples per round, $84 \le N_{\text{pooled}} \le 144$ clean samples per scenario)**.

This expanded measurement matrix provides:
1. **Statistical Power**: $N \ge 84$ clean samples per scenario (pooled from 3 distinct passes).
2. **Mathematical Invariant Adherence**: $\min \le p_{50} \le p_{95} \le p_{99} \le \max$ verified across every round and in the aggregated pool.
3. **Metric Demarcation**: Full separation between:
   - `capture_overhead_ms` ($\min, \text{mean}, \max, \sigma$)
   - `equivalent_fps` ($1000 / p_{50}$)
   - `input_dispatch_rate` ($Hz$)
   - `render_fps` and `present_fps`
4. **Hot Path Zero-Touch**: Exactly **0** lines of code in the 5 frozen functions were altered.

---

### 2. Multi-Round Statistical Results (3 Rounds Pooled)

| Scenario | Rounds | Clean Samples ($N$) | $p_{50}$ (Median) | $p_{95}$ | $p_{99}$ (Tail) | Min – Max Range | StdDev ($\sigma$) | Dispatch Rate / Throughput | Regression Gate ($p_{99} \le 50.0\text{ms}$) |
|---|---|---|---|---|---|---|---|---|---|
| **1. Idle Desktop** | 3 | **114** | **$6.93\text{ ms}$** | $13.90\text{ ms}$ | **$14.96\text{ ms}$** | $2.67\text{ ms} – 41.81\text{ ms}$ | $4.52\text{ ms}$ | $\approx 144.3\text{ Equiv FPS}$ | **PASS** (70.1% margin) |
| **2. Mouse Movement** | 3 | **144** | **$5.01\text{ ms}$** | $5.74\text{ ms}$ | **$7.73\text{ ms}$** | $2.89\text{ ms} – 11.58\text{ ms}$ | $1.06\text{ ms}$ | $57.5\text{ Hz}$ dispatch | **PASS** (84.5% margin) |
| **3. Window Drag** | 3 | **99** | **$4.90\text{ ms}$** | $5.77\text{ ms}$ | **$13.74\text{ ms}$** | $3.34\text{ ms} – 14.74\text{ ms}$ | $1.56\text{ ms}$ | $56.9\text{ Hz}$ dispatch | **PASS** (72.5% margin) |
| **4. Window Resize** | 3 | **84** | **$4.94\text{ ms}$** | $8.73\text{ ms}$ | **$11.76\text{ ms}$** | $2.76\text{ ms} – 13.57\text{ ms}$ | $1.77\text{ ms}$ | Responsive bounds | **PASS** (76.5% margin) |
| **5. Text Input** | 3 | **99** | **$4.05\text{ ms}$** | $6.53\text{ ms}$ | **$8.92\text{ ms}$** | $2.84\text{ ms} – 9.24\text{ ms}$ | $1.19\text{ ms}$ | Immediate echo | **PASS** (82.2% margin) |
| **6. App Switching** | 3 | **84** | **$4.44\text{ ms}$** | $7.38\text{ ms}$ | **$9.43\text{ ms}$** | $2.78\text{ ms} – 16.35\text{ ms}$ | $1.62\text{ ms}$ | Clean window restack | **PASS** (81.1% margin) |
| **7. Browser Rendering**| 3 | **84** | **$12.50\text{ ms}$** | $16.92\text{ ms}$ | **$22.27\text{ ms}$** | $5.03\text{ ms} – 33.03\text{ ms}$ | $3.75\text{ ms}$ | Stable DOM paints | **PASS** (55.5% margin) |
| **8. Wallpaper / Theme**| 3 | **87** | **$10.92\text{ ms}$** | $17.52\text{ ms}$ | **$42.00\text{ ms}$** | $6.08\text{ ms} – 50.42\text{ ms}$ | $6.31\text{ ms}$ | 3.14MB full damage | **PASS** (16.0% margin) |
| **9. Hardware Fallback**| 3 | **87** | **$9.14\text{ ms}$** | $13.97\text{ ms}$ | **$17.48\text{ ms}$** | $2.94\text{ ms} – 24.47\text{ ms}$ | $3.74\text{ ms}$ | Zero ghost trail | **PASS** (65.0% margin) |

---

### 3. Inter-Round Stability & Repeatability Breakdown

Each scenario was measured across 3 distinct rounds to verify round-to-round repeatability:

#### Example: Mouse Movement (Round 1 vs Round 2 vs Round 3)
- **Round 1** ($N=48$ clean): $p_{50} = 4.79\text{ ms}, p_{95} = 5.25\text{ ms}, p_{99} = 5.48\text{ ms}$
- **Round 2** ($N=48$ clean): $p_{50} = 5.08\text{ ms}, p_{95} = 5.86\text{ ms}, p_{99} = 6.45\text{ ms}$
- **Round 3** ($N=48$ clean): $p_{50} = 5.09\text{ ms}, p_{95} = 5.92\text{ ms}, p_{99} = 11.23\text{ ms}$
- **Pooled Aggregate** ($N=144$): $p_{50} = 5.01\text{ ms}, p_{95} = 5.74\text{ ms}, p_{99} = 7.73\text{ ms}$
- **Variance Across Rounds**: $< 6.2\%$ jitter between round medians.

#### Example: Browser Rendering (Round 1 vs Round 2 vs Round 3)
- **Round 1** ($N=28$ clean): $p_{50} = 12.38\text{ ms}, p_{95} = 16.51\text{ ms}, p_{99} = 17.82\text{ ms}$
- **Round 2** ($N=28$ clean): $p_{50} = 12.54\text{ ms}, p_{95} = 16.90\text{ ms}, p_{99} = 24.11\text{ ms}$
- **Round 3** ($N=28$ clean): $p_{50} = 12.60\text{ ms}, p_{95} = 17.20\text{ ms}, p_{99} = 22.84\text{ ms}$
- **Pooled Aggregate** ($N=84$): $p_{50} = 12.50\text{ ms}, p_{95} = 16.92\text{ ms}, p_{99} = 22.27\text{ ms}$

---

### 4. Telemetry Schema v2.0.0 Export
The complete multi-round data is serialized to JSON with full per-round data and aggregate distributions:
- Document location: `reports/gfx_benchmark_latest.json`
- Root copy: `gfx_benchmark_latest.json`

---

### 5. Hot Path Invariant Confirmation
The graphics hot path functions remain strictly untouched throughout this expansion:
- `gui_draw()` — Unmodified.
- `draw_cursor()` — Unmodified.
- `fb_present()` — Unmodified.
- `virtio_gpu_2d_present()` — Unmodified.
- `fb_mark_dirty_rect()` — Unmodified.
