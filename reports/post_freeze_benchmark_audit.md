# FirstOS Post-Freeze Benchmark Audit & Methodological Fix Report

**Date**: 2026-10-15  
**Stage**: Step 2 — Benchmark Methodology & Metrics Correction  
**Workspace**: `/Users/mohamadjlo/Desktop/FirstOS`  
**Status**: **AUDITED, CORRECTED & VERIFIED**

---

### 1. Executive Summary & Defect Discovery

During the post-freeze graphics stabilization audit, a critical statistical flaw was uncovered in the evaluation scripts (`tools/test_gfx_stabilization_bench.py`):
- **Unsorted Array Indexing in Percentiles**: The `percentile(data, p)` function previously calculated the percentile index $k = (N - 1) \times (p / 100.0)$ and indexed directly into chronological `data` without sorting. In empirical runs, this caused mathematical violations where $p_{99} < p_{95}$ or $p_{95} < p_{50}$ (e.g., Idle Desktop reported $p_{50} = 13.70\text{ ms}$, $p_{95} = 15.47\text{ ms}$, $p_{99} = 14.59\text{ ms}$).
- **Lack of Warm-up Sample Exclusion**: Initial samples often carried pipeline warmup or QMP socket handshake transients.
- **Metric Conflation**: QMP frame screendump roundtrip latency (host capture) was labeled as "Present FPS", without separating input event dispatch rates from frame presentation.
- **Single-Sample Scenarios**: Scenarios 8 and 9 were executing only a single capture sample, giving zero variance and masking jitter.

---

### 2. Methodological Corrections Implemented

#### A. Strict Sorted Linear Interpolation
The percentile calculation now strictly sorts the dataset:
$$\text{sorted\_data} = \text{sort}(\text{clean\_data})$$
$$k = (n - 1) \times \frac{p}{100}, \quad f = \lfloor k \rfloor, \quad c = \lceil k \rceil$$
$$\text{percentile} = \text{sorted\_data}[f] \times (c - k) + \text{sorted\_data}[c] \times (k - f)$$
This guarantees the mathematical invariant:
$$\min \le p_{50} \le p_{95} \le p_{99} \le \max$$

#### B. Unified Statistical Distribution (`compute_stats`)
A comprehensive function `compute_stats(data, warmup_count=2)` was introduced to compute:
- Sample Count ($N$ clean and $N$ raw)
- Range ($\min$, $\max$)
- Central Tendency ($\text{mean}$, $p_{50}/\text{median}$)
- Tail Latency ($p_{95}$, $p_{99}$)
- Dispersion ($\text{stddev} = \sqrt{\frac{1}{n} \sum (x_i - \mu)^2}$)

#### C. Metric Demarcation & Unit Standardization
- **Capture Latency / Frame-time**: Measured in milliseconds ($\text{ms}$).
- **Equivalent FPS**: $1000.0 / p_{50}$ (theoretical capture throughput).
- **Input Dispatch Rate**: Measured in Hertz ($\text{Hz}$) based on timestamp deltas over event counts.
- **Render / Present Cycles**: Monitored independently from host capture.

---

### 3. Empirical Before / After Comparison

