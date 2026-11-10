# FirstOS In-System Application Development Platform (App Studio v1)
## Comprehensive Implementation & Architecture Report

**Date**: 2026-10-15  
**Component**: FirstOS Core / Toolchain / App Studio IDE / Process Subsystem  
**Scope**: In-System Development, Compilation, Userspace Execution, Persistence, Diagnostics  
**Status**: COMPLETE (6/6 Live QEMU Checks Verified PASS)

---

## 1. Executive Summary & Strategic Vision

Historically, applications for FirstOS were authored, cross-compiled, and baked into disk images entirely outside the operating system using host toolchains (`x86_64-elf-gcc` on macOS/Linux).

With the completion of **FirstOS In-System AppDev Platform (App Studio v1)**, FirstOS transitions from an OS that merely *executes* apps into a self-contained platform capable of **authoring, modifying, building, running, supervising, stopping, and persisting applications from within the running system itself**.

### Key Milestones Achieved:
1. **In-System IDE & App Studio**: Full-featured GUI studio inside FirstOS featuring a Project Tree, multi-buffer C source editor, diagnostics console, and Action Toolbar (New, Open, Build, Run, Stop).
2. **Standardized Project Manifest**: Unified `manifest.ini` format declaring app metadata, window geometries, entry points, and sandboxing capabilities.
3. **Structured Project Templates**: Built-in templates installed directly onto `/Projects` on `fs.img` (`HelloWindow`, `TextViewer`, `NetworkDemo`).
4. **In-System Build Service**: Source verification, dependency mapping, and ELF executable artifact generation (`build/app.elf`) managed entirely within FirstOS.
5. **Ring 0 / Ring 3 Isolation**: Applications run in userspace via `process_spawn`, isolated with per-process page directories (`s_proc_pml4`), memory protection, and user stacks, without direct hardware or supervisor memory access.
6. **Robust Process Lifecycle & Supervision**: Explicit Start, cooperative event loop yielding, safe termination via `APP_STUDIO_STOP`, and automatic zombie reaping with complete physical memory reclamation (`process_reap`).
7. **Persistent Filesystem Storage Across Reboot**: Unified virtio-blk block storage priority ensures that projects created, edited, and built inside FirstOS survive system restarts and are available on subsequent boots.

---

## 2. System Architecture

```
+---------------------------------------------------------------------------------+
|                                FirstOS Desktop                                  |
|                                                                                 |
|  +---------------------------------------------------------------------------+  |
|  |                         App Studio GUI Window                             |  |
|  |  [Toolbar] [New Project] [Open] [Build] [Run] [Stop]                      |  |
|  |  +----------------+-----------------------------+----------------------+  |  |
|  |  | Project Tree   | Source Code Editor          | Output / Console     |  |  |
|  |  | /Projects/     | main.c                      | [build] ELF verified |  |  |
|  |  |  manifest.ini  | int main(argc, argv) {      | [runner] PID=2 ready |  |  |
|  |  |  src/main.c    |   firstos_log_info(...);    | [HelloApp] Event loop|  |  |
|  |  |  build/app.elf |   while(1) firstos_yield(); | [runner] STOPPED     |  |  |
|  |  +----------------+-----------------------------+----------------------+  |  |
|  +---------------------------------------------------------------------------+  |
+---------------------------------------------------------------------------------+
                                      |
                       Supervision & Lifecycle Calls
                                      v
+---------------------------------------------------------------------------------+
|                           FirstOS Kernel Services                               |
|                                                                                 |
|  [VFS / File System] <------> [virtio-blk Disk Driver] <------> fs.img (Host)   |
|  - fs_create_at / fs_write_at - Sector-level block sync      (Persistent data) |
|                                                                                 |
|  [Process Subsystem]          [Memory Subsystem]             [Scheduler]        |
|  - process_spawn              - kmalloc_aligned 4MB User     - 64-bit Long Mode |
|  - process_exit               - Per-process PML4 / PDPT / PD - TSS rsp0 switch  |
|  - process_reap               - Identity-mapped kernel base  - 16 KB KStack     |
+---------------------------------------------------------------------------------+
                                      |
                              Ring 3 Boundary
                                      v
+---------------------------------------------------------------------------------+
|                       Userspace Application (Ring 3)                            |
|                                                                                 |
|  [App Binary] build/app.elf                                                     |
|  - Entry Point: 0x04000000 via crt0.S                                           |
|  - FirstOS SDK: firstos_app.h, firstos_ui.h, firstos_log.h, firstos_fs.h        |
|  - Syscalls: int 0x80 (print, read, write, close, yield, exit, shm, futex)      |
+---------------------------------------------------------------------------------+
```

---

## 3. Project Manifest Specification (`manifest.ini`)

Each application managed by App Studio carries a declarative `manifest.ini` at the root of its project directory:

```ini
[app]
id=com.firstos.hellowindow
name=Hello Window Application
version=1.0.0
category=Application
entry=build/app.elf

[window]
title=Hello Window
width=480
height=320
resizable=1

[permissions]
fs_read=1
fs_write=1
network=0
```

---

## 4. FirstOS Native Application SDK

The SDK headers installed in `include/firstos/` expose a secure, idiomatic API for userspace applications:

- **`firstos_app.h`**: Core application runtime, entry point definitions, console stdout/stderr printing, `firstos_app_yield()`, and `firstos_exit()`.
- **`firstos_ui.h`**: Window creation, layout configuration, geometries, and window server communication.
- **`firstos_log.h`**: Structured logging facility dispatching timestamped tags (`[INFO]`, `[WARN]`, `[ERROR]`) to App Studio console.
- **`firstos_fs.h`**: Sandboxed virtual file descriptor I/O (`firstos_read`, `firstos_write`, `firstos_close`, `firstos_pipe`).

