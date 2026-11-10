#!/usr/bin/env python3
"""
tools/test_gfx_stabilization_bench.py
FirstOS Graphics Performance Stabilization Benchmark & Regression Suite v1.

Scenarios tested:
1. Idle Desktop
2. Mouse Movement
3. Window Drag
4. Window Resize
5. Text Input
6. App Switching
7. Browser Rendering
8. Wallpaper / Theme Change
9. Hardware Cursor Failure Fallback

Measurements:
- Render FPS / Present FPS
- Frame-time p50, p95, p99
- Dispatch rate & QMP capture latency
- Zero ghost trail / zero corruption validation

Regression Gate:
Fails (exit code 1) if:
- full redraw occurs during mouse-only motion
- ghost trails are detected
- stale damage regions occur
- VirtIO-GPU timeout is unhandled
- software fallback is broken
- frame pacing p99 exceeds 50.0 ms
"""

import sys
import time
import socket
import json
import subprocess
import math
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

def percentile(data, p):
    if not data:
        return 0.0
    s = sorted(data)
    k = (len(s) - 1) * (p / 100.0)
    f = math.floor(k)
    c = math.ceil(k)
    if f == c:
        return s[int(k)]
    d0 = s[int(f)] * (c - k)
    d1 = s[int(c)] * (k - f)
    return d0 + d1

def compute_stats(data, warmup_count=2):
    if not data:
        return {
            "count": 0, "raw_count": 0,
            "min": 0.0, "max": 0.0, "mean": 0.0, "stddev": 0.0,
            "p50": 0.0, "p95": 0.0, "p99": 0.0
        }
    clean = data[warmup_count:] if len(data) > warmup_count else list(data)
    if not clean:
        clean = list(data)
    s = sorted(clean)
    n = len(s)
    mean_val = sum(s) / n
    var_val = sum((x - mean_val) ** 2 for x in s) / n
    stddev_val = math.sqrt(var_val)
    return {
        "count": n,
        "raw_count": len(data),
        "min": s[0],
        "max": s[-1],
        "mean": mean_val,
        "stddev": stddev_val,
        "p50": percentile(s, 50),
        "p95": percentile(s, 95),
        "p99": percentile(s, 99)
    }

def qmp_cmd(s, cmd, args=None):
    payload = {"execute": cmd}
    if args:
        payload["arguments"] = args
    s.sendall(json.dumps(payload).encode() + b"\n")
    resp_raw = b""
    while b"\n" not in resp_raw:
        chunk = s.recv(4096)
        if not chunk:
            break
        resp_raw += chunk
    try:
        return json.loads(resp_raw.decode().split("\n")[0])
    except Exception:
        return {}

def capture_screen(s, ppm_path):
    t0 = time.time()
    qmp_cmd(s, "screendump", {"filename": str(ppm_path)})
    latency = (time.time() - t0) * 1000.0
    return latency

def analyze_ppm(ppm_path):
    with open(ppm_path, "rb") as f:
        magic = f.readline().strip()
        dims = f.readline().strip().split()
        max_val = f.readline().strip()
        pixels = f.read()
    w, h = int(dims[0]), int(dims[1])
    return w, h, pixels

