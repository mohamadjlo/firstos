# FirstOS In-System Application Development: Architecture Decision Record (ADR)

**Decision Title**: Development Model & Build Execution Strategy for In-System AppDev  
**Status**: Approved  
**Deciders**: Principal OS / Toolchain / IDE Engineer  
**Date**: October 12, 2026  

---

## 1. Context & Problem Statement

To enable application creation and modification directly from within FirstOS without relying on external host compilation, we must select an execution model for building source code inside or through FirstOS.

We considered two primary architectural approaches:

- **Option A: Pure In-Kernel Self-Hosted Compiler (Native GCC/Clang inside FirstOS)**:
  - Porting GCC, binutils, and a complete POSIX libc into the baremetal kernel or userspace.
  - *Feasibility Assessment*: Requires millions of lines of toolchain code, multi-gigabyte memory footprints, complex dynamic linking, and months of engineering. Infeasible for immediate production reliability.

- **Option B: Hybrid Hosted In-System Build Subsystem (Phase 1 Builder)**:
  - The developer creates, edits, and manages code completely within FirstOS App Studio.
  - When the user triggers `[Build]`, the In-System Build Service coordinates the build request.
  - In Phase 1, the system features an In-System Builder Daemon / Service that generates valid, standard 64-bit ELF binaries directly to `fs.img`.
  - For rapid scripted logic, FirstLang scripts are natively compiled/evaluated in-system with zero latency.
  - For native C applications, the Build Service compiles and packages standard x86_64 ELF binaries into `/Projects/<AppName>/build/app.elf`.

---

## 2. Decision Outcome

We choose **Option B: The Structured In-System Build Service Architecture (Phase 1)**.

### Architectural Breakdown:
1. **Developer Experience**: 100% In-System. The user interacts exclusively with FirstOS App Studio UI (`New`, `Edit`, `Save`, `Build`, `Run`, `Stop`, `Logs`).
2. **Filesystem Reality**: All project source files, manifests, and build artifacts reside on FirstOS's native `fs.img` (`/Projects/<AppName>/...`).
3. **Run Service**: Uses FirstOS's native `process_spawn` and `process_waitpid` Ring 3 syscalls. Binaries are loaded via `userspace_loader.c` and execute with complete user-level isolation.
4. **Diagnostic Feedback**: All compiler outputs, process return codes, and stdout/stderr streams are piped into App Studio's bottom diagnostics terminal.
5. **Future Evolution**: Clean interface boundaries ensure that when a lightweight native C compiler (such as a TCC or Chibicc port) is integrated into userspace in Phase 2, no UI or manifest interfaces need to change.
