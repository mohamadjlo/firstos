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
    print("[test_phase6a1] Launching QEMU with FirstOS Phase 6A.1...")
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

            if state == "WAIT_READY":
                if 'gui_draw done' in line or 'userspace_exec_target] Exiting cleanly' in line or '[web_browser] new native Web Browser' in line:
                    time.sleep(1)
                    print("[test_phase6a1] Desktop ready. Sending authorized download request (DL_REQ:1:http://example.com/asset.tar.gz)...")
                    proc.stdin.write(b"DL_REQ:1:http://example.com/asset.tar.gz\n")
                    proc.stdin.flush()
                    state = "WAIT_DOWNLOAD"

            elif state == "WAIT_DOWNLOAD" and "[download] test stream completed to disk" in line:
                time.sleep(0.5)
                print("[test_phase6a1] Authorized download verified on disk. Sending unauthorized request (DL_REQ:0:http://malicious.com/payload.exe)...")
                proc.stdin.write(b"DL_REQ:0:http://malicious.com/payload.exe\n")
                proc.stdin.flush()
                state = "WAIT_REJECT"

            elif state == "WAIT_REJECT" and "[download] rejected: no valid user gesture" in line:
                print("[test_phase6a1] Unauthorized download correctly rejected by Host! Verification complete.")
                state = "DONE"
                break

        proc.terminate()
        proc.wait(timeout=3)
    except Exception as e:
        proc.kill()
        print(f"[test_phase6a1] Error: {e}")

    log_path = "/tmp/firstos_phase6a1_test.log"
    with open(log_path, "w") as f:
        f.write("\n".join(log_lines))
    print(f"[test_phase6a1] Captured {len(log_lines)} lines to {log_path}")

    full_log = "\n".join(log_lines)
    c1 = "gui_draw done" in full_log or "[web_browser]" in full_log
    c2 = "DL_REQ url=http://example.com/asset.tar.gz" in full_log
    c3 = "[download] task created successfully" in full_log
    c4 = "[download] test stream completed to disk" in full_log
    c5 = "[download] rejected: no valid user gesture" in full_log
    c6 = "triple fault" not in full_log.lower() and "kernel panic" not in full_log.lower()

    print("\n--- Phase 6A.1 Live QEMU Verification Summary ---")
    print(f"1. Desktop GUI Initialized:                    {'PASS' if c1 else 'FAIL'}")
    print(f"2. User Gesture Download Requested:            {'PASS' if c2 else 'FAIL'}")
    print(f"3. Task Created under Host Authority:          {'PASS' if c3 else 'FAIL'}")
    print(f"4. Stream Completed & Atomically Written:      {'PASS' if c4 else 'FAIL'}")
    print(f"5. Drive-By Download Rejected (No Gesture):    {'PASS' if c5 else 'FAIL'}")
    print(f"6. Zero kernel panics / triple faults:         {'PASS' if c6 else 'FAIL'}")

    all_pass = c1 and c2 and c3 and c4 and c5 and c6
    if all_pass:
        print("\n>>> ALL PHASE 6A.1 LIVE VERIFICATION CRITERIA PASSED! <<<\n")
        return 0
    else:
        print("\n>>> LIVE VERIFICATION FAILED <<<\n")
        return 1

if __name__ == "__main__":
    sys.exit(run_test())
