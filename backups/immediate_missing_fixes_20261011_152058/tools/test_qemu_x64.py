#!/usr/bin/env python3
"""
tools/test_qemu_x64.py
Verification test suite for FirstOS x86_64 Long Mode kernel.
Boots firstos.iso in headless QEMU, collects serial output, and validates:
- 64-bit Long Mode entry & System V AMD64 ABI
- 64-bit TSS and GDT setup
- 64-bit IDT and interrupt dispatching
- 4-level PML4 paging (0..4GB identity mapping)
- 64-bit Task switching and TSS rsp0 stack management
- Ring 3 userspace task execution & yield
- Filesystem mount and GUI initialization
"""

import sys
import time
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

def run_test():
    iso_path = REPO_ROOT / "firstos.iso"
    fs_path = REPO_ROOT / "fs.img"
    if not iso_path.exists():
        print(f"Error: {iso_path} does not exist. Run 'make firstos.iso' first.")
        sys.exit(1)

    cmd = [
        "qemu-system-x86_64",
        "-drive", f"file={iso_path},format=raw,media=cdrom,readonly=on",
        "-boot", "d",
        "-m", "2G",
        "-serial", "stdio",
        "-display", "none",
        "-vga", "none",
        "-device", "virtio-gpu-pci,xres=1024,yres=768",
        "-drive", f"file={fs_path},format=raw,if=none,id=fsdisk",
        "-device", "virtio-blk-pci,drive=fsdisk",
        "-netdev", "user,id=net0",
        "-device", "virtio-net-pci,netdev=net0",
        "-no-reboot"
    ]

    print("[test] Launching FirstOS in qemu-system-x86_64...")
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        cwd=REPO_ROOT
    )

    # Let the OS boot and reach the GUI event loop
    time.sleep(7)
    proc.terminate()
    try:
        stdout, stderr = proc.communicate(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        stdout, stderr = proc.communicate()

    print(f"[test] Captured {len(stdout)} characters from serial output.")

    required_checkpoints = [
        "[kernel] entered kernel_main",
        "[gdt] 64-bit TSS initialized and loaded (selector 0x28)",
        "[linux_abi] Linux System Call Subsystem initialized",
        "[pipe] Pipe subsystem initialized",
        "[shm] Shared memory subsystem initialized",
        "[futex] Fast userspace mutex & futex subsystem initialized",
        "[process] Process management subsystem initialized",
        "[kernel] multiboot_magic=0x2BADB002",
        "[paging] 64-bit 4-level PML4 page tables built",
        "[paging] cr3 loaded with kernel_pml4",
        "[task] 64-bit multitasking initialized with TSS rsp0 switching",
        "[kernel] interrupts enabled!",
        "[fs] image mounted",
        "[task_yield] switch to tid=0x00000001",
        "[kernel] resumed from FS-loaded userspace task successfully!",
        "[kernel] gui_draw done",
        "[task] timer_handler switching to tid="
    ]

    missing = []
    for cp in required_checkpoints:
        if cp in stdout:
            print(f"  [PASS] Found: '{cp}'")
        else:
            print(f"  [FAIL] Missing: '{cp}'")
            missing.append(cp)

    if missing:
        print(f"\n[FAIL] {len(missing)} checkpoint(s) missing from serial output!")
        sys.exit(1)

    print("\n[SUCCESS] All x86_64 Long Mode kernel architecture checkpoints verified!")
    sys.exit(0)

if __name__ == "__main__":
    run_test()
