#!/usr/bin/env python3
"""Targeted verification test for Wikipedia (CSS), Facebook, and YouTube."""

import os
import sys
import time
import subprocess
import threading

SERIAL_LOG = "/tmp/firstos_target_sites_test.log"

TEST_URLS = [
    "https://en.wikipedia.org",
    "https://facebook.com",
    "https://youtube.com",
]

all_lines = []
lock = threading.Lock()

def reader_thread_func(proc, log_file):
    while True:
        line = proc.stdout.readline()
        if not line:
            break
        text = line.decode("latin1", errors="replace").rstrip("\r\n")
        log_file.write(text + "\n")
        log_file.flush()
        with lock:
            all_lines.append(text)

def main():
    root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    iso_path = os.path.join(root_dir, "firstos.iso")

    if not os.path.exists(iso_path):
        print(f"Error: {iso_path} not found")
        return 1

    log_file = open(SERIAL_LOG, "w", encoding="utf-8")
    fs_path = os.path.join(root_dir, "fs.img")

    qemu_cmd = [
        "qemu-system-x86_64",
        "-drive", f"file={iso_path},format=raw,media=cdrom,readonly=on",
        "-boot", "d",
        "-drive", f"file={fs_path},format=raw,if=none,id=fsdisk",
        "-device", "virtio-blk-pci,drive=fsdisk",
        "-m", "2G",
        "-vga", "none",
        "-device", "virtio-gpu-pci,xres=1024,yres=768",
        "-netdev", "user,id=net0",
        "-device", "virtio-net-pci,netdev=net0",
        "-serial", "stdio",
        "-display", "none"
    ]

    # Start host HTTPS proxy bridge
    webproxy_script = os.path.join(root_dir, "tools", "firstos_webproxy.py")
    print(f"[test_harness] Starting HTTPS proxy bridge on 127.0.0.1:18080...")
    proxy_proc = subprocess.Popen([sys.executable, webproxy_script, "--bind", "127.0.0.1", "--port", "18080"],
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(0.5)

    print(f"[test_harness] Starting QEMU...")
    proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    reader = threading.Thread(target=reader_thread_func, args=(proc, log_file), daemon=True)
    reader.start()

    try:
        print("[test_harness] Waiting for FirstOS boot...")
        boot_start = time.time()
        booted = False
        while time.time() - boot_start < 25.0:
            with lock:
                if any("new native Web Browser app initialized" in l or "gui_draw done" in l for l in all_lines):
                    booted = True
                    break
            time.sleep(0.2)

        if not booted:
            print("[test_harness] Boot signature not detected within 25s!")
            return 1

        print("[test_harness] FirstOS booted successfully! Waiting 2s for network stack...")
        time.sleep(2.0)

        results = {}

        for idx, url in enumerate(TEST_URLS, 1):
            print(f"\n=======================================================")
            print(f"[{idx}/{len(TEST_URLS)}] Testing navigation to: {url}")
            print(f"=======================================================")

            mark = len(all_lines)
            cmd = f"NAV:{url}\n".encode("utf-8")
            proc.stdin.write(cmd)
            proc.stdin.flush()

            nav_start = time.time()
            done = False
            site_lines = []
            while time.time() - nav_start < 60.0:
                time.sleep(0.5)
                with lock:
                    new_lines = all_lines[mark:]

                for l in new_lines[len(site_lines):]:
                    site_lines.append(l)
                    if any(k in l for k in [
                        "[serial_cmd]", "[web_browser]", "[browser_core]",
                        "[proxy_bridge]", "[http_v11]", "[http_client]",
                        "[resource]", "[html5_resources]", "[html5_script]",
                        "[html5_engine]", "[browser_host]", "[renderer]"
                    ]):
                        print(f"  {l}")

                if any("response rendered by native HTML5 pipeline" in l or
                       "response failed" in l or
                       "showing local page" in l or
                       "native offline view" in l for l in site_lines):
                    time.sleep(3.0)
                    with lock:
                        final_new = all_lines[mark:]
                    for l in final_new[len(site_lines):]:
                        site_lines.append(l)
                        if any(k in l for k in [
                            "[resource]", "[html5_resources]", "[html5_script]",
                            "[html5_engine]", "[browser_host]", "[renderer]"
                        ]):
                            print(f"  {l}")
                    done = True
                    break

            if not done:
                print(f"  [test_harness] Timed out waiting for {url}")

            rendered = any("response rendered by native HTML5 pipeline" in l or "showing local page" in l for l in site_lines)
            failed = any("response failed" in l for l in site_lines)
            offline = any("native offline view" in l for l in site_lines)
            stylesheets = [l for l in site_lines if "applying external stylesheet" in l or "STYLESHEET" in l]
            resources_fetched = [l for l in site_lines if "network subresource fetched" in l or "resource served from cache" in l]

            results[url] = {
                "rendered": rendered,
                "failed": failed,
                "offline": offline,
                "stylesheets_applied": len(stylesheets),
                "subresources_fetched": len(resources_fetched),
            }

        print("\n\n=======================================================")
        print("                   FINAL TEST RESULTS                  ")
        print("=======================================================")
        for url, res in results.items():
            status = "SUCCESS (HTML5 Pipeline)" if res["rendered"] else ("OFFLINE FALLBACK" if res["offline"] else "FAILED")
            print(f"URL: {url}")
            print(f"  Status: {status}")
            print(f"  Stylesheets Applied: {res['stylesheets_applied']}")
            print(f"  Subresources Fetched: {res['subresources_fetched']}")

    finally:
        try:
            proc.terminate()
            proc.wait(timeout=3)
        except Exception:
            proc.kill()
        try:
            proxy_proc.terminate()
            proxy_proc.wait(timeout=2)
        except Exception:
            proxy_proc.kill()

    return 0

if __name__ == "__main__":
    sys.exit(main())
