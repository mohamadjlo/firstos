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
    print("[test_phase6b] Launching QEMU with FirstOS Phase 6B Media Pipeline...")
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
        while time.time() - start_time < 35:
            line_bytes = proc.stdout.readline()
            if not line_bytes:
                break
            line = line_bytes.decode('latin1', errors='replace').rstrip('\r\n')
            log_lines.append(line)

            # 1. Desktop ready -> launch tab to test media
            if state == "WAIT_READY":
                if 'gui_draw done' in line or 'userspace_exec_target] Exiting cleanly' in line or '[web_browser] new native Web Browser' in line:
                    time.sleep(1)
                    print("[test_phase6b] Desktop ready. Sending TAB_NEW:about:html5...")
                    proc.stdin.write(b"TAB_NEW:about:html5\n")
                    proc.stdin.flush()
                    state = "WAIT_TAB_READY"

            # 2. Tab created -> send MEDIA_TEST serial command to trigger hardware audio
            elif state == "WAIT_TAB_READY":
                if "[AX_TREE]" in line or "AX tree built" in line or "[tab] created" in line:
                    time.sleep(1)
                    print("[test_phase6b] Tab ready. Sending MEDIA_TEST command to test sound playback...")
                    proc.stdin.write(b"MEDIA_TEST:/audio/click.mp3\n")
                    proc.stdin.flush()
                    state = "WAIT_MEDIA_RESP"

            elif state == "WAIT_MEDIA_RESP":
                if "[media] hardware sound triggered" in line:
                    print("[test_phase6b] Media playback successfully confirmed by host kernel.")
                    time.sleep(1)
                    state = "DONE"
                    break

    except Exception as e:
        print(f"Exception during test: {e}")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()

    full_log = "\n".join(log_lines)

    # Assertions
    checks = {
        "Userspace Renderer in Ring 3": "[browser_renderer] started in Ring 3" in full_log,
        "Browser Tab Initialized": "[tab] created" in full_log or "NAV_READY" in full_log,
        "Kernel handled MEDIA_TEST": "[serial_cmd] MEDIA_TEST" in full_log,
        "Hardware Sound Pipeline Triggered": "[media] hardware sound triggered" in full_log,
        "Zero kernel panics / triple faults": "Triple fault" not in full_log and "PANIC" not in full_log
    }

    print("\n--- Phase 6B Media Pipeline Live Verification Results ---")
    all_passed = True
    for desc, passed in checks.items():
        status = "PASS" if passed else "FAIL"
        print(f"[{status}] {desc}")
        if not passed:
            all_passed = False

    if all_passed:
        print("\n>>> ALL 5/5 PHASE 6B MEDIA CHECKS PASSED! <<<")
        return 0
    else:
        print("\n>>> PHASE 6B VERIFICATION FAILED <<<")
        print("\n--- Recent serial logs ---")
        for l in log_lines[-30:]:
            print(l)
        return 1

if __name__ == '__main__':
    sys.exit(run_test())
