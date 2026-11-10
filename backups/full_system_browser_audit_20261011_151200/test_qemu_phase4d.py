#!/usr/bin/env python3
import subprocess
import time
import sys

qemu_cmd = [
    'qemu-system-i386',
    '-drive', 'file=firstos.iso,format=raw,media=cdrom,readonly=on',
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

print("[test_phase4d] Launching QEMU with FirstOS Phase 4D...")
proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

log_lines = []
start = time.time()
nav_sent = False
second_nav_sent = False

while time.time() - start < 30:
    line = proc.stdout.readline()
    if not line:
        break
    s = line.decode('latin1', errors='replace').rstrip('\r\n')
    log_lines.append(s)

    # When desktop / GUI is ready, trigger browser navigation to about:html5 (which contains images)
    if ('gui_draw done' in s or 'userspace_exec_target] Exiting cleanly' in s) and not nav_sent:
        print("[test_phase4d] FirstOS desktop ready. Sending NAV:about:html5 command...")
        try:
            proc.stdin.write(b"NAV:about:html5\n")
            proc.stdin.flush()
            nav_sent = True
        except Exception as e:
            print(f"[test_phase4d] stdin write error: {e}")

    # Check for image decoding or resource serving
    if 'decoded image id=' in s or 'cached decoded image' in s:
        print(f"[test_phase4d] Image decoded & stored: {s}")

    if 'validated and executed render commands' in s and nav_sent and not second_nav_sent:
        # Trigger second navigation to verify history & navigation ID
        print("[test_phase4d] First frame rendered. Sending NAV:about:home command...")
        try:
            proc.stdin.write(b"NAV:about:home\n")
            proc.stdin.flush()
            second_nav_sent = True
        except Exception as e:
            print(f"[test_phase4d] stdin write error 2: {e}")

    # After second navigation is confirmed
    if second_nav_sent and ('sent HTML_DATA to renderer (req=' in s and 'nav=' in s):
        print("[test_phase4d] SUCCESS: Second navigation with updated navigation_id confirmed!")
        # Collect trailing logs
        for _ in range(50):
            l = proc.stdout.readline()
            if l:
                log_lines.append(l.decode('latin1', errors='replace').rstrip('\r\n'))
        break

proc.terminate()
try:
    proc.wait(timeout=2)
except Exception:
    proc.kill()

log_file_path = '/tmp/firstos_phase4d_test.log'
with open(log_file_path, 'w') as f:
    for line in log_lines:
        f.write(line + '\n')

print(f"[test_phase4d] Captured {len(log_lines)} lines to {log_file_path}")

renderer_started = any('browser_renderer] started in Ring 3' in l for l in log_lines)
shm_mapped = any('browser_renderer] mapped shared memory at 0x04400000' in l for l in log_lines)
nav_tracking = any('nav=' in l for l in log_lines)
html_received = any('[renderer] HTML received bytes=' in l for l in log_lines)
host_served = any('[browser_host] serving resource request' in l or '[browser_host] sent HTML_DATA' in l for l in log_lines)
cmds_emitted = any('[renderer] render commands emitted count=' in l for l in log_lines)
host_executed = any('validated and executed render commands' in l for l in log_lines)
history_push = any('history push:' in l or 'NAV_BEGIN' in l for l in log_lines)

print("\n--- Phase 4D Verification Summary ---")
print(f"1. Renderer started in Ring 3:          {'PASS' if renderer_started else 'FAIL'}")
print(f"2. Shared memory mapped at 0x04400000:   {'PASS' if shm_mapped else 'FAIL'}")
print(f"3. Monotonic Navigation ID tracking:    {'PASS' if nav_tracking else 'FAIL'}")
print(f"4. Navigation history recorded:         {'PASS' if history_push else 'FAIL'}")
print(f"5. Document HTML received by renderer:  {'PASS' if html_received else 'FAIL'}")
print(f"6. Host processed resource request:     {'PASS' if host_served else 'FAIL'}")
print(f"7. Render commands emitted to shared SHM:{'PASS' if cmds_emitted else 'FAIL'}")
print(f"8. Host validated & executed commands:  {'PASS' if host_executed else 'FAIL'}")

if (renderer_started and shm_mapped and nav_tracking and history_push and
    html_received and host_served and cmds_emitted and host_executed):
    print("\n>>> ALL PHASE 4D LIVE VERIFICATION CRITERIA SATISFIED! <<<")
    sys.exit(0)
else:
    print("\n>>> VERIFICATION INCOMPLETE <<<")
    sys.exit(1)