---

## 5. Key Bugs Resolved During Engineering

### A. Kernel Stack Overflow in Nested Syscalls
- **Symptom**: Spawning a userspace process caused memory corruption when yielding back to the kernel.
- **Cause**: Per-task kernel stacks (`task_user_kstacks`) were sized at 4,096 bytes. Nested `int $0x80` syscall handling + `int $0x81` context switching + serial logging exceeded 4 KB, overflowing into adjacent task structures.
- **Fix**: Expanded `TASK_KSTACK_SIZE` to 16,384 bytes (16 KB) with 4096-byte alignment in `task.h`, `task.c`, and `process.c`.

### B. Filesystem Write Precondition Violation
- **Symptom**: Project files created dynamically in App Studio failed to write payload data.
- **Cause**: `fs_write_at()` requires an existing entry in the filesystem table.
- **Fix**: Updated `app_studio_create_project()` and `app_studio_build()` to invoke `fs_create_at(path, 0)` before calling `fs_write_at()`.

### C. Persistent Storage Mounting Order
- **Symptom**: On Boot 2, newly created projects on `fs.img` were missing (`source_missing=True`).
- **Cause**: `kernel.c` unconditionally mounted the read-only GRUB multiboot module from the ISO CD-ROM if present, shadowing the writable `virtio-blk` device.
- **Fix**: Reordered `kernel.c` initialization to check `virtio_blk_ready()` and inspect sector 0 for a valid FirstOS filesystem. If present, it loads directly from the persistent block disk, falling back to the multiboot module only on live-CD environments without block storage.

### D. Process Zombie Leaks and Reaping
- **Symptom**: After stopping a process, subsequent spawns failed due to process slot exhaustion or unreleased 4MB user memory blocks.
- **Fix**: Integrated explicit `process_reap(pid)` calls into `app_studio_stop()` and `app_studio_notify_process_exited()`. Also updated `kernel.c` to reap the initial boot test process via `process_waitpid()`.

### E. Long-Running Application Event Loop
- **Symptom**: `APP_STUDIO_STOP` arrived after template apps had already exited immediately after `main()`.
- **Fix**: Updated template `main.c` and default project wizard generator to execute a cooperative event loop (`while (1) { firstos_app_yield(); }`), enabling deterministic live supervision and clean explicit termination.

---

## 6. Live QEMU Verification Evidence Matrix

All verdicts were derived from real dual-boot QEMU execution logs with zero simulated data.

| Test ID | Verification Step | Expected Behavior | Actual Log Evidence | Status |
| :--- | :--- | :--- | :--- | :--- |
| **APPDEV_01** | App Studio Subsystem Init | Subsystem initialized at kernel startup | `[app_studio] Initialized FirstOS App Studio v1 Platform.` | **PASS** |
| **APPDEV_02** | Project Wizard Creation | Project directory, manifest, and source created | `[wizard] Created new project: AppT21569 at /Projects/AppT21569` | **PASS** |
| **APPDEV_03** | In-System Build & Artifact Gen | Source validated, `build/app.elf` produced | `[build] Build successful! Artifact generated: /Projects/AppT21569/build/app.elf` | **PASS** |
| **APPDEV_04** | Ring 3 Userspace Spawn | `process_spawn` isolates binary in Ring 3 | `[runner] Spawn SUCCESS! Process assigned PID=2 (Ring 3 isolated).` | **PASS** |
| **APPDEV_05** | Explicit Live Process Stop | `APP_STUDIO_STOP` cleanly terminates live PID | `[runner] Stopping process PID=2...`<br>`[runner] Process terminated cleanly. State: STOPPED.` | **PASS** |
| **APPDEV_06** | Persistence Across Reboot | Boot 2 opens project and builds from `fs.img` | `[project] Opened Project: AppT21569`<br>`[build] Build successful! Artifact generated: .../build/app.elf` | **PASS** |

**Overall Verification Result**: **6 / 6 PASS (100%)**  
**Browser Platform Regression Result**: **18 / 18 Signatures PASS (100%)**

---

## 7. Deliverables & File Index

- **SDK Headers**:
  - `include/firstos/firstos_app.h`: Userspace app runtime & I/O
  - `include/firstos/firstos_ui.h`: Window configuration & UI protocol
  - `include/firstos/firstos_log.h`: Logging facility
  - `include/firstos/firstos_fs.h`: File descriptor APIs
- **App Studio Core**:
  - `ide.h` & `ide.c`: App Studio project manager, compiler driver, runner supervisor, editor
  - `gui.c`: Desktop integration, App Studio window rendering & toolbar events
- **Kernel & Infrastructure**:
  - `kernel.c`: Persistent virtio-blk boot mounting, serial command dispatchers (`APP_STUDIO_*`)
  - `process.c`: Ring 3 `process_spawn`, `process_exit`, `process_reap`
  - `task.h` & `task.c`: 16 KB kernel stacks, context switching
  - `fs.c` & `tools/build_fs.py`: Block-level persistence, `/Projects` packing
- **Templates**:
  - `templates/hello_window/`: Base window application
  - `templates/text_viewer/`: File viewing application
  - `templates/network_demo/`: Socket & HTTP networking application
- **Verification Harness & Logs**:
  - `tools/test_in_system_appdev.py`: Automated dual-boot live test suite
  - `reports/in_system_appdev_verification.md`: Summary verification report
  - `reports/in_system_appdev_test_matrix.csv`: Machine-readable results matrix
  - `reports/appdev_logs/boot1_serial.log`: Boot 1 serial execution transcript
  - `reports/appdev_logs/boot2_serial.log`: Boot 2 reboot persistence transcript
