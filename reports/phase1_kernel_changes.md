# FirstOS Kernel Hardening & Infrastructure Overhaul (Phase 1 Changes)

**Date**: October 15, 2026  
**Author**: FirstOS Core Systems & Kernel Engineering  
**Scope**: `/Users/mohamadjlo/Desktop/FirstOS`  
**Status**: Completed & Live Verified in QEMU  

---

## 1. Executive Summary

To provide a stable, production-grade foundation capable of hosting heavy userspace workloads—specifically modern browsers (multiprocess renderer pipelines, network daemons, JS runtimes) and in-system developer environments (Terminal CLI, App Studio)—we executed a phased kernel hardening overhaul.

Across 4 targeted batches, we addressed the root vulnerabilities identified in our architectural audit without breaking existing ABI contracts or privilege ring isolation.

---

## 2. Detailed Breakdown of Changes

### Batch 1: Kernel Tracing & Advanced Fault Diagnostics

1. **Kernel Tracing Framework (`ktrace.h`, `ktrace.c`)**:
   - Implemented a lockless, non-allocating circular ring buffer holding 256 structured records (`ktrace_entry_t`).
   - Categorized events across core subsystems:
     - `KTRACE_SUBSYS_KERNEL`
     - `KTRACE_SUBSYS_PROCESS`
     - `KTRACE_SUBSYS_SYSCALL`
     - `KTRACE_SUBSYS_FAULT`
     - `KTRACE_SUBSYS_VFS`
     - `KTRACE_SUBSYS_IPC`
   - Logged timestamp (via `rdtsc`), subsystem tag, severity level, PID, and a 64-character descriptive payload.
   - Added `ktrace_dump_recent(n)` for zero-allocation post-mortem dumps during panics and crashes.

2. **Hardware Fault Diagnostics Overhaul (`fault.c`)**:
   - Replaced fragile PID-based heuristic (`if (current_task > 0)`) with rigorous hardware privilege level decoding:
     ```c
     int is_user = ((frame->cs & 3) == 3);
     ```
     This prevents kernel faults occurring during syscall servicing from being erroneously treated as user-mode exceptions.
   - Decoded Page Fault (#PF, Vector 14) error-code bitmask alongside CR2:
     - `P` (Present violation vs non-present page)
     - `W/R` (Write vs Read access)
     - `U/S` (User vs Supervisor mode)
     - `RSVD` (Reserved bit violation)
     - `I/D` (Instruction fetch violation)
   - Emitted full register context: `RAX`, `RBX`, `RCX`, `RDX`, `RSI`, `RDI`, `RBP`, `RSP`, `R8`–`R15`, `CS`, `SS`, `RFLAGS`, and `CR2`.
   - Added stack word inspection at `frame->rsp` (dumping the top 4 64-bit words) and invoked `ktrace_dump_recent(10)` before initiating process termination or system panic.

---

### Batch 2: VFS File Descriptor Discipline & Syscall Hardening

1. **VFS FD Ownership & Reference Counting (`vfs_fd.h`, `vfs_fd.c`)**:
   - **Root Cause Addressed**: Previously, `vfs_fd_entry_t` lacked reference counting. When a file descriptor was duplicated via `proc_fd_dup()` / `proc_fd_dup2()` or inherited during `process_spawn()`, calling `vfs_close()` from one task deallocated the shared in-memory buffer (`kfree(entry->buffer)`), causing silent data corruption and use-after-free crashes in sibling processes.
   - **Implementation**:
     - Added `ref_count` and `owner_pid` to `vfs_fd_entry_t`.
     - Implemented `vfs_add_ref(int fd)` to safely increment reference counters.
     - Updated `vfs_close(int fd)` to decrement `ref_count` and only release buffers/structures when `ref_count <= 0`.
     - Integrated `vfs_add_ref()` into `proc_fd_dup()`, `proc_fd_dup2()`, and the inheritance loop in `process_spawn()`.

2. **Defensive Syscall Pointer & Vector Validation (`syscall.h`, `syscall.c`)**:
   - Created `validate_user_argv(const char *const *argv)`:
     - Validates that the outer argv pointer resides within valid userspace memory.
     - Bounds-checks array length (`argc <= 64`).
     - Validates each individual string pointer and enforces a maximum string length (`ARG_MAX = 1024`).
   - Integrated `validate_user_argv` into `FIRSTOS_SYS_SPAWN` and `FIRSTOS_SYS_EXEC`.
   - Added strict 8-byte pointer alignment checks to `FIRSTOS_SYS_SHM_MAP` (`addr & 7 != 0`).
   - Instrument all syscall entries with `ktrace_record(KTRACE_SUBSYS_SYSCALL, ...)`.

---

### Batch 3: Process & Task Lifecycle Synchronization

1. **State Discipline (`process.h`, `process.c`)**:
   - Standardized process state transitions across `PROC_STATE_NEW`, `PROC_STATE_READY`, `PROC_STATE_RUNNING`, `PROC_STATE_BLOCKED`, `PROC_STATE_EXITED`, `PROC_STATE_ZOMBIE`, `PROC_STATE_REAPED`, and `PROC_STATE_UNUSED`.
   - Implemented `process_set_state(int pid, proc_state_t new_state)` to ensure centralized state updates and automated ktrace event emission.
   - Maintained strict synchronization between `process_table[pid].state` and hardware scheduler `tasks[pid].state`.
   - Preserved the critical `sizeof(task_t) == 64` constraint required by assembly scheduler arithmetic (`shl rax, 6` in `isr.asm`).

---

### Batch 4: Clean Kernel Modularization (`kernel.c` & `kernel_cmd.c`)

1. **Extraction of Serial Command Bridge**:
   - **Problem**: `kernel.c` was an 882-line monolithic file mixing bootloader setup, hardware initialization, scheduler loop, and 440 lines of serial string command parsing for App Studio, Tab Management, and Developer Tools.
   - **Solution**:
     - Created `kernel_cmd.h` and `kernel_cmd.c`.
     - Relocated `serial_poll_commands()` along with its subsystem dependencies into `kernel_cmd.c`.
     - Added `kernel_cmd_init()` called cleanly during boot.
     - Reduced `kernel.c` from 882 lines to 444 lines (a 50% footprint reduction) with clean responsibility separation.
     - Updated `Makefile` with `kernel_cmd.o` build targets and flags.

---

## 3. ABI and System Guarantees Maintained

| Subsystem / Contract | Constraint | Status |
| :--- | :--- | :--- |
| **Task Struct Size** | `sizeof(task_t) == 64` (required by `isr.asm`) | **Preserved** |
| **Privilege Boundaries** | Ring 0 kernel vs Ring 3 userspace | **Enforced** |
| **Syscall ABI** | Registers `RDI`, `RSI`, `RDX`, `R10`, `R8`, `R9` | **Unchanged** |
| **Disk & Boot Image** | Multiboot compliance & `fs.img` alignment | **Preserved** |
| **Modular Build** | Independent compilation of kernel objects | **Verified** |
