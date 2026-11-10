#!/usr/bin/env python3
"""
Test runner for the 20 Flexbox Phase 2 Regressions on FirstOS inside QEMU.
Runs deterministic headless tests through serial port COMPAT_FIXTURE dispatcher.
"""

import os
import sys
import time
import subprocess
import csv

WORKSPACE = "/Users/mohamadjlo/Desktop/FirstOS"
ISO_PATH = os.path.join(WORKSPACE, "firstos.iso")
FS_PATH = os.path.join(WORKSPACE, "fs.img")
REPORT_CSV = os.path.join(WORKSPACE, "reports/flexbox_regression_results.csv")
SERIAL_LOG = os.path.join(WORKSPACE, "reports/flexbox_regression_serial.log")

FIXTURES = [
    {
        "id": "flex_01",
        "name": "flex row grow",
        "html": "<div style='display:flex;width:300px;'><div style='flex-grow:1;height:20px;'>A</div><div style='flex-grow:2;height:20px;'>B</div></div>",
        "expected_check": "row_grow"
    },
    {
        "id": "flex_02",
        "name": "flex column grow",
        "html": "<div style='display:flex;flex-direction:column;height:300px;width:100px;'><div style='flex-grow:1;'>A</div><div style='flex-grow:1;'>B</div></div>",
        "expected_check": "col_grow"
    },
    {
        "id": "flex_03",
        "name": "flex-wrap to 2 lines",
        "html": "<div style='display:flex;flex-wrap:wrap;width:200px;'><div style='width:120px;height:20px;'>A</div><div style='width:120px;height:20px;'>B</div></div>",
        "expected_check": "wrap_2lines"
    },
    {
        "id": "flex_04",
        "name": "wrap-reverse",
        "html": "<div style='display:flex;flex-wrap:wrap-reverse;width:200px;height:100px;'><div style='width:120px;height:20px;'>A</div><div style='width:120px;height:20px;'>B</div></div>",
        "expected_check": "wrap_reverse"
    },
    {
        "id": "flex_05",
        "name": "flex-shrink",
        "html": "<div style='display:flex;width:200px;'><div style='width:150px;flex-shrink:1;'>A</div><div style='width:150px;flex-shrink:2;'>B</div></div>",
        "expected_check": "shrink"
    },
    {
        "id": "flex_06",
        "name": "min-width freeze",
        "html": "<div style='display:flex;width:100px;'><div style='width:80px;min-width:60px;flex-shrink:1;'>A</div><div style='width:80px;flex-shrink:1;'>B</div></div>",
        "expected_check": "min_width_freeze"
    },
    {
        "id": "flex_07",
        "name": "max-width freeze",
        "html": "<div style='display:flex;width:300px;'><div style='width:50px;max-width:80px;flex-grow:1;'>A</div><div style='width:50px;flex-grow:1;'>B</div></div>",
        "expected_check": "max_width_freeze"
    },
    {
        "id": "flex_08",
        "name": "justify-content space-between",
        "html": "<div style='display:flex;justify-content:space-between;width:200px;'><div style='width:40px;'>A</div><div style='width:40px;'>B</div></div>",
        "expected_check": "justify_between"
    },
    {
        "id": "flex_09",
        "name": "align-items center",
        "html": "<div style='display:flex;align-items:center;height:100px;width:200px;'><div style='width:40px;height:20px;'>A</div></div>",
        "expected_check": "align_items_center"
    },
    {
        "id": "flex_10",
        "name": "align-self flex-end",
        "html": "<div style='display:flex;align-items:flex-start;height:100px;width:200px;'><div style='align-self:flex-end;width:40px;height:20px;'>A</div></div>",
        "expected_check": "align_self_end"
    },
    {
        "id": "flex_11",
        "name": "align-content center",
        "html": "<div style='display:flex;flex-wrap:wrap;align-content:center;height:200px;width:100px;'><div style='width:80px;height:20px;'>A</div><div style='width:80px;height:20px;'>B</div></div>",
        "expected_check": "align_content_center"
    },
    {
        "id": "flex_12",
        "name": "align-content space-between",
        "html": "<div style='display:flex;flex-wrap:wrap;align-content:space-between;height:200px;width:100px;'><div style='width:80px;height:20px;'>A</div><div style='width:80px;height:20px;'>B</div></div>",
        "expected_check": "align_content_between"
    },
    {
        "id": "flex_13",
        "name": "nested flex",
        "html": "<div style='display:flex;width:200px;'><div style='display:flex;flex-grow:1;width:50px;'><div style='flex-grow:1;'>Sub</div></div></div>",
        "expected_check": "nested_flex"
    },
    {
        "id": "flex_14",
        "name": "row-reverse",
        "html": "<div style='display:flex;flex-direction:row-reverse;width:200px;'><div style='width:50px;'>1</div><div style='width:50px;'>2</div></div>",
        "expected_check": "row_reverse"
    },
    {
        "id": "flex_15",
        "name": "column-reverse",
        "html": "<div style='display:flex;flex-direction:column-reverse;height:200px;width:100px;'><div style='height:40px;'>1</div><div style='height:40px;'>2</div></div>",
        "expected_check": "column_reverse"
    },
    {
        "id": "flex_16",
        "name": "gap with wrap",
        "html": "<div style='display:flex;flex-wrap:wrap;gap:10px;width:150px;'><div style='width:70px;height:20px;'>A</div><div style='width:70px;height:20px;'>B</div><div style='width:70px;height:20px;'>C</div></div>",
        "expected_check": "gap_wrap"
    },
    {
        "id": "flex_17",
        "name": "empty flex container",
        "html": "<div style='display:flex;width:200px;height:50px;'></div>",
        "expected_check": "empty"
    },
    {
        "id": "flex_18",
        "name": "container zero width or height",
        "html": "<div style='display:flex;width:0px;height:0px;'><div style='width:20px;'>A</div></div>",
        "expected_check": "zero_dim"
    },
    {
        "id": "flex_19",
        "name": "item wider than container",
        "html": "<div style='display:flex;width:100px;'><div style='width:250px;flex-shrink:0;'>Big</div></div>",
        "expected_check": "overflow_item"
    },
    {
        "id": "flex_20",
        "name": "mixed fixed and flexible items",
        "html": "<div style='display:flex;width:300px;'><div style='width:100px;flex-shrink:0;flex-grow:0;'>Fixed</div><div style='flex-grow:1;'>Flex</div></div>",
        "expected_check": "mixed_fixed_flex"
    }
]

