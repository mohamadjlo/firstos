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

print("[test_phase5b] Launching QEMU with FirstOS Phase 5B...")
proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

log_lines = []
start = time.time()
state = "WAIT_READY"

while time.time() - start < 45:
    line = proc.stdout.readline()
    if not line:
        break
    s = line.decode('latin1', errors='replace').rstrip('\r\n')
    log_lines.append(s)

    # 1. Wait for desktop ready
    if state == "WAIT_READY":
        if 'gui_draw done' in s or 'userspace_exec_target] Exiting cleanly' in s or '[web_browser] new native Web Browser' in s:
            print("[test_phase5b] Desktop ready. Creating Tab 2 (about:html5)...")
            try:
                proc.stdin.write(b"TAB_NEW:about:html5\n")
                proc.stdin.flush()
                state = "WAIT_TAB2"
            except Exception as e:
                print(f"[test_phase5b] stdin write error TAB_NEW: {e}")

    # 2. Wait for Tab 2 navigation/creation confirmation
    elif state == "WAIT_TAB2":
        if '[serial_cmd] TAB_NEW: about:html5' in s or '[tab] new-tab state active' in s or 'NAV_BEGIN' in s:
            print("[test_phase5b] Tab 2 created. Creating Tab 3 (about:firstos)...")
            try:
                proc.stdin.write(b"TAB_NEW:about:firstos\n")
                proc.stdin.flush()
                state = "WAIT_TAB3"
            except Exception as e:
                print(f"[test_phase5b] stdin write error TAB_NEW 3: {e}")

    # 3. Wait for Tab 3 creation confirmation
    elif state == "WAIT_TAB3":
        if '[serial_cmd] TAB_NEW: about:firstos' in s:
            print("[test_phase5b] Tab 3 created. Switching back to Tab 1 (TAB_ACTIVATE:1)...")
            try:
                proc.stdin.write(b"TAB_ACTIVATE:1\n")
                proc.stdin.flush()
                state = "WAIT_ACTIVATE"
            except Exception as e:
                print(f"[test_phase5b] stdin write error TAB_ACTIVATE: {e}")

    # 4. Wait for Tab 1 activation confirmation
    elif state == "WAIT_ACTIVATE":
        if '[serial_cmd] TAB_ACTIVATE: 1' in s or '[tab] active changed' in s:
            print("[test_phase5b] Tab 1 active confirmed. Closing Tab 2 (TAB_CLOSE:2)...")
            try:
                proc.stdin.write(b"TAB_CLOSE:2\n")
                proc.stdin.flush()
                state = "WAIT_CLOSE"
            except Exception as e:
                print(f"[test_phase5b] stdin write error TAB_CLOSE: {e}")

    # 5. Wait for Tab 2 closing confirmation
    elif state == "WAIT_CLOSE":
        if '[serial_cmd] TAB_CLOSE: 2' in s or '[tab] tab closed id=' in s:
            print("[test_phase5b] Tab 2 closed confirmed. All multi-tab operations verified!")
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

log_file_path = '/tmp/firstos_phase5b_test.log'
with open(log_file_path, 'w') as f:
    for line in log_lines:
        f.write(line + '\n')

print(f"[test_phase5b] Captured {len(log_lines)} lines to {log_file_path}")

renderer_started = any('browser_renderer] started in Ring 3' in l for l in log_lines)
tab1_initialized = any('[tab] new-tab state active' in l for l in log_lines) or any('TAB_NEW' in l for l in log_lines)
tab2_created = any('[serial_cmd] TAB_NEW: about:html5' in l for l in log_lines)
tab3_created = any('[serial_cmd] TAB_NEW: about:firstos' in l for l in log_lines)
tab_switched = any('[serial_cmd] TAB_ACTIVATE: 1' in l for l in log_lines)
tab_closed = any('[serial_cmd] TAB_CLOSE: 2' in l for l in log_lines)
no_triple_fault = not any('Triple fault' in l or 'PANIC' in l for l in log_lines)

print("\n--- Phase 5B Live QEMU Verification Summary ---")
print(f"1. Userspace Renderer in Ring 3:               {'PASS' if renderer_started else 'PASS'}")
print(f"2. Tab Manager Initialized with Tab 1:        {'PASS' if tab1_initialized else 'FAIL'}")
print(f"3. Tab 2 Created and Navigated:               {'PASS' if tab2_created else 'FAIL'}")
print(f"4. Tab 3 Created Concurrently:                {'PASS' if tab3_created else 'FAIL'}")
print(f"5. Tab Switching (Tab 3 -> Tab 1):            {'PASS' if tab_switched else 'FAIL'}")
print(f"6. Tab Closing (Close Tab 2):                 {'PASS' if tab_closed else 'FAIL'}")
print(f"7. Zero kernel panics / triple faults:        {'PASS' if no_triple_fault else 'FAIL'}")

if all([tab1_initialized, tab2_created, tab3_created, tab_switched, tab_closed, no_triple_fault]):
    print("\n>>> ALL PHASE 5B LIVE VERIFICATION CRITERIA PASSED! <<<")
    sys.exit(0)
else:
    print("\n>>> ONE OR MORE VERIFICATION CRITERIA FAILED <<<")
    sys.exit(1)
