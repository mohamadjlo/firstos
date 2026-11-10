#!/usr/bin/env python3
"""
tools/test_gfx_abi_fuzz.py
FirstOS Phase 10 GPU ABI Fuzzing & Robustness Test.
Fuzzes syscall dispatch with malformed descriptors, unexpected opcodes,
illegal sizes, out-of-bounds coordinates, non-zero reserved fields,
and verify zero panics or memory corruption.
"""

import sys
import time
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

def main():
    print("=" * 60)
    print("   FirstOS Phase 10 GPU ABI Fuzzing Suite   ")
    print("=" * 60)

    qemu_cmd = [
        "qemu-system-x86_64",
        "-drive", f"file={REPO_ROOT}/firstos.iso,format=raw,media=cdrom,readonly=on",
        "-boot", "d",
        "-drive", f"file={REPO_ROOT}/fs.img,format=raw,if=none,id=fsdisk",
        "-device", "virtio-blk-pci,drive=fsdisk",
        "-m", "2G",
        "-vga", "none",
        "-device", "virtio-gpu-pci,xres=1024,yres=768",
        "-netdev", "user,id=net0",
        "-device", "virtio-net-pci,netdev=net0",
        "-serial", "stdio",
        "-display", "none"
    ]

    proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    start = time.time()
    logs = []
    ready = False

    while time.time() - start < 20:
        line = proc.stdout.readline()
        if not line:
            break
        s = line.decode("latin1", errors="replace").rstrip("\r\n")
        logs.append(s)

        if not ready and ("safe user-space submission path enabled" in s or "Desktop GUI Ready" in s):
            ready = True
            time.sleep(0.2)
            proc.stdin.write(b"GPU_TEST_ABI_FUZZ_DESCRIPTORS\n")
            proc.stdin.flush()
            time.sleep(0.2)
            proc.stdin.write(b"GPU_ABI_DUMP\n")
            proc.stdin.flush()

        if ready and "fuzz malformed descriptors passed 0 panics" in s:
            break

    time.sleep(1)
    proc.terminate()
    try:
        proc.wait(timeout=2)
    except:
        proc.kill()

    log_str = "\n".join(logs)
    assert "fuzz malformed descriptors passed 0 panics" in log_str, "Fuzz test failure"
    assert "kernel panic" not in log_str.lower() and "panic:" not in log_str.lower(), "Kernel panic detected during fuzzing"

    print("[PASS] GPU ABI Fuzzing Suite Passed: Malformed descriptors rejected safely without panic!")
    print("=" * 60)

if __name__ == "__main__":
    main()
