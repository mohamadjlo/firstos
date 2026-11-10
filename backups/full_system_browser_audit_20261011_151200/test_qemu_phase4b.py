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

print("[test_phase4b] Launching QEMU with FirstOS Phase 4B...")
proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

log_lines = []
start = time.time()
nav_sent = False

while time.time() - start < 20:
    line = proc.stdout.readline()
    if not line:
        break
    s = line.decode('latin1', errors='replace').rstrip('\r\n')
    log_lines.append(s)

    # When desktop / GUI is ready, trigger browser launch via serial command
    if ('gui_draw done' in s or 'userspace_exec_target] Exiting cleanly' in s) and not nav_sent:
        print("[test_phase4b] FirstOS desktop ready. Sending NAV:about:home command to launch browser...")
        try:
            proc.stdin.write(b"NAV:about:home\n")
            proc.stdin.flush()
            nav_sent = True
        except Exception as e:
            print(f"[test_phase4b] stdin write error: {e}")

    # Check for successful host validation and render command execution
    if 'validated and executed render commands' in s:
        print("[test_phase4b] SUCCESS: Browser host received and executed userspace render commands!")
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

log_file_path = '/tmp/firstos_phase4b_test.log'
with open(log_file_path, 'w') as f:
    for line in log_lines:
        f.write(line + '\n')

print(f"[test_phase4b] Captured {len(log_lines)} lines to {log_file_path}")

renderer_started = any('browser_renderer] started in Ring 3' in l for l in log_lines)
shm_mapped = any('browser_renderer] mapped shared memory at 0x04400000' in l for l in log_lines)
html_received = any('[renderer] HTML received bytes=' in l for l in log_lines)
dom_built = any('[renderer] DOM built nodes=' in l for l in log_lines)
css_parsed = any('[renderer] CSS parsed' in l for l in log_lines)
layout_done = any('[renderer] layout complete doc_h=' in l for l in log_lines)
cmds_emitted = any('[renderer] render commands emitted count=' in l for l in log_lines)
host_executed = any('validated and executed render commands' in l for l in log_lines)

print("\n--- Phase 4B Verification Summary ---")
print(f"1. Renderer started in Ring 3:          {'PASS' if renderer_started else 'FAIL'}")
print(f"2. Shared memory mapped at 0x04400000:   {'PASS' if shm_mapped else 'FAIL'}")
print(f"3. Userspace HTML parser received data: {'PASS' if html_received else 'FAIL'}")
print(f"4. Userspace DOM tree constructed:      {'PASS' if dom_built else 'FAIL'}")
print(f"5. Userspace CSS parsed & cascaded:     {'PASS' if css_parsed else 'FAIL'}")
print(f"6. Userspace Layout boxes computed:     {'PASS' if layout_done else 'FAIL'}")
print(f"7. Render commands emitted to shared SHM:{'PASS' if cmds_emitted else 'FAIL'}")
print(f"8. Host validated & executed commands:  {'PASS' if host_executed else 'FAIL'}")

if (renderer_started and shm_mapped and html_received and dom_built and
    css_parsed and layout_done and cmds_emitted and host_executed):
    print("\n>>> ALL PHASE 4B LIVE VERIFICATION CRITERIA SATISFIED! <<<")
    sys.exit(0)
else:
    print("\n>>> VERIFICATION INCOMPLETE <<<")
    sys.exit(1)
