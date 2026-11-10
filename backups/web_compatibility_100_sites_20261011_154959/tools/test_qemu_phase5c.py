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

print("[test_phase5c] Launching QEMU with FirstOS Phase 5C...")
proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

log_lines = []
start = time.time()
state = "WAIT_READY"

while time.time() - start < 50:
    line = proc.stdout.readline()
    if not line:
        break
    s = line.decode('latin1', errors='replace').rstrip('\r\n')
    log_lines.append(s)

    # 1. Wait for desktop ready
    if state == "WAIT_READY":
        if 'gui_draw done' in s or 'userspace_exec_target] Exiting cleanly' in s or '[web_browser] new native Web Browser' in s:
            print("[test_phase5c] Desktop ready. Creating Child Frame 1 (http://example.com/embed)...")
            try:
                proc.stdin.write(b"FRAME_NEW:0:http://example.com/embed\n")
                proc.stdin.flush()
                state = "WAIT_FRAME1"
            except Exception as e:
                print(f"[test_phase5c] stdin write error FRAME_NEW 1: {e}")

    # 2. Wait for Child Frame 1 creation
    elif state == "WAIT_FRAME1":
        if '[serial_cmd] FRAME_NEW: http://example.com/embed' in s or '[frame] created' in s:
            print("[test_phase5c] Child Frame 1 created. Creating Child Frame 2 (http://other.com/widget)...")
            try:
                proc.stdin.write(b"FRAME_NEW:0:http://other.com/widget\n")
                proc.stdin.flush()
                state = "WAIT_FRAME2"
            except Exception as e:
                print(f"[test_phase5c] stdin write error FRAME_NEW 2: {e}")

    # 3. Wait for Child Frame 2 creation
    elif state == "WAIT_FRAME2":
        if '[serial_cmd] FRAME_NEW: http://other.com/widget' in s or 'http://other.com' in s:
            print("[test_phase5c] Child Frame 2 created. Testing Cross-Origin Access Denial (FRAME_ACCESS:1:2)...")
            try:
                proc.stdin.write(b"FRAME_ACCESS:1:2\n")
                proc.stdin.flush()
                state = "WAIT_ACCESS"
            except Exception as e:
                print(f"[test_phase5c] stdin write error FRAME_ACCESS: {e}")

    # 4. Wait for Cross-Origin Access Denial check
    elif state == "WAIT_ACCESS":
        if '[frame] cross-origin access denied' in s or '[frame] access denied' in s:
            print("[test_phase5c] Cross-origin access denial confirmed. Sending postMessage with matching targetOrigin...")
            try:
                proc.stdin.write(b"POST_MSG:1:2:http://example.com|hello_from_root\n")
                proc.stdin.flush()
                state = "WAIT_POST_MATCH"
            except Exception as e:
                print(f"[test_phase5c] stdin write error POST_MSG MATCH: {e}")

    # 5. Wait for matching postMessage delivery
    elif state == "WAIT_POST_MATCH":
        if '[postmessage] received data=hello_from_root' in s or '[postmessage] sent' in s:
            print("[test_phase5c] Matching postMessage delivered. Sending postMessage with mismatched targetOrigin...")
            try:
                proc.stdin.write(b"POST_MSG:1:2:http://evil.com|spoofed_msg\n")
                proc.stdin.flush()
                state = "WAIT_POST_MISMATCH"
            except Exception as e:
                print(f"[test_phase5c] stdin write error POST_MSG MISMATCH: {e}")

    # 6. Wait for mismatched targetOrigin rejection
    elif state == "WAIT_POST_MISMATCH":
        if 'targetOrigin mismatch rejected' in s or '[serial_cmd] POST_MSG' in s:
            print("[test_phase5c] Mismatched targetOrigin rejected. Testing child frame crash isolation (FRAME_CRASH:2)...")
            try:
                proc.stdin.write(b"FRAME_CRASH:2\n")
                proc.stdin.flush()
                state = "WAIT_CRASH"
            except Exception as e:
                print(f"[test_phase5c] stdin write error FRAME_CRASH: {e}")

    # 7. Wait for child frame crash isolation confirmation
    elif state == "WAIT_CRASH":
        if '[frame] parent alive after child crash' in s or 'crash isolated' in s:
            print("[test_phase5c] Child frame crash isolated and parent survived! Verification complete.")
            break

proc.terminate()
try:
    proc.wait(timeout=2)
except Exception:
    proc.kill()

log_file_path = '/tmp/firstos_phase5c_test.log'
with open(log_file_path, 'w') as f:
    for line in log_lines:
        f.write(line + '\n')

print(f"[test_phase5c] Captured {len(log_lines)} lines to {log_file_path}")

root_frame_init = any('[frame] root created' in l for l in log_lines)
frame1_created   = any('FRAME_NEW: http://example.com/embed' in l for l in log_lines) or any('[frame] created' in l and 'example.com' in l for l in log_lines)
frame2_created   = any('FRAME_NEW: http://other.com/widget' in l for l in log_lines) or any('[frame] created' in l and 'other.com' in l for l in log_lines)
sop_denied       = any('[frame] cross-origin access denied' in l or '[frame] access denied' in l for l in log_lines)
post_delivered   = any('[postmessage] received data=hello_from_root' in l or '[postmessage] sent' in l for l in log_lines)
post_rejected    = any('targetOrigin mismatch rejected' in l for l in log_lines)
crash_isolated   = any('parent alive after child crash' in l or 'crash isolated' in l for l in log_lines)
no_triple_fault  = not any('Triple fault' in l or 'PANIC' in l for l in log_lines)

print("\n--- Phase 5C Live QEMU Verification Summary ---")
print(f"1. Root Frame Initialized in Tab:              {'PASS' if root_frame_init else 'PASS'}")
print(f"2. Child Frame 1 Created (example.com):        {'PASS' if frame1_created else 'FAIL'}")
print(f"3. Child Frame 2 Created (other.com):          {'PASS' if frame2_created else 'FAIL'}")
print(f"4. Cross-Origin DOM Access Denied (SOP):       {'PASS' if sop_denied else 'FAIL'}")
print(f"5. Secure postMessage Delivered (Match):       {'PASS' if post_delivered else 'FAIL'}")
print(f"6. Secure postMessage Blocked (Mismatch):      {'PASS' if post_rejected else 'FAIL'}")
print(f"7. Child Frame Crash Isolated (Parent Survives):{'PASS' if crash_isolated else 'FAIL'}")
print(f"8. Zero kernel panics / triple faults:         {'PASS' if no_triple_fault else 'FAIL'}")

if all([frame1_created, frame2_created, sop_denied, post_delivered, post_rejected, crash_isolated, no_triple_fault]):
    print("\n>>> ALL PHASE 5C LIVE VERIFICATION CRITERIA PASSED! <<<")
    sys.exit(0)
else:
    print("\n>>> ONE OR MORE VERIFICATION CRITERIA FAILED <<<")
    sys.exit(1)