| Scenario | Old Calculation (Unsorted / Flawed) | New Corrected Calculation (`compute_stats`) | Mathematical Validity |
|---|---|---|---|
| **1. Idle Desktop** | $p_{50}=13.70\text{ms}, p_{95}=15.47\text{ms}, p_{99}=14.59\text{ms}$ | **$p_{50}=11.70\text{ms}, p_{95}=18.51\text{ms}, p_{99}=30.29\text{ms}$**<br>$\min=8.89\text{ms}, \text{mean}=12.84\text{ms}, \max=33.23\text{ms}, \sigma=5.29\text{ms}$ | **$\min \le p_{50} \le p_{95} \le p_{99} \le \max$** (PASSED) |
| **2. Mouse Movement** | $p_{50}=4.18\text{ms}, p_{95}=4.28\text{ms}, p_{99}=4.07\text{ms}$ | **$p_{50}=4.00\text{ms}, p_{95}=4.69\text{ms}, p_{99}=4.75\text{ms}$**<br>$\min=3.31\text{ms}, \text{mean}=4.04\text{ms}, \max=4.77\text{ms}, \sigma=0.39\text{ms}$<br>Dispatch: $44.3\text{ Hz}$ | **$\min \le p_{50} \le p_{95} \le p_{99} \le \max$** (PASSED) |
| **3. Window Drag** | $p_{50}=5.01\text{ms}, p_{95}=4.85\text{ms}, p_{99}=4.52\text{ms}$ | **$p_{50}=4.04\text{ms}, p_{95}=5.65\text{ms}, p_{99}=5.82\text{ms}$**<br>$\min=3.73\text{ms}, \text{mean}=4.30\text{ms}, \max=5.86\text{ms}, \sigma=0.64\text{ms}$<br>Dispatch: $35.2\text{ Hz}$ | **$\min \le p_{50} \le p_{95} \le p_{99} \le \max$** (PASSED) |
| **4. Window Resize** | $p_{50}=5.25\text{ms}, p_{95}=5.07\text{ms}, p_{99}=5.39\text{ms}$ | **$p_{50}=7.07\text{ms}, p_{95}=11.68\text{ms}, p_{99}=11.94\text{ms}$**<br>$\min=4.18\text{ms}, \text{mean}=7.44\text{ms}, \max=12.00\text{ms}, \sigma=2.43\text{ms}$ | **$\min \le p_{50} \le p_{95} \le p_{99} \le \max$** (PASSED) |
| **5. Text Input** | $p_{50}=5.12\text{ms}, p_{95}=5.30\text{ms}, p_{99}=5.17\text{ms}$ | **$p_{50}=5.04\text{ms}, p_{95}=5.51\text{ms}, p_{99}=5.60\text{ms}$**<br>$\min=4.67\text{ms}, \text{mean}=5.05\text{ms}, \max=5.62\text{ms}, \sigma=0.29\text{ms}$ | **$\min \le p_{50} \le p_{95} \le p_{99} \le \max$** (PASSED) |
| **6. App Switching** | $p_{50}=5.11\text{ms}, p_{95}=5.47\text{ms}, p_{99}=5.50\text{ms}$ | **$p_{50}=3.88\text{ms}, p_{95}=4.95\text{ms}, p_{99}=5.07\text{ms}$**<br>$\min=3.13\text{ms}, \text{mean}=4.01\text{ms}, \max=5.11\text{ms}, \sigma=0.63\text{ms}$ | **$\min \le p_{50} \le p_{95} \le p_{99} \le \max$** (PASSED) |
| **7. Browser Rendering** | $p_{50}=12.77\text{ms}, p_{95}=26.03\text{ms}, p_{99}=29.13\text{ms}$ | **$p_{50}=12.89\text{ms}, p_{95}=14.81\text{ms}, p_{99}=14.88\text{ms}$**<br>$\min=6.44\text{ms}, \text{mean}=11.78\text{ms}, \max=14.90\text{ms}, \sigma=2.94\text{ms}$ | **$\min \le p_{50} \le p_{95} \le p_{99} \le \max$** (PASSED) |
| **8. Wallpaper/Theme** | $1\text{ sample: } 17.07\text{ms}$ (no variance) | **$p_{50}=12.88\text{ms}, p_{95}=14.12\text{ms}, p_{99}=14.26\text{ms}$**<br>$\min=6.63\text{ms}, \text{mean}=11.46\text{ms}, \max=14.30\text{ms}, \sigma=3.10\text{ms}$ | Multi-sample robust verification |
| **9. Hardware Fallback**| $1\text{ sample: } 7.09\text{ms}$ (no variance) | **$p_{50}=11.46\text{ms}, p_{95}=16.84\text{ms}, p_{99}=16.93\text{ms}$**<br>$\min=7.23\text{ms}, \text{mean}=12.39\text{ms}, \max=16.95\text{ms}, \sigma=3.64\text{ms}$ | Multi-sample robust verification |

---

### 4. Regression Gate Validation
Running the official verification pipeline:
```bash
make test-gfx-gate
```
Output:
```
========================================
     REGRESSION GATE EVALUATION         
========================================
[PASS] All 9 scenarios met performance, latency, and stability thresholds!
[PASS] Scanout resolution: 1024x768x32bpp, Pixel Fill: 99.98%
[PASS] Idle frame latency: 11.70ms, Mouse active latency: 4.00ms
========================================
[gate] >>> REGRESSION GATE PASSED! <<<
[gate] Graphics stack verified clean, responsive, and stable.
```

### 5. Hot Path Integrity
Zero lines of code in the frozen hot path (`gui_draw`, `draw_cursor`, `fb_present`, `virtio_gpu_2d_present`, `fb_mark_dirty_rect`) were touched or modified. Only the testing instrumentation in `tools/test_gfx_stabilization_bench.py` was refined.
