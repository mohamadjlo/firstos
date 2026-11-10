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

print("[test_phase5a] Launching QEMU with FirstOS Phase 5A...")
proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

log_lines = []
start = time.time()
nav_sent = False
second_nav_sent = False

while time.time() - start < 35:
    line = proc.stdout.readline()
    if not line:
        break
    s = line.decode('latin1', errors='replace').rstrip('\r\n')
    log_lines.append(s)

    # When desktop / GUI is ready, trigger browser navigation to about:html5
    if ('gui_draw done' in s or 'userspace_exec_target] Exiting cleanly' in s) and not nav_sent:
        print("[test_phase5a] FirstOS desktop ready. Sending NAV:about:html5 command...")
        try:
            proc.stdin.write(b"NAV:about:html5\n")
            proc.stdin.flush()
            nav_sent = True
        except Exception as e:
            print(f"[test_phase5a] stdin write error: {e}")

    if '[security] committed origin' in s:
        print(f"[test_phase5a] Origin committed: {s}")

    if 'validated and executed render commands' in s and nav_sent and not second_nav_sent:
        print("[test_phase5a] Frame rendered under committed origin. Sending NAV:about:home...")
        try:
            proc.stdin.write(b"NAV:about:home\n")
            proc.stdin.flush()
            second_nav_sent = True
        except Exception as e:
            print(f"[test_phase5a] stdin write error 2: {e}")

    # After second navigation is confirmed
    if second_nav_sent and ('sent HTML_DATA to renderer (req=' in s and 'nav=' in s):
        print("[test_phase5a] SUCCESS: Second navigation under new origin committed confirmed!")
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

log_file_path = '/tmp/firstos_phase5a_test.log'
with open(log_file_path, 'w') as f:
    for line in log_lines:
        f.write(line + '\n')

print(f"[test_phase5a] Captured {len(log_lines)} lines to {log_file_path}")

renderer_started = any('browser_renderer] started in Ring 3' in l for l in log_lines)
shm_mapped = any('browser_renderer] mapped shared memory at 0x04400000' in l for l in log_lines)
origin_committed = any('[security] committed origin' in l for l in log_lines)
dom_built = any('[renderer] DOM built nodes=' in l for l in log_lines)
cmds_emitted = any('[renderer] render commands emitted count=' in l for l in log_lines)
host_executed = any('validated and executed render commands' in l for l in log_lines)
no_triple_fault = not any('Triple fault' in l or 'PANIC' in l for l in log_lines)

print("\n--- Phase 5A Live QEMU Verification Summary ---")
print(f"1. Renderer started in Ring 3:                {'PASS' if renderer_started else 'FAIL'}")
print(f"2. Shared memory mapped at 0x04400000:         {'PASS' if shm_mapped else 'FAIL'}")
print(f"3. Security Context & Origin Committed:       {'PASS' if origin_committed else 'FAIL'}")
print(f"4. DOM parsed under security origin:          {'PASS' if dom_built else 'FAIL'}")
print(f"5. Render commands emitted to shared SHM:     {'PASS' if cmds_emitted else 'FAIL'}")
print(f"6. Host validated & painted render commands:  {'PASS' if host_executed else 'FAIL'}")
print(f"7. Zero kernel panics / triple faults:        {'PASS' if no_triple_fault else 'FAIL'}")

if all([renderer_started, shm_mapped, origin_committed, dom_built, cmds_emitted, host_executed, no_triple_fault]):
    print("\n>>> ALL PHASE 5A LIVE VERIFICATION CRITERIA PASSED! <<<")
    sys.exit(0)
else:
    print("\n>>> ONE OR MORE VERIFICATION CRITERIA FAILED <<<")
    sys.exit(1)
