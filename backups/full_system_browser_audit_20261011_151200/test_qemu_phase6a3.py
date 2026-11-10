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
    print("[test_phase6a3] Launching QEMU with FirstOS Phase 6A.3...")
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

            # 1. Desktop ready -> create Tab 2 to trigger navigation & AX tree build
            if state == "WAIT_READY":
                if 'gui_draw done' in line or 'userspace_exec_target] Exiting cleanly' in line or '[web_browser] new native Web Browser' in line:
                    time.sleep(1)
                    print("[test_phase6a3] Desktop ready. Sending TAB_NEW:about:html5 to launch tab & build AX tree...")
                    proc.stdin.write(b"TAB_NEW:about:html5\n")
                    proc.stdin.flush()
                    state = "WAIT_TAB_AX"

            # 2. Wait for AX tree to be built on Tab 2
            elif state == "WAIT_TAB_AX":
                if "[AX_TREE]" in line or "AX tree built" in line:
                    time.sleep(1)
                    print("[test_phase6a3] Accessibility tree synthesized! Sending AX_DUMP serial command to host...")
                    proc.stdin.write(b"AX_DUMP\n")
                    proc.stdin.flush()
                    state = "WAIT_AX_DUMP"

            elif state == "WAIT_AX_DUMP":
                if "[ax_host] active_tab_id=" in line:
                    print("[test_phase6a3] Active tab confirmed by host kernel.")
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
        "AX Tree synthesized by renderer": "[renderer] AX tree built nodes=" in full_log or "[AX_TREE]" in full_log,
        "AX Tree dump generated": "[AX_TREE] Nodes=" in full_log,
        "AX semantic document & heading roles present": "role=document" in full_log and ("role=heading" in full_log or "role=button" in full_log),
        "Kernel handled AX_DUMP command": "[serial_cmd] AX_DUMP" in full_log,
        "Browser Host active tab reported": "[ax_host] active_tab_id=" in full_log,
        "Zero kernel panics / triple faults": "Triple fault" not in full_log and "PANIC" not in full_log
    }

    print("\n--- Phase 6A.3 Accessibility Live Verification Results ---")
    all_passed = True
    for desc, passed in checks.items():
        status = "PASS" if passed else "FAIL"
        print(f"[{status}] {desc}")
        if not passed:
            all_passed = False

    if all_passed:
        print("\n>>> ALL 7/7 PHASE 6A.3 ACCESSIBILITY CHECKS PASSED! <<<")
        return 0
    else:
        print("\n>>> PHASE 6A.3 VERIFICATION FAILED <<<")
        print("\n--- Recent serial logs ---")
        for l in log_lines[-30:]:
            print(l)
        return 1

if __name__ == '__main__':
    sys.exit(run_test())
