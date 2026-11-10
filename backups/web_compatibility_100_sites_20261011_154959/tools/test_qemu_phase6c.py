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
    print("[test_phase6c] Launching QEMU with FirstOS Phase 6C Advanced CSS Engine...")
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

            # 1. Desktop ready -> launch tab to test advanced CSS
            if state == "WAIT_READY":
                if 'gui_draw done' in line or 'userspace_exec_target] Exiting cleanly' in line or '[web_browser] new native Web Browser' in line:
                    time.sleep(1)
                    print("[test_phase6c] Desktop ready. Sending TAB_NEW:about:html5 to test CSS engine...")
                    proc.stdin.write(b"TAB_NEW:about:html5\n")
                    proc.stdin.flush()
                    state = "WAIT_TAB_CMD"

            # 2. Wait for TAB_NEW command handling
            elif state == "WAIT_TAB_CMD":
                if "[serial_cmd] TAB_NEW" in line or "[tab] created" in line:
                    print("[test_phase6c] Host created tab. Waiting for userspace renderer evaluation...")
                    state = "WAIT_CSS_VARS"

            # 3. Wait for CSS variables to be evaluated in renderer
            elif state == "WAIT_CSS_VARS":
                if "CSS variables evaluated count=" in line:
                    print(f"[test_phase6c] Detected CSS variable evaluation: {line}")
                    state = "DONE"
                    time.sleep(2)
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
        "CSS Styles Computed in Ring 3": "[renderer] CSS parsed & styles computed" in full_log,
        "CSS Variables Evaluated": "CSS variables evaluated count=" in full_log,
        "Zero kernel panics / triple faults": "Triple fault" not in full_log and "PANIC" not in full_log
    }

    print("\n--- Phase 6C Advanced CSS Engine Live Verification Results ---")
    all_passed = True
    for desc, passed in checks.items():
        status = "PASS" if passed else "FAIL"
        print(f"[{status}] {desc}")
        if not passed:
            all_passed = False

    if all_passed:
        print("\n>>> ALL 5/5 PHASE 6C ADVANCED CSS CHECKS PASSED! <<<")
        return 0
    else:
        print("\n>>> PHASE 6C VERIFICATION FAILED <<<")
        print("\n--- Recent serial logs ---")
        for l in log_lines[-30:]:
            print(f"  {l}")
        return 1

if __name__ == "__main__":
    sys.exit(run_test())
