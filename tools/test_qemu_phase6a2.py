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

def run_test():
    print("[test_phase6a2] Launching QEMU with FirstOS Phase 6A.2...")
    proc = subprocess.Popen(
        qemu_cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT
    )

    log_lines = []
    start_time = time.time()
    state = "WAIT_READY"

    try:
        while time.time() - start_time < 30:
            line_bytes = proc.stdout.readline()
            if not line_bytes:
                break
            line = line_bytes.decode('utf-8', errors='ignore').strip()
            log_lines.append(line)

            # 1. Desktop ready -> trigger atomic storage save
            if state == "WAIT_READY":
                if 'gui_draw done' in line or 'userspace_exec_target] Exiting cleanly' in line or '[web_browser] new native Web Browser' in line:
                    time.sleep(1)
                    print("[test_phase6a2] Desktop ready. Triggering STORAGE_SAVE (atomic flush with CRC32)...")
                    proc.stdin.write(b"STORAGE_SAVE\n")
                    proc.stdin.flush()
                    state = "WAIT_SAVE"

            # 2. Save completed -> trigger session restore
            elif state == "WAIT_SAVE" and "atomic flush completed successfully" in line:
                time.sleep(0.5)
                print("[test_phase6a2] Storage flushed to disk! Triggering STORAGE_RESTORE...")
                proc.stdin.write(b"STORAGE_RESTORE\n")
                proc.stdin.flush()
                state = "WAIT_RESTORE"

            # 3. Restore completed
            elif state == "WAIT_RESTORE" and "[storage] restored session tabs" in line:
                print("[test_phase6a2] Session tabs restored with fresh IDs! Verification complete.")
                state = "DONE"
                break

        proc.terminate()
        proc.wait(timeout=3)
    except Exception as e:
        proc.kill()
        print(f"[test_phase6a2] Error: {e}")

    log_path = "/tmp/firstos_phase6a2_test.log"
    with open(log_path, "w") as f:
        f.write("\n".join(log_lines))
    print(f"[test_phase6a2] Captured {len(log_lines)} lines to {log_path}")

    full_log = "\n".join(log_lines)
    c1 = "gui_draw done" in full_log or "[web_browser]" in full_log
    c2 = "[serial_cmd] STORAGE_SAVE" in full_log
    c3 = "atomic flush completed successfully" in full_log
    c4 = "[serial_cmd] STORAGE_RESTORE" in full_log
    c5 = "[storage] restored session tabs" in full_log
    c6 = "triple fault" not in full_log.lower() and "kernel panic" not in full_log.lower()

    print("\n--- Phase 6A.2 Live QEMU Verification Summary ---")
    print(f"1. Desktop GUI Initialized:                    {'PASS' if c1 else 'FAIL'}")
    print(f"2. Persistent Storage Save Requested:          {'PASS' if c2 else 'FAIL'}")
    print(f"3. Atomic Flush & CRC32 Verified:              {'PASS' if c3 else 'FAIL'}")
    print(f"4. Session Restore Requested:                  {'PASS' if c4 else 'FAIL'}")
    print(f"5. Fresh Tabs Recreated from Storage:          {'PASS' if c5 else 'FAIL'}")
    print(f"6. Zero kernel panics / triple faults:         {'PASS' if c6 else 'FAIL'}")

    all_pass = c1 and c2 and c3 and c4 and c5 and c6
    if all_pass:
        print("\n>>> ALL PHASE 6A.2 LIVE VERIFICATION CRITERIA PASSED! <<<\n")
        return 0
    else:
        print("\n>>> LIVE VERIFICATION FAILED <<<\n")
        return 1

if __name__ == "__main__":
    sys.exit(run_test())
