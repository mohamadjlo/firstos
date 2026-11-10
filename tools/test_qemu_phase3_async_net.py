#!/usr/bin/env python3
import subprocess
import time
import sys

qemu_cmd = [
    'qemu-system-x86_64',
    '-drive', 'file=firstos.iso,format=raw,media=cdrom,readonly=on',
    '-boot', 'd',
    '-drive', 'file=fs.img,format=raw,if=none,id=fsdisk',
    '-device', 'virtio-blk-pci,drive=fsdisk',
    '-m', '2G',
    '-vga', 'none',
    '-device', 'virtio-gpu-pci,xres=1024,yres=768',
    '-netdev', 'user,id=net0',
    '-device', 'virtio-net-pci,netdev=net0',
    '-serial', 'stdio',
    '-display', 'none'
]

print("[test_phase3_async] Launching QEMU with FirstOS for Async Network Verification...")
proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

log_lines = []
start = time.time()
nav_sent = False

while time.time() - start < 22:
    line = proc.stdout.readline()
    if not line:
        break
    s = line.decode('latin1', errors='replace').rstrip('\r\n')
    log_lines.append(s)

    # When desktop / GUI is ready, trigger async HTTP navigation via serial command
    if ('gui_draw done' in s or 'userspace_exec_target] Exiting cleanly' in s) and not nav_sent:
        print("[test_phase3_async] FirstOS desktop ready. Sending NAV:http://google.com command...")
        try:
            proc.stdin.write(b"NAV:http://google.com\n")
            proc.stdin.flush()
            nav_sent = True
        except Exception as e:
            print(f"[test_phase3_async] stdin write error: {e}")

    # Check for async load initiation and non-blocking lifecycle markers
    if '[web_browser] async loading' in s or '[browser_core] async navigation' in s:
        print(f"[test_phase3_async] Captured marker: {s}")

    # If offline fallback or complete reached
    if '[web_browser] loading native offline view for: http://google.com' in s or 'validated and executed render commands' in s:
        print("[test_phase3_async] Navigation completed gracefully without freezing GUI!")
        for _ in range(30):
            l = proc.stdout.readline()
            if l:
                log_lines.append(l.decode('latin1', errors='replace').rstrip('\r\n'))
        break

proc.terminate()
try:
    proc.wait(timeout=2)
except Exception:
    proc.kill()

full_log = "\n".join(log_lines)
with open("/tmp/firstos_phase3_async_test.log", "w") as f:
    f.write(full_log)

print(f"[test_phase3_async] Captured {len(log_lines)} lines to /tmp/firstos_phase3_async_test.log")

async_loading_seen = any('[web_browser] async loading' in l for l in log_lines)
async_start_seen = any('[browser_core] async navigation start' in l for l in log_lines)
renderer_in_ring3 = any('[browser_renderer] started in Ring 3' in l for l in log_lines)
shm_mapped = any('[browser_renderer] mapped shared memory at 0x04400000' in l for l in log_lines)
navigation_finished = any(('[web_browser] loading native offline view' in l) or ('[browser_core] async navigation complete' in l) for l in log_lines)

print("\n--- Phase 3 Async Network Service Verification Summary ---")
print(f"1. Userspace Renderer in Ring 3:        {'PASS' if renderer_in_ring3 else 'FAIL'}")
print(f"2. Shared memory mapped at 0x04400000:   {'PASS' if shm_mapped else 'FAIL'}")
print(f"3. Web Browser async loading initiated: {'PASS' if async_loading_seen else 'FAIL'}")
print(f"4. Browser Core async state machine:    {'PASS' if async_start_seen else 'FAIL'}")
print(f"5. Non-blocking navigation cycle:       {'PASS' if navigation_finished else 'FAIL'}")

if renderer_in_ring3 and shm_mapped and async_loading_seen and async_start_seen and navigation_finished:
    print("\n>>> ALL PHASE 3 LIVE VERIFICATION CRITERIA SATISFIED! <<<")
    sys.exit(0)
else:
    print("\n>>> VERIFICATION FAILED <<<")
    sys.exit(1)
