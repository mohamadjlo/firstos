# FirstOS Graphics Performance Metrics & Telemetry Schema v1.0

**Date**: 2026-10-15  
**Stage**: Step 5 — Formal Unified Metrics Schema Definition  
**Workspace**: `/Users/mohamadjlo/Desktop/FirstOS`  
**Output Target**: `reports/gfx_benchmark_latest.json`  
**Status**: **FORMALIZED & VALIDATED**

---

### 1. Specification Overview
This document defines the formal JSON telemetry schema used by FirstOS graphics performance benchmarks and continuous regression gates. It unifies host-side frame capture latency, input dispatch throughput, statistical distribution parameters, and guest kernel hardware presentation telemetry into a single, parseable schema.

---

### 2. JSON Schema Definition (Draft 2020-12)

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "FirstOSGraphicsTelemetry",
  "type": "object",
  "required": ["schema_version", "timestamp", "environment", "scenarios", "kernel_telemetry"],
  "properties": {
    "schema_version": {
      "type": "string",
      "const": "1.0.0"
    },
    "timestamp": {
      "type": "string",
      "format": "date-time"
    },
    "environment": {
      "type": "object",
      "required": ["scanout_width", "scanout_height", "bpp", "pixel_fill_pct"],
      "properties": {
        "scanout_width": { "type": "integer", "minimum": 640 },
        "scanout_height": { "type": "integer", "minimum": 480 },
        "bpp": { "type": "integer", "enum": [16, 24, 32] },
        "pixel_fill_pct": { "type": "number", "minimum": 0.0, "maximum": 100.0 }
      }
    },
    "scenarios": {
      "type": "object",
      "required": [
        "idle_desktop",
        "mouse_movement",
        "window_drag",
        "window_resize",
        "text_input",
        "app_switching",
        "browser_rendering",
        "wallpaper_theme",
        "cursor_fallback"
      ],
      "additionalProperties": {
        "type": "object",
        "required": ["stats", "p50", "p95", "p99"],
        "properties": {
          "stats": {
            "type": "object",
            "required": ["count", "raw_count", "min", "max", "mean", "stddev", "p50", "p95", "p99"],
            "properties": {
              "count": { "type": "integer", "minimum": 1 },
              "raw_count": { "type": "integer", "minimum": 1 },
              "min": { "type": "number", "minimum": 0.0 },
              "max": { "type": "number", "minimum": 0.0 },
              "mean": { "type": "number", "minimum": 0.0 },
              "stddev": { "type": "number", "minimum": 0.0 },
              "p50": { "type": "number", "minimum": 0.0 },
              "p95": { "type": "number", "minimum": 0.0 },
              "p99": { "type": "number", "minimum": 0.0 }
            }
          },
          "p50": { "type": "number" },
          "p95": { "type": "number" },
          "p99": { "type": "number" },
          "fps": { "type": "number" },
          "dispatch_hz": { "type": "number" }
        }
      }
    },
    "kernel_telemetry": {
      "type": "object",
      "required": [
        "unhandled_timeouts",
        "device_failures",
        "full_presents",
        "partial_presents",
        "cursor_only_presents",
        "virtio_batched_submits",
        "last_damage_w",
        "last_damage_h"
      ],
      "properties": {
        "unhandled_timeouts": { "type": "integer", "minimum": 0 },
        "device_failures": { "type": "integer", "minimum": 0 },
        "full_presents": { "type": "integer", "minimum": 0 },
        "partial_presents": { "type": "integer", "minimum": 0 },
        "cursor_only_presents": { "type": "integer", "minimum": 0 },
        "virtio_batched_submits": { "type": "integer", "minimum": 0 },
        "last_damage_w": { "type": "integer", "minimum": 0 },
        "last_damage_h": { "type": "integer", "minimum": 0 }
      }
    }
  }
}
```

---

### 3. Metric Fields & Units Reference Table

| Metric Field | Unit | Mathematical Definition / Source | Threshold / Gate Rule |
|---|---|---|---|
| `scanout_width` | Pixels | Horizontal resolution read from QEMU scanout | Equal to $1024$ |
| `scanout_height` | Pixels | Vertical resolution read from QEMU scanout | Equal to $768$ |
| `pixel_fill_pct` | Percent ($\%$) | $\frac{\text{non-zero pixels}}{W \times H} \times 100$ | $\ge 99.5\%$ (Fail if $< 99.5\%$) |
| `min` | Milliseconds ($\text{ms}$) | Minimum value in sorted sample array ($s_0$) | None |
| `p50` | Milliseconds ($\text{ms}$) | Median interpolated value at $50^{\text{th}}$ percentile | $\le 25.0\text{ ms}$ |
| `p95` | Milliseconds ($\text{ms}$) | Tail latency at $95^{\text{th}}$ percentile | $\le 35.0\text{ ms}$ |
| `p99` | Milliseconds ($\text{ms}$) | Peak tail latency at $99^{\text{th}}$ percentile | $\le 50.0\text{ ms}$ (Regression Gate Fail if $> 50$) |
| `max` | Milliseconds ($\text{ms}$) | Maximum value in sorted sample array ($s_{n-1}$) | None |
| `mean` | Milliseconds ($\text{ms}$) | Arithmetic mean $\frac{1}{n} \sum x_i$ | Informational |
| `stddev` | Milliseconds ($\text{ms}$) | Sample standard deviation $\sqrt{\frac{1}{n}\sum (x_i - \mu)^2}$ | Low jitter verification |
| `dispatch_hz` | Hertz ($\text{Hz}$) | Event injection rate $\frac{N_{\text{events}}}{\Delta t_{\text{duration}}}$ | Measured during input scenarios |
| `fps` | Frames / Sec | Equivalent capture throughput $\frac{1000}{p_{50}}$ | Theoretical capture bound |
| `unhandled_timeouts` | Count | Kernel timeout log detection | Equal to $0$ (Fail if $> 0$) |
| `device_failures` | Count | `STATUS_FAILED` hardware register detections | Equal to $0$ (Fail if $> 0$) |

---

### 4. Mathematical Consistency Invariants
Every emitted benchmark dataset must satisfy:
1. **Sorted Monotonicity**:
   $$\min \le p_{50} \le p_{95} \le p_{99} \le \max$$
2. **Warm-up Filtration**:
   $$\text{count} = \text{raw\_count} - \text{warmup\_count} \quad (\text{if raw\_count} > \text{warmup\_count})$$
3. **Variance Positivity**:
   $$\text{stddev} \ge 0.0$$

---

### 5. Automated Validation & Storage
Every execution of `make test-gfx-gate` or `python3 tools/test_gfx_stabilization_bench.py` automatically updates:
```
reports/gfx_benchmark_latest.json
```
This artifact allows automated continuous integration (CI) systems to track performance regressions over git commits.