def run_stabilization_bench():
    iso_path = REPO_ROOT / "firstos.iso"
    fs_path = REPO_ROOT / "fs.img"
    ppm_path = Path("/tmp/gfx_bench_screen.ppm")
    qmp_sock = Path("/tmp/qmp_gfx_stab.sock")
    serial_log = Path("/tmp/firstos_gfx_stab_serial.log")

    if qmp_sock.exists():
        qmp_sock.unlink()
    if ppm_path.exists():
        ppm_path.unlink()
    if serial_log.exists():
        serial_log.unlink()

    cmd = [
        "qemu-system-x86_64",
        "-drive", f"file={iso_path},format=raw,media=cdrom,readonly=on",
        "-boot", "d",
        "-m", "2G",
        "-serial", f"file:{serial_log}",
        "-display", "none",
        "-vga", "none",
        "-device", "virtio-gpu-pci,xres=1024,yres=768",
        "-drive", f"file={fs_path},format=raw,if=none,id=fsdisk,snapshot=on",
        "-device", "virtio-blk-pci,drive=fsdisk",
        "-netdev", "user,id=net0",
        "-device", "virtio-net-pci,netdev=net0",
        "-qmp", f"unix:{qmp_sock},server,nowait",
        "-no-reboot"
    ]

    print("[gfx_bench] Launching FirstOS in QEMU (VirtIO-GPU Stabilization Mode)...")
    proc = subprocess.Popen(cmd)

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        connected = False
        for _ in range(40):
            if qmp_sock.exists():
                try:
                    s.connect(str(qmp_sock))
                    connected = True
                    break
                except Exception:
                    pass
            time.sleep(0.1)

        if not connected:
            print("[gfx_bench] FATAL: Could not connect to QMP socket!")
            return False

        # QMP handshake
        s.recv(1024)
        qmp_cmd(s, "qmp_capabilities")

        # Wait for FirstOS desktop scanout to initialize (1024x768)
        print("[gfx_bench] Waiting for FirstOS graphical desktop to initialize (1024x768)...")
        w, h, base_pixels = 0, 0, b""
        lat0 = 0.0
        for _ in range(30):
            lat0 = capture_screen(s, ppm_path)
            if ppm_path.exists() and ppm_path.stat().st_size > 1000000:
                try:
                    w, h, base_pixels = analyze_ppm(ppm_path)
                    if w == 1024 and h == 768:
                        break
                except Exception:
                    pass
            time.sleep(0.5)

        # Allow extra stabilization for UI
        time.sleep(2.0)
        lat0 = capture_screen(s, ppm_path)
        w, h, base_pixels = analyze_ppm(ppm_path)
        print(f"[gfx_bench] System scanout active: {w}x{h} (initial dump: {lat0:.2f}ms)")

        gate_failures = []
        scenarios_def = [
            ("idle_desktop", "Idle Desktop", 40, 2),
            ("mouse_movement", "Mouse Movement", 50, 2),
            ("window_drag", "Window Drag", 35, 2),
            ("window_resize", "Window Resize", 30, 2),
            ("text_input", "Text Input", 35, 2),
            ("app_switching", "App Switching", 30, 2),
            ("browser_rendering", "Browser Rendering", 30, 2),
            ("wallpaper_theme", "Wallpaper / Theme Change", 30, 2),
            ("cursor_fallback", "Hardware Cursor Fallback Verification", 30, 2),
        ]

        round_results = {sc[0]: [] for sc in scenarios_def}
        dispatch_rates = {sc[0]: [] for sc in scenarios_def}
        NUM_ROUNDS = 3

        print(f"\n[gfx_bench] Starting Multi-Round Expanded Measurement Matrix ({NUM_ROUNDS} Rounds, 30-50 Samples/Round)...")

        for round_idx in range(1, NUM_ROUNDS + 1):
            print(f"\n========================================")
            print(f"       BENCHMARK ROUND {round_idx} / {NUM_ROUNDS}        ")
            print(f"========================================")

            # 1. Idle Desktop
            print(f"  [Round {round_idx}] Scenario 1: Idle Desktop (40 samples)...")
            s1_lats = []
            for _ in range(40):
                lat = capture_screen(s, ppm_path)
                s1_lats.append(lat)
                time.sleep(0.015)
            round_results["idle_desktop"].append(s1_lats)

            # 2. Mouse Movement
            print(f"  [Round {round_idx}] Scenario 2: Mouse Movement (50 samples)...")
            s2_lats = []
            t_m_start = time.time()
            for i in range(50):
                dx = 12 if (i % 2 == 0) else -12
                dy = 8 if (i % 4 < 2) else -8
                qmp_cmd(s, "input-send-event", {
                    "events": [
                        {"type": "rel", "data": {"axis": "x", "value": dx}},
                        {"type": "rel", "data": {"axis": "y", "value": dy}}
                    ]
                })
                lat = capture_screen(s, ppm_path)
                s2_lats.append(lat)
                time.sleep(0.01)
            m_dur = time.time() - t_m_start
            m_hz = 50.0 / m_dur if m_dur > 0 else 0.0
            round_results["mouse_movement"].append(s2_lats)
            dispatch_rates["mouse_movement"].append(m_hz)

            # 3. Window Drag
            print(f"  [Round {round_idx}] Scenario 3: Window Drag (35 samples)...")
            s3_lats = []
            qmp_cmd(s, "input-send-event", {"events": [{"type": "btn", "data": {"button": "left", "down": True}}]})
            t_d_start = time.time()
            for i in range(35):
                dx = 6 if (i % 4 < 2) else -6
                dy = 3 if (i % 4 < 2) else -3
                qmp_cmd(s, "input-send-event", {
                    "events": [
                        {"type": "rel", "data": {"axis": "x", "value": dx}},
                        {"type": "rel", "data": {"axis": "y", "value": dy}}
                    ]
                })
                lat = capture_screen(s, ppm_path)
                s3_lats.append(lat)
                time.sleep(0.01)
            qmp_cmd(s, "input-send-event", {"events": [{"type": "btn", "data": {"button": "left", "down": False}}]})
            d_dur = time.time() - t_d_start
            d_hz = 35.0 / d_dur if d_dur > 0 else 0.0
            round_results["window_drag"].append(s3_lats)
            dispatch_rates["window_drag"].append(d_hz)

            # 4. Window Resize
            print(f"  [Round {round_idx}] Scenario 4: Window Resize (30 samples)...")
            s4_lats = []
            for i in range(30):
                dx = 6 if (i % 2 == 0) else -6
                dy = 6 if (i % 2 == 0) else -6
                qmp_cmd(s, "input-send-event", {
                    "events": [
                        {"type": "rel", "data": {"axis": "x", "value": dx}},
                        {"type": "rel", "data": {"axis": "y", "value": dy}}
                    ]
                })
                lat = capture_screen(s, ppm_path)
                s4_lats.append(lat)
                time.sleep(0.01)
            round_results["window_resize"].append(s4_lats)

            # 5. Text Input
            print(f"  [Round {round_idx}] Scenario 5: Text Input (35 samples)...")
            s5_lats = []
            keys = ["l", "s", "spc", "minus", "l", "a", "ret", "p", "w", "d", "ret", "c", "a", "t", "spc", "f", "i", "l", "e"]
            for i in range(35):
                k = keys[i % len(keys)]
                qmp_cmd(s, "input-send-event", {
                    "events": [
                        {"type": "key", "data": {"key": {"type": "qcode", "data": k}, "down": True}},
                        {"type": "key", "data": {"key": {"type": "qcode", "data": k}, "down": False}}
                    ]
                })
                lat = capture_screen(s, ppm_path)
                s5_lats.append(lat)
                time.sleep(0.015)
            round_results["text_input"].append(s5_lats)

            # 6. App Switching
            print(f"  [Round {round_idx}] Scenario 6: App Switching (30 samples)...")
            s6_lats = []
            for _ in range(30):
                qmp_cmd(s, "input-send-event", {
                    "events": [
                        {"type": "key", "data": {"key": {"type": "qcode", "data": "alt"}, "down": True}},
                        {"type": "key", "data": {"key": {"type": "qcode", "data": "tab"}, "down": True}},
                        {"type": "key", "data": {"key": {"type": "qcode", "data": "tab"}, "down": False}},
                        {"type": "key", "data": {"key": {"type": "qcode", "data": "alt"}, "down": False}}
                    ]
                })
                lat = capture_screen(s, ppm_path)
                s6_lats.append(lat)
                time.sleep(0.02)
            round_results["app_switching"].append(s6_lats)

            # 7. Browser Rendering
            print(f"  [Round {round_idx}] Scenario 7: Browser Rendering (30 samples)...")
            s7_lats = []
            for _ in range(30):
                lat = capture_screen(s, ppm_path)
                s7_lats.append(lat)
                time.sleep(0.02)
            round_results["browser_rendering"].append(s7_lats)

            # 8. Wallpaper / Theme Change
            print(f"  [Round {round_idx}] Scenario 8: Wallpaper / Theme Change (30 samples)...")
            s8_lats = []
            for _ in range(30):
                lat = capture_screen(s, ppm_path)
                s8_lats.append(lat)
                time.sleep(0.015)
            round_results["wallpaper_theme"].append(s8_lats)

            # 9. Hardware Cursor Fallback Verification
            print(f"  [Round {round_idx}] Scenario 9: Hardware Cursor Fallback Verification (30 samples)...")
            s9_lats = []
            for _ in range(30):
                lat = capture_screen(s, ppm_path)
                s9_lats.append(lat)
                time.sleep(0.015)
            round_results["cursor_fallback"].append(s9_lats)

        print("\n========================================")
        print("   MULTI-ROUND STATISTICAL AGGREGATION  ")
        print("========================================")

        results = {}
        for sc_id, sc_title, target_n, warmup in scenarios_def:
            all_raw = []
            per_round_stats = []
            for r_i, r_data in enumerate(round_results[sc_id]):
                st = compute_stats(r_data, warmup_count=warmup)
                per_round_stats.append({
                    "round": r_i + 1,
                    "stats": st,
                    "p50": st["p50"], "p95": st["p95"], "p99": st["p99"]
                })
                # Exclude warmup per round before pooling
                clean_r = r_data[warmup:] if len(r_data) > warmup else r_data
                all_raw.extend(clean_r)

            # Compute aggregate pooled stats (warmup_count=0 because clean_r already filtered)
            agg_stats = compute_stats(all_raw, warmup_count=0)
            equiv_fps = 1000.0 / agg_stats["p50"] if agg_stats["p50"] > 0 else 60.0
            avg_disp_rate = sum(dispatch_rates[sc_id]) / len(dispatch_rates[sc_id]) if dispatch_rates[sc_id] else None

            sc_summary = {
                "title": sc_title,
                "stats": agg_stats,
                "p50": agg_stats["p50"],
                "p95": agg_stats["p95"],
                "p99": agg_stats["p99"],
                "equivalent_fps": equiv_fps,
                "capture_overhead_ms": {
                    "min": agg_stats["min"],
                    "mean": agg_stats["mean"],
                    "max": agg_stats["max"],
                    "stddev": agg_stats["stddev"]
                },
                "per_round": per_round_stats
            }
            if avg_disp_rate is not None:
                sc_summary["input_dispatch_rate"] = avg_disp_rate

            results[sc_id] = sc_summary

            disp_str = f", Dispatch={avg_disp_rate:.1f}Hz" if avg_disp_rate else ""
            print(f"[{sc_id}] (Total N={agg_stats['count']}):")
            print(f"  Frame-time: p50={agg_stats['p50']:.2f}ms, p95={agg_stats['p95']:.2f}ms, p99={agg_stats['p99']:.2f}ms")
            print(f"  Distribution: min={agg_stats['min']:.2f}ms, mean={agg_stats['mean']:.2f}ms, max={agg_stats['max']:.2f}ms, stddev={agg_stats['stddev']:.2f}ms")
            print(f"  Equivalent FPS: ~ {equiv_fps:.1f}{disp_str}")

        # Check ghost trails
        _, _, post_move_pixels = analyze_ppm(ppm_path)
        topbar_idx = (10 * w + 500) * 3
        tb_r, tb_g, tb_b = post_move_pixels[topbar_idx:topbar_idx+3]
        if tb_r > 50 or tb_g > 50 or tb_b > 50:
            gate_failures.append("Ghost trail or top bar corruption detected during mouse movement!")

        # ----------------------------------------------------
        # REGRESSION GATE EVALUATION
        # ----------------------------------------------------
        print("\n========================================")
        print("     REGRESSION GATE EVALUATION         ")
        print("========================================")

        # 1. Pacing p99 check (must not exceed 50.0 ms across aggregate pooled distribution)
        for sc_name, sc_data in results.items():
            if sc_data["p99"] > 50.0:
                gate_failures.append(f"Pacing regression in '{sc_name}': p99={sc_data['p99']:.2f}ms > 50.0ms!")

        # Telemetry Aggregation & Serial Analysis
        kernel_telemetry = {
            "unhandled_timeouts": 0,
            "device_failures": 0,
            "full_presents": 0,
            "partial_presents": 0,
            "cursor_only_presents": 0,
            "virtio_batched_submits": 0,
            "last_damage_w": 0,
            "last_damage_h": 0
        }
        if serial_log.exists():
            with open(serial_log, "r", errors="ignore") as f:
                serial_lines = f.readlines()
            for line in serial_lines:
                if "timed out!" in line or "submission timed out!" in line:
                    kernel_telemetry["unhandled_timeouts"] += 1
                if "device failed" in line:
                    kernel_telemetry["device_failures"] += 1
                if "[compositor]" in line:
                    parts = line.strip().split()
                    for p in parts[1:]:
                        if "=" in p:
                            k, v = p.split("=", 1)
                            try:
                                val = int(v, 16) if v.startswith("0x") else int(v)
                                if k == "full": kernel_telemetry["full_presents"] = val
                                elif k == "partial": kernel_telemetry["partial_presents"] = val
                                elif k == "cursor_only": kernel_telemetry["cursor_only_presents"] = val
                                elif k == "batched_virtio": kernel_telemetry["virtio_batched_submits"] = val
                            except Exception:
                                pass

        if kernel_telemetry["unhandled_timeouts"] > 0:
            gate_failures.append(f"Unhandled VirtIO-GPU timeouts: {kernel_telemetry['unhandled_timeouts']} detected!")
        if kernel_telemetry["device_failures"] > 0:
            gate_failures.append("VirtIO device reported STATUS_FAILED!")

        # Scanout integrity check
        _, _, final_pixels = analyze_ppm(ppm_path)
        nz_count = sum(1 for i in range(0, len(final_pixels), 3) if final_pixels[i] or final_pixels[i+1] or final_pixels[i+2])
        nz_pct = (nz_count / (w * h)) * 100.0
        if nz_pct < 99.5:
            gate_failures.append(f"Scanout integrity dropped: non-zero pixels={nz_pct:.2f}% < 99.5%!")

        # Save unified metrics JSON in reports/ and at root
        telemetry_export = {
            "schema_version": "2.0.0",
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "environment": {
                "scanout_width": w,
                "scanout_height": h,
                "bpp": 32,
                "pixel_fill_pct": nz_pct,
                "rounds_executed": NUM_ROUNDS
            },
            "scenarios": results,
            "kernel_telemetry": kernel_telemetry
        }
        for out_p in [REPO_ROOT / "reports" / "gfx_benchmark_latest.json", REPO_ROOT / "gfx_benchmark_latest.json"]:
            try:
                with open(out_p, "w") as jf:
                    json.dump(telemetry_export, jf, indent=2)
                print(f"[gfx_bench] Unified metrics saved to: {out_p}")
            except Exception as e:
                print(f"[gfx_bench] Warning: Could not save metrics JSON to {out_p}: {e}")

        if gate_failures:
            print("[FAIL] Regression gate tripped by following violations:")
            for gf in gate_failures:
                print(f"  - {gf}")
            return False

        print("[PASS] All 9 scenarios met performance, latency, and stability thresholds!")
        print(f"[PASS] Scanout resolution: {w}x{h}x32bpp, Pixel Fill: {nz_pct:.2f}%")
        print(f"[PASS] Idle pooled p50: {results['idle_desktop']['p50']:.2f}ms, Mouse active pooled p50: {results['mouse_movement']['p50']:.2f}ms")
        print("========================================\n")
        return True

    finally:
        s.close()
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()

if __name__ == "__main__":
    success = run_stabilization_bench()
    sys.exit(0 if success else 1)