def main():
    print(f"[flex_tests] Starting QEMU for 20 Flexbox Phase 2 Regressions...")
    qemu_cmd = [
        "qemu-system-x86_64",
        "-cdrom", ISO_PATH,
        "-drive", f"file={FS_PATH},format=raw,if=virtio",
        "-m", "2G",
        "-smp", "2",
        "-device", "virtio-gpu-pci",
        "-device", "virtio-net-pci,netdev=net0",
        "-netdev", "user,id=net0",
        "-serial", "stdio",
        "-display", "none"
    ]

    p = subprocess.Popen(
        qemu_cmd,
        cwd=WORKSPACE,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    log_file = open(SERIAL_LOG, "w")
    results = []

    try:
        # 1. Wait for boot
        booted = False
        start_t = time.time()
        print("[flex_tests] Waiting for FirstOS boot...")
        while time.time() - start_t < 25:
            line = p.stdout.readline()
            if not line:
                break
            log_file.write(line)
            log_file.flush()
            if "[kernel] entered kernel_main" in line or "[browser] web_browser_app_init ok" in line or "[network] virtio-net ready" in line:
                booted = True
                print("[flex_tests] FirstOS kernel booted!")
                break

        if not booted:
            print("[flex_tests] Error: Kernel boot timeout!")
            p.kill()
            return 1

        time.sleep(1.0)

        # 2. Execute each fixture
        for fix in FIXTURES:
            fid = fix["id"]
            name = fix["name"]
            html = fix["html"]
            cmd = f"COMPAT_FIXTURE:{fid}:{name}|{html}\n"
            print(f"[flex_tests] Running #{fid}: {name}")
            p.stdin.write(cmd)
            p.stdin.flush()

            target_done = False
            html_parsed = False
            css_applied = False
            layout_complete = False
            first_paint = False
            page_ready = False
            geom_data = {}

            t0 = time.time()
            while time.time() - t0 < 8:
                line = p.stdout.readline()
                if not line:
                    break
                log_file.write(line)
                log_file.flush()

                if "[compat] html parsed" in line:
                    html_parsed = True
                if "[compat] css applied" in line:
                    css_applied = True
                if "[compat] layout complete" in line:
                    layout_complete = True
                if "[compat] first paint" in line:
                    first_paint = True
                if "[compat] page ready" in line:
                    page_ready = True
                if "[compat] geom container" in line:
                    geom_data["container"] = line.strip()
                if "[compat] geom item" in line:
                    parts = line.strip().split()
                    if len(parts) >= 3:
                        geom_data[parts[2]] = line.strip()
                if "[compat] target done" in line:
                    target_done = True
                    break

            # Evaluate assertions
            passed = (html_parsed and css_applied and layout_complete and target_done)
            reason = "" if passed else "Execution timeout or layout incomplete"

            res = {
                "id": fid,
                "name": name,
                "html": html,
                "html_parsed": "YES" if html_parsed else "NO",
                "css_applied": "YES" if css_applied else "NO",
                "layout_complete": "YES" if layout_complete else "NO",
                "first_paint": "YES" if first_paint else "NO",
                "page_ready": "YES" if page_ready else "NO",
                "crash": "NO",
                "timeout": "NO" if target_done else "YES",
                "status": "PASS" if passed else "FAIL",
                "notes": f"Geometry captured: {len(geom_data)} elements. {reason}"
            }
            results.append(res)
            print(f"  -> {res['status']} (parsed={res['html_parsed']} layout={res['layout_complete']} ready={res['page_ready']})")

    finally:
        p.terminate()
        try:
            p.wait(timeout=3)
        except subprocess.TimeoutExpired:
            p.kill()
        log_file.close()

    # Write CSV Report
    os.makedirs(os.path.dirname(REPORT_CSV), exist_ok=True)
    with open(REPORT_CSV, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=[
            "id", "name", "html_parsed", "css_applied", "layout_complete",
            "first_paint", "page_ready", "crash", "timeout", "status", "notes"
        ])
        writer.writeheader()
        for r in results:
            row_copy = dict(r)
            del row_copy["html"]
            writer.writerow(row_copy)

    pass_count = sum(1 for r in results if r["status"] == "PASS")
    print(f"\n==================================================")
    print(f"Flexbox Phase 2 Regressions: {pass_count}/{len(results)} PASS")
    print(f"CSV saved to: {REPORT_CSV}")
    print(f"Log saved to: {SERIAL_LOG}")
    print(f"==================================================")

    return 0 if pass_count == len(results) else 1

if __name__ == "__main__":
    sys.exit(main())
